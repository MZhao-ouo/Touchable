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

from build_shaders import check_metallib
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


def shell_tv_mode_audio_route(shell):
    """Optional TV mode that follows the audio output. The value must be a boolean."""
    value = shell.get('tv_mode_audio_route', False)
    if not isinstance(value, bool):
        raise WorkspaceError('ios.tv_mode_audio_route must be true or false.')
    return value


RENDER_HEIGHTS = range(720, 1441)   # SS_RENDER_HEIGHT range in runtime/common/ss_display.c
DISPLAY_RATES = (60, 120)            # SS_DISPLAY_HZ values in runtime/common/ss_platform.c


def shell_player_settings(shell, environment):
    """Optional ios.player_settings: {"render_heights": [...], "display_hz": [...], "health_bars": true}. Return None or
    {"render_heights", "display_hz", "health_bars", "default_height", "default_hz"}. The defaults come from the profile environment."""
    value = shell.get('player_settings')
    if value is None:
        return None
    if not isinstance(value, dict) or not value or set(value) - {'render_heights', 'display_hz', 'health_bars'}:
        raise WorkspaceError('ios.player_settings may contain only render_heights, display_hz, and health_bars.')
    result = {}
    for key, allowed in (('render_heights', RENDER_HEIGHTS), ('display_hz', DISPLAY_RATES)):
        items = value.get(key, [])
        if (not isinstance(items, list) or len(set(items)) != len(items) or
                not all(isinstance(v, int) and not isinstance(v, bool) and v in allowed for v in items)):
            raise WorkspaceError(f'ios.player_settings.{key} must be distinct values in the supported range.')
        result[key] = sorted(items)
    result['health_bars'] = value.get('health_bars', False)
    if not isinstance(result['health_bars'], bool):
        raise WorkspaceError('ios.player_settings.health_bars must be true or false.')
    if not result['render_heights'] and not result['display_hz'] and not result['health_bars']:
        raise WorkspaceError('ios.player_settings needs render_heights, display_hz, or health_bars.')
    result['default_height'] = int(environment.get('SS_RENDER_HEIGHT', 720))
    result['default_hz'] = 120 if str(environment.get('SS_DISPLAY_HZ', '60')) == '120' else 60
    return result


SETTINGS_TEXT = {
    'en': {'Graphics': 'Graphics', 'Resolution': 'Render resolution', 'Rate': 'Refresh rate',
           'Gameplay': 'Gameplay', 'HealthBars': 'Enemy health bars', 'GameplayFooter': 'Shows a bar above each enemy that you hit. Takes effect the next time the game starts.',
           'Default': 'Default (%s)', 'Footer': 'Changes take effect the next time the game starts. '
           'A lower resolution or 60 Hz makes the GPU work less, so fewer frames drop and the device stays cooler. '
           'The picture is at most 1920 pixels wide, so a wide screen can use fewer lines than selected.'},
    'zh-Hans': {'Graphics': '画面', 'Resolution': '渲染分辨率', 'Rate': '刷新率', 'Default': '默认（%s）',
                'Gameplay': '游戏', 'HealthBars': '敌人血条', 'GameplayFooter': '在被击中的敌人上方显示血条。下次启动游戏时生效。',
                'Footer': '下次启动游戏时生效。较低的分辨率或 60 Hz 可以减轻 GPU 负担，掉帧更少，设备也更凉。'
                '画面宽度最多 1920 像素，宽屏实际行数可能低于所选值。'},
}


def stage_settings_bundle(settings, directory):
    """Write Settings.bundle for the app page in the iOS Settings app. Return its path, or None."""
    if directory.exists(): shutil.rmtree(directory)
    if not settings: return None
    directory.mkdir(parents=True)
    items = [{'Type': 'PSGroupSpecifier', 'Title': 'Graphics', 'FooterText': 'Footer'}]
    if settings['render_heights']:
        values = ['default'] + [str(v) for v in settings['render_heights']]
        titles = ['DefaultHeight'] + [f'{v}p' for v in settings['render_heights']]
        items.append({'Type': 'PSMultiValueSpecifier', 'Key': 'TouchableRenderHeight', 'Title': 'Resolution',
                      'DefaultValue': 'default', 'Values': values, 'Titles': titles})
    if settings['display_hz']:
        values = ['default'] + [str(v) for v in settings['display_hz']]
        titles = ['DefaultRate'] + [f'{v} Hz' for v in settings['display_hz']]
        items.append({'Type': 'PSMultiValueSpecifier', 'Key': 'TouchableDisplayHz', 'Title': 'Rate',
                      'DefaultValue': 'default', 'Values': values, 'Titles': titles})
    if settings['health_bars']:
        items.append({'Type': 'PSGroupSpecifier', 'Title': 'Gameplay', 'FooterText': 'GameplayFooter'})
        items.append({'Type': 'PSToggleSwitchSpecifier', 'Key': 'TouchableHealthBars', 'Title': 'HealthBars', 'DefaultValue': False})
    with (directory / 'Root.plist').open('wb') as stream:
        plistlib.dump({'StringsTable': 'Root', 'PreferenceSpecifiers': items}, stream, fmt=plistlib.FMT_XML)
    for language, text in SETTINGS_TEXT.items():
        strings = {k: text[k] for k in ('Graphics', 'Resolution', 'Rate', 'Footer', 'Gameplay', 'HealthBars', 'GameplayFooter')}
        strings['DefaultHeight'] = text['Default'] % f"{settings['default_height']}p"
        strings['DefaultRate'] = text['Default'] % f"{settings['default_hz']} Hz"
        (directory / f'{language}.lproj').mkdir()
        with (directory / f'{language}.lproj' / 'Root.strings').open('wb') as stream:
            plistlib.dump(strings, stream, fmt=plistlib.FMT_BINARY)
    return directory


