# Rhythm Heaven build history

This record is current to 2026-10-19.
This file keeps the test results, install paths, and limits of each build at its time.
For the current build, operations, and limits, see [README.md](README.md).
Untested items of old builds do not show the current status.
On 2026-10-17, old apps and IPA files were deleted at the user's request.
Old output paths in this file are historical records. The checksum records stay in `.touchable/build/games/rhythm-heaven/logs/retired-builds-20261017/ipa/`.
The documentation cleanup of build 16 did not run new Mac or iPhone game tests.
Build 17 added iPad device checks. The results are in the records below.
Build 29 is installed on the iPhone, but it has no device game test. Its results come from the Mac host, unit tests, and the iOS Simulator.
The user ran build 30 on the iPhone. Builds 31–34 have no device tests. Their results come from the Mac host, unit tests, and offline renders.

## TV mode follows the audio output (build 38)

The mode button is removed. Built-in speaker, wired headphones, USB audio, and line out give handheld mode; other outputs (Bluetooth, AirPlay, HDMI) give TV mode. The app checks at launch, on route changes, and when it becomes active. Installed on the iPhone on 2026-10-21; saves matched the backup.

## TV mode switches without a restart (build 37)

The TV button changes the operation mode while the game runs (notification 30 and a controller style event). Mac host: handheld → TV → handheld, the game took notification 30 each time and kept running. Installed on the iPhone on 2026-10-21; saves matched the backup. The iPad mini was unavailable.

## Long list taps and optional TV mode (build 36)

Build 36 takes the ideas of pull requests #6 and #7: tap areas for every visible row of a scrolled list, and an optional TV mode that the app saves and applies at the next launch.
Mac host: 11 tap targets after 20 rows in the music room list (2 before). `DM_OPMODE=1` opens the TV latency calibration. Unit tests, both Mac hosts, and the device build passed.
Installed on the iPhone on 2026-10-21; saves matched the backup. The iPad mini was unavailable. See README.md.

## System glass press effect (build 34)

In a Silksong session, the user asked for the glass button light effect to match the official Apple buttons.
The Silksong session removed the custom press effect from the shared `App.m` (`showGlassPress`: white background, black text, outer glow).
When the user presses a Rhythm Heaven glass button, it shows only the system effect: the glass grows and becomes evenly brighter from the inside. There is no outer glow.
The touch-point halo and the thin rings of unused directions do not change.

Check results:

- Simulator real-touch test in the Silksong session (rhythm layout, BeatSpell hint): press and release of B `0x2`, ▶ `0x4000`, and Y `0x8` are the same as before the change. Two-finger simultaneous presses were not tested. Evidence: `.touchable/work/silksong/reports/glass-press-20261019/`.
- The iOS device SDK syntax check of `App.m` has no warnings. `test_touch_layout.c`, the 48 Python tests, and `games.py check` pass.
- Xcode device build 34 passes with 41 warnings (the same as build 33). IPA packaging and the provisioning profile check pass.

Build 34 is not installed.
The IPA, JSON, and SHA-256 of build 33 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build34/`.
IPA: `IPA/rhythm-heaven/RhythmHeaven-build34-glass-press-20261019.ipa`.

## Game Mode (build 33)

In a Silksong session, the user asked to turn on Game Mode for all games.
The Silksong session changed the shared `Info.plist`, `App.m`, `ss_platform.c`, `hle_nvn.c`, and `nvn_mtl.m`.
Rhythm Heaven gets `LSSupportsGameMode` and the game category. Its display and presentation stay at 60 Hz. The clear-continuation change does not run in Rhythm Heaven (`DM_NVN_TBDR=0`).

Check results:

- Mac host BeatSpell battle (new scratch host): 60 fps, longest frame interval 18 ms, busy_ms 9.90 (build 29 had 8.95; this is in the prior variation range), 13 passes per frame. The log has the 60 Hz presentation cadence and the BeatSpell touch hint.
- The 48 Python tests, `games.py check`, `test_touch.c`, and `test_touch_layout.c` pass. Upstream `t_ambient`, `t_ambient_ll`, `t_clear_keep`, `t_present_crop`, and `t_ub` pass.
- Xcode device build 33 passes with 41 warnings. The app `Info.plist` contains the Game Mode keys and does not contain `SS_DISPLAY_HZ`. IPA packaging and the provisioning profile check pass.

Build 33 is not installed.
The IPA, JSON, and SHA-256 of build 32 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build33/`.
IPA: `IPA/rhythm-heaven/RhythmHeaven-build33-game-mode-20261019.ipa`.
For details, see [the Game Mode record](../../../.touchable/work/rhythm-heaven/reports/game-mode-20261019/README.md).

