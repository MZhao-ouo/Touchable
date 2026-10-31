"""Check that a new game build cannot reuse removed generated resources."""
import json
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build_shaders import check_metallib, metallib_target
from prepare_ios import (localized_names, shell_controller_haptics, shell_display_names, shell_localizations,
                         shell_player_settings, shell_tv_mode_audio_route, stage_display_names, stage_module_tables,
                         stage_settings_bundle, stage_shaders)
from workspace import ROOT, WorkspaceError


class GeneratedResourceTests(unittest.TestCase):
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


def metallib_header(platform, major, minor):
    # Header layout of xcrun metallib output: magic, target, file version, type, platform, OS version.
    return b'MTLB' + bytes([1, 0, 2, 0, 9, 0, 0, 0x80 | platform]) + bytes([major, 0, minor, 0]) + bytes(16)


class MetalLibraryTargetTests(unittest.TestCase):
    def test_header_gives_platform_and_minimum_os(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'a.metallib'
            path.write_bytes(metallib_header(2, 27, 1))
            self.assertEqual(metallib_target(path), ('iphoneos', (27, 1)))
            path.write_bytes(metallib_header(1, 26, 0))
            self.assertEqual(metallib_target(path), ('macosx', (26, 0)))
            path.write_bytes(b'#include <metal_stdlib>\n' + bytes(16))
            with self.assertRaises(ValueError): metallib_target(path)

    def test_library_for_a_newer_ios_is_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'a.metallib'
            path.write_bytes(metallib_header(2, 26, 0))
            check_metallib(path, 'iphoneos', '26.0')
            check_metallib(path, 'iphoneos', '27.0')
            with self.assertRaises(ValueError): check_metallib(path, 'macosx', '26.0')
            path.write_bytes(metallib_header(2, 27, 1))
            with self.assertRaises(ValueError): check_metallib(path, 'iphoneos', '26.0')

    def test_staging_refuses_shaders_for_a_newer_ios(self):
        with tempfile.TemporaryDirectory() as temp:
            root, out = Path(temp) / 'shaders', Path(temp) / 'generated'
            chain = root / 'ios' / 'b-abc'
            chain.mkdir(parents=True)
            (root / 'shader_map.tsv').write_text('package\n')
            (chain / 'k_b.metallib').write_bytes(metallib_header(2, 27, 1))
            with self.assertRaises(WorkspaceError): stage_shaders(root, out, 'abc', '26.0')
            self.assertFalse((out / 'ios').exists())
            (chain / 'k_b.metallib').write_bytes(metallib_header(2, 26, 0))
            stage_shaders(root, out, 'abc', '26.0')
            self.assertTrue((out / 'ios' / 'b-abc' / 'k_b.metallib').is_file())

    def test_tv_mode_audio_route_is_an_optional_boolean(self):
        data = json.loads((ROOT / 'games/rhythm-heaven/game.json').read_text())
        self.assertTrue(shell_tv_mode_audio_route(data['ios']))
        self.assertFalse(shell_tv_mode_audio_route({}))
        with self.assertRaises(WorkspaceError):
            shell_tv_mode_audio_route({'tv_mode_audio_route': 'yes'})



class PlayerSettingsTests(unittest.TestCase):
    def test_silksong_offers_resolution_and_refresh_rate(self):
        data = json.loads((ROOT / 'games/silksong/game.json').read_text())
        profile = json.loads((ROOT / 'games/silksong/profile.json').read_text())
        settings = shell_player_settings(data['ios'], profile['environment'])
        self.assertEqual(settings['render_heights'], [720, 900, 1080, 1440])
        self.assertEqual(settings['display_hz'], [60, 90, 120])
        self.assertEqual(settings['world_scales'], [70, 80, 90])
        self.assertTrue(settings['health_bars'])
        self.assertTrue(settings['control_style'])
        self.assertEqual((settings['default_height'], settings['default_hz']), (1080, 120))
        self.assertIsNone(shell_player_settings({}, {}))

    def test_values_outside_the_runtime_range_are_refused(self):
        for bad in ({'render_heights': [540]}, {'display_hz': [75]}, {'render_heights': [720, 720]},
                    {'render_heights': ['720']}, {'other': [1]}, {}, {'render_heights': [], 'display_hz': []},
                    {'health_bars': 'yes'}, {'health_bars': False}, {'world_scales': [100]}, {'world_scales': [40]},
                    {'world_scales': [80, 80]}, {'control_style': 'plain'}):
            with self.assertRaises(WorkspaceError):
                shell_player_settings({'player_settings': bad}, {})

    def test_settings_bundle_lists_the_choices(self):
        settings = shell_player_settings({'player_settings': {'render_heights': [720, 1080], 'display_hz': [60, 120], 'world_scales': [80, 70],
                                                              'health_bars': True, 'control_style': True}},
                                         {'SS_RENDER_HEIGHT': '1080', 'SS_DISPLAY_HZ': '120'})
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / 'Settings.bundle'
            self.assertEqual(stage_settings_bundle(settings, out), out)
            with (out / 'Root.plist').open('rb') as stream:
                items = plistlib.load(stream)['PreferenceSpecifiers']
            choices = {i['Key']: i['Values'] for i in items if i['Type'] == 'PSMultiValueSpecifier'}
            self.assertEqual(choices, {'TouchableRenderHeight': ['default', '720', '1080'],
                                       'TouchableDisplayHz': ['default', '60', '120'],
                                       'TouchableWorldScale': ['default', '80', '70'],
                                       'TouchableControlStyle': ['glass', 'plain']})
            toggles = [i for i in items if i['Type'] == 'PSToggleSwitchSpecifier']
            self.assertEqual([(t['Key'], t['DefaultValue']) for t in toggles], [('TouchableHealthBars', False)])
            with (out / 'en.lproj' / 'Root.strings').open('rb') as stream:
                strings = plistlib.load(stream)
            self.assertEqual(strings['DefaultRate'], 'Default (120 Hz)')
            self.assertEqual((strings['WorldDefault'], strings['StylePlain']), ('Full (100%)', 'Simple'))
            self.assertTrue((out / 'zh-Hans.lproj' / 'Root.strings').is_file())
            self.assertIsNone(stage_settings_bundle(None, out))
            self.assertFalse(out.exists())

    def test_rhythm_heaven_offers_voice_play(self):
        data = json.loads((ROOT / 'games/rhythm-heaven/game.json').read_text())
        settings = shell_player_settings(data['ios'], {})
        self.assertTrue(settings['voice_play'])
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / 'Settings.bundle'
            stage_settings_bundle(settings, out)
            with (out / 'Root.plist').open('rb') as stream:
                items = plistlib.load(stream)['PreferenceSpecifiers']
            # No resolution or rate choices, so no empty Graphics group.
            self.assertEqual([i.get('Title') for i in items if i['Type'] == 'PSGroupSpecifier'], ['Voice'])
            self.assertEqual([(i['Key'], i['DefaultValue']) for i in items if i['Type'] == 'PSToggleSwitchSpecifier'],
                             [('TouchableVoicePlay', False)])
            self.assertEqual([(i['Key'], i['DefaultValue'], i['Values']) for i in items if i['Type'] == 'PSMultiValueSpecifier'],
                             [('TouchableVoiceSensitivity', 'medium', ['low', 'medium', 'high'])])
            with (out / 'zh-Hans.lproj' / 'Root.strings').open('rb') as stream:
                self.assertEqual(plistlib.load(stream)['VoicePlay'], '用声音游玩')
        with self.assertRaises(WorkspaceError):
            shell_player_settings({'player_settings': {'voice_play': 'yes'}}, {})

    def test_microphone_text_is_localized_with_the_names(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            stage_display_names({'en': 'Game', 'zh-Hans': '游戏'}, out, {'zh-Hans': {'NSMicrophoneUsageDescription': '说明'}})
            with (out / 'zh-Hans.lproj' / 'InfoPlist.strings').open('rb') as stream:
                self.assertEqual(plistlib.load(stream), {'CFBundleDisplayName': '游戏', 'NSMicrophoneUsageDescription': '说明'})


class DisplayNameTests(unittest.TestCase):
    def test_every_game_has_an_english_default_name(self):
        for game in ('rhythm-heaven', 'silksong'):
            data = json.loads((ROOT / 'games' / game / 'game.json').read_text())
            names = shell_display_names(data['ios'], data['display_name'])
            self.assertTrue(names['en'].isascii())

    def test_only_different_names_get_a_language_folder(self):
        names = {'en': 'Rhythm Heaven Groove', 'en-GB': 'Rhythm Paradise Groove', 'fr': 'Rhythm Paradise Groove',
                 'fr-CA': 'Rhythm Heaven Groove', 'it': 'Rhythm Heaven Groove', 'zh-Hans': '节奏天国 奇迹之星'}
        self.assertEqual(localized_names(names), {'en-GB': 'Rhythm Paradise Groove', 'fr': 'Rhythm Paradise Groove',
                                                  'fr-CA': 'Rhythm Heaven Groove', 'zh-Hans': '节奏天国 奇迹之星'})

    def test_names_are_validated_and_written(self):
        self.assertEqual(shell_display_names({}, 'Game'), {'en': 'Game'})
        for bad in ({'zh-Hans': 'x'}, {'en': ''}, {'en': 'A', 'zh_Hans': 'x'}, ['en']):
            with self.assertRaises(WorkspaceError):
                shell_display_names({'display_names': bad}, 'Game')
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out / 'old.lproj').mkdir()
            folders = stage_display_names({'en': 'A', 'ja': 'B'}, out)
            self.assertEqual([f.name for f in folders], ['ja.lproj'])
            self.assertFalse((out / 'old.lproj').exists())
            with (out / 'ja.lproj' / 'InfoPlist.strings').open('rb') as stream:
                self.assertEqual(plistlib.load(stream), {'CFBundleDisplayName': 'B'})


if __name__ == '__main__': unittest.main()
