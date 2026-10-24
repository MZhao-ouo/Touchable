# Rhythm Heaven

## 2026-10-24: Map input guard during unlock transitions

The map touch adapter now accepts map selection and dragging only in input state 1.
State 2 keeps the native selector path.
Other states clear the armed tap.
If no selector can accept input, the adapter publishes an empty menu frame, resets the gesture, and discards queued pointer events.
The native map function still runs.
This prevents transition touches from changing the unlock target or starting a level.

Earlier controlled ARM64 emulation reproduced a wrong unlock target after map selection changed during an unlock transition.
The same input guard prevented that case.
This is emulation evidence. It does not prove the exact touch sequence on an iPhone.
The change uses the existing v0 module profile.
It does not repair progression already saved in the wrong state.
The application identity, save path, and package version are unchanged.

Windows checks on the current public source passed `test_menu`, `test_pointer`, and both game configuration checks.
The C checks used MinGW GCC with `-Wall -Wextra -Werror`.
The configuration checks used Python UTF-8 mode.
The workspace Python suite could not start because the local interpreter lacks `lz4`.
No Mac host test or new iPhone gameplay test ran for this source change.
Deliberate touches during a new unlock transition still need iPhone verification.

## 2026-10-23: Rhythm Toys touch and Drum Lessons controller (build 39)

The user asked for touch in the Rhythm Toys and for a virtual controller in Drum Lessons.
- Who's Got Rhythm? (`rabbit`): a tap on a rabbit selects it; a tap on the selected rabbit confirms it. The targets follow the rabbits in every grid (2×1 to 5×5). The front row takes a tap where two rabbits overlap. The phone shows the touch targets only while the game takes a choice; the dialogs and Game Over keep the A area.
- Sound boards (`audio_tile`): a tap selects a cell, a second tap opens the note palette (A). The palette also takes select-then-confirm. "A 摆放" presses A, "X 移除" presses X. The speed panel and "Y 速度" press Y. A tap on a page at the top selects that page (ZL/ZR presses); "ZL" and "ZR" press one step. "+ 返回" presses +. The phone shows no D-pad there.
- Drum Lessons (`drummer`): a new `SS_TOUCH_PAD` hint. The phone shows the D-pad at the lower left, A, B, X, and Y in a diamond at the lower right, ZR above them, and − and + at the sides. There is no touch area. The lesson texts use A, B, X, Y, the four directions, ZR, and − (`stage_drummer_00.msbt`; E0E7 is ZR, as the sound board shows).
- How: `rh_menu.c` finds the toy object in the minigame scene's object list and publishes a menu frame with targets. A tap on an item that is not selected walks the cursor with one D-pad (or ZL/ZR) press at a time until the item is selected. A tap on the selected item presses A. Each press goes through the game's own input checks (`ss_touch_press()`, new in `ss_input.c` and `ss_touch.h`). Addresses and offsets are in the comment above `toy_input()`.
- Mac host (unit and host tests): `test_touch` and `test_menu` passed. Rabbits: the dialogs kept the level frame. In two runs, 32 rounds were answered by a select tap and a confirm tap (walks of 1 and 2 presses; grids 2×1, 2×2, 3×3, and 4×4). The test script missed the frame in 2 rounds; the score reached 10. Game Over returned to the level frame. Sound board: walk to a cell (3 presses), palette open, palette walk and place (blue note), page walk to page 3, Remove, and Speed (▶▶ to ▶▶▶) passed. All button targets cover their labels. Evidence: `.touchable/work/rhythm-heaven/reports/toy-touch-20261023/`.
- Not tested: Drum Lessons. The host saves keep the cafe closed ("店里现在还没准备好"), so the lesson scene and the full controller were not reached. The phone layout was not tested on a device.
- Builds: both Mac hosts and the Xcode device build (41 warnings, none in the changed files) passed. IPA `IPA/rhythm-heaven/RhythmHeaven-build39-toy-touch-20261023.ipa` (SHA-256 `340bb20bf7c4c91a38ba67002ef6335a4fdc76884dcce6e029fcacdc233e846e`). It also has the localized app names. Installed on the iPhone on 2026-10-23 (save backup `.touchable/saves/rhythm-heaven/phone-before-install-20261023-081752-3c995a/`). Not installed on the iPad mini: the game was running there.

## 2026-10-23: App name follows the language

The home screen name now follows the language of the device. The names are the official titles from the game's `control.nacp` (`internal/tools/game_titles.py`), stored in `ios.display_names` in `game.json`.
- Rhythm Heaven: "Rhythm Heaven Groove" (English, Latin American Spanish, Canadian French), "Rhythm Paradise Groove" (British English, French, German, Spanish, Italian, Dutch), "リズム天国 ミラクルスターズ", "리듬 천국 미라클 스타즈", "節奏天國 奇蹟之星", and "节奏天国 奇迹之星".
The default name is English. The Files app folder shows the same localized name; iOS has no separate setting for it. The product name, the save folders, and the IPA names stay English.
Checked in the iOS Simulator (Simplified Chinese) with a test app that uses the generated names: the home screen and the Files folder show the Chinese name. Build 39 includes the names.

## 2026-10-21: TV mode follows the audio output (build 38)