## Smoother transition between image edge and letterbox border (build 32)

The user asked for a smoother transition at the edges.
Build 32 adds a transition zone in the part of the letterbox border near the image (4% of the image edge length, at most half of the border width).
The start value is the color of the outermost cell of the light map, at strength 0.75 × `DM_NVN_AMBIENT_BARS`. The brightness cap does not apply to it. It transitions to the light with `u(2 − u)`.
We tried to sample the image edge pixels directly, but this drew ghost images. So we did not use it.

Check results:

- Offline render: the seam step in the start menu went from 119 → 52 on iPhone and from 88 → 44 on iPad (0–255). The maximum difference between adjacent columns (rows) in the border is ≤ 2.2.
- `t_ambient` and `t_ambient_ll` pass 22 tests each (`MTL_DEBUG_LAYER=1`). Other upstream graphics tests are the same as before.
- Xcode device build 32 passes. The warning count is the same as build 31. IPA packaging and the provisioning profile check pass.

Build 32 is not installed.
The IPA, JSON, and SHA-256 of build 31 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build32/`.
IPA: `IPA/rhythm-heaven/RhythmHeaven-build32-ambient-seam-20261019.ipa`.

## Letterbox border follows the image faster (build 31)

After the user ran build 30 on the iPhone, the user asked for faster border changes.
Build 31 changes the time constant of the cross-frame smoothing from 0.16 seconds to 0.06 seconds. Other parts do not change.

Check results:

- `t_ambient` and `t_ambient_ll` pass 21 tests each. Each ran 3 times with `MTL_DEBUG_LAYER=1`. The tests require the Metal 4 present path to catch up within 8 frames.
- iPhone log of build 30 (run by the user): the new effect runs. Frame time p50 is 16.67 ms. GPU p50 is approximately 2.1 ms.
- Xcode device build 31 passes. The warning count is the same as build 30. IPA packaging and the provisioning profile check pass.

Build 31 is not installed. The iPhone has build 30.
The IPA, JSON, and SHA-256 of build 30 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build31/`.
IPA: `IPA/rhythm-heaven/RhythmHeaven-build31-ambient-fast-20261019.ipa`.

## More natural border glow (build 30)

Build 30 rewrites the border glow in upstream `nvn_mtl.m`.

- Light map: the image is divided into 64 × 36 cells. Each cell is averaged in linear light. The present path smooths it across frames with a 0.16-second time constant.
- Letterbox border: a Gaussian blur along the image edge. Farther from the image, the blur is wider and the sample goes deeper. First, too-bright light sources are reduced. Then the light falls off with distance in linear light and is 0 at the screen edge.
- Fixes the sample coordinates of the old version. The old version normalized with the screen size. On iPhone, it sampled from approximately 9% inside the image.
- Fixes the presentation position: the size is rounded first, then centered. On 2868×1320, the old version moved the image up by 1 pixel, and the bottom row was black.

Check results:

- The new unit tests `t_ambient` and `t_ambient_ll` (bottom-left origin) pass 21 tests each. This includes the cross-frame smoothing of the Metal 4 present path. There are no validation errors with `MTL_DEBUG_LAYER=1`.
- Other upstream graphics tests pass, except `t_refl` and `t_rt_range_ro` (the local reflection samples are missing).
- Offline render comparison: 8 game images, iPhone 16 Pro Max and iPad mini sizes, old and new versions.
- A 45-second Rhythm Heaven Mac host run has no Metal validation errors. This host does not present to a window.
- Silksong Mac host regression: the log, frame times, and image are the same as in the build 29 check.
- Xcode device build 30 passes. The warning count is the same as build 29. IPA packaging and the provisioning profile check pass.

