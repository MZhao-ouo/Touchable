"""Check the Hayarigami adapter identity without game data."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from workspace import load_game, validate_registry


class HayarigamiTests(unittest.TestCase):
    def test_independent_app_and_save_identity(self):
        game = load_game('hayarigami')
        self.assertEqual(game.title_id, '010095b01af94000')
        self.assertEqual(game.bundle_id, 'com.guiyu.TouchableHayarigami')
        self.assertEqual(game.save_directory, 'Hayarigami')
        self.assertEqual(game.save_namespace, 'hayarigami')
        self.assertNotEqual(game.bundle_id, 'com.guiyu.hayarigami')
        validate_registry(game.root)

    def test_native_module_profile_is_pinned(self):
        game = load_game('hayarigami')
        self.assertEqual(game.module_names, ('main', 'subsdk0', 'sdk'))
        self.assertEqual(tuple(game.profile['runtime_modules']), game.module_names)
        self.assertNotIn('rtld', game.module_names)
        self.assertEqual(game.profile['upstream_revision'], '2eb5758771f110cd53082b999d88d242766610da')
        for name in (*game.module_names, 'rtld'):
            self.assertRegex(game.profile['modules'][name]['sha256'], r'^[0-9a-f]{64}$')
            self.assertRegex(game.profile['modules'][name]['build_id'], r'^[0-9a-f]{64}$')
        self.assertNotEqual(game.data['status'], 'adapted')


if __name__ == '__main__':
    unittest.main()