The user asked to remove the mode button and to choose the mode from the audio output.
- Handheld mode: built-in speaker, built-in receiver, wired headphones, USB audio, and line out.
- TV mode: every other output, for example Bluetooth, AirPlay, and HDMI. In TV mode the game offers its latency calibration.
- The app reads the output at launch (before the game starts), on each audio route change, and when the app becomes active. A change switches the game at once with `dm_opmode_set()` (build 37).
- `game.json` `ios.tv_mode_audio_route` enables this (`TouchableTVModeAudioRoute` in Info.plist). Only Rhythm Heaven sets it. `DM_OPMODE` in the launch environment still wins for device tests.
- Removed: the TV mode button, the saved `TouchableTVMode` setting, and the level flag that hid the button.
- Device evidence from build 37: the user switched handheld → TV → handheld on the iPhone; the game took notification 30 both times.
- Checks: 50 Python tests, `test_touch`, the App.m syntax check, both Mac host builds, the Mac live-switch test, and the Xcode device build (41 warnings) passed. The route detection itself was not tested on a device.
Build 38 is installed on the iPhone (2026-10-21, second try; the first try lost the device connection and left build 37). The saves matched the backup `.touchable/saves/rhythm-heaven/phone-before-install-20261021-145922-75e359/`. Build 38 is also installed on the iPad mini (2026-10-21 21:49); its saves matched the backup `phone-before-install-20261021-214935-12b07d`. On the iPad, a remote launch logged `TV mode: launch handheld, audio output Speaker` and ran at a frame time p50 of 16.67 ms. A route change (Bluetooth) was not tested on a device. The iPad save had the cafe locked (menu entry 0 = 0); it was fixed afterwards (see the cafe section).
The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build38-tv-audio-route-20261021.ipa` (SHA-256 `e5d5269d9dd2aff4ad9e3aa6bf82fe59de4107b2193606eef3ed996643bc8e99`). The build 37 IPA set moved to `.touchable/archive/ipa/rhythm-heaven/before-build38/`.

## 2026-10-21: TV mode switches without a restart (build 37)

The TV button now changes the mode at once. A restart is no longer needed.
- `dm_opmode_set()` in `ss_platform.c` changes the operation mode while the game runs, like docking a Switch. The game gets notification 30 (operation mode changed) after it enables that notification.
- `ss_input.c` signals the controller style events, so the game reads the controller style again (handheld 0x2, full-key 0x1).
- `App.m`: after the confirmation, the button calls `dm_opmode_set()`, changes its icon, and saves `TouchableTVMode` for the next launch.
- Mac host test hook: `SS_OPMODE_FILE` names a file with 0 or 1. A change switches the mode.
- Mac host (start menu): handheld → TV → handheld. Each time the log shows the change, the new controller style, and `the game took notification 30`. The game kept running, and A opened the level map in TV mode. See `.touchable/work/rhythm-heaven/reports/tv-live-20261021/`.
- Not tested: whether the game offers the TV latency calibration after a live switch.
- Checks: 50 Python tests, `test_touch`, the App.m syntax check, both Mac host builds, a 30 s Silksong host run (handheld, 120 Hz), and the Xcode device build passed.
Build 37 is installed on the iPhone (2026-10-21). The saves matched the backup `.touchable/saves/rhythm-heaven/phone-before-install-20261021-144426-ab15f5/`. The iPad mini was unavailable and has build 35.
The build 37 IPA `RhythmHeaven-build37-tv-live-20261021.ipa` (SHA-256 `07bd52d193a1b0b7107953a5231e1c5df2a917cc460b8dc0cd8ad41625c8a2ef`) is now in `.touchable/archive/ipa/rhythm-heaven/before-build38/`. The build 36 IPA set moved to `.touchable/archive/ipa/rhythm-heaven/before-build37/`.

## 2026-10-21: Long list taps and optional TV mode (build 36)

The user liked the ideas of pull requests #6 and #7 (sdmj76) and asked for them in the local code. Their code was used where it fits. The changes below differ from the pull requests.

Long lists (`runtime/rh_menu.c`):
- Before: in a scrolling list, only rows on the first page had a touch area. After the list scrolled, most visible rows could not be tapped.
- Now each visible row gets its place from the first-page row position and the row spacing.
- A scrolling list keeps an empty menu frame when no row is found, so a drag does not turn into an A or D-pad press.
- Change from #7: a short (non-scrolling) list with no targets still returns no frame. The fallback A and D-pad stay for menus that the adapter cannot read.
- Mac host: music room, category 单人游戏, 20 rows down (past 三只螃蟹): 11 tap targets instead of 2.

Optional TV mode (`App.m`, `ss_platform.c`, `ss_input.c`, `ss_touch.h`, `rh_game.c`):
- `nn::oe::GetOperationMode` follows `DM_OPMODE` (0 handheld, 1 TV). The default is handheld, so other games do not change.
- In TV mode the game sees a full-key controller and offers its TV latency calibration at start.
- A glass TV button shows outside levels and not during loads. A tap asks for confirmation and saves `TouchableTVMode`. The new mode starts with the next app launch.
- `game.json` `ios.tv_mode_toggle` enables the button (`TouchableTVModeToggle` in Info.plist). Only Rhythm Heaven sets it.
- Change from #6: on iPad the button is in the top bar, left of −. The #6 position was inside the game picture. It also hides during loads.
- Mac host with `DM_OPMODE=1`: log `Operation mode: TV`, and the game opens the calibration screen ("请配合念4的声音按A哦"). Calibration was not completed in the test.
- iOS Simulator (rhythm layout test app): the button is left of − on iPad mini and below − on iPhone.

Checks: `test_menu`, `test_touch`, `test_touch_layout`, 50 Python tests, `games.py check`, Rhythm Heaven and Silksong Mac host builds, a 40 s Silksong host run (120 Hz, no red lines), and the Xcode device build (41 warnings, none in `App.m`) passed.
Build 36 is installed on the iPhone (2026-10-21). The three save files matched the pre-install backup `.touchable/saves/rhythm-heaven/phone-before-install-20261021-142621-b0c6a1/`. The cafe stays unlocked. The iPad mini was unavailable, so it still has build 35.
The build 36 IPA is `RhythmHeaven-build36-tv-mode-long-lists-20261021.ipa` (SHA-256 `c8641a7537daee532de405c8ce31c294ffd164071f1a9a719d78aee5bd5cbdf5`), now in `.touchable/archive/ipa/rhythm-heaven/before-build37/`. The build 35 IPA set moved to `.touchable/archive/ipa/rhythm-heaven/before-build36/`.
Evidence: [the review record](../../../.touchable/work/rhythm-heaven/reports/pr-review-20261021/README.md).

## 2026-10-21: Cafe unlocked in the phone save

The user reported that the cafe (咖啡店) could not be unlocked.
Cause: `tools/unlock_all.py` unlocked menu entries 1–13, but entry 0 is the cafe. The name table at main+0xcdad60 lists entry 0 as `cafe` and entry 13 as `dummy`.
The game unlocks the cafe after an early event. In the phone save, all stages were already cleared by the tool, so this event cannot occur.
The tool now unlocks entries 0–12 and leaves the `dummy` entry 13 unchanged. On a save that the old tool changed, it changes only the cafe byte (0x28: 0 → 3).
Mac host test with a copy of the phone save: with the fix, the cafe item (coffee cup, "new" badge) shows in the menu column of the level map. Without the fix, it does not show. The cafe itself was not entered in the test.
On 2026-10-21, the fixed save was written to the iPhone (`Documents/Saves/RhythmHeaven` and `.work`). The game was closed. The read-back matched.
The backup is in `.touchable/saves/rhythm-heaven/phone-before-cafe-fix-20261021/`.
On 2026-10-21 (21:5x), the same fix was applied to the iPad mini save at the user's request. The game was closed. The fixed tool changed only byte 0x28 (0 → 3). Both save copies read back equal. The backup is in `.touchable/saves/rhythm-heaven/ipad-before-cafe-fix-20261021/`.
Evidence: [the cafe record](../../../.touchable/work/rhythm-heaven/reports/cafe-unlock-20261021/README.md).

## 2026-10-21: Simplified Chinese with a Hong Kong or Taiwan region (source change, no build)

Contributed by @sdmj76 in pull request #3.

- Fault: with Traditional Chinese as the system language, the per-app choice Simplified Chinese did not change the game language. iOS stores the choice as a tag such as `zh-Hans-HK`. The old mapping read the region (`HK`) before the script (`Hans`) and selected Traditional Chinese.
- `internal/runtime/common/ss_language.h` (new, used by `ss_platform.c`): the script subtag selects the written form. The region is used only when the tag has no script. Cantonese (`yue`) follows the same rule; without a script it selects Traditional Chinese.
- `internal/platform/ios/App.m`: the app reads `AppleLanguages` first, where iOS stores the per-app language. Then it uses the bundle localizations and `NSLocale.preferredLanguages`. `game.log` records the three values.
- `internal/tests/common/test_language.c`: checks `zh-Hans-HK`, `zh-Hans-TW`, `zh-Hant-HK`, Cantonese, and the other game languages.
- Not tested yet: a device build with a Traditional Chinese system and a Simplified Chinese app language.

## 2026-10-20: Touch controls hidden on loading screens (build 35)

In a Silksong session, the user asked that the touch controls show only when needed, also in Rhythm Heaven.
Before the change, the small D-pad and B, −, + showed during screen changes, because no frame was published.
`runtime/rh_game.c` now publishes `SS_POINTER_WAIT` when a screen archive under `graph/` opens and no menu or level frame is current. Remix scene archives during play do not count (`rh_touch_loading()` in `runtime/rh_touch.c`).
The app hides the small D-pad, B, −, +, X, and Y during the load, and keeps the controls unchanged for 2.5 s after a menu or level frame stops (menu fade-out).
Mac host tests at the iPhone and iPad aspects showed no D-pad between screens. A frame gap inside a level now keeps the level controls.
The title screen ("press A") still shows the fallback D-pad.
Build 35 (manual Xcode path) is installed on the iPhone and the iPad mini. The saves were backed up before the install and not restored.
On the iPhone, the user played: the log shows the controls hidden at boot and in five screen changes, and not hidden for the remix scene archives during play. The iPad mini was not tested.
The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build35-loading-controls-20261020.ipa` (SHA-256 `a27ffc633e98f11fd7e68cec3deaca6452c15a1815812cef82452a9ade16f551`). The build 34 IPA set moved to `.touchable/archive/ipa/rhythm-heaven/before-build35/`.
Evidence: [the loading controls record](../../../.touchable/work/rhythm-heaven/reports/loading-controls-20261020/README.md).