Build 30 was not installed at that time. Later, the user installed and ran build 30 on the iPhone.
The IPA, JSON, and SHA-256 of build 29 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build30/`.
IPA: `IPA/rhythm-heaven/RhythmHeaven-build30-ambient-natural-20261019.ipa`.
For details, see [the border glow record](../../../.touchable/work/rhythm-heaven/reports/ambient-natural-20261019/README.md).

## BeatSpell performance and touch (build 29)

Build 29 fixes BeatSpell performance and touch.

- The new file `runtime/rh_batch.c` skips the GPU wait of the sprite batcher in the same batch. Present keeps its wait.
- The upstream Metal layer adds `nvn_hook` and `mtl_copy_on_submit`. Uniform buffers B1 and B2 are copied at each submit.
- Inline uniform updates outside the range and smaller than 64 KiB are copied at encode time. This change applies to all games.
- The shared touch hint adds `extra`. In BeatSpell, the left half of the screen is B, and it shows the small D-pad, X, and Y.
- The rhythm layout in `App.m` adds an X button.

Check results:

- Mac host (copy of the iPhone save, seconds 20–50 of the first BeatSpell battle): busy time per frame went down from 14.45–14.83 ms to 8.95–10.13 ms. Render passes went down from 21 to 13. In 1800 frames of the final code, the longest interval is 20 ms.
- Mac host log: `Touch left area: layout 1 buttons 1 0 extra 0x7`.
- `test_touch.c`, `test_touch_layout.c`, the 47 Python tests, and `games.py check` pass.
- Upstream `t_ub` (47 tests) and `t_nvn` (75 tests) pass. `t_refl` and `t_rt_range_ro` did not complete because the local reflection sample files are missing.
- The iOS Simulator (iPhone 17 Pro Max, iPad mini) checked the BeatSpell layout, X and ◀ presses (`0x1004`), normal levels, and menus.
- The Silksong Mac host regression (iPhone aspect ratio) and the Simulator regression pass.
- Xcode device build 29 passes. The warning count is the same as build 28. IPA packaging and the provisioning profile check pass.

At 16:03, the build was not installed. At that time, the iPhone was not available, and Rhythm Heaven build 27 was still running on the iPad mini.
At 18:55, at the user's request, build 29 was installed on the iPhone through the launcher. Before the install, neither game was running.
The pre-install backup is in `.touchable/saves/rhythm-heaven/phone-before-install-20261018-185546-17258b/`.
After the install, the 3 save files match the backup. The game was not started after the install. Silksong is still build 18. The iPad mini is still build 27.
The IPA, JSON, and SHA-256 of build 28 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build29/`.
IPA: `IPA/rhythm-heaven/RhythmHeaven-build29-beatspell-20261018.ipa`.
For details, see [the BeatSpell record](../../../.touchable/work/rhythm-heaven/reports/beatspell-20261018/README.md).

## Liquid Glass buttons (build 28)

In build 28, B, −, +, tutorial Y, and the small D-pad use the clear style of Liquid Glass.
The buttons are in one glass container. Each button that shows glass has a 35% black circle below it.
Unused directions change to thin rings without glass. They no longer use 25% opacity.
The touch ranges, button masks, and display conditions do not change.
The change is in the shared `internal/platform/ios/App.m`.

Check results:

- 47 Python tests pass.
- `games.py check` passes. Rhythm Heaven stays at `in-progress`.
- `test_touch_layout.c` passes.
- The iOS device SDK syntax check of `App.m` passes with no warnings.
- The iOS Simulator (iPhone 16 Pro Max, iPad mini, iOS 27.0) checked levels, presses, menus, and pure black letterbox borders.
- The Silksong Simulator regression test matches the build 17 log.
- Xcode device build 28 passes.
- IPA packaging and the provisioning profile check pass.

The launcher refused to build Rhythm Heaven because three upstream tool hashes in the rewrite record are stale.
Build 28 used the manual Xcode path.
At the 13:55 check, Rhythm Heaven was running on both devices. So the build was not installed at that time.
At 14:02, at the user's request, build 28 was installed on the iPhone through the launcher.
The pre-install backup is in `.touchable/saves/rhythm-heaven/phone-before-install-20261018-140232-4808f7/`.
After the install, the 3 save files match the backup. The game was not started after the install. The iPad mini is still build 27.
IPA: `IPA/rhythm-heaven/RhythmHeaven-build28-liquid-glass-20261018.ipa`.
For details, see [the Liquid Glass record](../../../.touchable/work/rhythm-heaven/reports/liquid-glass-20261018/README.md).

## Border glow (build 27)

Build 27 adds a real-time border glow.
The presentation shader samples the edges of the game image in each frame.
The soft light is drawn only in the top and bottom, or left and right, letterbox borders.
Full-screen 16:9 does not draw it.
The game image itself does not change.

`DM_NVN_AMBIENT_BARS` in `profile.json` controls the strength.
The current value is `0.8`.
The shader peak brightness cap is 0.48.
Sampling uses 3 depths × 17 horizontal points.

The Mac host check uses a 1280×800 canvas.
Screenshots without the glow and with the final glow are saved.
Build 27 is installed on the iPad mini.
The final device check was in a Stage Manager window. It confirmed the left and right borders.
Earlier full-screen iPad screenshots confirmed the top and bottom borders and the Metal 4 path.
The iPhone was locked. Build 27 is not installed on it.

Check results:

