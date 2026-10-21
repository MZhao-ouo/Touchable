#!/usr/bin/env python3
"""Create an isolated iPhone and iPad project for a registered game."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys

from workspace import ROOT, DEPENDENCIES, UPSTREAM, WorkspaceError, load_game, validate_registry


def ident(text): return hashlib.sha256(text.encode()).hexdigest()[:24].upper()
def quote(text): return json.dumps(str(text), ensure_ascii=False)
def array(items): return '(' + ','.join(items) + ')'


def header_search_paths(paths):
    # Xcode splits search path values after it reads the project strings.
    # Keep a second set of quotes around each path for that split.
    return array(quote('"' + str(path).replace('\\', '\\\\').replace('"', '\\"') + '"') for path in paths)


def shell_localizations(shell):
    values = shell.get('localizations')
    if values is None:
        return None
    if not isinstance(values, list) or not values:
        raise WorkspaceError('ios.localizations must be a non-empty language ID array.')
    if not all(isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9-]+', value) for value in values):
        raise WorkspaceError('ios.localizations must contain language IDs.')
    return values


def shell_controller_haptics(shell):
    value = shell.get('controller_haptics', False)
    if not isinstance(value, bool):
        raise WorkspaceError('ios.controller_haptics must be true or false.')
    return value


def write_game_header(game, directory):
    directory.mkdir(parents=True, exist_ok=True)
    header = directory / 'touchable_game_config.h'
    content = (
        '/* Generated from games/' + game.id + '/game.json. */\n#pragma once\n' +
        '#define TOUCHABLE_GAME_ID ' + quote(game.id) + '\n' +
        '#define TOUCHABLE_GAME_NAME ' + quote(game.display_name) + '\n' +
        '#define TOUCHABLE_GAME_PRODUCT ' + quote(game.product_name) + '\n' +
        '#define TOUCHABLE_GAME_MODULES {' + ','.join(quote(v) for v in game.module_names) + '}\n' +
        '#define TOUCHABLE_GAME_TITLE_ID 0x' + game.require_title_id() + 'ull\n')
    if not header.exists() or header.read_text() != content: header.write_text(content)
    chain_file = game.shaders_dir / 'chain.json'
    if chain_file.is_file():
        chain = json.loads(chain_file.read_text())['chain']
        if not re.fullmatch(r'[0-9a-f]+', chain):
            raise WorkspaceError('The shader chain is invalid.')
        shader_header = directory / 'ss_shader_chain.h'
        shader_text = '#define DMSH_CHAIN "' + chain + '"\n'
        if not shader_header.exists() or shader_header.read_text() != shader_text:
            shader_header.write_text(shader_text)


def stage_module_tables(patched, modules, names):
    """Replace generated tables. Keep unrelated files and source tables."""
    if patched.resolve() == modules.resolve():
        raise WorkspaceError('Source and generated module directories must differ.')
    modules.mkdir(parents=True, exist_ok=True)
    for suffix in ('fixups.tsv', 'gates.tsv', 'indirect.tsv'):
        for table in modules.glob('*.' + suffix): table.unlink()
    for name in names:
        for suffix in ('fixups.tsv', 'gates.tsv', 'indirect.tsv'):
            source = patched / f'{name}.{suffix}'
            if source.exists(): shutil.copy2(source, modules / source.name)


def stage_shaders(shader_root, shaders, chain):
    """Keep the current generated shader chain. Keep source chains intact."""
    source = shader_root / 'ios' / f'b-{chain}'
    if shader_root.resolve() == shaders.resolve():
        raise WorkspaceError('Source and generated shader directories must differ.')
    if not source.is_dir() or not (shader_root / 'shader_map.tsv').is_file():
        raise WorkspaceError('Build the iOS shaders first.')
    shaders.mkdir(parents=True, exist_ok=True)
    for stale in (shaders / 'ios').glob('b-*'):
        if stale.name != 'b-' + chain:
            if stale.is_symlink() or not stale.is_dir(): stale.unlink()
            else: shutil.rmtree(stale)
    shutil.copyfile(shader_root / 'shader_map.tsv', shaders / 'shader_map.tsv')
    shutil.copytree(source, shaders / 'ios' / f'b-{chain}', dirs_exist_ok=True, copy_function=shutil.copyfile)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', default='silksong', help='Game ID in games/<id>/game.json')
    parser.add_argument('--modules', type=Path, help='Rewritten modules; defaults to work/<game>/patched')
    args = parser.parse_args()
    try:
        game = load_game(args.game)
        validate_registry(game.root)
        game.require_adapted('ios')
        patched = (args.modules or game.patched_dir).resolve()
        for name in game.module_names:
            if not (patched / f'{name}.patched.bin').is_file():
                raise WorkspaceError('Rewritten module does not exist: ' + name)
        sources = list(game.common_runtime_sources) + list(game.runtime_sources)
        sources += list(game.common_ios_sources) + list(game.ios_sources)
        for path in sources:
            if not path.is_file(): raise WorkspaceError('Source does not exist: ' + str(path))
        assets = game.ios_dir / 'Assets.xcassets'
        if not assets.is_dir(): raise WorkspaceError('The game app assets do not exist: ' + str(assets))
        if not (game.content_dir / 'RomFS').is_dir(): raise WorkspaceError('Game RomFS does not exist: ' + str(game.content_dir / 'RomFS'))
        shader_root = game.shaders_dir
        chain = json.loads((shader_root / 'chain.json').read_text())['chain']
        if not isinstance(chain, str) or not chain or Path(chain).name != chain:
            raise WorkspaceError('The shader chain is invalid.')
        if not (shader_root / 'ios' / f'b-{chain}').is_dir(): raise WorkspaceError('Build the iOS shaders first.')
        gen = game.generated_dir
        write_game_header(game, gen)
        modules = gen / 'Modules'
        stage_module_tables(patched, modules, game.module_names)
        embedded = gen / 'Guest.S'
        flags = subprocess.check_output([sys.executable, str(UPSTREAM / 'dm/ios/tools/gen_embed.py'),
                                         str(patched), str(embedded), *game.module_names], text=True).split()
        sources.append(embedded)
        shaders = gen / 'Shaders'
        stage_shaders(shader_root, shaders, chain)

        with (ROOT / 'platform/ios/Info.plist').open('rb') as stream:
            info = plistlib.load(stream)
        info.update(CFBundleName=game.product_name, CFBundleDisplayName=game.display_name,
                    TouchableGameID=game.id, TouchableSaveDirectory=game.save_directory,
                    TouchableRuntimeEnvironment=game.profile.get('environment', {}))
        # Optional per-game shell settings from game.json "ios".
        shell = game.data.get('ios', {})
        if shell.get('touch_layout') not in (None, 'switch', 'rhythm'):
            raise WorkspaceError('ios.touch_layout must be switch or rhythm.')
        if shell.get('touch_layout'): info['TouchableTouchLayout'] = shell['touch_layout']
        if shell.get('native_drawable'): info['TouchableNativeDrawable'] = True
        if shell_controller_haptics(shell): info['TouchableControllerHaptics'] = True
        localizations = shell_localizations(shell)
        if localizations:
            info['CFBundleLocalizations'] = localizations
            info['CFBundleDevelopmentRegion'] = 'en'
        io_buffer = shell.get('audio_io_buffer_seconds')
        if io_buffer is not None:
            if not isinstance(io_buffer, (int, float)) or not 0.001 <= io_buffer <= 0.1:
                raise WorkspaceError('ios.audio_io_buffer_seconds must be between 0.001 and 0.1.')
            info['TouchableAudioIOBufferDuration'] = float(io_buffer)
        plist = gen / 'Info.plist'
        with plist.open('wb') as stream: plistlib.dump(info, stream, sort_keys=False)

        host = UPSTREAM / 'dm/host'
        gfx = UPSTREAM / 'dm/gfx'
        sources += [host / name for name in ('loader.c', 'hle.c', 'hle_svc.c', 'hle_thread.c', 'hle_os.c',
                                            'hle_sdkinit.c', 'hle_table.c', 'hle_count.c', 'hle_account.c', 'entry.S')]
        for pattern in ('*.c', 'nvn/*.c', 'nvn/*.m', 'nvn/*.S', 'tex/*.c', 'tex/*.m'):
            sources.extend(sorted(gfx.glob(pattern)))
        sources = list(dict.fromkeys(sources))
        objects = []

        def obj(key, body):
            name = ident(game.id + ':' + key)
            objects.append(f'{name} = {{{body}}};')
            return name

        refs, source_build, resource_build = [], [], []
        kinds = {'.c': 'sourcecode.c.c', '.m': 'sourcecode.c.objc', '.S': 'sourcecode.asm',
                 '.cpp': 'sourcecode.cpp.cpp', '.mm': 'sourcecode.cpp.objcpp'}
        for path in sources:
            ref = obj('ref:' + str(path), f'isa=PBXFileReference; lastKnownFileType={kinds[path.suffix]}; path={quote(path)}; sourceTree="<absolute>";')
            refs.append(ref)
            source_build.append(obj('build:' + str(path), f'isa=PBXBuildFile; fileRef={ref};'))
        for path in (modules, shaders, game.content_dir / 'RomFS', assets):
            kind = 'folder.assetcatalog' if path.suffix == '.xcassets' else 'folder'
            ref = obj('ref:' + str(path), f'isa=PBXFileReference; lastKnownFileType={kind}; path={quote(path)}; sourceTree="<absolute>";')
            refs.append(ref)
            resource_build.append(obj('build:' + str(path), f'isa=PBXBuildFile; fileRef={ref};'))
        product = obj('product', f'isa=PBXFileReference; explicitFileType=wrapper.application; path={quote(game.product_name + ".app")}; sourceTree=BUILT_PRODUCTS_DIR;')
        products = obj('products', f'isa=PBXGroup; children=({product}); name=Products; sourceTree="<group>";')
        main_group = obj('main', f'isa=PBXGroup; children={array(refs + [products])}; sourceTree="<group>";')
        source_phase = obj('sources', f'isa=PBXSourcesBuildPhase; buildActionMask=2147483647; files={array(source_build)}; runOnlyForDeploymentPostprocessing=0;')
        resource_phase = obj('resources', f'isa=PBXResourcesBuildPhase; buildActionMask=2147483647; files={array(resource_build)}; runOnlyForDeploymentPostprocessing=0;')
        framework_phase = obj('frameworks', 'isa=PBXFrameworksBuildPhase; buildActionMask=2147483647; files=(); runOnlyForDeploymentPostprocessing=0;')
        frameworks = ['UIKit', 'Foundation', 'CoreGraphics', 'QuartzCore', 'Metal', 'AudioToolbox', 'AVFAudio', 'CoreHaptics', 'ImageIO', 'UniformTypeIdentifiers', 'GameController']
        ldflags = flags + [v for framework in frameworks for v in ('-framework', framework)]
        common = {
            'SDKROOT': 'iphoneos', 'IPHONEOS_DEPLOYMENT_TARGET': game.minimum_ios_version, 'ARCHS': 'arm64',
            'CLANG_ENABLE_OBJC_ARC': 'YES', 'GCC_C_LANGUAGE_STANDARD': 'gnu11',
            'GCC_OPTIMIZATION_LEVEL': '1', 'GCC_GENERATE_DEBUGGING_SYMBOLS': 'YES',
            'ENABLE_USER_SCRIPT_SANDBOXING': 'YES', 'ALWAYS_SEARCH_USER_PATHS': 'NO',
            'PRODUCT_NAME': game.product_name, 'PRODUCT_BUNDLE_IDENTIFIER': game.bundle_id,
            'CODE_SIGN_STYLE': 'Automatic', 'INFOPLIST_FILE': str(plist),
            'GENERATE_INFOPLIST_FILE': 'NO', 'CURRENT_PROJECT_VERSION': game.build_version,
            'TARGETED_DEVICE_FAMILY': '1,2', 'ENABLE_BITCODE': 'NO', 'SWIFT_VERSION': '6.0',
            'ASSETCATALOG_COMPILER_APPICON_NAME': 'AppIcon',
        }
        settings = ''.join(f'{k}={quote(v)};' for k, v in common.items())
        includes = (gen, ROOT / 'runtime/common', game.game_dir / 'runtime', host, gfx / 'nvn/include', gfx / 'tex')
        settings += 'HEADER_SEARCH_PATHS=' + header_search_paths(includes) + ';'
        cflags = ['-D' + define for define in game.compile_defines] + ['-Wno-unused-function', '-Wno-deprecated-declarations']
        settings += 'OTHER_CFLAGS=' + array(quote(v) for v in cflags) + ';'
        settings += 'OTHER_LDFLAGS=' + array(quote(v) for v in ldflags) + ';'
        configs = [obj('config:' + name, f'isa=XCBuildConfiguration; name={name}; buildSettings={{{settings}}};') for name in ('Debug', 'Release')]
        config_list = obj('configs', f'isa=XCConfigurationList; buildConfigurations={array(configs)}; defaultConfigurationIsVisible=0; defaultConfigurationName=Debug;')
        project_configs = [obj('projectconfig:' + name, f'isa=XCBuildConfiguration; name={name}; buildSettings={{}};') for name in ('Debug', 'Release')]
        project_config_list = obj('projectconfigs', f'isa=XCConfigurationList; buildConfigurations={array(project_configs)}; defaultConfigurationIsVisible=0; defaultConfigurationName=Debug;')
        target = obj('target', f'isa=PBXNativeTarget; buildConfigurationList={config_list}; buildPhases={array([source_phase, resource_phase, framework_phase])}; buildRules=(); dependencies=(); name={quote(game.product_name)}; productName={quote(game.product_name)}; productReference={product}; productType="com.apple.product-type.application";')
        project = obj('project', f'isa=PBXProject; attributes={{LastUpgradeCheck=2700;}}; buildConfigurationList={project_config_list}; compatibilityVersion="Xcode 14.0"; developmentRegion=en; hasScannedForEncodings=0; knownRegions=(en,Base); mainGroup={main_group}; productRefGroup={products}; projectDirPath=""; projectRoot=""; targets=({target});')
        destination = game.project_dir / (game.product_name + '.xcodeproj')
        destination.mkdir(parents=True, exist_ok=True)
        (destination / 'project.pbxproj').write_text('// !$*UTF8*$!\n{archiveVersion=1; classes={}; objectVersion=56; objects={\n' + '\n'.join(objects) + f'\n}}; rootObject={project};}}\n')
        (gen / 'build-manifest.json').write_text(json.dumps({
            'game_id': game.id, 'product_name': game.product_name, 'bundle_id': game.bundle_id,
            'build_version': game.build_version, 'module_names': game.module_names,
            'sources': [str(path) for path in sources],
            'shader_chain': chain, 'project': str(destination),
        }, indent=2, ensure_ascii=False) + '\n')
        print('Prepared', destination)
        print('Build directory:', game.ios_build_dir)
        return 0
    except (WorkspaceError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print('Error:', error, file=sys.stderr)
        return 1


if __name__ == '__main__': sys.exit(main())
