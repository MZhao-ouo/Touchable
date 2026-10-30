#!/usr/bin/env python3
"""Load a registered game and keep its files separate from other games."""

from dataclasses import dataclass
import json
import os
from pathlib import Path
import re

from game_formats import GameError

WorkspaceError = GameError

ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = ROOT.parent
LOCAL_ROOT = PROJECT_ROOT / '.touchable'
DEPENDENCIES = LOCAL_ROOT / 'dependencies'
UPSTREAM = DEPENDENCIES / 'd2r-mobile'
PYTHON = LOCAL_ROOT / 'venv/bin/python'


def project_root(root=ROOT):
    root = Path(root).resolve()
    return root.parent if root.name == 'internal' else root


def state_root(root=ROOT):
    if Path(root).resolve() == ROOT and os.environ.get('TOUCHABLE_STATE_DIR'):
        return Path(os.environ['TOUCHABLE_STATE_DIR']).expanduser().resolve()
    return project_root(root) / '.touchable'


def read_local_config(root=ROOT):
    path = project_root(root) / '.touchable/config.json'
    if not path.exists():
        return {'schema_version': 1, 'games': {}}
    return _read_json(path, 'Local settings')
GAME_ID = re.compile(r"[a-z][a-z0-9-]{0,63}\Z")
RESERVED_ENVIRONMENT_KEYS = frozenset({
    "DM_SAVE", "DM_TEMP", "DM_ROMFS", "DM_MSL_DIR", "DM_NVN_SHMAP",
    "SS_INPUT_FILE", "SS_SHADER_CAPTURE",
})


def validate_game_id(value):
    if not isinstance(value, str) or not GAME_ID.fullmatch(value):
        raise GameError("Use a game ID that starts with a lowercase letter. Use only letters, digits, and hyphens.")
    return value


def _inside(base, relative, label):
    if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
        raise GameError(label + " must be a relative path.")
    result = (base / relative).resolve()
    if not result.is_relative_to(base.resolve()):
        raise GameError(label + " must stay inside " + str(base))
    return result


def _read_json(path, label):
    try:
        value = json.loads(path.read_text())
    except (OSError, ValueError) as error:
        raise GameError("Cannot read " + label + ": " + str(path)) from error
    if not isinstance(value, dict):
        raise GameError(label + " must be a JSON object: " + str(path))
    return value