- 50 Python tests pass.
- `games.py check` passes. Rhythm Heaven stays at `in-progress`.
- The Mac host build passes.
- The Silksong host build and a 20-second bounded start pass.
- Xcode device build 27 passes.
- Build 27 is installed on the iPad mini.
- IPA packaging and the CRC check pass.
- The device median frame time stays at 16.65–16.70 ms.

IPA:

- `IPA/rhythm-heaven/RhythmHeaven-build27-ambient-bars-20261018.ipa`
- SHA-256 `6dff216774210ec54a9e701c8e170169e8adb165ae1718ab8678c4200b8bb6a9`
- The IPA, JSON, and SHA-256 of build 26 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build27/`.

Saves:

- Pre-install backup: `.touchable/saves/rhythm-heaven/ipad-before-build27-20261018/`
- Post-test copy: `.touchable/saves/rhythm-heaven/post-build27-20261018/`
- `pso_user.tsv` is the same before and after.
- The main save had normal writes during the test.

For details, see [the border glow record](../../../.touchable/work/rhythm-heaven/reports/ambient-bars-20261018/README.md).

## Controller haptics (builds 20–26)

Build 20 adds a per-game controller haptics backend.
The setting is `ios.controller_haptics`.
Rhythm Heaven merges the left and right Switch rumble values into one intensity and sharpness signal.
The same signal goes to the phone haptics backend and to controllers that support haptics.
The app monitors controller connect, disconnect, and current-controller changes.

Builds 20 and 21 use the advanced Core Haptics player.
The controller engine on the iPad mini refuses this player.
The log error is `NSCocoaErrorDomain` 4097.
After the advanced player fails, a player object stays in the controller engine.
The next standard player gets `self.player != nil`. Thus this path does not work.

Build 22 changes to create only the standard player.
The standard player can start. It can also receive intensity and sharpness parameters.
But the original implementation muted the player first and then unmuted it.
The log shows that the commands succeeded, but the user did not feel physical haptics. Thus command success is not the same as hardware output success.

Build 24 runs an isolation test.
The test bypasses the game rumble values.
The test directly creates the GameController controller engine.
The test plays an unmuted 0.5-second continuous event at maximum intensity.
The user confirmed physical haptics on the DualSense.

Build 25 removes the muted-start step of the player.
The player is created and started directly with the current game intensity and sharpness.
The user confirmed DualSense haptics in menus and in levels.
Build 26 removes the temporary direct test entry.
Build 26 passes the device regression again. The user confirmed correct haptics in menus and levels.

Device environment:

- iPad mini (iPad16,2, A17 Pro)
- iPadOS 27.0.1 (24A446)
- UDID `<device UDID>`
- DualSense Wireless Controller
- Bundle ID `com.mmmz.TouchableRhythmHeaven`
- App build 26

Device log records:

- The controller supports the `Default`, `All`, `Handles`, `Left Handle`, and `Right Handle` localities.
- The build 26 menu haptics intensity is 1.0000, and the sharpness is 0.5401.
- The game and the player both accept non-zero rumble updates.
- The user confirmed physical haptics in menus and levels.

Check results:

- 50 Python tests pass.
- The shared haptics C test passes.
- The Objective-C `-Wall -Wextra -Werror` syntax check passes.
- `games.py check` passes. Rhythm Heaven stays at `in-progress`.
- Xcode device build 26 passes.
- Build 26 is installed on the iPad mini.
- The IPA CRC and SHA-256 checks pass.

Save checks:

- The full pre-install backup is in `.touchable/saves/rhythm-heaven/ipad-before-build21-20261017/`.
- The final save copy is in `.touchable/saves/rhythm-heaven/post-build26-20261018/`.
- The SHA-256 of the final `RhythmHeaven/game_save.bin` and `RhythmHeaven.work/game_save.bin` is `4445745f0f7d92db603353ce71a2d626f907d4b2433c7f60dfcce48273e47688` for both.
- The main save changed after the install and game tests. The cause is a normal save, not file loss.
- The settings file `pso_user.tsv` is the same as before the install. Its SHA-256 is `35adf69e08f53df2b240e88b84f59a2b351eab5f8098b512f87f7acbbb7e54fb`.

The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build26-controller-haptics-20261018.ipa`.
Its SHA-256 is `7d9ac84b92df64be8d5141e7bd920f1dd23919bfcea729c9ed69de76bb01979c`.
The IPA, JSON, and SHA-256 of builds 19 and 20 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build26/`.

The iPhone-with-controller combination is not tested.
Other controller models are not tested.
Controller disconnect and reconnect are not tested item by item.
A single continuous rumble of more than one hour is not tested.
For evidence, see [the controller haptics record](../../../.touchable/work/rhythm-heaven/reports/controller-haptics-20261018/README.md).

## Language setting (builds 18 and 19)

Build 18 adds a per-app iOS language setting.
The setting path is "Settings > Apps > Rhythm Heaven > Language".
The app declares 13 game languages.
At app start, the app reads `NSLocale.preferredLanguages.firstObject`.
The runtime converts the language tag to a Switch game language code.
Unsupported tags fall back to American English.

On 2026-10-17, it was installed on the iPad mini (iPad16,2, iPadOS 27.0.1).
The Settings page shows a separate `Language` row.
These six languages showed correctly on the device:

- English
- Japanese
- Simplified Chinese
- Traditional Chinese
- German
- Spanish (Mexico)

Build 18 check results:

- 49 Python tests pass.
- `games.py check` passes.
- `git diff --check` passes.
- The device build, installed version, and signing checks pass.
- The IPA CRC and SHA-256 checks pass.
- The Silksong and Rhythm Heaven Mac hosts each ran for 30 seconds. Both passed 1500 frames. No start errors were found.

The build 18 IPA is archived.
The six-language screenshots of build 18 are kept as evidence of the language implementation.
For detailed evidence, see [the language setting record](../../../.touchable/work/rhythm-heaven/reports/language-20261017/README.md).

Screenshots confirm the setting entry.
The language selection row was not tapped automatically.
The device language check uses the `-AppleLanguages` launch argument.
The 13 languages do not each have a screenshot.

Build 19 fixes the language handling in the shared `App.m`.
`DM_LANG` is set only when the app declares `CFBundleLocalizations`.
Silksong does not have this declaration. It continues to use its existing Simplified Chinese default.
Build 19 was re-checked on the iPad mini for English, Japanese, Simplified Chinese, and the fallback for an unsupported language.
Build 19 uses Xcode automatic signing.
The current IPA is `IPA/rhythm-heaven/RhythmHeaven-build19-language-20261017.ipa`.
Its SHA-256 is `a59a4bc313351b329e2c580e2ac9532dc5373afa123fe14fa6df86c523feea76`.
The IPA, JSON, and SHA-256 of build 18 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build19/`.
Build 19 was not installed or re-tested on the iPhone.
Full levels, multiplayer mode, and all extra modes are not complete.

