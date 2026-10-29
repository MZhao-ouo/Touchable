"""Check game isolation, input selection, cache checks, and installation guards."""
import hashlib
from datetime import datetime
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import launcher
from games import scaffold
from workspace import load_game, state_root, WorkspaceError


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.code = self.root / 'internal'
        self.code.mkdir()
        scaffold(self.code, 'one-game')

    def test_local_identity_is_separate_from_source_manifest(self):
        game = load_game('one-game', self.code)
        original = game.manifest_path.read_bytes()
        launcher.write_json(self.root / '.touchable/config.json', {
            'games': {'one-game': {'bundle_id': 'dev.example.one-game'}}})
        changed = load_game('one-game', self.code)
        self.assertEqual(changed.bundle_id, 'dev.example.one-game')
        self.assertEqual(changed.manifest_path.read_bytes(), original)
        self.assertEqual(changed.ipa_dir, self.root / 'IPA/one-game')
        self.assertEqual(changed.work_dir, self.root / '.touchable/work/one-game')
        self.assertEqual(changed.save_dir, self.root / '.touchable/saves/one-game/one-game')

    def test_unknown_game_cannot_build_or_install(self):
        game = load_game('one-game', self.code)
        with patch.object(launcher, 'run') as run:
            with self.assertRaises(WorkspaceError):
                launcher.build(game)
            run.assert_not_called()

    def test_changed_rewrite_output_invalidates_cache(self):
        directory = self.root / 'modules'
        directory.mkdir()
        binary = directory / 'main.patched.bin'
        binary.write_bytes(b'original')
        table = directory / 'main.fixups.tsv'
        table.write_bytes(b'table')
        tool = self.root / 'tool.py'
        tool.write_bytes(b'tool')
        game = SimpleNamespace(patched_dir=directory, module_names=['main'],
                               profile={'modules': {'main': {'sha256': 'input'}}})
        launcher.write_json(directory / 'verification.json', {
            'modules': {'main': {'source_sha256': 'input', 'structure': 'passed', 'equivalence': 'passed',
                        'output_sha256': {p.name: launcher.sha256(p) for p in (binary, table)}}},
            'current_tool_sha256': {'tool.py': launcher.sha256(tool)}})
        with patch.object(launcher, 'UPSTREAM', self.root):
            self.assertTrue(launcher.rewrite_is_current(game))
            binary.write_bytes(b'modified')
            self.assertFalse(launcher.rewrite_is_current(game))

    def test_ipa_game_mismatch_stops_before_extraction(self):
        game = load_game('one-game', self.code)
        ipa = self.root / 'other.ipa'
        ipa.write_bytes(b'not an archive')
        launcher.write_json(ipa.with_suffix('.json'), {'game_id': 'other-game', 'bundle_id': game.bundle_id})
        with self.assertRaises(WorkspaceError):
            launcher.verify_ipa(game, ipa, self.root / 'unpacked')
        self.assertFalse((self.root / 'unpacked').exists())

    def test_ipa_same_size_damage_stops_before_extraction(self):
        game = load_game('one-game', self.code)
        ipa = self.root / 'app.ipa'
        ipa.write_bytes(b'modified')
        launcher.write_json(ipa.with_suffix('.json'), {'game_id': game.id, 'bundle_id': game.bundle_id,
                            'sha256': hashlib.sha256(b'original').hexdigest()})
        with self.assertRaises(WorkspaceError):
            launcher.verify_ipa(game, ipa, self.root / 'unpacked')
        self.assertFalse((self.root / 'unpacked').exists())

    def test_input_selection_uses_game_folder_and_required_update(self):
        directory = self.root / 'MyGameFiles/silksong'
        directory.mkdir(parents=True)
        base, update = directory / 'Base[v0].nsp', directory / 'Update[v655360].nsp'
        base.write_bytes(b'base'); update.write_bytes(b'update')
        keys = self.root / 'MyGameFiles/keys/prod.keys'
        keys.parent.mkdir(); keys.write_bytes(b'synthetic')
        game = SimpleNamespace(id='silksong', profile={'package_version': 655360})
        with patch.object(launcher, 'PROJECT_ROOT', self.root), \
             patch.object(launcher, 'read_local_config', return_value={}):
            self.assertEqual(launcher.input_files(game, False), (base, update, keys))
            update.unlink()
            with self.assertRaises(WorkspaceError):
                launcher.input_files(game, False)

    def test_cancel_and_invalid_menu_input(self):
        with patch('builtins.input', side_effect=['invalid', '9', '2']):
            self.assertEqual(launcher.choose('game', ['first', 'second']), 'second')
        with patch('builtins.input', return_value='0'), self.assertRaises(KeyboardInterrupt):
            launcher.choose('game', ['first'])

    def test_failed_phone_backup_stops_installation(self):
        game = load_game('one-game', self.code)
        app = self.root / 'One.app'
        app.mkdir()
        profile = {'ExpirationDate': datetime(2099, 1, 1), 'ProvisionedDevices': ['device']}
        with patch.object(launcher, 'LOCAL_ROOT', self.root), \
             patch.object(launcher, 'verify_ipa', return_value=(app, profile)), \
             patch.object(launcher, 'device_json', return_value={'apps': [{'bundleIdentifier': game.bundle_id}]}), \
             patch.object(launcher, 'run', side_effect=WorkspaceError('backup failed')) as run:
            with self.assertRaises(WorkspaceError):
                launcher.install(game, self.root / 'app.ipa', device='device')
            self.assertEqual(run.call_count, 1)
            self.assertIn('copy', run.call_args.args[0])
            self.assertNotIn('install', run.call_args.args[0])

    def test_idle_paired_phone_is_available_for_reconnection(self):
        phone = {'identifier': 'coredevice', 'hardwareProperties': {'reality': 'physical', 'udid': 'phone'},
                 'connectionProperties': {'pairingState': 'paired', 'tunnelState': 'disconnected'},
                 'deviceProperties': {'bootState': 'booted', 'name': 'Phone'}}
        simulator = {**phone, 'hardwareProperties': {'reality': 'simulated', 'udid': 'simulator'}}
        with patch.object(launcher, 'device_json', return_value={'devices': [phone, simulator]}):
            self.assertEqual(launcher.devices(), [('phone', 'Phone')])

    def test_upstream_patch_includes_new_files_from_plain_apply(self):
        upstream = self.root / 'upstream'
        git = lambda *arguments: subprocess.run(['git', '-C', str(upstream), *arguments], check=True,
                                                capture_output=True)
        upstream.mkdir()
        git('init', '-q')
        (upstream / 'old.c').write_text('one\n')
        git('add', 'old.c')
        git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-q', '-m', 'base')
        (upstream / 'old.c').write_text('two\n')
        (upstream / 'new dir').mkdir()
        (upstream / 'new dir/new.c').write_text('new\n')
        git('add', '--intent-to-add', 'new dir/new.c')
        expected = launcher.upstream_patch(upstream, b'')
        self.assertIn(b'new file mode', expected)
        for mode in ([], ['--intent-to-add']):
            git('reset', '-q', '--hard')
            git('clean', '-qfd')
            (upstream / 'patch').write_bytes(expected)
            git('apply', *mode, 'patch')
            (upstream / 'patch').unlink()
            (upstream / 'other.c').write_text('other\n')
            self.assertEqual(launcher.upstream_patch(upstream, expected), expected)
            untracked = git('ls-files', '--others').stdout.decode().split()
            self.assertEqual(untracked, ['other.c'])


if __name__ == '__main__':
    unittest.main()