@dataclass(frozen=True)
class GameConfig:
    root: Path
    id: str
    data: dict
    profile: dict

    @property
    def game_dir(self): return self.root / "games" / self.id
    @property
    def manifest_path(self): return self.game_dir / "game.json"
    @property
    def profile_path(self): return self.game_path(self.data["profile"])
    @property
    def product_name(self): return self.data["product_name"]
    @property
    def display_name(self): return self.data["display_name"]
    @property
    def bundle_id(self): return self.data["bundle_id"]
    @property
    def build_version(self): return str(self.data.get("build_version", 1))
    @property
    def save_namespace(self): return self.data.get("save_namespace", self.id)
    @property
    def save_directory(self): return self.data.get("save_directory", self.product_name)
    @property
    def minimum_ios_version(self): return str(self.data.get("ios", {}).get("deployment_target", "26.0"))
    @property
    def minimum_macos_version(self): return str(self.data.get("host", {}).get("deployment_target", "26.0"))
    @property
    def module_names(self): return tuple(self.data["module_names"])
    @property
    def runtime_sources(self): return tuple(self.game_path(p) for p in self.data["runtime_sources"])
    @property
    def common_runtime_sources(self): return tuple(self.root_path(p) for p in self.data["common_runtime_sources"])
    @property
    def ios_sources(self): return tuple(self.game_path(p) for p in self.data["ios_sources"])
    @property
    def common_ios_sources(self): return tuple(self.root_path(p) for p in self.data.get("common_ios_sources", []))
    @property
    def compile_defines(self): return tuple(self.data.get("compile_defines", []))
    @property
    def frameworks(self):
        values = self.data.get("frameworks", [])
        if not isinstance(values, list) or any(not isinstance(v, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", v) for v in values):
            raise WorkspaceError("frameworks must contain system framework names.")
        return tuple(values)
    @property
    def title_id(self): return self.data.get("title_id") or self.profile.get("title_id")
    @property
    def work_dir(self): return state_root(self.root) / "work" / self.id
    @property
    def base_dir(self): return self.work_dir / "base"
    @property
    def content_dir(self):
        """Extracted game content that builds use: the base game or base plus an update."""
        return self.work_dir / self.data.get("content_dir", "base")
    @property
    def patched_dir(self): return self.work_dir / self.data.get("patched_dir", "patched")
    @property
    def shaders_dir(self): return self.work_dir / "shaders"
    @property
    def capture_dir(self): return self.shaders_dir / "capture"
    @property
    def reports_dir(self): return self.work_dir / "reports"
    @property
    def build_dir(self): return state_root(self.root) / "build" / "games" / self.id
    @property
    def host_dir(self): return self.build_dir / "host"
    @property
    def ios_build_dir(self): return self.build_dir / "ios"
    @property
    def generated_dir(self): return self.build_dir / "ios-generated"
    @property
    def logs_dir(self): return self.build_dir / "logs"
    @property
    def ipa_dir(self): return project_root(self.root) / 'IPA' / self.id
    @property
    def project_dir(self): return self.build_dir / "ios-project"
    @property
    def ios_dir(self): return self.game_dir / "ios"
    @property
    def input_file(self): return self.host_dir / "input.txt"
    @property
    def save_dir(self): return state_root(self.root) / 'saves' / self.id / self.save_namespace
    @property
    def temp_dir(self): return self.work_dir / "temp"
    @property
    def shader_chain_header(self): return self.game_path(self.data.get("shader_chain_header", "runtime/ss_shader_chain.h"))

    def game_path(self, relative): return _inside(self.game_dir, relative, "Game path")
    def root_path(self, relative): return _inside(self.root, relative, "Workspace path")

    def require_title_id(self):
        if not self.title_id:
            raise GameError("Set title_id for " + self.id + " before extraction.")
        return self.title_id

    def require_adapted(self, operation="runtime"):
        # in-progress allows development builds. Only a tested game is marked adapted.
        if self.data["status"] not in ("adapted", "in-progress"):
            raise GameError(self.id + " is not adapted. Add and verify its adapter before " + operation + ".")
        adapter = self.data.get("adapters", {}).get(operation)
        if not isinstance(adapter, str) or not adapter:
            raise GameError(self.id + " has no adapter for " + operation + ".")
        if adapter.startswith("silksong") and self.id != "silksong":
            raise GameError("The Silksong adapter cannot be used for " + self.id + ".")
        return adapter


def load_game(game="silksong", root=ROOT):
    root = Path(root).resolve()
    game = validate_game_id(game)
    directory = _inside(root / "games", game, "Game directory")
    manifest = directory / "game.json"
    if not manifest.is_file():
        raise GameError("Game is not registered: " + game + ". Use Touchable.command to select a game.")
    data = _read_json(manifest, "game manifest")
    overrides = read_local_config(root).get('games', {}).get(game, {})
    if 'bundle_id' in overrides:
        data['bundle_id'] = overrides['bundle_id']
    if data.get("schema_version") != 1 or data.get("id") != game:
        raise GameError("The game manifest has an invalid schema_version or id: " + str(manifest))
    if data.get("status") not in ("adapted", "in-progress", "not-adapted"):
        raise GameError("Game status must be adapted, in-progress, or not-adapted: " + str(manifest))
    for name in ("display_name", "product_name", "bundle_id", "profile"):
        if not isinstance(data.get(name), str) or not data[name]:
            raise GameError("The game manifest needs " + name + ": " + str(manifest))
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", data["product_name"]):
        raise GameError("product_name must be an app product identifier.")
    if not re.fullmatch(r"[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+", data["bundle_id"]):
        raise GameError("bundle_id must use reverse domain form.")
    validate_game_id(data.get("save_namespace", game))
    if not re.fullmatch(r"[A-Za-z0-9_-]+", data.get("save_directory", data["product_name"])):
        raise GameError("save_directory must be one folder name.")
    for name in ("content_dir", "patched_dir"):
        if name in data and (not isinstance(data[name], str) or not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{0,63}", data[name])
                             or data[name] in ("saves", "reports", "shaders", "temp")):
            raise GameError(name + " must be one lowercase folder name inside work/<game>.")
    for name in ("module_names", "runtime_sources", "common_runtime_sources", "ios_sources", "common_ios_sources", "compile_defines"):
        values = data.get(name, [] if name in ("common_ios_sources", "compile_defines") else None)
        if not isinstance(values, list) or any(not isinstance(value, str) or not value for value in values):
            raise GameError(name + " must be a list of strings.")
        if len(values) != len(set(values)):
            raise GameError(name + " has duplicate entries.")
    if any(not re.fullmatch(r"main|sdk|subsdk\d+", name) for name in data["module_names"]):
        raise GameError("module_names must contain executable NSO names. Do not run rtld.")
    if any(not re.fullmatch(r"[A-Za-z_]\w*(?:=[A-Za-z0-9_.+-]+)?", name) for name in data.get("compile_defines", [])):
        raise GameError("compile_defines contains an invalid C define.")
    profile = _read_json(_inside(directory, data["profile"], "Profile"), "game profile")
    environment = profile.get("environment", {})
    if not isinstance(environment, dict):
        raise GameError("profile.environment must be an object with string keys and string values.")
    for key, value in environment.items():
        if not isinstance(key, str) or not isinstance(value, str):
            raise GameError("profile.environment must contain string keys and string values.")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key) or "\0" in value:
            raise GameError("profile.environment has an invalid environment name or value.")
        if key in RESERVED_ENVIRONMENT_KEYS:
            raise GameError("profile.environment cannot set " + key + ". The workspace sets game paths.")
    config = GameConfig(root, game, data, profile)
    if config.title_id is not None and not re.fullmatch(r"[0-9a-f]{16}", config.title_id):
        raise GameError("title_id must contain 16 lowercase hexadecimal digits.")
    if data.get("title_id") and profile.get("title_id") and data["title_id"] != profile["title_id"]:
        raise GameError("The manifest and profile title_id values do not match.")
    if profile.get("runtime_modules") is not None and tuple(profile["runtime_modules"]) != config.module_names:
        raise GameError("module_names must match profile.runtime_modules.")
    # Resolve all configured paths now. Reject traversal before any command writes files.
    config.runtime_sources, config.common_runtime_sources, config.ios_sources, config.common_ios_sources, config.shader_chain_header
    return config


def registered_games(root=ROOT):
    directory = Path(root).resolve() / "games"
    return [path.parent.name for path in sorted(directory.glob("*/game.json"))]


def registry_identity_errors(root=ROOT, candidate=None):
    """Check app and save identities before a command creates any output."""
    errors, configs = [], []
    for game_id in registered_games(root):
        try: configs.append(load_game(game_id, root))
        except GameError as error: errors.append(str(error))
    if candidate is not None:
        if any(config.id == candidate.id for config in configs):
            errors.append("Game is already registered: " + candidate.id)
        else: configs.append(candidate)
    bundles, saves, products = {}, {}, {}
    for config in configs:
        for label, value, seen in (("bundle_id", config.bundle_id, bundles),
                                   ("save_namespace", config.save_namespace, saves),
                                   ("product_name", config.product_name.casefold(), products)):
            if value in seen:
                errors.append(label + " is shared by " + seen[value] + " and " + config.id + ". Use a separate value.")
            else: seen[value] = config.id
    return errors


def validate_registry(root=ROOT, candidate=None):
    """Raise WorkspaceError if registered app or save identities are not unique."""
    errors = registry_identity_errors(root, candidate)
    if errors: raise GameError("\n".join(errors))


def check_game(config, files=True):
    """Return missing adapter inputs. A new scaffold is valid but is not runnable."""
    if config.data["status"] == "not-adapted":
        return []
    errors = []
    for operation in ("runtime", "ios", "rewrite", "shader_extract", "shader_build"):
        try: config.require_adapted(operation)
        except GameError as error: errors.append(str(error))
    if not config.module_names or "main" not in config.module_names:
        errors.append("The adapter needs a main module.")
    if not config.runtime_sources:
        errors.append("The adapter needs game runtime sources.")
    if not config.ios_sources and not config.common_ios_sources:
        errors.append("The adapter needs iOS sources.")
    if files:
        for path in (*config.runtime_sources, *config.common_runtime_sources, *config.ios_sources, *config.common_ios_sources):
            if not path.is_file(): errors.append("File not found: " + str(path))
    return errors