## iPad adaptation (build 17)

Build 17 adds an iPad layout.
The game image stays at 16:9 and is centered.
In iPad landscape, the letterbox borders are at the top and bottom.
B, Y, −, and + move to the top border.
The A and menu touch areas are limited to the 16:9 game image. The D-pad is at the bottom-left corner of the image.
The iPhone layout path keeps its original logic.
iPad supports only landscape full screen.
`UIRequiresFullScreen` is deprecated from iOS 26. The current build still uses this key.

On 2026-10-17, it was installed on the iPad mini (iPad16,2, A17 Pro, iPadOS 27.0.1, UDID `<device UDID>`).
The app started and continued to output frames.
The user confirmed that menus, entering minigames, and A touch work correctly.
The start log records the A area as `{{570.5, 60}, {558.5, 624.65625}}` points.
This area is in the game image.
B, Y, −, and + are in the top border.
The log records save commit #1.

The final log contains 30 groups of 300-frame statistics.
Frame time p50 is 16.65–16.70 ms.
After frame 2100, each group has 0 dropped frames.
The start phase still has isolated dropped frames and long frames. The maximum single frame is 113.66 ms.
The start log has one `c1_full.tsv` fallback message.
This message was already in the build 15 iPhone log and in the Silksong log.
The game continues to run.

Check results:

- 48 Python tests pass.
- `games.py check` passes.
- `git diff --check` passes.
- The device build, signing, and IPA CRC/SHA-256 checks pass.
- The iPad Simulator build fails. The Metal 4 headers of the Simulator SDK do not have `MTL4ComputeCommandEncoder` and `MTL4ArgumentTable`. The device build is not affected.

The IPA is `IPA/rhythm-heaven/RhythmHeaven-build17-ipad-20261017.ipa`.
Its SHA-256 is `8edf28bf2679974edf03bcdae251ffe8de9f7182c9932b4bcc2d59c865dd6752`.
The signing provisioning profile expires on 2026-10-25T02:46:41Z.
The IPA, JSON, and SHA-256 of build 16 are moved to `.touchable/archive/ipa/rhythm-heaven/before-build17/`.
For detailed evidence, see [the iPad mini record](../../../.touchable/work/rhythm-heaven/reports/ipad-mini-20261017/README.md).

Build 17 was not installed or re-tested on the iPhone.
Other iPad sizes are not tested.
Full levels, multiplayer mode, and all extra modes are not complete.

