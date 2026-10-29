"""Test game registration and path isolation with synthetic files only."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from game_formats import GameError
from games import check_registry, scaffold
from workspace import RESERVED_ENVIRONMENT_KEYS, check_game, load_game, registered_games, validate_registry
import analyze_game
import build_shaders
import extract_shaders
import prepare_game
import rewrite_game


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()

    def create(self, game="first-game", **keywords):
        scaffold(self.root, game, **keywords)
        return load_game(game, self.root)

    def change_manifest(self, game, **fields):
        path = self.root / "games" / game / "game.json"
        data = json.loads(path.read_text())
        data.update(fields)
        path.write_text(json.dumps(data))

    def change_profile(self, game, **fields):
        path = self.root / "games" / game / "profile.json"
        data = json.loads(path.read_text())
        data.update(fields)
        path.write_text(json.dumps(data))

    def test_extra_frameworks_are_explicit_names(self):
        self.create()
        self.change_manifest('first-game',frameworks=['VideoToolbox','CoreMedia'])
        self.assertEqual(getattr(load_game('first-game',self.root),'frameworks',None),('VideoToolbox','CoreMedia'))

    def test_rejects_framework_paths(self):
        self.create()
        self.change_manifest('first-game',frameworks=['../Private'])
        with self.assertRaises(GameError):
            getattr(load_game('first-game',self.root),'frameworks',None)

    def test_game_paths_and_names_do_not_overlap(self):
        first, second = self.create(), self.create("second-game")
        for attribute in ("game_dir", "base_dir", "patched_dir", "capture_dir", "reports_dir",
                          "build_dir", "host_dir", "ios_build_dir", "generated_dir", "project_dir",
                          "input_file", "save_dir", "temp_dir", "ipa_dir", "logs_dir", "shader_chain_header"):
            with self.subTest(attribute=attribute):
                self.assertNotEqual(getattr(first, attribute), getattr(second, attribute))
        self.assertEqual(first.base_dir, self.root / ".touchable/work/first-game/base")
        self.assertEqual(first.save_dir, self.root / ".touchable/saves/first-game/first-game")
        self.assertEqual(first.project_dir, self.root / ".touchable/build/games/first-game/ios-project")
        self.assertEqual(first.ios_dir, self.root / "games/first-game/ios")
        self.assertNotEqual(first.bundle_id, second.bundle_id)
        self.assertNotEqual(first.product_name, second.product_name)

    def test_scaffold_is_not_a_runnable_adapter(self):
        config = self.create()
        self.assertEqual(config.data["status"], "not-adapted")
        self.assertEqual(config.minimum_ios_version, "26.0")
        self.assertEqual(config.minimum_macos_version, "26.0")
        self.assertEqual(config.runtime_sources, ())
        self.assertEqual(config.module_names, ())
        self.assertEqual(check_game(config), [])
        self.assertFalse(config.build_dir.exists())
        self.assertFalse(config.work_dir.exists())
        self.assertEqual(registered_games(self.root), ["first-game"])
        for operation in ("runtime", "ios", "rewrite", "shader_extract", "shader_build"):
            with self.subTest(operation=operation), self.assertRaises(GameError): config.require_adapted(operation)
        with self.assertRaises(GameError): config.require_title_id()

    def test_invalid_scaffold_leaves_no_game_directory(self):
        for game in ("../other", "/tmp/game", "BadGame", "a/b", "", "a;command"):
            with self.subTest(game=game), self.assertRaises(GameError): scaffold(self.root, game)
        with self.assertRaises(GameError): self.create(bundle_id="bad bundle")
        self.assertFalse((self.root / "games/first-game").exists())

    def test_scaffold_does_not_overwrite_game_or_user_files(self):
        config = self.create()
        sentinel = config.game_dir / "own-file.txt"
        sentinel.write_text("preserve me")
        with self.assertRaises(GameError): self.create()
        self.assertEqual(sentinel.read_text(), "preserve me")
        package = self.root / "MyGameFiles/local.nsp"
        package.parent.mkdir()
        package.write_bytes(b"user source")
        self.create("second-game")
        self.assertEqual(package.read_bytes(), b"user source")

    def test_configuration_rejects_path_escape_and_symlinks(self):
        config = self.create()
        outside = self.root / "outside.json"
        outside.write_text('{}')
        for profile in ("../../outside.json", str(outside)):
            self.change_manifest(config.id, profile=profile)
            with self.subTest(profile=profile), self.assertRaises(GameError): load_game(config.id, self.root)
        self.change_manifest(config.id, profile="link.json")
        (config.game_dir / "link.json").symlink_to(outside)
        with self.assertRaises(GameError): load_game(config.id, self.root)
        self.change_manifest(config.id, profile="profile.json", runtime_sources=["../../outside.c"])
        with self.assertRaises(GameError): load_game(config.id, self.root)
        self.change_manifest(config.id, runtime_sources=[])
        (self.root / "games/linked").symlink_to(config.game_dir)
        # The target manifest ID is checked, too.
        with self.assertRaises(GameError): load_game("linked", self.root)

    def test_registry_rejects_shared_app_identity(self):
        first, second = self.create(), self.create("second-game")
        self.change_manifest(second.id, bundle_id=first.bundle_id, save_namespace=first.save_namespace)
        _, errors = check_registry(self.root)
        self.assertTrue(any("bundle_id" in error for error in errors))
        self.assertTrue(any("save_namespace" in error for error in errors))
        with self.assertRaises(GameError): validate_registry(self.root)

    def test_registry_preflight_and_scaffold_reject_duplicate_identity(self):
        first = self.create()
        validate_registry(self.root)
        for game, keywords in (("same-bundle", {"bundle_id": first.bundle_id}),
                               ("same-product", {"product_name": first.product_name.lower()})):
            with self.subTest(game=game), self.assertRaises(GameError): self.create(game, **keywords)
            self.assertFalse((self.root / "games" / game).exists())
        self.change_manifest(first.id, save_namespace="future-game")
        with self.assertRaises(GameError): self.create("future-game")
        self.assertFalse((self.root / "games/future-game").exists())
        self.assertFalse(any(path.name.startswith(".game-scaffold-") for path in (self.root / "games").iterdir()))

    def test_environment_accepts_string_settings_and_rejects_invalid_values(self):
        config = self.create()
        environment = {"DM_AUDIO": "1", "DM_PSO_MANIFEST": "off", "OPTIONAL_VALUE": ""}
        self.change_profile(config.id, environment=environment)
        self.assertEqual(load_game(config.id, self.root).profile["environment"], environment)
        for environment in ([], "not-an-object", None, {"DM_AUDIO": 1}, {"DM_AUDIO": False},
                            {"DM_AUDIO": None}, {"BAD=NAME": "value"}, {"BAD NAME": "value"},
                            {"VALID_NAME": "contains\0null"}):
            self.change_profile(config.id, environment=environment)
            with self.subTest(environment=environment), self.assertRaises(GameError): load_game(config.id, self.root)

    def test_environment_cannot_replace_workspace_paths(self):
        config = self.create()
        for key in RESERVED_ENVIRONMENT_KEYS:
            self.change_profile(config.id, environment={key: "/another/game/path"})
            with self.subTest(key=key), self.assertRaises(GameError): load_game(config.id, self.root)

    def test_module_profile_mismatch_and_rtld_are_rejected(self):
        config = self.create()
        self.change_manifest(config.id, module_names=["rtld"])
        with self.assertRaises(GameError): load_game(config.id, self.root)
        self.change_manifest(config.id, module_names=["main"])
        with self.assertRaises(GameError): load_game(config.id, self.root)

    def test_new_game_cannot_reuse_silksong_adapter(self):
        config = self.create()
        self.change_manifest(config.id, status="adapted", adapters={"runtime": "silksong"})
        with self.assertRaises(GameError): load_game(config.id, self.root).require_adapted()

    def test_analysis_uses_selected_game_paths(self):
        config = self.create()
        with patch.object(analyze_game, "load_game", return_value=config), \
             patch.object(analyze_game, "analyze") as analyze, \
             patch.object(sys, "argv", ["analyze_game.py", "--game", config.id]):
            self.assertEqual(analyze_game.main(), 0)
        self.assertEqual(analyze.call_args.args[0], config.content_dir)
        self.assertEqual(analyze.call_args.args[2], config.reports_dir)
        self.assertIs(analyze.call_args.args[3], config)

    def test_extraction_uses_selected_title_and_output(self):
        config = self.create(title_id="0100000000000022")
        with patch.object(prepare_game, "load_game", return_value=config), \
             patch.object(prepare_game, "extract") as extract, \
             patch.object(sys, "argv", ["prepare_game.py", "extract", "synthetic.nsp", "--keys", "own.keys", "--game", config.id]):
            self.assertEqual(prepare_game.main(), 0)
        self.assertEqual(extract.call_args.args[3:], (config.base_dir, "0100000000000022", config.id))

    def test_unadapted_commands_fail_before_external_tools_or_outputs(self):
        config = self.create()
        with patch.object(rewrite_game, "load_game", return_value=config), \
             patch.object(rewrite_game.subprocess, "check_output") as external, \
             patch.object(sys, "argv", ["rewrite_game.py", "--game", config.id]):
            self.assertEqual(rewrite_game.main(), 1)
            external.assert_not_called()
        with patch.object(build_shaders, "load_game", return_value=config), \
             patch.object(build_shaders, "run") as external, \
             patch.object(sys, "argv", ["build_shaders.py", "--game", config.id]):
            with self.assertRaises(GameError): build_shaders.main()
            external.assert_not_called()
        with patch.object(extract_shaders, "load_game", return_value=config), \
             patch.object(sys, "argv", ["extract_shaders.py", "--game", config.id]):
            self.assertEqual(extract_shaders.main(), 1)
        self.assertFalse(config.build_dir.exists())
        self.assertFalse(config.work_dir.exists())


if __name__ == "__main__": unittest.main()
