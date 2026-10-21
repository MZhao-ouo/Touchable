"""Check that a new game build cannot reuse removed generated resources."""
import json
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest.mock import patch
import shutil

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_ios import (shell_controller_haptics, shell_localizations,
                         stage_module_tables, stage_shaders)
from workspace import ROOT, WorkspaceError


class GeneratedResourceTests(unittest.TestCase):
    def test_shader_staging_does_not_require_source_file_attributes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, target = root / 'source', root / 'target'
            chain = source / 'ios/b-test'
            chain.mkdir(parents=True)
            (chain / 'shader.metallib').write_bytes(b'library')
            (source / 'shader_map.tsv').write_text('map')
            original = shutil.copystat
            def copy_attributes(src, dst, **kwargs):
                if Path(src).is_file():
                    raise PermissionError('Source compression flags cannot be set')
                return original(src, dst, **kwargs)
            with patch('shutil.copystat', side_effect=copy_attributes):
                stage_shaders(source, target, 'test')
            self.assertEqual((target / 'ios/b-test/shader.metallib').read_bytes(), b'library')
            self.assertEqual((target / 'shader_map.tsv').read_text(), 'map')

    def test_rhythm_heaven_declares_its_supported_languages(self):
        data = json.loads((ROOT / 'games/rhythm-heaven/game.json').read_text())
        self.assertEqual(shell_localizations(data['ios']), [
            'en', 'en-GB', 'fr', 'fr-CA', 'es', 'es-419', 'de', 'it',
            'nl', 'ja', 'ko', 'zh-Hans', 'zh-Hant',
        ])
        with self.assertRaises(WorkspaceError):
            shell_localizations({'localizations': []})
        with self.assertRaises(WorkspaceError):
            shell_localizations({'localizations': ['en', '']})

    def test_ipad_shell_uses_full_screen_landscape(self):
        with (ROOT / 'platform/ios/Info.plist').open('rb') as stream:
            info = plistlib.load(stream)
        self.assertEqual(info['UISupportedInterfaceOrientations~ipad'],
                         ['UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'])
        self.assertTrue(info['UIRequiresFullScreen'])

    def test_shell_is_a_game_with_game_mode_and_promotion(self):
        with (ROOT / 'platform/ios/Info.plist').open('rb') as stream:
            info = plistlib.load(stream)
        self.assertEqual(info['LSApplicationCategoryType'], 'public.app-category.games')
        self.assertIs(info['LSSupportsGameMode'], True)
        self.assertIs(info['CADisableMinimumFrameDurationOnPhone'], True)

    def test_controller_haptics_is_an_optional_boolean(self):
        data = json.loads((ROOT / 'games/rhythm-heaven/game.json').read_text())
        self.assertTrue(shell_controller_haptics(data['ios']))
        self.assertFalse(shell_controller_haptics({}))
        with self.assertRaises(WorkspaceError):
            shell_controller_haptics({'controller_haptics': 'yes'})

if __name__ == '__main__': unittest.main()