## Draw resolution (build 5)

The game has two draw sizes: handheld 1280×720 and docked 1920×1080. When the performance mode is Boost, the game uses the docked size (`main+0xab50`, `main+0xab90`). `rh_game.c` first checks the size table at `main+0xada6d0`. Then it sets the shared variable `ss_performance_mode` to 1, and the host `GetPerformanceMode` returns Boost. The operation mode stays handheld, and the controller style does not change. If the size table does not match, the game keeps the handheld size and writes one `Draw size` line to the log. The environment variable `RH_HANDHELD_SIZE=1` also keeps 1280×720. Use it for comparisons.

The swapchain was already 1920×1080. So the scene now copies 1:1 to the swapchain, and the phone then scales it up by 1.22. On the Mac, frame time does not change (p50 16.66 ms). GPU time goes from approximately 0.8 → 1.4 ms. GPU memory increases by approximately 50 MB. A larger size requires changes to the window size in the game code. This was not done. On the iPhone (build 5), the title, menus, jump rope, and similar scenes stay at 60 fps. The wiper scene GPU time is approximately 7–8 ms, with 18–63 dropped frames per 300 frames. This was measured before the texture fix. Build 6 was not re-tested yet. The records and comparison images are in `.touchable/work/rhythm-heaven/reports/hires-20261015/`.

## Fixed: some textures scrambled (build 6)

User report: in docked mode (1080p), when elements scale up, scale down, or move, the textures are correct at only one size. At other sizes, they look randomly assembled. The two small figures in the wiper level show this most.

Cause: the game `.tex` textures (nn::gfx, no pre-layout parameters) use the NVN default block height. The automatic block height in `nvn_tex.c` rounded up the GOB count of "height × 1.5". At boundaries, this is one step too large. For example, mip level 0 of a 2560×160 BC7 texture has 40 block rows. The actual block height is 4 GOBs, but the code decoded with 8 GOBs. Levels 1 and 2 were correct by chance. So 720p (which uses level 1) was correct, and 1080p (which uses level 0) was scrambled.

Fix: the GOB count now rounds down. Offline decode of all 16 affected texture files in RomFS (23 minus duplicates): all were scrambled with the old rule, and all are correct with the new rule. Other affected items are the score digits (`nervous/number.tex`, `common/letter_00.tex`), the 4-3-2-1 countdown, and the drum, frog, and germ scenes. Most of them have no mips, so they were also wrong at 720p. The issue "score digits in level 2 sometimes show as black blocks" is probably this problem. It is not re-checked in the game yet.

Checks: `t_nvn` adds 4 block-height cases (all 75 tests pass; 3 fail with the old rule). `t_deswz_gpu` and `t_mip_extent` pass. On the Mac, the wiper level at 1080p is correct from start to end. Silksong re-check: screenshots are the same as before the change (only animated parts differ). The log has no new errors. GPU memory does not change. The record is in `.touchable/work/rhythm-heaven/reports/hires-20261015/texture-block-height/`.

## Fixed: texture-ID view cache holds destroyed textures (build 5)

After the game destroys a texture (`nvnTextureFinalize`), the texture-ID table `g_treg[].view` in `nvn_mtl.m` still holds a strong reference to the view or to the base texture. A texture ID that the game no longer registers or samples keeps holding it. On the Mac, after one round of Remix 1, there were 400–520 MB of such textures. Fix: `mtl_tex_release` releases the cached view of the same index. After the fix, this is 0. Silksong also went down from 38 MB to 0.

## Fixed: iPhone crash when entering a level or after finishing a level (build 2)

On the device, build 1 GPU memory increased by approximately 1 GB for each level entered. At approximately 3.4 GB, the system killed it. The log had no errors. The cause is in the shared graphics layer `nvn_mtl.m`:

- The game streams writes to large textures (for example, a 5344×2640 BC7 background). Each time the host detects a write, it uploads again. Old textures and 32 MB upload staging blocks are released with a delay after they are retired.
- Metal autoreleases objects in calls such as removal from the residency set. `mtl_sub_begin` did the delayed release first and opened the submit autorelease pool after it. So the objects went into the outer pool of the game thread. This pool never drains during the session, so the objects are never destroyed.
- The delayed release sometimes ran on the GPU callback thread (no leak) and sometimes on the submit thread (leak). So the leak was intermittent. On the Mac, memory grew up to 27 GB.

Fix: the submit opens the pool first. The delayed release, residency set add and remove, memory pool registration, texture readback, and CPU writes each have their own autorelease pool. After the fix, on the Mac, GPU memory is stable during 4 minutes of play in the same level. Retired but not destroyed buffers are 0. Silksong was also re-checked.

