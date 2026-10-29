#!/usr/bin/env python3
"""Build and install one selected game. Keep local state outside the source tree."""
import argparse
from contextlib import contextmanager, nullcontext
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import uuid
import zipfile

from analyze_game import load_nso
from workspace import (ROOT, PROJECT_ROOT, LOCAL_ROOT, DEPENDENCIES, UPSTREAM, PYTHON,
                       WorkspaceError, load_game, read_local_config, registered_games,
                       state_root, validate_registry)


def stamp():
    return datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6]


def sha256(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(8 << 20), b''):
            value.update(block)
    return value.hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    with temporary.open('x') as stream:
        os.chmod(temporary, 0o600)
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    temporary.replace(path)


def choose(title, values, label=str):
    if not values:
        raise WorkspaceError('No items to select: ' + title)
    print('\n' + title)
    for index, value in enumerate(values, 1):
        print(f'  {index}. {label(value)}')
    print('  0. Back or quit')
    while True:
        answer = input('Type a number: ').strip()
        if answer == '0':
            raise KeyboardInterrupt
        if answer.isdigit() and 1 <= int(answer) <= len(values):
            return values[int(answer) - 1]
        print('Type a number from the list.')


def run(command, log, title):
    print('\n' + title, flush=True)
    print('Log: ' + str(log), flush=True)
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('w') as output:
        process = subprocess.Popen([str(v) for v in command], cwd=ROOT, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            while True:
                try:
                    result = process.wait(timeout=20)
                    break
                except subprocess.TimeoutExpired:
                    print('Still working. Please wait.', flush=True)
        except KeyboardInterrupt:
            import signal
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise
    if result:
        # Commands that process keys already suppress their raw output.
        print('\n'.join(log.read_text(errors='replace').splitlines()[-12:]))
        raise WorkspaceError(title + ' failed. Read the log: ' + str(log))


def tool(name, game, *arguments):
    return [PYTHON, ROOT / 'tools' / name, '--game', game.id, *arguments]


@contextmanager
def workspace_lock():
    LOCAL_ROOT.mkdir(parents=True, exist_ok=True)
    with (LOCAL_ROOT / 'operation.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise WorkspaceError('Another build or installation is running. Wait for it to complete.')
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def preflight():
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise WorkspaceError('An Apple Silicon Mac is necessary.')
    for command in (['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'],
                    ['xcrun', '--sdk', 'macosx', '--show-sdk-path'], ['xcodebuild', '-version']):
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    validate_registry(ROOT)
    print('The Mac, Xcode, and game configuration checks passed.')


def upstream_patch(upstream, expected):
    """Return the upstream diff. Earlier bootstraps left the new patch files untracked, so git diff omitted them."""
    created = [m.decode() for m in re.findall(rb'^diff --git a/(.+?) b/.+\nnew file mode ', expected, re.M)]
    if created:
        untracked = subprocess.check_output(['git', '-C', str(upstream), 'ls-files', '-z', '--others', '--', *created])
        untracked = [p for p in untracked.decode().split('\0') if p]
        if untracked:
            subprocess.run(['git', '-C', str(upstream), 'add', '--intent-to-add', '--force', '--', *untracked], check=True)
    return subprocess.check_output(['git', '-C', str(upstream), 'diff', 'HEAD', '--binary', '--full-index', '--no-ext-diff', '--no-color'])


def prepare_dependencies(game, logs):
    required = [DEPENDENCIES / 'hactool/hactool', UPSTREAM / 'dm/tools/patch_nso.py']
    if not all(path.is_file() for path in required):
        run(['bash', ROOT / 'tools/bootstrap.sh'], logs / 'bootstrap.log', 'Prepare the extraction tools')
    revision = subprocess.check_output(['git', '-C', str(UPSTREAM), 'rev-parse', 'HEAD'], text=True).strip()
    if game.profile.get('upstream_revision') != revision:
        raise WorkspaceError('The upstream source revision does not match. Read the developer documentation.')
    expected = (ROOT / 'patches/d2r-mobile.patch').read_bytes()
    if upstream_patch(UPSTREAM, expected) != expected:
        raise WorkspaceError('The upstream source does not match the project patch. Check the local changes first.')
    required = [DEPENDENCIES / 'SPIRV-Cross/spirv-cross', DEPENDENCIES / 'dotnet/dotnet',
                UPSTREAM / 'dm/gfx/tools/dmsh_xlate/bin/Release/net10.0/dmsh_xlate.dll']
    if not all(path.is_file() for path in required):
        run(['bash', ROOT / 'tools/bootstrap_shader.sh'], logs / 'bootstrap-shader.log', 'Prepare the shader tools')


def select_file(title, paths, interactive):
    paths = sorted(set(p.resolve() for p in paths if p.is_file()))
    if len(paths) == 1:
        print(title + ': ' + paths[0].name)
        return paths[0]
    if paths and interactive:
        return choose(title, paths, lambda p: str(p.relative_to(PROJECT_ROOT)))
    raise WorkspaceError(title + ': the file is missing or there are multiple candidates. Put the file in the game folder, or select it in the menu.')


def input_files(game, interactive):
    settings = read_local_config().get('games', {}).get(game.id, {})
    directory = PROJECT_ROOT / 'MyGameFiles' / game.id
    packages = list(directory.rglob('*.nsp'))
    if not packages and settings.get('input_directory'):
        packages = list(Path(settings['input_directory']).rglob('*.nsp'))
    version = game.profile.get('package_version', 0)
    # Names are selection hints only. Extraction and module checks verify the input.
    base = [p for p in packages if re.search(r'\[v0\]|\[base\]|^base\.', p.name, re.I)]
    if not base and len(packages) == 1 and not version:
        base = packages
    if not base and interactive:
        base = packages
    source = select_file('Select the base NSP', base, interactive)
    update = None
    if version:
        updates = [p for p in packages if p != source and re.search(r'\[v' + str(version) + r'\]|^update\.', p.name, re.I)]
        if not updates and interactive:
            updates = [p for p in packages if p != source]
        update = select_file(f'Select the v{version} update NSP', updates, interactive)
    keys = list((PROJECT_ROOT / 'MyGameFiles/keys').rglob('prod.keys'))
    # Do not ask twice when two local copies contain the same keys.
    unique = {}
    for path in keys:
        unique.setdefault(sha256(path), path)
    key = select_file('Select prod.keys', list(unique.values()), interactive)
    return source, update, key


def verify_modules(game):
    for name, expected in game.profile['modules'].items():
        actual = load_nso(game.content_dir / 'ExeFS' / name)
        if any(actual[key] != expected[key] for key in ('sha256', 'build_id')):
            raise WorkspaceError('This game version is not supported. Module does not match: ' + name)


def rewrite_is_current(game):
    path = game.patched_dir / 'verification.json'
    if not path.is_file():
        return False
    report = json.loads(path.read_text())
    for name in game.module_names:
        record = report.get('modules', {}).get(name, {})
        if record.get('source_sha256') != game.profile['modules'][name]['sha256']:
            return False
        if record.get('structure') != 'passed' or record.get('equivalence') != 'passed':
            return False
        outputs = record.get('output_sha256', {})
        if name + '.patched.bin' not in outputs or name + '.fixups.tsv' not in outputs:
            return False
        for filename, digest in outputs.items():
            if Path(filename).name != filename or not (game.patched_dir / filename).is_file():
                return False
            if sha256(game.patched_dir / filename) != digest:
                return False
    tools = report.get('current_tool_sha256', {})
    return bool(tools) and all(sha256(UPSTREAM / name) == digest for name, digest in tools.items())


def prepare_game(game, logs, interactive):
    if not game.content_dir.exists():
        source, update, key = input_files(game, interactive)
        command = [PYTHON, ROOT / 'tools/prepare_game.py', 'extract-update' if update else 'extract', source]
        if update:
            command.append(update)
        command += ['--game', game.id, '--keys', key, '--out', game.content_dir]
        run(command, logs / 'extract.log', 'Extract and check the game files')
    verify_modules(game)
    if not rewrite_is_current(game):
        if game.patched_dir.exists():
            raise WorkspaceError('The existing rewrite files do not match the record. Select "Full rebuild from NSP" in the menu.')
        run(tool('rewrite_game.py', game, '--jobs', '6'), logs / 'rewrite.log', 'Rewrite and check the CPU modules')
    print('The game version, module, and rewrite file checks passed.')
    # Rebuild shaders when tools or translation options change.
    from build_shaders import tool_fingerprint
    fingerprint = tool_fingerprint(game)
    chain_file = game.shaders_dir / 'chain.json'
    chain = json.loads(chain_file.read_text()) if chain_file.exists() else {}
    controls = list(game.capture_dir.glob('*.control'))
    libraries = game.shaders_dir / 'ios' / ('b-' + chain.get('chain', ''))
    current = (chain.get('game_id') == game.id and chain.get('tools') == fingerprint and bool(controls)
               and all((libraries / (control.stem + '_b' + suffix)).is_file()
                       for control in controls for suffix in ('.metallib', '.json', '.metal')))
    if not current:
        run(tool('extract_shaders.py', game), logs / 'extract-shaders.log', 'Extract the shaders')
        run(tool('build_shaders.py', game, '--ios'), logs / 'build-shaders.log', 'Build the Metal shaders')
    seconds = game.profile.get('shader_warmup_seconds', 0)
    if seconds:
        inputs = {'tools': fingerprint, 'modules': game.profile['modules'],
                  'capture_runtime': sha256(ROOT / 'runtime/common/ss_shader_capture.c'),
                  'backend': sha256(UPSTREAM / 'dm/gfx/nvn/nvn_mtl.m')}
        marker = game.shaders_dir / 'startup-verification.json'
        if marker.is_file() and json.loads(marker.read_text()).get('inputs') == inputs:
            return
        run(tool('build_host.py', game), logs / 'build-warmup-host.log', 'Build the first-run check program')
        for attempt in range(3):
            before = {p.name for p in game.capture_dir.glob('*.control')}
            save = game.temp_dir / ('shader-startup-' + stamp())
            run(tool('run_host.py', game, '--seconds', str(seconds), '--save-dir', save),
                logs / f'startup-{attempt}.log', 'Check the first launch and collect the built-in shaders')
            text = (game.logs_dir / 'host-boot.log').read_text(errors='replace')
            shutil.copy2(game.logs_dir / 'host-boot.log', logs / f'startup-{attempt}-runtime.log')
            after = {p.name for p in game.capture_dir.glob('*.control')}
            if after != before:
                run(tool('build_shaders.py', game, '--ios'), logs / f'startup-{attempt}-shaders.log', 'Add the missing first-run shaders')
                continue
            tool_log = (logs / f'startup-{attempt}.log').read_text()
            if 'Boot time limit reached:' not in tool_log:
                raise WorkspaceError('The first-run check stopped early. Read the startup log.')
            # These Chinese strings match upstream runtime log lines. Do not translate them.
            if '计时 至帧' not in text:
                raise WorkspaceError('The first run did not complete the frame check. Read the startup log.')
            if any(message in text for message in ('不在映射表里', '加载着色器失败', '程序没有顶点着色器', '翻译链版本的')):
                raise WorkspaceError('The first run still has missing shaders. The build stopped. Read the startup log.')
            write_json(marker, {'inputs': inputs, 'log': str(logs / f'startup-{attempt}-runtime.log')})
            break
        else:
            raise WorkspaceError('The first-run shaders did not become stable. Read the startup log.')


def decode_profile(path):
    result = subprocess.run(['security', 'cms', '-D', '-i', str(path)], capture_output=True)
    if result.returncode:
        raise WorkspaceError('Cannot read the provisioning profile.')
    return plistlib.loads(result.stdout)


def local_profiles(game):
    candidates = list((Path.home() / 'Library/Developer/Xcode/UserData/Provisioning Profiles').glob('*.mobileprovision'))
    candidates += list((Path.home() / 'Library/MobileDevice/Provisioning Profiles').glob('*.mobileprovision'))
    candidates += list(game.build_dir.glob('ios/Build/Products/*/*.app/embedded.mobileprovision'))
    matches = []
    for path in candidates:
        try:
            profile = decode_profile(path)
        except (WorkspaceError, ValueError):
            continue
        entitlements = profile.get('Entitlements', {})
        if entitlements.get('application-identifier', '').split('.', 1)[-1] != game.bundle_id:
            continue
        if profile['ExpirationDate'] <= datetime.now(timezone.utc).replace(tzinfo=None):
            continue
        certificates = {hashlib.sha1(data).hexdigest().upper() for data in profile.get('DeveloperCertificates', [])}
        identities = subprocess.check_output(['security', 'find-identity', '-v', '-p', 'codesigning'], text=True)
        identity = next((value for value in certificates if value in identities), None)
        if identity:
            matches.append((path, profile, identity))
    return sorted(matches, key=lambda value: value[1]['ExpirationDate'], reverse=True)


def signing_settings(game, interactive):
    config = read_local_config()
    settings = config.setdefault('games', {}).setdefault(game.id, {})
    # Existing users keep their installed application identity.
    profiles = local_profiles(game)
    if profiles and settings.get('bundle_id') and settings.get('signing_mode') != 'automatic':
        return {'mode': 'manual', 'profile': profiles[0]}
    team = config.get('team_id')
    if not team and interactive:
        print('First, sign in to your Apple Account in Xcode Settings > Accounts.')
        team = input('Type the development team Team ID: ').strip()
    if not team or not re.fullmatch(r'[A-Z0-9]{10}', team):
        raise WorkspaceError('Complete the signing settings in the menu. A valid Team ID is necessary.')
    config['team_id'] = team
    if not settings.get('bundle_id'):
        prefix = config.setdefault('bundle_prefix', 'dev.touchable.user' + uuid.uuid4().hex[:12])
        settings['bundle_id'] = prefix + '.' + game.id
    write_json(LOCAL_ROOT / 'config.json', config)
    return {'mode': 'automatic', 'team': team}


def configure_signing(game):
    config = read_local_config()
    print('Find the Team ID of your development team in Xcode Settings > Accounts.')
    team = input('Type the Team ID: ').strip()
    if not re.fullmatch(r'[A-Z0-9]{10}', team):
        raise WorkspaceError('The Team ID must contain 10 uppercase letters or digits.')
    mode = choose('Select the signing method', ['automatic', 'local'],
                  lambda value: 'Use Xcode automatic signing' if value == 'automatic' else 'Reuse a valid local signature first')
    config['team_id'] = team
    config.setdefault('games', {}).setdefault(game.id, {})['signing_mode'] = mode
    write_json(LOCAL_ROOT / 'config.json', config)
    print('Signing settings saved. The existing app identity and save path did not change.')


def build(game, interactive=False, device=None):
    game.require_adapted('ios')
    run_id = stamp()
    logs = game.logs_dir / run_id
    logs.mkdir(parents=True)
    signing = signing_settings(game, interactive)
    game = load_game(game.id)
    prepare_dependencies(game, logs)
    prepare_game(game, logs, interactive)
    run(tool('prepare_ios.py', game), logs / 'generate-ios.log', 'Generate the iPhone/iPad project')
    derived = game.build_dir / 'runs' / run_id
    command = ['xcodebuild', '-project', game.project_dir / (game.product_name + '.xcodeproj'),
               '-scheme', game.product_name, '-configuration', 'Debug', '-sdk', 'iphoneos',
               '-derivedDataPath', derived, 'build']
    if signing['mode'] == 'manual':
        command += ['CODE_SIGNING_ALLOWED=NO', 'CODE_SIGNING_REQUIRED=NO']
    else:
        command += ['-allowProvisioningUpdates', 'DEVELOPMENT_TEAM=' + signing['team']]
        if not device:
            connected = devices()
            if len(connected) == 1:
                device = connected[0][0]
            elif connected and interactive:
                device = choose('Select the iPhone or iPad for signing', connected, lambda value: value[1])[0]
        if device:
            command += ['-destination', 'id=' + device, '-allowProvisioningDeviceRegistration']
    run(command, logs / 'xcodebuild.log', 'Build the iPhone/iPad app')
    app = derived / 'Build/Products/Debug-iphoneos' / (game.product_name + '.app')
    if signing['mode'] == 'manual':
        path, profile, identity = signing['profile']
        shutil.copy2(path, app / 'embedded.mobileprovision')
        entitlements = logs / 'entitlements.plist'
        with entitlements.open('wb') as stream:
            plistlib.dump(profile['Entitlements'], stream)
        run(['codesign', '--force', '--sign', identity, '--entitlements', entitlements,
             '--generate-entitlement-der', app], logs / 'codesign.log', 'Sign the app')
    output = game.ipa_dir / f'{game.product_name}-build{game.build_version}-{run_id}.ipa'
    run(tool('package_ios.py', game, '--app', app, '--output', output), logs / 'package.log', 'Make and check the IPA')
    # Retain previous IPAs privately. Update the visible output only after verification.
    for old in game.ipa_dir.glob('*.ipa'):
        if old == output:
            continue
        archive = LOCAL_ROOT / 'archive/ipa' / game.id / run_id
        archive.mkdir(parents=True, exist_ok=True)
        for source in (old, old.with_suffix('.json'), old.with_suffix('.sha256')):
            if source.exists():
                source.rename(archive / source.name)
    record = {'app': str(app), 'ipa': str(output), 'logs': str(logs)}
    write_json(game.build_dir / 'latest.json', record)
    write_json(LOCAL_ROOT / 'build/games' / game.id / 'latest.json', record)
    print('\nBuild complete. IPA: ' + str(output))
    return output


def device_json(arguments):
    with tempfile.TemporaryDirectory(prefix='device-', dir=LOCAL_ROOT) as directory:
        path = Path(directory) / 'result.json'
        result = subprocess.run(['xcrun', 'devicectl', *arguments, '--timeout', '60', '--json-output', str(path)], capture_output=True)
        if result.returncode:
            raise WorkspaceError('Cannot read the device information. Unlock the device, then check the connection, trust, and Developer Mode.')
        return json.loads(path.read_text())['result']


def devices():
    values = []
    for device in device_json(['list', 'devices']).get('devices', []):
        hardware = device.get('hardwareProperties', {})
        properties = device.get('properties', {})
        hardware = properties.get('hardware', hardware)
        connection = device.get('connectionProperties', {})
        state = properties.get('state', device.get('deviceProperties', {}))
        # CoreDevice closes idle tunnels. A paired, booted phone can reconnect on demand.
        if (hardware.get('reality') == 'physical' and connection.get('pairingState') == 'paired'
                and state.get('bootState') == 'booted'):
            values.append((hardware.get('udid', device['identifier']),
                           properties.get('state', {}).get('name', device.get('deviceProperties', {}).get('name', 'iPhone'))))
    return values


def verify_ipa(game, path, destination):
    report_path = path.with_suffix('.json')
    if not report_path.is_file():
        raise WorkspaceError('The IPA has no build record. Build again.')
    report = json.loads(report_path.read_text())
    if report.get('game_id') != game.id or report.get('bundle_id') != game.bundle_id:
        raise WorkspaceError('The IPA does not match the selected game or the local app identity.')
    print('Checking the IPA checksum.', flush=True)
    if sha256(path) != report.get('sha256'):
        raise WorkspaceError('The IPA check failed. Build again.')
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        for name in names:
            relative = Path(name)
            if relative.is_absolute() or '..' in relative.parts or not name.startswith('Payload/'):
                raise WorkspaceError('The IPA contains an invalid path.')
        archive.extractall(destination)
    apps = list((destination / 'Payload').glob('*.app'))
    if len(apps) != 1:
        raise WorkspaceError('The IPA must contain exactly one app.')
    app = apps[0]
    with (app / 'Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if info.get('CFBundleIdentifier') != game.bundle_id or info.get('TouchableGameID') != game.id:
        raise WorkspaceError('The app identity in the IPA does not match.')
    # zipfile does not restore executable modes. Restore only the declared executable.
    executable = info['CFBundleExecutable']
    if Path(executable).name != executable:
        raise WorkspaceError('The app executable name is invalid.')
    (app / executable).chmod(0o755)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    return app, decode_profile(app / 'embedded.mobileprovision')


def install(game, ipa, device=None, interactive=False):
    if not device:
        connected = devices()
        if not connected:
            raise WorkspaceError('No iPhone or iPad is connected. Connect and unlock the device.')
        if len(connected) == 1:
            device = connected[0][0]
        elif interactive:
            device = choose('Select an iPhone or iPad', connected, lambda value: value[1])[0]
        else:
            raise WorkspaceError('More than one iPhone or iPad is connected. Select one in the menu.')
    logs = game.logs_dir / ('install-' + stamp())
    logs.mkdir(parents=True)
    game.build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='install-', dir=game.build_dir) as directory:
        app, profile = verify_ipa(game, ipa, Path(directory))
        if profile['ExpirationDate'] <= datetime.now(timezone.utc).replace(tzinfo=None):
            raise WorkspaceError('The signature expired. Build again.')
        if not profile.get('ProvisionsAllDevices') and device not in profile.get('ProvisionedDevices', []):
            raise WorkspaceError('The provisioning profile does not include this iPhone or iPad. Register the device in Xcode, then build again.')
        existing = device_json(['device', 'info', 'apps', '--device', device]).get('apps', [])
        if any(value.get('bundleIdentifier') == game.bundle_id for value in existing):
            backup = LOCAL_ROOT / 'saves' / game.id / ('phone-before-install-' + stamp())
            run(['xcrun', 'devicectl', 'device', 'copy', 'from', '--device', device,
                 '--domain-type', 'appDataContainer', '--domain-identifier', game.bundle_id,
                 '--source', 'Documents', '--destination', backup], logs / 'backup.log', 'Back up the device Documents and save data')
            if not backup.is_dir():
                raise WorkspaceError('The device backup did not complete. The installation stopped.')
            write_json(logs / 'backup.json', {'path': str(backup), 'sha256': {
                str(p.relative_to(backup)): sha256(p) for p in backup.rglob('*') if p.is_file()}})
        run(['xcrun', 'devicectl', 'device', 'install', 'app', '--device', device, app], logs / 'install.log', 'Install on the iPhone or iPad')
    print('Installation complete. Open the game on the iPhone or iPad.')


def select_ipa(game, interactive):
    values = sorted(game.ipa_dir.glob('*.ipa'), key=lambda p: p.stat().st_mtime_ns, reverse=True)
    if len(values) == 1:
        return values[0]
    if interactive:
        return choose('Select an existing IPA', values, lambda p: p.name)
    raise WorkspaceError('There is no single IPA. Build first, or select one in the menu.')


def menu():
    print('Touchable\nPut the game files in MyGameFiles. Finished packages are in IPA.')
    games = sorted([load_game(game) for game in registered_games()],
                   key=lambda game: (game.data['status'] != 'adapted', game.id))
    write_json(LOCAL_ROOT / 'logs/last-launch.json', {'event': 'menu-ready', 'pid': os.getpid(),
               'time_utc': datetime.now(timezone.utc).isoformat(), 'games': [game.id for game in games]})
    game = choose('Select a game', games, lambda g: g.display_name + (' (in progress)' if g.data['status'] == 'in-progress' else ''))
    print(f'Supported game package: v{game.profile.get("package_version", "not set")}. App build: {game.build_version}.')
    action = choose('Select an action', ['build-install', 'build', 'install', 'check', 'fresh', 'files', 'logs', 'settings'],
                    lambda value: {'build-install': 'Build and install', 'build': 'Build IPA only', 'install': 'Install existing IPA',
                                   'check': 'Check environment', 'fresh': 'Full rebuild from NSP',
                                   'files': 'Open game files folder', 'logs': 'Open logs folder', 'settings': 'Signing settings'}[value])
    return game, action


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', choices=registered_games())
    parser.add_argument('--action', choices=['build', 'build-install', 'install', 'check', 'fresh'])
    parser.add_argument('--device', help='Physical device UDID.')
    args = parser.parse_args()
    interactive = not args.action
    try:
        if interactive:
            game, action = menu()
        else:
            if not args.game:
                parser.error('--game is required with --action')
            game, action = load_game(args.game), args.action
        with (nullcontext() if action in ('check', 'files', 'logs') else workspace_lock()):
            if action in ('files', 'logs'):
                path = PROJECT_ROOT / 'MyGameFiles' / game.id if action == 'files' else game.logs_dir
                path.mkdir(parents=True, exist_ok=True)
                subprocess.run(['open', str(path)], check=True)
                return 0
            preflight()
            if action == 'settings':
                configure_signing(game)
                return 0
            if action == 'check':
                print('Game files folder: ' + str(PROJECT_ROOT / 'MyGameFiles' / game.id))
                print('Signing certificates:')
                subprocess.run(['security', 'find-identity', '-v', '-p', 'codesigning'], check=True)
                print('Paired and booted iPhone/iPad devices: ' + str(len(devices())))
                return 0
            if action == 'fresh':
                os.environ['TOUCHABLE_STATE_DIR'] = str(LOCAL_ROOT / 'rebuilds' / stamp())
                game = load_game(game.id)
            device = args.device
            if action == 'build-install' and not device:
                connected = devices()
                if not connected:
                    raise WorkspaceError('Connect and unlock an iPhone or iPad first.')
                if len(connected) == 1:
                    device = connected[0][0]
                elif interactive:
                    device = choose('Select an iPhone or iPad', connected, lambda value: value[1])[0]
                else:
                    raise WorkspaceError('More than one iPhone or iPad is connected. Select one in the menu.')
            if action in ('build', 'build-install', 'fresh'):
                ipa = build(game, interactive, device)
            else:
                ipa = select_ipa(game, interactive)
            if action in ('build-install', 'install'):
                install(load_game(game.id), ipa, device, interactive)
        return 0
    except (WorkspaceError, OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print('\nError: ' + str(error), file=sys.stderr)
        return 1
    except (KeyboardInterrupt, EOFError):
        print('\nQuit.')
        return 0


if __name__ == '__main__':
    sys.exit(main())