def shell_display_names(shell, fallback):
    """Return {language: app name}. "en" is the default name. The names are the official titles from
    control.nacp (tools/game_titles.py). Without a table, the app uses fallback for all languages."""
    names = shell.get('display_names')
    if names is None:
        return {'en': fallback}
    if (not isinstance(names, dict) or not isinstance(names.get('en'), str) or
            not all(isinstance(k, str) and re.fullmatch(r'[a-z]{2,3}(-[A-Za-z0-9]{2,4})?', k) and isinstance(v, str) and v.strip()
                    for k, v in names.items())):
        raise WorkspaceError('ios.display_names must map language IDs to names and include "en".')
    return names


def localized_names(names):
    """Return {language: name} for the .lproj folders. A name that equals the default needs no folder,
    because iOS falls back to it, unless another variant of the same language has a different name."""
    base = names['en']
    result = {}
    for language, name in names.items():
        if language == 'en':
            continue
        family = {v for k, v in names.items() if k.split('-')[0] == language.split('-')[0]}
        if name != base or len(family) > 1:
            result[language] = name
    return result


def stage_display_names(names, directory):
    """Write <language>.lproj/InfoPlist.strings with CFBundleDisplayName. Remove other .lproj folders."""
    directory.mkdir(parents=True, exist_ok=True)
    wanted = {language + '.lproj': name for language, name in localized_names(names).items()}
    for stale in directory.glob('*.lproj'):
        if stale.name not in wanted: shutil.rmtree(stale)
    folders = []
    for folder, name in sorted(wanted.items()):
        path = directory / folder
        path.mkdir(exist_ok=True)
        with (path / 'InfoPlist.strings').open('wb') as stream:
            plistlib.dump({'CFBundleDisplayName': name}, stream, fmt=plistlib.FMT_BINARY)
        folders.append(path)
    return folders


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


def stage_shaders(shader_root, shaders, chain, minimum_ios=None):
    """Keep the current generated shader chain. Keep source chains intact."""
    source = shader_root / 'ios' / f'b-{chain}'
    if shader_root.resolve() == shaders.resolve():
        raise WorkspaceError('Source and generated shader directories must differ.')
    if not source.is_dir() or not (shader_root / 'shader_map.tsv').is_file():
        raise WorkspaceError('Build the iOS shaders first.')
    if minimum_ios:
        # A library for a newer iOS than the app's minimum does not load on the older system.
        for library in source.glob('*.metallib'):
            try: check_metallib(library, 'iphoneos', minimum_ios)
            except ValueError as error:
                raise WorkspaceError(str(error) + ' Rebuild the iOS shaders.') from None
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
        stage_shaders(shader_root, shaders, chain, game.minimum_ios_version)

        with (ROOT / 'platform/ios/Info.plist').open('rb') as stream:
            info = plistlib.load(stream)
        shell = game.data.get('ios', {})
        names = shell_display_names(shell, game.display_name)
        # The default app name is the English title. Other languages use <language>.lproj/InfoPlist.strings.
        info.update(CFBundleName=game.product_name, CFBundleDisplayName=names['en'],
                    TouchableGameID=game.id, TouchableSaveDirectory=game.save_directory,
                    TouchableRuntimeEnvironment=game.profile.get('environment', {}))
        # Optional per-game shell settings from game.json "ios".
        info['CFBundleDevelopmentRegion'] = 'en'
        name_folders = stage_display_names(names, gen / 'Names')
        if shell.get('touch_layout') not in (None, 'switch', 'rhythm'):
            raise WorkspaceError('ios.touch_layout must be switch or rhythm.')
        if shell.get('touch_layout'): info['TouchableTouchLayout'] = shell['touch_layout']
        if shell.get('native_drawable'): info['TouchableNativeDrawable'] = True
        if shell_controller_haptics(shell): info['TouchableControllerHaptics'] = True
        if shell_tv_mode_audio_route(shell): info['TouchableTVModeAudioRoute'] = True
        player_settings = shell_player_settings(shell, game.profile.get('environment', {}))
        settings_bundle = stage_settings_bundle(player_settings, gen / 'Settings.bundle')
        if player_settings:
            info['TouchablePlayerSettings'] = {'RenderHeights': player_settings['render_heights'],
                                               'DisplayHz': player_settings['display_hz'],
                                               'HealthBars': player_settings['health_bars']}
        localizations = shell_localizations(shell)
        if localizations:
            info['CFBundleLocalizations'] = localizations
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
        for path in (modules, shaders, game.content_dir / 'RomFS', assets, *name_folders, *([settings_bundle] if settings_bundle else [])):
            kind = {'.xcassets': 'folder.assetcatalog', '.bundle': 'wrapper.plug-in'}.get(path.suffix, 'folder')
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
        frameworks += list(game.frameworks)
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
        # An API newer than the deployment target must be guarded, or the app fails on the older iOS.
        cflags = ['-D' + define for define in game.compile_defines] + ['-Wno-unused-function', '-Wno-deprecated-declarations',
                                                                     '-Werror=unguarded-availability-new']
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