Diagnostic tool: `DM_RES_CENSUS=2` lists, every 300 frames, the 6 largest object groups by kind, size, and format. It also gives sample addresses. Give these addresses to `leaks --traceTree=<address> <pid>` to find what still holds them.

## Install records of builds 1–13

The early phone install record is build 6 (1080p, texture leak fix, block height fix). The build 5 IPA is at `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build5.ipa` (SHA-256 `afc1b760…f4c`). It has only 1080p and the leak fix. The build 4 IPA is at `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build4.ipa` (SHA-256 `10be50e3…91c`). Build 4 contains the memory fix, the new touch layout, and the frame-pacing fix from the Silksong session. The build 2 IPA contains only the memory fix. The build 1 IPA (`RhythmHeaven-build1.ipa`) has a memory leak. Do not use it again. `internal/tools/package_ios.py --game rhythm-heaven` generates the IPA.

The final build 7 IPA is at `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build7-touch.ipa`. On 2026-10-16, it was installed on an iPhone 16 Pro Max (iOS 27.2). The pre-install save backup is in `.touchable/work/rhythm-heaven/saves/phone-backup-20261016-before-build7/`. After the install, the checksums of the 3 save files match the backup. The signing and checksum information is in the `.json` and `.sha256` files in the IPA directory. For device evidence, see [the install and input record](../../../.touchable/work/rhythm-heaven/reports/menu-touch-implementation-20261016/iphone-build7/README.md). The device log of the later P-mode test confirmed level entry on a second tap and taps on mode options. At that time, the Y hint still needed device confirmation. For the build 13 re-check, see the later records.

The build 8 IPA is at `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build8-auto-a.ipa`.
On 2026-10-16, it was installed on the same phone.
The pre-install backup is in `.touchable/work/rhythm-heaven/saves/phone-backup-20261016-before-build8/`.
After the install, the SHA-256 of the 3 files matches the backup.
All 80 P-mode states are kept.

The build 9 IPA is at `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build9-level-controls.ipa`.
On 2026-10-16, it was installed on the same phone.
The pre-install backup is in `.touchable/work/rhythm-heaven/saves/phone-backup-20261016-before-build9/`.
After the install, the SHA-256 of the 3 files matches the backup.
The start log confirms that the button appearance starts in the hidden state.
The frog level log confirms that the buttons show in the level, hide on pause, and show again after resume.
For the detailed device scope, see [the hidden-by-default record](../../../.touchable/work/rhythm-heaven/reports/level-controls-20261016/README.md).

The build 10 IPA is at `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build10-compact-dpad.ipa`.
On 2026-10-16, it was installed on the same phone.
The pre-install backup is in `.touchable/work/rhythm-heaven/saves/phone-backup-20261016-before-build10/`.
After the install, the SHA-256 of the 3 files matches the backup.
All 80 P-mode states are kept.
The dancer level log confirms `SS_TOUCH_DPAD` and the in-level display state.
For the detailed scope, see [the small D-pad record](../../../.touchable/work/rhythm-heaven/reports/multidirection-dpad-20261016/README.md).

The build 11 IPA is at `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build11-dim-directions.ipa`.
On 2026-10-16, it was installed on the same phone.
The pre-install backup is in `.touchable/work/rhythm-heaven/saves/phone-backup-20261016-before-build11/`.
After the install, the SHA-256 of the 3 files matches the backup.
All 80 P-mode states are kept.
The device log confirms `alpha 0.25` for the up and down keys in the dancer level.
After pause and resume, the same opacity returns.
For the detailed scope, see [the D-pad opacity record](../../../.touchable/work/rhythm-heaven/reports/unused-directions-20261016/README.md).

The build 12 IPA is at `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build12-touch-feedback.ipa`.
On 2026-10-16, it was installed on the same phone.
After the install, the 3 save files match the backup.
The user confirmed that the right-side halo and the letterbox border button effects are correct.
The user found that the tutorial Y flickers.
Build 13 fixes this display rule.

The build 13 IPA is at `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build13-steady-touch.ipa`.
On 2026-10-16, it was installed on the same phone.
The final pre-install backup is in `.touchable/work/rhythm-heaven/saves/phone-backup-20261016-before-build13-final/`.
After the install, 8 bytes of the save differ from the backup.
One raw score value increased from 762 to 956.
The P-mode state bytes of all 80 levels match the backup.
The committed save matches the work copy.
This task did not change the phone save.
The device log records touches on ▶ at the left and A at the right.
When the tutorial hint target rotates, Y stays visible.
After the user presses − to skip the tutorial, Y hides.
For the detailed scope, see [the build 13 record](../../../.touchable/work/rhythm-heaven/reports/steady-touch-20261016/README.md).