## 2026-10-19: System glass press effect (build 34)

In a Silksong session, the user asked for the glass effect of the touch buttons to match the official Apple buttons.
The Silksong session changed the shared `App.m`. It removed the custom press effect (white fill, black text, white outer glow).
A pressed glass button now shows only the system press effect: the glass becomes larger and gets brighter evenly from the inside. There is no outer glow.
This applies to the small D-pad, B, −, +, Y, and X in Rhythm Heaven.
The touch-point glow for A on the right half and for the single key on the left half is not a glass button. It does not change. The thin rings of unused directions also do not change.
The Silksong session used real touches in the Simulator to check the rhythm layout (BeatSpell). The input of B, ▶, and Y is the same as before the change. A two-finger simultaneous press was not tested.
Evidence: `.touchable/work/silksong/reports/glass-press-20261019/`.
Unit tests, the device build, and the IPA check passed. Build 34 is not installed.

The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build34-glass-press-20261019.ipa`.
The SHA-256 is `e00e8817f69e5ebfd2b4ec856bb7e4f65bf3f44b797debbededd65ea8a7f27c0`.
The build 33 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build34/`.

## 2026-10-19: Game Mode (build 33)

Build 33 declares Game Mode (`LSSupportsGameMode`, category: games).
The change comes from the shared `Info.plist` of the Silksong session. The user asked for Game Mode in all games.
Rhythm Heaven stays at 60 Hz. Its profile does not set `SS_DISPLAY_HZ`.
Reason: in build 13, when frames became slow, both audio and video became slow. Thus the game time most likely advances per frame.
The Silksong session changes for 120 Hz, present pacing, and the clear continuation have no effect on Rhythm Heaven.
Mac host BeatSpell battle: 60 fps, longest frame interval 18 ms, 13 passes per frame. This is the same as build 29.
Unit tests, the device build, and the IPA check passed. Build 33 is not installed.

The build 33 IPA is `RhythmHeaven-build33-game-mode-20261019.ipa`. It is moved to `.touchable/archive/ipa/rhythm-heaven/before-build34/`.
The SHA-256 is `6de716fb55c3ce338e2fc542ede01a8ec33b2f4db25da0cb3db61e3456a5f416`.
The build 32 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build33/`.
For details, see the [Game Mode record](../../../.touchable/work/rhythm-heaven/reports/game-mode-20261019/README.md).

## 2026-10-19: Smoother transition between the picture edge and the letterbox border (build 32)

Build 32 makes the transition between the picture edge and the letterbox border smoother.
A band of the letterbox border near the picture starts with the color of the picture edge. This band is 4% of the picture edge length, at most half of the border width, and about 53 pixels on iPhone and iPad. The color changes to the glow, fast at first and then slowly.
The brightness limit of the glow does not apply to this band. Thus there is no longer a dark step next to a bright picture.
The start color comes from the outermost cell of the light map, not directly from picture pixels. Thus there is no ghosting.
In offline renders, the seam step on the start menu decreased from 119 to 52 on iPhone and from 88 to 44 on iPad (scale 0–255). There are no jumps inside the letterbox border.
22 Mac unit tests passed. Device build 32 and the IPA check passed. Build 32 is not installed.

The build 32 IPA is `RhythmHeaven-build32-ambient-seam-20261019.ipa`. It is moved to `.touchable/archive/ipa/rhythm-heaven/before-build33/`.
The SHA-256 is `ae0848612c84173da6c0c889d19074283ed865629ea0ea977e65aef24dd22df3`.
The build 31 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build32/`.
For details, see the [border glow record](../../../.touchable/work/rhythm-heaven/reports/ambient-natural-20261019/README.md).

## 2026-10-19: Letterbox border follows the picture faster (build 31)

The user ran build 30 on the iPhone. Then the user asked for faster changes in the letterbox border.
Build 31 changes the cross-frame smoothing time constant of the border glow from 0.16 seconds to 0.06 seconds.
The letterbox border reaches 95% of the picture in about 0.18 seconds (build 30: about 0.48 seconds). A single-frame flash shows on the letterbox border at about one quarter of its strength.
A Mac unit test (Metal 4 present path) confirms that the border follows within 8 frames.
The build 30 iPhone log confirms that the new effect runs: frame time p50 16.67 ms, GPU p50 about 2.1 ms.
Device build 31 and the IPA check passed. Build 31 is not installed. The iPhone has build 30.

The build 31 IPA is `RhythmHeaven-build31-ambient-fast-20261019.ipa`. It is moved to `.touchable/archive/ipa/rhythm-heaven/before-build32/`.
The SHA-256 is `da61ee38a379e1dc322f466bccb3213dd315d83dd7ce449403c8ca3512f81cd1`.
The build 30 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build31/`.
For details, see the [border glow record](../../../.touchable/work/rhythm-heaven/reports/ambient-natural-20261019/README.md).

## 2026-10-19: More natural border glow (build 30)

Build 30 rewrites the border glow.
The light in the letterbox border comes from the adjacent picture edge and spreads outward.
Farther from the picture, the light becomes more blurred and darker. At the screen edge, it decreases smoothly to black.
Flashes and camera cuts do not make the letterbox border strobe. The build 30 time constant is 0.16 seconds (build 31 changes it to 0.06 seconds).

Problems in the old version:

- The light did not spread with distance. The letterbox border repeated the details of the picture edge. This caused horizontal or vertical stripes.
- The sample position was incorrect. On iPhone, the sample points were about 9% inside the picture, not at the picture edge.
- Next to a white picture, there was first a flat gray band.
- There was no cross-frame smoothing.

Build 30 also corrects a 1-pixel offset in the present position.
On iPhone 16 Pro Max, the old version moved the picture up by 1 pixel. The bottom row was black.

The strength is still `DM_NVN_AMBIENT_BARS` (0.8) in `profile.json`.
The code is in the upstream `nvn_mtl.m`. Silksong shares it. Silksong does not enable this effect. The Mac host regression check result did not change.
The new unit test `t_ambient.m` uses real Metal on the Mac to check both present paths.
The offline render comparison uses 8 game frames, at iPhone and iPad sizes.
The device build and the IPA check passed.
The user then installed build 30 on the iPhone.

The build 30 IPA is `RhythmHeaven-build30-ambient-natural-20261019.ipa`. It is moved to `.touchable/archive/ipa/rhythm-heaven/before-build31/`.
The SHA-256 is `73103adc4df9294fafc8707b902cad75aca94195ed6fec9e38a447d5f2f1d5d4`.
The build 29 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build30/`.
For details, see the [border glow record](../../../.touchable/work/rhythm-heaven/reports/ambient-natural-20261019/README.md).

## 2026-10-18: BeatSpell performance and touch (build 29)

Build 29 fixes the performance and touch controls of BeatSpell.

Performance:
A BeatSpell battle submits commands 46 times per frame.
The sprite batcher of the game waits for the GPU at each flush.
`runtime/rh_batch.c` skips these waits inside the same batch.
Present keeps its wait.
The upstream Metal layer copies the uniform buffers B1 and B2 at each submit. Thus the GPU reads the correct data.
In a Mac host battle, the busy time per frame decreased from about 14.6 ms to about 9.5 ms. Render passes decreased from 21 to 13 per frame.
`rh_batch.c` applies only to v0 main. If the code does not match, it keeps all waits.

Touch:
Build 28 used the small D-pad layout in BeatSpell. The left half had no key.
In build 29, the left half is B (step). The right half is still A (attack).
The small D-pad, X (items), and Y (map, backflip) show at the same time.

The Mac host, unit tests, and iOS Simulator checks passed.
The Silksong Mac host and Simulator regression checks passed.
The device build and the IPA check passed.
At 18:55 on 2026-10-18, build 29 was installed on the iPhone. After installation, the 3 save files matched the backup.
The game was not started after installation. The iPad mini still has build 27.
There is no BeatSpell result on the iPhone.

The build 29 IPA is `RhythmHeaven-build29-beatspell-20261018.ipa`. It is moved to `.touchable/archive/ipa/rhythm-heaven/before-build30/`.
The SHA-256 is `3c30ec326138cba3e712f52c63bb873b25b817c7e4f2409637c5cb01aecddebf`.
The build 28 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build29/`.
For details, see the [BeatSpell record](../../../.touchable/work/rhythm-heaven/reports/beatspell-20261018/README.md).

## 2026-10-18: Liquid Glass buttons (build 28)

In build 28, the always-visible buttons use Liquid Glass.
B, −, +, tutorial Y, and the small D-pad are glass circles in the clear style.
All buttons are in one glass container.
Each button that shows glass has a 35% black circle below it.
A pressed button glowed white, and its text became black (from build 34, the system press effect replaces this; see above).
The small D-pad arrows now use SF Symbols.

Unused directions no longer get 25% opacity.
The Apple guidelines require that glass views do not use alpha < 1.
Unused directions now show a thin ring without glass and a 30% arrow.
The right-half A, the left-half single-key glow, and the two-key areas do not change.
The touch areas, button masks, display conditions, and positions do not change.

The code is in `internal/platform/ios/App.m`. Silksong shares it.
The appearance and behavior of the Silksong buttons do not change. The Simulator regression test matches the log of Silksong build 17.

The iOS Simulator checked levels, presses, menus, and pure black letterbox borders on iPhone 16 Pro Max and iPad mini.
47 Python tests, `test_touch_layout.c`, and the device build passed.
At 14:02 on 2026-10-18, build 28 was installed on the iPhone. After installation, the 3 save files matched the backup from before installation.
The game was not started after installation. The iPad mini still has build 27.
The appearance and feel on the iPhone still need confirmation.

The launcher now refuses to build Rhythm Heaven.
Reason: three upstream tool hashes in the rewrite record are out of date. The module output still matches the record.
Build 28 uses the same manual Xcode path as build 27.

The build 28 IPA is `RhythmHeaven-build28-liquid-glass-20261018.ipa`. It is moved to `.touchable/archive/ipa/rhythm-heaven/before-build29/`.
The SHA-256 is `2261ef4889dd817fdedf5460abd40ea265b2113951677e7b45f2a8968e8bb719`.
The build 27 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build28/`.
For details, see the [Liquid Glass record](../../../.touchable/work/rhythm-heaven/reports/liquid-glass-20261018/README.md).

## 2026-10-18: Border glow (build 27)

Build 27 adds a real-time soft glow to the letterbox borders.
Each presented frame samples the edges of the game picture.
The soft glow is drawn only in the top and bottom letterbox borders or in the left and right letterbox borders.
The game picture itself does not change.
At full-screen 16:9, there are no letterbox borders. Thus nothing is drawn.

`DM_NVN_AMBIENT_BARS` in `profile.json` controls the strength.
The current value is `0.8`.
The range of this variable is 0 to 1.
Other games do not set this variable.

The shader limits the peak brightness. This prevents a white picture from making the letterbox border a white frame.
Sampling uses 17 horizontal points and 3 vertical depths. This decreases vertical stripes.

The Mac host and the iPad mini show this effect.
The final iPad check used a Stage Manager window to confirm the left and right letterbox borders.
Earlier full-screen screenshots confirmed the top and bottom letterbox borders and the Metal 4 path.
The iPhone was locked. Build 27 was not installed on it.

The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build27-ambient-bars-20261018.ipa`.
The SHA-256 is `6dff216774210ec54a9e701c8e170169e8adb165ae1718ab8678c4200b8bb6a9`.
The build 26 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build27/`.
For details, see the [border glow record](../../../.touchable/work/rhythm-heaven/reports/ambient-bars-20261018/README.md).

## 2026-10-18: Controller haptics (build 26)

Build 26 adds controller haptics.
The setting is `ios.controller_haptics`.
Only Rhythm Heaven enables this setting now.
The Switch left and right rumble values are merged into one intensity signal and one sharpness signal.
When a controller is connected, the game haptics go to both the phone haptic hardware and the controller.
The iPad mini has no phone haptic hardware. Thus it uses only the controller.

Tested on iPad mini (iPad16,2, iPadOS 27.0.1).
The controller is a DualSense Wireless Controller.
The code handles controller connection, disconnection, and change of the current controller.
The user confirmed physical haptics in menus and in levels.

The controller uses the standard Core Haptics player.
The iPadOS 27 controller engine refuses the advanced player with error code 4097.
The standard player accepts continuous intensity and sharpness updates.
The player no longer uses the "mute first, then unmute" start method.

50 Python tests passed.
The shared haptics C test passed.
The Objective-C syntax check passed.
The device build and installation passed.
The IPA CRC and SHA-256 checks passed.

The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build26-controller-haptics-20261018.ipa`.
The SHA-256 is `7d9ac84b92df64be8d5141e7bd920f1dd23919bfcea729c9ed69de76bb01979c`.
The build 19 and build 20 IPAs, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build26/`.
For details, see the [controller haptics record](../../../.touchable/work/rhythm-heaven/reports/controller-haptics-20261018/README.md).

## 2026-10-17: Language setting and build 19

Build 19 supplies a per-app language setting in iOS.
The setting path is "Settings > Apps > Rhythm Heaven > Language".
The app declares 13 game languages.
At start, the app reads the iOS preferred language. At run time, it selects the matching game language.
Unsupported iOS languages fall back to US English.
The settings page entry is confirmed on iPad mini.
These six screen languages are checked on iPad mini:

- English
- 日本語
- 简体中文
- 繁體中文
- Deutsch
- Español (México)

49 Python tests passed.
The Mac hosts of Silksong and Rhythm Heaven each ran for 30 seconds. Both exceeded 1500 frames.
Build 19 corrects a regression in the shared language handling. Silksong does not declare `ios.localizations`. Thus it continues to use the original Simplified Chinese default.
Build 19 checked again on iPad mini: English, Japanese, Simplified Chinese, and the fallback for an unsupported language.
Build 19 was not installed or tested again on iPhone.
The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build19-language-20261017.ipa`.
The SHA-256 is `a59a4bc313351b329e2c580e2ac9532dc5373afa123fe14fa6df86c523feea76`.
The build 18 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build19/`.
For details, see the [language setting record](../../../.touchable/work/rhythm-heaven/reports/language-20261017/README.md).

## 2026-10-17: iPad adaptation (build 17)

Build 17 adds an iPad layout.
The 16:9 game picture is centered. In iPad landscape, the letterbox borders are at the top and bottom.
B, Y, −, and + are in the top letterbox border.
The A and menu touch areas are limited to the game picture. The D-pad is at the bottom-left corner of the picture.
The iPhone layout keeps its original logic.
On iPad, the app supports only landscape full screen.
The app is installed and started on iPad mini (iPad16,2, iPadOS 27.0.1).
The user confirmed that menus, entry into minigames, and A touch work correctly.
The log records save commit #1.
For 30 groups of 300 frames, p50 is 16.65–16.70 ms.
After frame 2100, each group has 0 dropped frames.
The start phase still has isolated long frames.
Build 17 was not tested again on iPhone.
The full levels and the multiplayer mode are not completed.
The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build17-ipad-20261017.ipa`.
The SHA-256 is `8edf28bf2679974edf03bcdae251ffe8de9f7182c9932b4bcc2d59c865dd6752`.
The build 16 IPA, JSON, and SHA-256 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build17/`.
For details, see the [iPad mini record](../../../.touchable/work/rhythm-heaven/reports/ipad-mini-20261017/README.md).