## Extra menu touch (build 15)

The Rhythm Archive, the Music Room, and the Demo Videos use a category list and a content list.
The two lists keep different positions on the same screen.
The old touch code always read the `menu` position first.
When the category list shows, this position can be off screen.
Build 15 selects the `category` or `menu` position from the native list binding.
It also excludes hidden duplicate rows, so that they do not take taps from visible items.

Both lists support drag and tap.
Release after a drag does not run the item.
A tap on a category opens that category.
A tap on content runs the original game operation, for example opening a comic, playing a track, or starting a demo video.
The original operations of the start menu and the map do not change.
While music plays, B first stops playback. Press B again to go back one level.
This follows the original game back flow.

The Mac checks covered archive category and content scrolling, opening a comic after scrolling, going back with the selected item kept, music scrolling and track playback, repeated taps, and the demo video categories and start.
The scroll boundary check of the new category list passes.
The iOS build and signing checks pass.
Build 15 is installed on the iPhone.
After the install, the 3 save files match the backup.
The user confirmed correct device touch in the three menus.
After the re-check, the committed save and the work save both keep all 80 P modes.
The development test input is off, and the app was restarted in normal mode.
This change is in the game adapter layer. It does not change shared platform code.
For detailed evidence, see [the extra menu touch record](../../../.touchable/work/rhythm-heaven/reports/extra-menu-touch-20261016-144228/README.md).

The IPA is `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build15-extra-menu-touch.ipa`.
The pre-install save backup is `.touchable/work/rhythm-heaven/saves/phone-backup-20261016-145434-before-build15/`.

## Performance fix (build 14)

The user reported that audio and video slowed down in the frisbee section of Remix 8.
The build 13 per-frame device record and CPU sampling confirmed many repeated texture uploads.
The problem is in the shared `.touchable/dependencies/d2r-mobile/dm/gfx/nvn/nvn_dirty.c`.
Switch memory pools are aligned to 4 KiB. The phone host page is 16 KiB.
Several pools can share one host page.
The original lookup checked only the start address. It could find a small pool with too small a range first.
Thus the program treated large textures as untracked content and uploaded them again at each use.
Pool removal could also select an adjacent pool.

After the fix, the lookup uses the full range.
Removal looks up the original pool address.
This fix is in the shared NVN layer. It does not depend on level addresses.
Real writes still cause texture updates.
The draw size and render options keep their original values.

Samples with the same 2 render passes, 17 draws, and 15.820 MB of attachment loads:

| Per-frame median | Build 13 | Build 14 |
| --- | --- | --- |
| Frame time | 66.92 ms | 16.68 ms |
| Submit thread CPU time | 41.71 ms | 0.85 ms |
| Repeated texture uploads | 15 | 0 |

The two runs had different operations and animation times.
This table matches samples by draw load. It is not a frame-by-frame replay comparison.
In the approximately 103-second automatic record of build 14, frame time p95 is 17.71 ms, and the maximum is 44.08 ms.
A small number of transition and load spikes remain.
Then a 44,377-frame manual record of approximately 12 minutes 21 seconds was collected.
The log confirms entry into Remix 8, two other remixes, and the wiper level.
The user confirmed correct audio and video.
The full record includes menus and play. Frame time p50 is 16.67 ms, and p99 is 19.03 ms.
The 45 frames longer than 33.34 ms are all isolated. They do not occur in sequence.
The maximum single frame is 199.88 ms. Of this, the first pipeline and shader library creation took 186.4 ms.
The continuous slowdown from many repeated uploads per frame did not occur again.
Isolated spikes, such as the first load, can still be optimized later.
This re-check does not prove that all levels are free of stutter.
Both iOS apps are built.
The shared page regression test, the concurrency test, and the 75 graphics checks all pass.
Time-limited regressions of Remix 8 and Silksong were also completed on the Mac.
For full evidence and limits, see [the performance investigation record](../../../.touchable/work/rhythm-heaven/reports/performance-20261016-134827/README.md).

The build 14 IPA is `.touchable/build/games/rhythm-heaven/ipa/RhythmHeaven-build14-pool-tracking.ipa`.
The pre-install save backup is `.touchable/work/rhythm-heaven/saves/phone-backup-20261016-before-build14/`.
After the install, the SHA-256 of the 3 files matches the backup.
After the automatic re-test and the manual re-check, the committed save and the work save both keep all 80 P modes.
This task did not write saves to the phone.