## 2026-10-17: Directory reorganization and rebuild

This rebuild uses build 16.
The current IPA is in `IPA/rhythm-heaven/`.
The latest app path is recorded in `.touchable/build/games/rhythm-heaven/latest.json`.
The original packages, keys, old apps, original IPAs, and saves are kept.
The original IPA is moved to `.touchable/archive/ipa/`.
This task adds the build, launcher, and start check records.
Build 16 was installed on the iPhone through the launcher.
Before and after installation, the SHA-256 of the 3 save-related files matched.
A short start on the iPhone produced frames continuously. There were no missing shader errors.
This task did not verify the full play flow again.
The older build numbers below belong to existing test records.
For details, see the [reorganization and verification record](../../../docs/developer/reorganization.md).

## Existing game records


Rhythm Heaven Groove (Chinese title: 节奏天国 奇迹之星) Switch → iPhone port workspace.
Updated: 2026-10-18.

## Current status

The status is `in-progress`.
Before the reorganization, `game.json` and the phone installation record were both build 15.
The game results below come from existing records.
The records below do not include the short start check after this reorganization.

| Item | Current result |
| --- | --- |
| Game input | v0, Title ID `0100d9f01d474000`, SDK 21.4.5 |
| Active content | `.touchable/work/rhythm-heaven/base/` |
| Active modules | `.touchable/work/rhythm-heaven/patched/`, runs `main` and `sdk` |
| Graphics | 1920×1080, NVN to Metal, 35 shader stages |
| iPad/iPhone app | Build 35 (installed on the iPhone and the iPad mini on 2026-10-20), Bundle ID `com.mmmz.TouchableRhythmHeaven` (installed builds before 2026-10-20 use the previous Bundle ID. See [the rename record](../../../docs/developer/project-rename.md)) |
| Phone save | `Documents/Saves/RhythmHeaven` |
| Play record | Multiple rhythm games, some Remixes, menus, and save read/write are recorded. The full play-through is not verified |
| Direct touch | Normal lists, the map, pause options, the rhythm archive, the music room, and demo videos |
| Save check | After build 29 was installed on the iPhone, the 3 files matched the backup. After the build 15 recheck, both saves kept 80 P modes |

Build 34 uses the system glass press effect. Build 33 declares Game Mode and stays at 60 Hz. Build 32 makes the transition between the picture edge and the letterbox border smoother. Build 31 makes the letterbox border follow the picture faster (time constant 0.06 seconds). None of these builds is installed on a device yet.
Build 30 rewrites the border glow. It passed unit tests and the offline render comparison. The user ran it on the iPhone.
Build 29 fixes the performance and touch controls of BeatSpell. It passed the Mac host and Simulator checks and is installed on the iPhone. BeatSpell on the iPhone is not confirmed yet.
The build 28 buttons use Liquid Glass. Build 28 passed the Simulator check and is installed on the iPhone. The appearance on the iPhone is not confirmed yet.
The iPad mini still has build 27.
Build 27 is installed on the iPad mini.
Build 27 adds the border glow.
Build 19 confirmed the language setting entry and six screen languages.
The language setting entry and six screen languages are verified.
Build 17 confirmed that menus, entry into minigames, and A touch work correctly.
Build 27 was not tested again on iPhone.

Build 15 is installed on iPhone 16 Pro Max (iOS 27.2).
The user confirmed that touch works correctly in the three new menus.
Mac checks cover list drag, item tap, comic open, music playback, and demo video start.
For details, see the [extra menu touch report](../../../.touchable/work/rhythm-heaven/reports/extra-menu-touch-20261016-144228/README.md).

Build 14 fixes repeated texture uploads caused by the shared memory pool lookup.
A manual iPhone record of about 12 minutes contains 44,377 timed frames.
The median frame time is 16.67 ms, and p99 is 19.03 ms.
The user confirmed that audio and video are correct.
The first load still has isolated spikes.
For details, see the [performance report](../../../.touchable/work/rhythm-heaven/reports/performance-20261016-134827/README.md).

For past changes, test conditions, and installation records, see [HISTORY.md](HISTORY.md).

2026-10-17: Old builds were removed at the request of the user.
The latest local build is in `.touchable/build/games/rhythm-heaven/ipad-device-build26/`.
The checksum records of old IPAs and old build logs are kept in `.touchable/build/games/rhythm-heaven/logs/retired-builds-20261017/`.
The old paths are only for the history record.
For details, see the [build retention notes](../../../docs/developer/workspace-maintenance.md#latest-build-retention-2026-10-17).
For the workspace reorganization rules, see the [maintenance notes](../../../docs/developer/workspace-maintenance.md).

## Known limits

- In level 2, the score digits sometimes show as black blocks. The block height fix in build 6 corrected the decoding of the score digit texture. This most likely solved the problem, but it was not checked again in the game.

- The software keyboard is not connected. When it opens, it immediately returns "Cancel".
- Reverb is an approximation from I3DL2 parameters. It does not sound fully the same as on the console. When the game disables reverb by default, only the dry path is used, so there is no effect.
- `internal/runtime/common/ss_fallback_font.h` replaces the system shared font. It contains only U+25A1 and the button icons (U+E0A0–E0AF, U+E0E0–E0F0). `internal/tools/make_fallback_font.py` generates it.
- Multiple rhythm games and some Remixes are verified. Build 15 verified touch in the rhythm archive, the music room, and demo videos. All levels, the multiplayer mode, and all extra modes are not yet tested one by one.
- Bluetooth headphones increase audio latency. The game notes also state this.
- The build 14 manual iPhone record covers the wipers game and multiple Remixes. In that record, the continuous slowdown from texture re-uploads did not occur again. The first load still has isolated spikes. This result does not prove that all levels play without stutter.
- The picture stays at 16:9, without crop or stretch. The iPhone screen has letterbox borders on the two sides. In iPad landscape, there are letterbox borders at the top and bottom. A trial with a wider camera view (Mac host) showed that the scene art stops at the 16:9 edge. There is no content on the two sides. The record is in `.touchable/work/rhythm-heaven/reports/widescreen-trial-20261014/`.
- iPad is tested only on iPad mini (iPad16,2). Other iPad sizes are not tested. The app forces landscape full screen. `UIRequiresFullScreen` is deprecated from iOS 26. A future system can ignore this key.
- The build 30 border glow ran on the iPhone (the user). GPU p50 is about 2.1 ms. The 0.06-second time constant of build 31 and the seam transition of build 32 are checked only in Mac unit tests and offline renders. The falloff curve, blur width, brightness limit, and time constant are tuned by eye. There is no HDR or colorimeter measurement. When the picture edge is black, the letterbox border is also almost black.
- The Rhythm Heaven Mac host runs in command-line mode. It does not present to a window. Thus host runs do not use the letterbox border code.
- Controller haptics are tested only with DualSense and iPad mini. Other controllers, iPhone with a controller, controller disconnect and reconnect, and long continuous haptics are not tested one by one.
- Device screenshots confirm the language setting entry. The language selection row was not tapped automatically. Device language tests use the `-AppleLanguages` launch argument. Only six of the 13 languages are checked one by one.
- The Bundle ID changed to `com.mmmz.TouchableRhythmHeaven` on 2026-10-20. The installed apps use the previous Bundle ID. Move the save to the new app after the first install.
- The build 28 Liquid Glass is checked only in the iOS Simulator. The appearance and the dark layer on the iPhone are not confirmed. The build 34 system press effect is checked only with one finger in the Simulator. A two-finger simultaneous press and the feel on the iPhone are not confirmed. Faded directions are not glass and show no effect when pressed.
- The build 29 BeatSpell changes are checked only on the Mac host and in the iOS Simulator. There are no iPhone frame time or feel results. The host tested only the first battle of chapter 1 and the tutorial. Backflip, branches, items, and the map are not tested one by one.
- In BeatSpell, the small D-pad covers part of the HP display at the bottom-left corner. On iPad, it covers more. The BeatSpell items, map, and swap magic menus are not tapped directly.
- `rh_batch.c` depends on fixed addresses of v0 main. Between Begin and End, it skips the GPU waits of the sprite batcher. `RH_BATCH_WAIT=1` restores all waits.
- The launcher refuses to build Rhythm Heaven. The `patch_nso.py`, `structck.py`, and `equiv.py` hashes recorded in `patched/verification.json` are older than the upstream changes after 2026-10-14. The module output matches the record. Builds now use the manual Xcode path. Before you update the record, rewrite and verify the modules again in a new output directory.

## Touch controls

`ios.touch_layout` in `game.json` is `rhythm` (from build 4):

| Position | Key |
| --- | --- |
| Right half of the screen | A. From build 12, there is no always-visible hint. A press shows a white glow at the touch point and shows A above the finger. On the opening screen, you can still tap the right half to continue |
| Left half of the screen | Changes with the current scene (see the table below) |
| Small D-pad at the bottom-left corner | ◀ ▲ ▶ ▼. From build 10, used for levels that need multiple directions. On unrecognized screens, the original tap areas stay, and the appearance is hidden |
| Top of the left letterbox border | B (back), always visible |
| Below B | Y. From build 13, visible during the interactive tutorial phase. Hold it to do the demonstration action. From build 29, always visible in BeatSpell, and a press is Y |
| Below Y | X. From build 29, visible only in BeatSpell |
| Top of the right letterbox border | + (pause) and − (skip practice), always visible |

In iPad landscape, B, Y, X, −, and + move to the top letterbox border.
The A area, the small D-pad at the bottom-left corner, and the menu tap areas stay inside the 16:9 game picture.
The iPhone position rules do not change.

From build 28, B, Y, −, +, and the small D-pad are Liquid Glass circles.
From build 34, a press shows only the system glass press effect (larger, brighter from the inside, no outer glow).
Unused directions are thin rings without glass.
See the build 28 record at the top of this document.

Use of the left half:

| Scene | Left half |
| --- | --- |
| Recognized menus and level maps | Tap or drag directly. See the table below |
| Menus or result screens that are not recognized yet | The original tap areas of the small D-pad and the right half still work. No button appearance shows |
| Rhythm games that use only A (hoop jumping, frisbee, and others) | A |
| Rhythm games that use A and one direction | That direction: ▼ for umbrella, hurdles, squat fungus, and others; ◀ for wipers, beat keeping, shooting practice, and others; ▶ for rolling march |
| Levels that need two or more directions, such as dancer and martial arts master | Small D-pad at the bottom-left corner: ▲, ▼, ◀, ▶ |
| Wrestler contest and cast quiz (A and B) | B |
| BeatSpell (from build 29) | B (step). The small D-pad at the bottom-left corner, X, and Y also show. The right-half A is attack |
| Multi-key modes such as drum lessons, and unrecognized scenes | Small D-pad at the bottom-left corner: ▲, ▼, ◀, ▶ |

Both halves support multiple fingers. You can press them at the same time. A short tap is held for at least 25 ms. Press + to open the pause menu. You can tap a recognized pause menu directly. An external controller works directly.

Build 10 changes multi-direction levels to a four-direction key group at the bottom-left corner.
Each button is 50 × 50 points.
The full group is 158 × 158 points and avoids the screen safe area margins.
In these levels, the original large touch area on the left half is hidden.
A tap on empty space does not trigger the D-pad.
Single-direction levels still use the original single-key area.
A Remix also uses the small D-pad when it reads two or more directions.
For the detailed check, see the [small D-pad record](../../../.touchable/work/rhythm-heaven/reports/multidirection-dpad-20261016/README.md).

Build 11 adjusts the small D-pad appearance to the direction needs of the current level.
Unused directions have 25% of the original overall opacity.
Needed directions keep the original value.
For example, in the dancer level, the up and down keys fade, and the left and right keys keep the original value.
The tap areas and key functions of the four directions keep the original values.
A Remix updates with the union of directions from the scenes read so far.
Modes with unknown direction needs keep the original display level for all four directions.
Build 28 no longer decreases opacity. Unused directions now have an appearance without glass.
For the detailed check, see the [D-pad opacity record](../../../.touchable/work/rhythm-heaven/reports/unused-directions-20261016/README.md).

The direction keys use the level display rules of build 9.
The direction keys show only when the current level state is valid.
The direction keys are hidden on the opening screen, in menus, on the map, in the pause menu, and during screen transitions.
The direction keys are hidden when the screen data is out of date.
When the level display state ends, keys that were held are released.
On the opening screen, you can still tap the right half to continue.
This rule applies only to the `rhythm` layout.
For the build 8 menu detection and the build 9 level detection, see the [hidden-by-default record](../../../.touchable/work/rhythm-heaven/reports/level-controls-20261016/README.md).

Build 12 keeps the trigger area of the right-side A. It removes the always-visible letter, the border, and the full-area press background.
A white radial glow shows at the touch point. The letter A is above the finger.
When the finger moves, the feedback follows the touch point.
After release, the feedback fades out within 0.16 seconds.
Each finger shows its own feedback.
B, +, and − become always-visible letterbox border buttons. Y shows with the tutorial demonstration hint.
For the detailed check, see the [touch feedback record](../../../.touchable/work/rhythm-heaven/reports/touch-feedback-20261016/README.md).

Build 13 uses the same feedback for the single-key area on the left half.
The left side no longer always shows the letter, the border, and the full-area press background.
The original trigger area does not change.
The current key symbol, such as A, B, ◀, or ▶, shows above the touch point.
The inputs of the two halves are combined as a union.
When you hold A on both sides, release of one side keeps the input of the other side.
Y shows according to the native tutorial enable state.
When the demonstration hint alternates with the skip hint, Y stays visible.
When the interactive tutorial phase ends or the pause menu opens, Y is hidden.
For the detailed check, see the [left-side glow and tutorial Y record](../../../.touchable/work/rhythm-heaven/reports/steady-touch-20261016/README.md).

Direct touch in build 7:

| Screen | Action |
| --- | --- |
| Normal list menu | Drag up and down in the list. Tap a visible item to execute it. Release after a drag does not execute an item |
| Level map | Drag the map left and right. The first tap only selects a level. A second tap on the same level starts the game. Two taps are necessary even when the level was already selected. A drag or a change of the selected level resets the count |
| Pause menu and common option pop-ups | Tap an option to execute it |
| In-game "Y demonstration" hint | Holding the hint is the same as holding Y. Release is the same as releasing Y |
| In-game "− skip" hint | A tap on the hint is the same as a press of − |

The tap areas come from the node positions that the game currently shows. Touch points are converted to the 16:9 game picture. The letterbox borders on the two sides are not part of the menu. A screen change, a touch cancel, or out-of-date screen data cancels the current gesture. A second entry into a level is confirmed in a P mode test on the iPhone. Build 13 recorded an iPhone recheck of tutorial Y. Multi-finger input and background cancel still need a dedicated check.

Implementation: when the host opens game files, `internal/games/rhythm-heaven/runtime/rh_touch.c` identifies the scene from the directory that is read. When it reads `graph/prologue/<name>/` (title card) or `graph/scene/<name>/`, it enters that rhythm game. When it reads `graph/menu/` or `graph/result/`, it returns to the menu. Remix levels are expected to read multiple scene directories, so the code uses the union of the keys. In a Mac test of Remix 1, only the scene directory of the umbrella level was read. The other three levels did not trigger. The left half stayed at ▼ of the umbrella level the whole time. For each rhythm game, the keys other than A come from the button icons in the controller hints in `mesg/CN/CNzh/stage_<name>_00.msbt` (not the H version for a single sideways Joy-Con). `internal/runtime/common/ss_touch.h` passes the result to `internal/platform/ios/App.m`. Test: `internal/games/rhythm-heaven/tests/test_touch.c`.

The first item of the start menu, "What is Rhythm Heaven?", plays an introduction. Tap "Single player" to go to the level map. You can also press ▼ and then A.

2026-10-16: The [menu tap investigation](../../../.touchable/work/rhythm-heaven/reports/menu-touch-20261016/README.md) and the first implementation are complete. `rh_menu.c` reads the current menu, map, and hint nodes. It reuses the select, confirm, and button input flow of the game. The game module files are not changed. The fixed addresses apply only to the current v0 module. The shared touch-point queue is in `internal/runtime/common/ss_pointer.c`. The iPhone touch-point entry is in `internal/platform/ios/App.m`. For detailed tests and coverage limits, see the [implementation record](../../../.touchable/work/rhythm-heaven/reports/menu-touch-implementation-20261016/README.md). The extra modes and all pop-ups are not yet tested one by one.

## Unlock all levels

`internal/games/rhythm-heaven/tools/unlock_all.py` changes the save `game_save.bin`. It unlocks all 80 rhythm games and Remixes of the main world, the 30 levels of the reverse world, and the 13 menu items on the left side of the level map, including the cafe. (On the Mac, Rhythm Toys and the cafe were confirmed to appear. The mapping of the other items comes from the name table and is not verified one by one.) Cleared levels and earned medals do not change.

```sh
.touchable/venv/bin/python internal/games/rhythm-heaven/tools/unlock_all.py original-save/game_save.bin --output new-directory/game_save.bin
```

Save format (from the save management code in `main` at main+0x3ca0 and main+0x3f70):

| Offset | Content |
| --- | --- |
| 0x00–0x0F | Not a checksum. The game does not check it |
| 0x10, 0x14 | Header size 0x34 and file size 0x1188. The game checks only these two fields |
| From 0x28, 4 bytes per entry | Menu items (kind 0, numbers 0–13; names from main+0xcdad60: 0 cafe, 1 bar, 2–3 toys, 4 score, 5 BeatSpell, 6 drum, 7 credit, 8 cast, 9–10 parts, 11 player number, 12 name edit, 13 dummy): 0 hidden, 1 shows "?", 3 unlocked. Builds before 2026-10-21 of the tool skipped entry 0 (cafe) |
| From 0x60, 16 bytes per entry | Main world levels (kind 1, numbers 0–79): 0 hidden, 1 "?", 2 unlocked (NEW), 4 and higher for played, cleared, and medals |
| From 0x560, 8 bytes per entry | Reverse world levels (kind 2, numbers 0–29). The states are the same as above |

The mapping between levels and numbers is in the level table of `main` (from main+0xd19108, 0x30 bytes per entry).

On 2026-10-14, this was done on the phone save. The original save is backed up in `.touchable/work/rhythm-heaven/saves/phone-backup-20261014-before-unlock/`. The unlocked save is in `.touchable/work/rhythm-heaven/saves/phone-unlocked-20261014/`. The same change was tried on the Mac: the level map showed all 8 columns of levels marked NEW, the menu items appeared on the left side, and THE A in column 6 played correctly.

Before you replace the phone save, close the game. Then write to the committed path `Documents/Saves/RhythmHeaven/game_save.bin` (at start, the game copies `.work` from this file):

```sh
xcrun devicectl device copy to --device DEVICE_ID --domain-type appDataContainer \
  --domain-identifier com.mmmz.TouchableRhythmHeaven --source new-save/game_save.bin \
  --destination Documents/Saves/RhythmHeaven/game_save.bin
```

To restore the original progress, use the same command to write back `game_save.bin` from the backup directory.

## Unlock all P modes

`unlock_all.py --perfect-only` opens the P mode of the 80 main world levels.
This option only raises the state of these levels from below `7` to `7`.
The original scores, play counts, P win streaks, menu states, and reverse world progress do not change.
Existing `8` and `9` states do not change.
This option does not open additional P+ modes.

```sh
.touchable/venv/bin/python internal/games/rhythm-heaven/tools/unlock_all.py original-save/game_save.bin \
  --perfect-only --output new-directory/game_save.bin
```

The P unlock and the map rank use the same state field.
Thus the map also shows the P mark and counts the total medals as 80.
`main+0x1d2d64` reads the state. State `7` shows two options: normal play and perfect challenge.
This address applies only to the v0 module recorded in the configuration.

On 2026-10-16, the phone save was updated. The app was still build 7.
The original save is in `.touchable/work/rhythm-heaven/saves/phone-backup-20261016-before-perfect/`.
The changed version is in `.touchable/work/rhythm-heaven/saves/phone-perfect-unlocked-20261016/`.
The read-back check after the write matched. The change touches only 80 state bytes.
After the iPhone committed the save again, the 80 states were still `7`.
The Mac verified the P selection menu and level load.
The iPhone log recorded two level taps, a selection of the P item, and a level load.
The challenges were not completed level by level.
For tests, screenshots, and restore instructions, see the [P mode record](../../../.touchable/work/rhythm-heaven/reports/perfect-unlock-20261016/README.md).

## iPhone/iPad build and installation

The Bundle ID is `com.mmmz.TouchableRhythmHeaven`. Silksong uses `com.mmmz.TouchableSilksong`. Each Bundle ID needs its own provisioning profile (team `<TEAM_ID>`). Builds before 2026-10-20 used a borrowed previous Bundle ID. See [the rename record](../../../docs/developer/project-rename.md). The save is in `Documents/Saves/RhythmHeaven` in the container of this app. Before a build, check the actual signing and provisioning profile.

```sh
.touchable/venv/bin/python internal/tools/prepare_ios.py --game rhythm-heaven
xcodebuild -project .touchable/build/games/rhythm-heaven/ios-project/RhythmHeaven.xcodeproj -scheme RhythmHeaven \
  -configuration Debug -sdk iphoneos -derivedDataPath .touchable/build/games/rhythm-heaven/ios \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO DEVELOPMENT_TEAM=<TEAM_ID> build
APP=.touchable/build/games/rhythm-heaven/ios/Build/Products/Debug-iphoneos/RhythmHeaven.app
cp "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles/<profile UUID>.mobileprovision" "$APP/embedded.mobileprovision"
codesign --force --sign "Apple Development" --entitlements entitlements.plist --generate-entitlement-der "$APP"
xcrun devicectl device install app --device DEVICE_ID "$APP"
```

`entitlements.plist` is the same as for Silksong. It contains only `application-identifier` (`<TEAM_ID>.com.mmmz.TouchableRhythmHeaven`), `com.apple.developer.team-identifier`, and `get-task-allow`. The app is about 3.2 GB. Installation takes a few minutes.

Build 27 and build 28 use Xcode automatic signing.
The provisioning profile includes both the original iPhone and the iPad mini.
Build 27 is installed on the iPad mini. Build 28 is installed on the iPhone.
The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build28-liquid-glass-20261018.ipa`.
The launcher action "Install existing IPA" can install it. The launcher build actions now refuse Rhythm Heaven. See the known limits.
The build 27 IPA is moved to `.touchable/archive/ipa/rhythm-heaven/before-build28/`.
The build 26 IPA is moved to `.touchable/archive/ipa/rhythm-heaven/before-build27/`.
The build 19 and build 20 IPAs are kept in `.touchable/archive/ipa/rhythm-heaven/before-build26/`.

Before the reorganization, the phone had build 15.
The IPA at that time was `RhythmHeaven-build15-extra-menu-touch.ipa`.
That IPA and its signing and checksum records are moved to `.touchable/archive/ipa/rhythm-heaven/`.
For the installation and save checks of builds 1–14, see the [build record](HISTORY.md).
For that history phase, only the build 15 IPA and the latest local build of that time are kept.

iPhone tests can run without touching the phone. Start the app with `devicectl device process launch --environment-variables '{"RH_INPUT_FILE":"Documents/rh_input.txt"}'`. Then use `devicectl device copy to` to write this file (content `MASK 0 0`, a hexadecimal button mask, A = 1, ▼ = 8000). In tests, when the press file and the release file had the same size, one of the alternating copies had no effect. Thus the two files must have different sizes and modification times. The app cannot read input when it is in the background. The app reads the file only when the environment variable is set. It does not read the file when it starts from the Home Screen.

The game log is at `Documents/Logs/game.log` in the app container:

```sh
xcrun devicectl device copy from --device DEVICE_ID --domain-type appDataContainer \
  --domain-identifier com.mmmz.TouchableRhythmHeaven --source Documents/Logs/game.log --destination game.log
```

The first build with `com.mmmz.TouchableRhythmHeaven` installs as a different app. Copy the old save from the old container into the new app. When the provisioning profile expires, sign and install the app again.

## Move to another Mac

You can reuse the current extracted content, rewritten modules, and iOS shaders.
The transfer tool packages only the Rhythm Heaven game data. It keeps the current app identity and save path.
The original NSP, keys, signing material, IPAs, and personal saves are not put in the transfer package.

```sh
.touchable/venv/bin/python internal/tools/package_mac_transfer.py --game rhythm-heaven \
  --output .touchable/build/transfers/rhythm-heaven-build15-mac
```

Copy the generated ZIP and SHA-256 files to the new Mac.
After you extract them, read `README-FIRST.md` first. Then double-click `Open-RhythmHeaven.command`.
The script verifies all files. It generates and opens the Xcode project for the new path.
The project generator keeps the quotes in the header search paths. This supports new directories with spaces in their names.
The new Mac needs the full Xcode, Apple Silicon, and local signing settings.
Before you reinstall, back up the phone save.
For the full procedure, see the [Mac transfer guide](../../../docs/developer/mac-transfer.md).

## Rebuild from game files

```sh
.touchable/venv/bin/python internal/tools/prepare_game.py extract \
  "MyGameFiles/rhythm-heaven/Rhythm Heaven Groove [0100D9F01D474000][v0][Base][Conv].nsp" \
  --game rhythm-heaven --keys MyGameFiles/keys/ProdKeys.-v22.5.0/prod.keys
.touchable/venv/bin/python internal/tools/analyze_game.py --game rhythm-heaven
.touchable/venv/bin/python internal/tools/rewrite_game.py --game rhythm-heaven --jobs 8
.touchable/venv/bin/python internal/tools/extract_shaders.py --game rhythm-heaven
.touchable/venv/bin/python internal/tools/build_shaders.py --game rhythm-heaven --ios
bash internal/tools/build_host.sh --game rhythm-heaven
mkdir -p .touchable/work/rhythm-heaven/temp
TASK_SAVE_DIR=$(mktemp -d .touchable/work/rhythm-heaven/temp/host-check.XXXXXX)
.touchable/venv/bin/python internal/tools/run_host.py --game rhythm-heaven --seconds 60 --save-dir "$TASK_SAVE_DIR"
```

At run time, use `internal/tools/input_host.py --game rhythm-heaven A` to press keys. `SS_AUDIO_DUMP=out.wav` writes the mix result to a WAV file. `DM_AUDIO=1` plays audio on the Mac.

## NSP notes

This NSP was converted from a game card XCI. The conversion changed the distribution type in 4 NCA headers from "game card" to "download". Thus the NCA hashes do not match the content IDs. `prepare_game.py` changes this byte back to "game card" and calculates the hashes again. It accepts the result only when the hashes then match the content IDs. The report records this as `gamecard_conversion`. `internal/tools/nca_header.py` does the AES-XTS decryption and encryption of NCA headers.

## Game information

- Title ID `0100d9f01d474000`, version 0, SDK 21.4.5. ExeFS has only `main` and `sdk`. It is not a Unity game.
- `main` Build ID prefix `03ff4a3bec96bde3`. The module hashes are fixed in `profile.json`.
- RomFS has 7,237 files, about 3.17 GiB: audio `sound/sm/*.cwv` (about 2.2 GiB), textures `graph/*.tex`, text `mesg/*.msbt` (includes Simplified and Traditional Chinese), beat markers `wavmark/*.wmb`, fonts `mesg/Font/*.bfttf.zs`, and speech synthesis `voicetext/*.vtdb2`. There is no video.
- Shaders are in `shader/shader_NX.bnsh`: 20 control blocks and 35 different stages (stages with the same control block and different code are separated by code; see `SS_SHADER_KEY_CODE`).
- Audio uses AudioRenderer: 48 kHz, 240 samples per frame, 64 voices, 1 submix and I3DL2 reverb, stereo output.
- The render window origin is at the bottom-left corner (`SS_NVN_LOWER_LEFT_ORIGIN`).

## Adaptation contents

| File | Content |
| --- | --- |
| `internal/games/rhythm-heaven/game.json` | Build configuration, touch layout, 5 ms audio buffer |
| `internal/games/rhythm-heaven/runtime/rh_game.c` | Host draw size, per-scene touch layout, development input file |
| `internal/runtime/common/ss_platform.c` | `ss_performance_mode`: optional performance mode for game adapters (default 0) |
| `internal/games/rhythm-heaven/profile.json` | Module hashes, shader files, NVN binding layout, vertex attribute types |
| `internal/runtime/common/ss_audren.c` | Mixer: PCM16 voices, Hermite resampling, submix, reverb |
| `internal/runtime/common/ss_audio_renderer.c` | `nn::audio` AudioRenderer interface, CoreAudio output |
| `internal/runtime/common/ss_services.c` | System services such as random numbers, time, accounts, shared fonts, save mount, and software keyboard |
| `internal/runtime/common/ss_input.c` | Dual Joy-Con state, controller setup screen, short-press hold |
| `internal/tools/extract_shaders.py` | BNSH shader extraction |
| `internal/tools/build_shaders.py` | NVN binding layout, vertex attribute types, lower-left origin flip |
| `.touchable/dependencies/d2r-mobile/dm/gfx/nvn/` | Automatic block height round-down (`nvn_tex.c`), release of the texture-ID cache view when a texture is destroyed (`nvn_mtl.m`), 16-byte texture views of the new SDK, depth clamp, texture view channel swizzle, scaled copy, linear textures, shared page write-protect fix |
| `internal/platform/ios/App.m` | `rhythm` touch layout, native resolution canvas, audio buffer setting |
| `internal/tests/common/test_audren.c` | Mixer tests: passthrough, counting, looping, pitch change, reverb dry path |

For the full procedure, see [Add a game](../../../docs/developer/add-game.md). Do not put keys or game data in `internal/games/`.
