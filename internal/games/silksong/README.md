**Silksong Switch → iPhone / iPad port workspace**






## 2026-10-26: Enemy health bars (build 36, installed on the iPhone, no device test yet)

User request: "build the health bars first" (GitHub issue #10).

- `runtime/ss_enemy_bars.c` hooks `HealthManager.Update` and publishes a bar for each hit enemy (remaining hp / initHp) at the top of its collider. `App.m` draws the bars above the game picture.
- Settings app → Silksong → Gameplay → Enemy health bars (off by default). Takes effect at the next start.
- Mac host: the projection matches Hornet's position in screenshots; no fight was tested. Build 36: `IPA/silksong/Silksong-build36-20261026-012358-d465d9.ipa`.
- See [the health bar record](../../../.touchable/work/silksong/reports/health-bars-20261026/README.md).

## 2026-10-25: Render resolution and refresh rate on the app page in the Settings app (build 35)

User request: "Add a option in settings to let player choose render resolution and refresh rate."

- `game.json` `ios.player_settings` lists the choices: render height 720, 900, 1080 or 1440 lines, and 60 or 120 Hz. "Default" keeps the profile values (1080 lines, 120 Hz).
- `internal/tools/prepare_ios.py` writes `Settings.bundle` (English and Simplified Chinese) and `TouchablePlayerSettings` in Info.plist. `App.m` reads the choices at launch and logs a `Player settings:` line. A change takes effect at the next game start.
- The picture is at most 1920 pixels wide, so an iPhone (2.17:1) uses at most 882 lines; 1080 and 1440 give the same result there. An iPad mini uses up to 1280 lines.
- Build 35 (`IPA/silksong/Silksong-build35-20261025-171151-96fbc1.ipa`) is installed on the iPhone. iPhone test: 900p at 60 Hz dropped 0.2 % of frames (steady 60 fps); 720p at 120 Hz dropped 14.9 % (default 882 lines at 120 Hz in build 34: 27.1 %), GPU p50 6.3 ms. It contains the page; it also has the D-pad height change and the upstream graphics merge. Python tests: 59 passed.

## 2026-10-25: D-pad at the height of the face buttons (source change, no build)

User request: "Align D-PAD and ABXY, make sure they are on same height".

- `internal/platform/ios/TouchLayout.h`: the D-pad center now has the height of the face-button center (Y and A). Before, it kept the build 17 height, 26 points lower on a phone. iPhone 16 Pro Max: D-pad and A at y 305. iPad mini: 205 points above the lower edge (174 before).
- `internal/tests/common/test_touch_layout.c`: checks the equal height; PASS on 11 screen sizes.

## 2026-10-24: Upstream graphics translation thread and renderer optimizations (build 34)

User request: "Inspect upstream repo find if there are good performance optimization which can be used in Touchable project. Use them and optimize our project's performance."

- The shared Metal layer now includes the newer upstream graphics code (to upstream `195ae11`): command replay and Metal encoding run on a separate real-time thread (`nvn-translate`), unchanged render state is not sent again, memory pools become resident only on first GPU use, and fewer autorelease operations occur for each draw.
- The texture object still fits Silksong's 128 bytes: `magic` is 8 bits and the pool offset 32 bits in the Touchable build.
- Mac host: menus and gameplay render the same as before; the present thread uses 0.23 ms of CPU for each frame, and translation (0.35 ms) runs on the new thread. On the iPhone, build 32 spent 1.4 to 1.9 ms of each frame on translation on the game thread.
- Build 34 IPA: `IPA/silksong/Silksong-build34-20261024-174420-de2b5f.ipa`. Installed on the iPhone. iPhone test (different areas from the build 32 log): dropped frames 27.1 % (build 32: 32.6 %), frame time p95 10.9 ms (12.1 ms), p50 9.5 ms (8.9 ms). The game thread is no longer the limit; at 120 Hz the GPU is (GPU p50 about 9 ms against an 8.33 ms budget).
- See [the upstream graphics merge record](../../../.touchable/work/silksong/reports/upstream-gfx-merge-20261024/README.md).

## 2026-10-23: Select, then confirm on the save select (build 33)

User request: "There are still a lot of double tap to confirm logic, change all of them to select-then-confirm" (also for Silksong).

- `runtime/ss_touch_ui.c`: on the save select, a tap on the item that was already selected clicks it. A tap on another item only selects it. Before, an already selected item needed two taps. This applies to the slots, the restore points, the clear button, and Back.
- The inventory already worked this way (a tap on the selected item is A). No other screen used two taps.
- Mac host test on a copy of the 2026-10-23 phone save (`SS_TOUCH_TRACE=1`): with slot 2 selected, one tap on slot 2 loaded it (`game=4`). In a second run, a tap on slot 3 selected it, a tap on slot 2 selected it, and a second tap on slot 2 loaded it. Evidence: `.touchable/work/silksong/reports/select-confirm-20261023/`.
- Build 33 (IPA `Silksong-build33-20261023-153701-ae1fc9.ipa`, SHA-256 `c1ba7fa6dfe82349205d0cd880059fc0fe89278650f2df6033c6968ae5377a3e`) is installed on the iPhone and the iPad mini (2026-10-23). On the iPad, the first try could not read the device (locked) and the second lost the connection; the third try succeeded. Not tested on a device.

## 2026-10-23: Controls nearer the side edges, and a faster press response (build 32)

User request: "Move the virtual buttons closer to the left and right edges. Make button interaction animation faster".

- `internal/platform/ios/TouchLayout.h`: on a phone the controls start at the edge of the safe area (12 points inside it before); on a tablet 8 points from it (16 before). iPhone 16 Pro Max: D-pad 12 points left, right group 8 points right.
- `internal/platform/ios/App.m`: the glass press look ends at once when the finger lifts (0.02 s instead of 0.20 s, measured from screen recordings). Layer speed did not affect the system glass response. This applies to the glass buttons of both games.
- See [the edges and press record](../../../.touchable/work/silksong/reports/edges-fast-press-20261023/README.md).
- Build 32 (`IPA/silksong/Silksong-build32-20261023-141808-0d65ac.ipa`) is installed on the iPhone and the iPad mini. It also has the map button aligned with R. The launcher backed up the saves before each install. No game test on a device yet.

## 2026-10-23: Right-side layout from the user's sketch (source change, no build)

User request: "I want a layout looks like this" with a sketch. ZL and ZR are slanted buttons below and outside the X, Y, A, B diamond, lower than B. R is a diamond shape above and right of A. After "Make map button also diamond", the map button (L) below + is a diamond too, and it has the horizontal center of R ("Align horizontal center of Map button and R button"). A shear or rotation transform shapes the glass, and the titles stay upright. The diamond moves left 18 points on the iPhone 16 Pro Max so that ZR fits. This replaces the earlier ZL and ZR beside B and the round R. Layout test (11 sizes) and iOS Simulator tests passed. See [the sketch layout record](../../../.touchable/work/silksong/reports/sketch-layout-20261023/README.md).

## 2026-10-23: ZR slides like the face buttons (source change, no build)

User request: "Make ZR button also slideable: when pressing ZR, it can slide to other buttons".

- `internal/platform/ios/App.m`: ZR is a sliding button. ZR stays pressed until the finger lifts, and a slide onto A, B, X, or Y also presses that button. A slide from a face button onto ZR presses ZR. The slide mark has the capsule shape of ZR.
- Real-touch iOS Simulator test: ZR → B → A → ZR gives ZR, B+ZR, A+ZR, ZR, and none at the lift. See [the ZR slide record](../../../.touchable/work/silksong/reports/zr-slide-20261023/README.md).

## 2026-10-23: L is a round map button below +; ZL and ZR below ABXY; R on the right (source change, no build)

User request: "Move L button to most right side, below + button. Use a round shape. Use a map icon".

- `internal/platform/ios/TouchLayout.h`: L is a round button of the size of +, below + at the right edge. A phone up to 390 points of usable height puts it next to +. The pad toggle moved into the row of +, left of + and L.
- `internal/platform/ios/App.m`: L shows the SF Symbol `map.fill`.
- Follow-up requests "Move ZL to right side, to the lower left of Y and B", then "Slightly move AXBY panel up and Move ZL and ZR to bottom of ABXY, use capsule shape. Move R button to right side.": ZL and ZR are low capsules (34 points high on a phone, after "make ZL and ZR button height lower") beside B, in its row (after "Make ZL and ZR besides B not below"). The diamond rises 12 points and moves left so that ZR fits: about 48 points on the iPhone 16 Pro Max, 53 points on the iPad mini. R is on the right, above A, and round after the follow-up "Use round shape for R". The D-pad does not move.
- The layout test (11 sizes) and an iOS Simulator test passed. See [the L button record](../../../.touchable/work/silksong/reports/map-l-button-20261023/README.md).

## 2026-10-23: App name follows the language

The home screen name now follows the language of the device. The names are the official titles from the game's `control.nacp` (`internal/tools/game_titles.py`), stored in `ios.display_names` in `game.json`.
- Silksong: "Hollow Knight: Silksong" for all languages except Simplified Chinese ("空洞骑士 丝之歌"). The game has no Traditional Chinese title.
The default name is English. The Files app folder shows the same localized name; iOS has no separate setting for it. The product name, the save folders, and the IPA names stay English.
Checked in the iOS Simulator (Simplified Chinese) with a test app that uses the generated names: the home screen and the Files folder show the Chinese name. The change applies with the next app build.

## 2026-10-21: Shader libraries for iOS 26 (source and shader change, no build)

On iOS 26 the game screen stayed black, with working audio and touch controls. The shader libraries targeted iOS 27, the version of the Xcode 27 SDK, and iOS 26 refused to load them. `build_shaders.py` now compiles for the manifest deployment targets (iOS 26.0, macOS 26.0) and checks each library header. `prepare_ios.py` refuses newer libraries. Runtime MSL compiles with Metal 4.0. A rejected library falls back to its `.metal` source. The iOS and host builds treat an unguarded new API as an error. The new chain is `a9bf9ac9ed21`. Mac host test, graphics tests, and an unsigned iOS build passed. No iOS 26 device was available, so the fix is not confirmed on iOS 26. Evidence: `.touchable/work/silksong/reports/ios26-metal-target-20261021/`.

## 2026-10-20: Build 28 installed on the iPhone and the iPad mini (wider view removed)

User request: "undo wider view and install". Build 28 is build 27 without the camera zoom; the map changes stay. IPA: `IPA/silksong/Silksong-build28-20261020-220739-0b1591.ipa`. Silksong was closed on both devices.

- iPhone: backup `.touchable/saves/silksong/phone-before-install-20261020-221047-dea46c/`. The iPhone reports build 28. All 14 save files match the backup.
- iPad mini: the first install failed because the Mac disk was full. After the user approved the deletion of 19 old Silksong Xcode run folders (126.8 GB; see `.touchable/work/silksong/reports/disk-cleanup-20261020/`), the install passed. Backup `.touchable/saves/silksong/phone-before-install-20261020-222025-af9d8a/`. The iPad reports build 28. All 6 save files match the backup.
- The game was not started after the installs.

## 2026-10-20: Camera zoom for the touch layout (removed; only build 27 had it)

Removed later on 2026-10-20 at the user's request ("undo wider view and install"): the source is back to its state before the zoom, and build 28 has no zoom. The zoom source is kept as `zoom-source.patch` in the camera zoom record.

User request: keep the controller layout and scale the game view, so that the distance from the D-pad center to the face-button center shows the horizontal range of the original 16:9 picture. The vertical range scales by the same factor.

- `ss_display_set_control_span()` (shared `ss_display.c`) stores the zoom r = (16/9) × view height / span. `App.m` passes the span of the Switch layout. The Mac host reads `SS_VIEW_SPAN`. iPhone 16 Pro Max: span 636 points, r 1.2299. iPad mini: span 894.6 points, r 1.4785.
- `runtime/ss_display.c` hooks `ForceCameraAspect.Update` (field of view so that tan(F/2) = r·tan(F0/2), 24° → 29.30° on the iPhone) and `CameraController.LateUpdate` (the camera stays inside the scene with the zoomed half view; a scene smaller than the view is centered). Only main `fc9ea4cc`, and only when r ≠ 1. 16:9 and Rhythm Heaven do not change.
- The world texture grows to min(720, 576·r) reference lines: iPhone 1884×868, iPad 1644×1080.
- Tests: `test_display.c`, App.m syntax check, and Mac host runs at the iPhone and iPad aspects: measured scale, room edges and a room transition, HUD, inventory, pause, and frame time (p50 8.33 ms at 120 Hz). No device test.
- Limits: a room smaller than the zoomed view shows dark area outside the room (iPad: about 2.3 units above and below a 20-unit corridor). Lock areas and screen-edge effects keep their 16:9 values. The phone GPU cost of the larger world texture is not measured.

For details, see [the camera zoom record](../../../.touchable/work/silksong/reports/camera-zoom-20261020/README.md).

## 2026-10-20: Build 27 installed on the iPhone and the iPad mini

Build 27 adds the camera zoom (the strip between the D-pad and ABXY centers shows the 16:9 width; see the camera zoom entry) and the map changes (drag on the marker page; Y and A at the left edge, X and B at the right edge). IPA: `IPA/silksong/Silksong-build27-20261020-214239-623bef.ipa`. Silksong was running on both devices when the build finished. A watcher installed each device only after the game was closed there. Backups: iPad `.touchable/saves/silksong/phone-before-install-20261020-214639-eaf93a/`, iPhone `.touchable/saves/silksong/phone-before-install-20261020-215155-eabc69/`. Both devices report build 27. The save files match the backups (iPad 6 files, iPhone 14 files). The game was not started after the installs. The phone GPU cost of the zoom is not yet measured.

## 2026-10-20: Build 26 installed on the iPhone

Build 26 adds the map view buttons below (A at the left edge, X and B at the right edge). IPA: `IPA/silksong/Silksong-build26-20261020-203824-cc3d43.ipa`. Silksong was not running before the install. The launcher backed up the saves to `.touchable/saves/silksong/phone-before-install-20261020-204127-e230b6/`. The iPhone reports build 26, and all 14 save files match that backup. The game was not started after the install. The iPad mini is not updated.

## 2026-10-20: Map view shows only A, X, and B (source change, no build)

User request: "in map view, only show A button at bottom left, X and B at bottom right".

- `internal/platform/ios/App.m`: the zoomed map hides the other controls. Y above A at the left edge, X above B at the right edge, 76 points apart on a phone.
- `ss_touch_ui.c`: a drag also moves the map on the marker placement page (after A). The cursor stays in the center. The pad toggle shows the full pad. A drag moves the map.
- iOS Simulator test passed. See [the map buttons record](../../../.touchable/work/silksong/reports/map-buttons-20261020/README.md).

## 2026-10-20: Build 25 installed on the iPhone

Build 25 contains the next entry: crest carousel, side arrows, map drag, cutscene controls, room transitions, and capsule shoulders. IPA: `IPA/silksong/Silksong-build25-20261020-201614-40a7ca.ipa`. Silksong was running on the iPhone when the build started, so the install waited until it was closed. The launcher backed up the saves to `.touchable/saves/silksong/phone-before-install-20261020-201923-a28287/`. The iPhone reports build 25, and all 14 save files match that backup. The game was not started after the install. The iPad mini is not updated.

## 2026-10-20: Crest carousel, side arrows, map drag, cutscene controls, room transitions, capsule shoulders (source change, no build)

- Change Crest takes taps: a side crest or a scroll arrow moves the carousel one step, and the center crest selects it.
- The large ◇ arrows at the sides of the inventory change the pane (L and R).
- A drag moves the zoomed map through the game's `GameMap.UpdateMapPosition`. The pad stays on the map.
- A cutscene hides the pad. It shows Skip (A, only when the game allows a skip) in the place of +, and the pad toggle. The new pointer mode is `SS_POINTER_CUTSCENE`.
- Room transitions keep the pad. The title-to-game load and quit to title still hide it.
- ZL, L, R, and ZR are capsules (74 × 46 points on a phone, 88 × 55 on a tablet). A very short iPad window keeps circles. On the iPhone 16 Pro the pad toggle now sits next to +.
- Tests: Mac host at 16:9, iOS Simulator harness, layout and pointer C tests, Python tests, and a Rhythm Heaven host build. The iPhone and iPad aspect host runs were stopped by the user.

For details, see [the record](../../../.touchable/work/silksong/reports/cutscene-capsule-20261020/README.md).

## 2026-10-20: Build 24 installed on the iPhone

Build 24 contains the entries below down to the save-select and drag-scroll changes: the slide that keeps the first button pressed, the D-pad press light, the two-tap save select, and inventory drag scrolling. IPA: `IPA/silksong/Silksong-build24-20261020-193450-ed25b2.ipa`. The launcher backed up the iPhone saves to `.touchable/saves/silksong/phone-before-install-20261020-193734-fb615a/`. The iPhone reports build 24. After the install, all 14 save files match that backup. The game was not started after the install. The iPad mini is not updated.

## 2026-10-20: Face-button slide keeps the first button pressed (source change, no build)

User request: "when sliding, the first pressed key should be always pressed unless finger leave screen."

- `internal/platform/ios/App.m` (`PadButton`): the button where the touch starts stays pressed until the finger lifts. A slide onto another face button also presses that button, and a slide onward releases only that second button. A slide back leaves only the first button.
- The first slide no longer releases the first button for a moment while UIKit's touch tracking ends.
- Real-touch iOS Simulator test: B → Y → X → B gives B, B+Y, B+X, B, and none at the lift.

For details, see [the slide hold record](../../../.touchable/work/silksong/reports/slide-hold-first-20261020/README.md).

## 2026-10-20: D-pad press light fitted to the glass arm (source change, no build)

User request: "the press effect of D-pad is not exactly match. check and fix it".

- Fault: the lit arm was a separate shape with a white glow outside the glass edge, and a hard line at the center.
- `internal/platform/ios/App.m` (`PadDpadMarks`): the light is clipped to the glass bar, so it has the arm outline. The outer glow is removed. The light fades in from the other bar. The center stays unlit, and a diagonal lights two clean arms. The fill (white 0.2) and the face-button press effect do not change.
- Real-touch iOS Simulator test (iPhone 17 Pro Max): right, up-right, and up give the correct inputs, and no light falls outside the arm.

For details, see [the D-pad press record](../../../.touchable/work/silksong/reports/dpad-press-match-20261020/README.md).

## 2026-10-20: Two taps on the save select, and drag scrolling in the inventory (source change, no build)

User requests: "change save choosing to double confirm" and "in inventory/tasks or similar page, touch can't drag to scroll, Fix it".

- `runtime/ss_touch_ui.c`: on the save select (menu state 6), the first tap on an item only selects it. A second tap on the same item clicks it. This applies to the slots, the restore points, the clear button, and Back.
- A vertical drag in an inventory pane moves the pane list with the finger. The adapter sets the position of the game's `ScrollView`; the game clamps it and updates the scroll arrows. The quest list and the item grids (equipment, tools, journal) have a list. The map has none.
- An inventory tap now acts on the release, and only when the touch does not drag.
- Mac host tests at 16:9 and at the iPhone and iPad aspects: the save select needs two taps, and the journal list scrolls, takes a tap, and stops at the top.
- Not tested: a drag that starts on the selected item (the app holds A there), the completed-quest view, shops, and a device.

For details, see [the two-tap and scroll record](../../../.touchable/work/silksong/reports/touch-confirm-scroll-20261020/README.md).

## 2026-10-20: Slide between A, B, X, and Y (source change, no build)

User request: "Make ABXY buttons able to slide. example: when I am pressing B while I want to click Y for attack. I can slide my finger to Y without leaving screen, then Y activated."

- `App.m` (`PadButton`): a finger on A, B, X, or Y can slide onto another face button. The press moves when the finger enters the touch area of the new button. The finger never holds two buttons. (Changed later on 2026-10-20: the first button now stays pressed. See the entry above.) A button that another finger holds does not take the press. Other buttons and the D-pad do not slide.
- The slid-to button shows a drawn white fill (0.2, as the lit D-pad arm). The system glass press look shows only for real touches, so it cannot show there. The first button drops its press look when the finger leaves it.
- Real-touch iOS Simulator tests (iPhone 17 Pro Max): B → Y, A → B → X, B → Y → B, a tap, a slide with the D-pad held, and a slide onto a button that another finger holds gave the expected input. The Rhythm Heaven layout is unchanged.
- Not tested: a device and the iPad simulator.

For details, see [the slide record](../../../.touchable/work/silksong/reports/slide-face-buttons-20261020/README.md).

## 2026-10-20: −, +, and the pad toggle at the side edges (source change, no build)

User request: "Move + and - and controller key to most left or right".

- `TouchLayout.h`: on a phone, − (and Back) and + move to the side edges of the safe area: 12 points outward. On a tablet they stay 8 points from the edges.
- The pad toggle moves from the + row to the right edge, below +. On a phone 375 points high there is no space above ZR, so it stays next to +.
- In a small iPad window (phone layout), − and + now stay below the iPadOS window controls.
- `test_touch_layout.c` passes at 11 sizes. The iOS Simulator harness with the real `App.m` shows the new positions on the iPhone 17 Pro Max and in an iPad mini window. The Rhythm Heaven layout gave identical iPhone screenshots.
- Not tested: the full-screen iPad layout in a simulator (the simulator stays in portrait), and a device.

For details, see [the edge buttons record](../../../.touchable/work/silksong/reports/edge-menu-buttons-20261020/README.md).

## 2026-10-20: Touch controls hidden on loading screens (build 22)

User request: "Make virtual controller only appears when needed. Sometimes it appears when loading screen. Fix it for both game".

- Cause: the adapter published controller mode during loads, so the app showed the pad.
- `runtime/ss_touch_ui.c` now publishes the new pointer mode `SS_POINTER_WAIT` in GameManager states 0, 2, 3, and 6, during the Unity boot, and on the title screen when a hero object exists (a save is loading). A title or pause menu without a selected item after the 90-frame grace also publishes `WAIT`, for up to 8 s. This covers quit to title.
- The app hides the pad, the back button, and the pad toggle for a `WAIT` frame, and for up to 30 s after the last one (load hitches). Cutscenes keep the pad.
- Mac host tests at the iPhone and iPad aspects: the pad is hidden from boot to the title menu, from the save tap to gameplay, and during quit to title. It shows on the title menu, in gameplay, and in the pause menu. An iOS Simulator harness with the real `App.m` confirmed the hidden controls.
- Build 22 (launcher build) is installed on the iPhone and the iPad mini. The saves were backed up before the install and not restored. IPA: `IPA/silksong/Silksong-build22-20261020-134449-60dce2.ipa` (SHA-256 `d08a5ed3cc4129d2ffc88ae4475e901a46d4edf42376762ccc4641eac5bc6dc1`). The build 21 IPA is archived to `.touchable/archive/ipa/silksong/20261020-134449-60dce2/`.
- iPhone test (the user played): the log shows the controls hidden from boot to the title menu, in two save loads, in four room transitions, and in quit to title. They showed again in gameplay and on title menu items. Menus kept their touch-menu controls.
- Not tested: fast travel, deaths, cutscenes, and the iPad mini.

For details, see [the loading controls record](../../../.touchable/work/silksong/reports/loading-controls-20261020/README.md).

## 2026-10-20: PC save imported into slots 2 and 3 (save data only, no build)

User request: install an all-unlocked PC (Steam) save on both devices.

- A PC save is a .NET BinaryFormatter string of Base64 AES-256-ECB data. The plain data is the same JSON that the Switch game uses. The scripts in the record decode it.
- PC slot 1 (normal, 101%, 16 h 51 min, version 1.0.28561) is now slot 2. PC slot 2 (Steel Soul, 99%, 24 h 10 min, version 1.0.28324) is now slot 3. Only `profileID` changed. `shared.dat` keeps all device keys and adds the PC endings A to I.
- Mac host tests on a save copy: the save select shows both slots, both slots load into gameplay, and the inventory values match. Save and quit converts slot 2 to version 1.0.30000, and the reload passes.
- The files were copied into `Documents/Saves/Silksong/` of build 21 on the iPhone and the iPad mini. Silksong was not running. A read-back matched. Slot 1 and all other files are unchanged. Backups are in `.touchable/saves/silksong/<device>-before-pc-save-20261020-133856/`.
- After the build 22 install, the iPhone saves show that slots 2 and 3 were played and saved. The game converted them to version 1.0.30000. The iPad mini saves are unchanged.

For details, see [the PC save import record](../../../.touchable/work/silksong/reports/pc-save-import-20261020/README.md).

## 2026-10-19: Builds 20 and 21, glass press effect aligned with Apple

Build 21: The user did not like the new D-pad effect in build 20. The user asked for the previous highlight at a lower brightness. The D-pad uses the build 17–19 design again: two glass bars and a drawn white arm highlight (fill plus soft outer glow). The brightness is lower: white fill 0.32 → 0.2, outer glow 0.9 → 0.5. On a dark background, the brightness of a pressed arm decreases from 153 to 106 (49 at rest). The buttons continue to use the build 20 system press effect. Build 20 was not installed.

User request: "The light effect of liquid glass controller seems not correct. compare to Apple's official buttons and align with theirs."

- In the Simulator, XCUITest sent real touches, and the screen was recorded. The screen also showed Apple reference glass buttons. When pressed, an Apple button becomes larger (54 pt → approximately 71 pt). The glass becomes brighter evenly from the inside, and the text becomes lighter. There is no outer glow.
- Before the change: our buttons added a white base color, black text, and a white outer glow on top of the system effect. Thus they were overexposed and had a halo. The pressed D-pad arm was flat white plus an outer glow.
- Buttons: The custom press style is removed. Only the system press effect shows. The Rhythm Heaven glass buttons also use this effect.
- D-pad (build 20, reverted in build 21): It has four glass arms. A pressed arm gets a white glass tint (0.42) and becomes 8.5 pt larger outward and to the sides, with a spring animation.
- Input does not change: the buttons, the four D-pad directions, sliding, and diagonal directions all give correct input. The Rhythm Heaven B, ▶, and Y input is the same before and after the change.

Limits: Two-finger simultaneous presses were not tested. The test was only on the iPhone Simulator.

IPA: `IPA/silksong/Silksong-build21-20261019-111904-af2379.ipa` (SHA-256 `1c8ce91e…6b503`). The build 20 IPA is archived. One Silksong install occurred at 09:27–09:29 (this session did not start it; build 20 was the latest at that time; log `install-20261019-092648-560f94`; no device was recorded). At 11:23, the iPhone was not available for the build 21 install (locked or not connected). Thus there was no install and no backup. The iPad install is still necessary.

For details, see [the glass press record](../../../.touchable/work/silksong/reports/glass-press-20261019/README.md).

## 2026-10-19: Build 19, 120 Hz, Game Mode, and render pass merge

User request: "Try to unlock 120hz; optimize performance; Active game mode for all".

- 120 Hz: `profile.json` sets `SS_DISPLAY_HZ=120`. The display event thread (`ss_platform.c`) and the present cycle (`hle_nvn.c`) use this rate. The input display link in `App.m` requests 80–120 Hz. The game starts at 60 Hz. It changes to 120 Hz after the display link reports 120 Hz for 30 frames in sequence. When the screen goes down to 60 Hz (Low Power Mode, overheating, iPad mini), the game changes back to 60 Hz.
- Game speed: On the Mac host, the same step gives the same Hornet position at 60 Hz and at 120 Hz. Game time is calculated from real time.
- Game Mode: The shared `Info.plist` adds `LSSupportsGameMode=YES` and `LSApplicationCategoryType=public.app-category.games` (Apple documentation: `GCSupportsGameMode` is deprecated from iOS 18.6).
- Render pass merge (`nvn_mtl.m`, all games with the TBDR optimization on): When a clear is pending and the same target is set again, the clear is applied in the pass of the first draw. At iPhone size, passes per frame decrease 16 → 14. Attachment loads decrease 67 → 61 MB, and stores decrease 62 → 57 MB. `DM_NVN_RULER=1` screenshots are byte-identical. `DM_NVN_CLEAR_KEEP=0` disables it. New unit test: `t_clear_keep.m`.

iPhone result (build 19, the user played for approximately 2.7 minutes): frame time p50 8.33 ms, GPU p50 6.2–6.8 ms, p95 approximately 7.1 ms, 0 dropped presented frames in stable gameplay. Frames dropped only during room loads and when the app went to the background. Build 18 (60 Hz) in similar scenes: GPU p50 7.2–8.0 ms.

Checks: 48 Python tests, 22 `t_clear_keep` tests, and the other graphics unit tests pass. The iOS compile check has no warnings. A temporary Rhythm Heaven host build passes.

Limits:

- The test was only approximately 2.7 minutes. Check the logs again for heavier areas and long play sessions (heat).
- The decrease to 60 Hz in Low Power Mode is only in code. It was not tested on a device.
- Game Mode: The user confirmed that Game Mode shows on the iPhone (2026-10-19).
- Rhythm Heaven stays at 60 fps (its game time is calculated from frames). It gets Game Mode only after a new build.

IPA: `IPA/silksong/Silksong-build19-20261019-025850-1743a7.ipa` (SHA-256 `45ed1d7b…059d32`). The iPhone installed build 19 at 08:49. During the iPad install, the connection stopped. After that, the device was not available. The iPad must still install build 19.
For details, see [the 120 Hz record](../../../.touchable/work/silksong/reports/promotion-120hz-20261019/README.md).

## 2026-10-18: Builds 17 and 18, D-pad, Liquid Glass, and automatic hide for physical controllers

Build 17 changes the virtual controller as the user requested. The user confirmed that build 17 works correctly on the iPhone. Build 18 makes the D-pad larger as the user requested.

- Build 18: The D-pad width is equal to the width of the ABXY diamond: 168 pt on phones (previously 140 pt), 202 pt on iPad (previously 168 pt). The D-pad center stays at the build 17 height. The shoulder button row moves up so that the touch areas do not overlap: 15 pt up on iPhone 16 Pro Max, 18 pt up on iPad mini. The change is only in `TouchLayout.h`. `test_touch_layout.c` checks the new size and center height.

- D-pad: The left stick is replaced by an 8-direction D-pad. It sends only the Switch D-pad buttons (12–15). Each direction covers 45 degrees. A diagonal direction holds two buttons. A held direction stays active for 5 more degrees on each side. No direction is selected within 0.12 × diameter of the center. Mac host tests confirmed that the D-pad moves Hornet (including diagonal directions) and moves the cursor in the inventory.
- − and +: On phones, − is in the top-left corner and + is in the top-right corner. On iPad, they are below the iPadOS window control buttons (`safeAreaLayoutRegionWithCornerAdaptation:`), 8 pt from the left and right edges, 40 pt in size. They do not cover the silk spool.
- iPad spacing: The left and right margins decrease from 40 pt to 16 pt. The D-pad and A each move 24 pt outward. ZL and ZR are at least 8 pt from the edge. The phone layout does not change.
- Liquid Glass: Per Apple's Liquid Glass and game controls guidelines, the buttons use the clear glass button style. All controls are in one `UIGlassContainerEffect`, with a 35% dark layer below. When pressed, a control glows white and its text becomes black. When "Reduce Transparency" and "Increase Contrast" are on, the system makes the glass opaque.
- Physical controllers: When a physical controller sends a new button press (or a stick moves more than half way), the virtual controller hides. The virtual controller shows again on a new screen touch or when the last controller disconnects. This touch only shows the controls.
- The layout log line adds `window top`. The line is written again when the safe area changes.
- Only the Switch layout uses these changes. The Rhythm Heaven rhythm layout does not change. A temporary Rhythm Heaven build with the new `App.m` passes with no warnings.

Check results:

- Unit tests: `test_touch_layout.c` passes (D-pad sectors, dead zone, and hold; iPad window control area and silk spool). 47 Python tests pass.
- Mac host tests (save copy, runs 32–34): The game results for all 8 D-pad directions are correct. The map in this save is too small to confirm panning.
- iOS Simulator tests (iOS 27.0, iPhone 17 Pro Max and iPad mini): A test app with the real `App.m` was used. Screenshots checked the layout, glass, pressed state, menu touch mode, and accessibility settings. Simulated controller button presses hid the controls. Simulated touches and disconnects showed the controls.
- No touch input was sent on the iPhone or iPad. No test used a real controller.

Limits:

- The D-pad feel and the glass effect must be confirmed on a device.
- Confirm the size of the device window control area in the `window top` log line.
- After a physical controller hides the controls, the first touch only shows the controls.
- The build 18 D-pad size was only checked in the Simulator. The feel on a device must be confirmed.
- At 13:28, the Rhythm Heaven session changed the shared `App.m` (the rhythm layout uses Liquid Glass; the Switch layout glass code moved to `addGlassLayer`). Build 18 does not include this change. The Rhythm Heaven session checked the Switch layout in the Simulator with the changed `App.m` (SHA-256 `f6ece98b…942a79`) and the test app from this record: game screen, D-pad and button presses, menu mode, hide for physical controllers, and show after a touch and after a disconnect. The logs are identical line by line to the build 17 and 18 results. The D-pad is 168 pt (phone) and 202 pt (iPad). The evidence is in the Rhythm Heaven record (`.touchable/work/rhythm-heaven/reports/liquid-glass-20261018/silksong-regression/`). The next Silksong build will include this change. It is not tested on a device yet.

IPA: `IPA/silksong/Silksong-build18-20261018-123447-fd4781.ipa` (SHA-256 `d74ea875…cb9f50`). The build 17 IPA is archived to `.touchable/archive/ipa/silksong/20261018-123447-fd4781/`. The build 16 IPA is in `.touchable/archive/ipa/silksong/20261018-121557-652127/`.
The iPhone installed build 17 at 12:19. The user confirmed that build 17 works correctly on the iPhone. The iPad mini (13:06) and the iPhone (13:28) both installed build 18. The app was not running before the install. The install tool backed up the save data first. The app was not started.
For details, see [the virtual controller record](../../../.touchable/work/silksong/reports/touch-controls-20261018/README.md).

## 2026-10-18: Builds 14, 15, and 16, menu and inventory touch

Build 14 lets the user tap the menus and the inventory (−) directly. When the UI accepts touch, the app hides the virtual controller. Build 15 corrects slider rows: a tap on the row name only selects the row. It does not change the value. Build 16 lets the user tap the page icons at the top of the inventory: a tap on an icon goes directly to that page.

- Adapter: `runtime/ss_touch_ui.c`. It applies only to the main module `fc9ea4cc` (v655360). Before installation, it checks method slots, code addresses, and class slots. If they do not match, it does not install. It only writes one log line.
- Menus: title, save select, pause, options, game options, audio, controller settings, and quit confirmation. A tap on an item selects and confirms it. A tap or drag on a slider line sets the value. A tap on a left/right option row (for example, camera shake) changes to the next value. This is the original mouse rule of the game.
- Inventory: A tap on an item selects it. A second tap on the selected item is equal to A. A tap on a page icon at the top goes directly to that page. A tap on the L and R icons moves one page at a time. The submenu of the crest page accepts only items of the same type. When the map is zoomed in, the virtual controller shows.
- Page icons (build 16): The adapter does not change the page directly. It makes the game's `ListenForPaneLeft` or `ListenForPaneRight` send an event. Then it temporarily changes the direction of `SetNextInventoryPane` to the index difference between the target page and the current page. The game state machine does the change, so the page animation and state variables stay correct. A tap on the icon of the current page, or a tap while a submenu is open, does not change the page.
- Back: Touch mode shows a "Back" button (B) in the top-left corner. The controller switch in the top-right corner shows the virtual controller. When the UI closes, the switch resets.
- When the adapter cannot identify the UI, or when there is no menu information for 250 ms, the app shows the virtual controller.
- The iOS changes are in the shared `internal/platform/ios/App.m` and `TouchLayout.h`. Rhythm Heaven does not publish menu information, so its controller does not change.

Check results:

- Mac host tests (save copy, runs 6–18): All listed menu and inventory operations work correctly. Run 18 disabled the trace log and repeated the full flow.
- Mac host tests at device aspect ratios (runs 19–24): The full flow was repeated at the iPhone (2.17:1) and iPad (3:2) aspect ratios. The results are the same as at 16:9. Run 20 found a problem: a tap on a slider row name set the value from the tap position (for example, music became 0). Build 15 corrects this problem. Run 21 confirms the correction.
- Mac host test (run 25): On the game options page, camera shake and HUD appearance change on a tap. "Back" on the controller page works.
- Mac host tests (runs 26–31, build 16 code): At the 16:9, iPhone, and iPad aspect ratios, one tap on a page icon moves to the target page (+4, −3, +1, −2, −1). A tap on the current page or a tap in a submenu does not change the page. Two fast taps on two icons stop at the page of the second icon. After the inventory is closed and opened again, taps still work. The touch areas of the L and R icons end at the midpoint to the nearest page icon.
- Unit tests: 47 Python tests pass. `test_touch_layout.c`, `test_display.c`, and `test_viewport.py` pass.
- iOS compile: `App.m` and `ss_touch_ui.c` have no new warnings. The strict compile of `ss_touch_ui.c` (`-Werror -Wshadow`) passes.
- No touch input was sent on the iPhone or iPad.

Limits:

- When the map is zoomed in, use the controller. In long lists, use the controller to scroll to items outside the screen.
- Shops, yes/no confirmation boxes, fast travel, the quest board, and display boards do not accept touch yet. This save did not get to these screens.

IPA: `IPA/silksong/Silksong-build16-20261018-083911-334e34.ipa` (SHA-256 `6884d7b7…3b3ba2`). The build 15 IPA is archived to `.touchable/archive/ipa/silksong/20261018-083911-334e34/`. The build 14 IPA is in `.touchable/archive/ipa/silksong/20261018-045801-ed4b0e/`.
The iPad mini (08:46) and the iPhone (08:50) both installed build 16. The app was not running before the install. The install tool backed up the save data first. The app was not started. Touch mode was not tested on a device yet.
2026-10-19: The user tried it on the device and confirmed that menu and inventory touch work correctly ("Menu touch is done, all good").
For details, see [the touch menu record](../../../.touchable/work/silksong/reports/touch-ui-20261018/README.md).

## 2026-10-18: Builds 12 and 13, virtual controller layout per device

Build 12 sets different virtual controller positions for iPhone and iPad. Build 13 moves the iPad buttons 80 pt down, per user feedback.
The layout calculation is in `internal/platform/ios/TouchLayout.h`. The switch layout in `App.m` calls it. Rhythm Heaven uses the rhythm layout and is not affected.

Default game button mapping (`ControllerMapping` constructor, the same for all controller types):

| Button | Action |
| --- | --- |
| B | Jump |
| Y | Attack |
| A | Heal (`cast` field, the game calls it Bind) |
| X | Fourth action (`dreamNail` field) |
| ZR | Dash |
| ZL | Harpoon dash (`superDash` field) |
| R | Tool (`quickCast` field) |
| L | Quick map |

Changes:

- The old layout put L, ZL, R, and ZR at the top of the screen. L and ZL covered the health and silk in the top-left corner. ZR (dash) was far from the right thumb.
- The new layout puts ZL and L above the stick, and R and ZR above ABXY. The triggers ZL and ZR are on the outer side.
- Phone: stick 140 pt, ABXY 54 pt, near the two bottom corners, similar to the old layout.
- iPad (shorter side at least 600 pt): All buttons are 1.2 times larger. The stick center is 174 pt from the bottom edge (iPad mini). Smaller iPad windows use the phone layout.
- The touch area of round buttons is 8 pt larger than the visible area (9.6 pt on iPad). The touch area of the stick is 24 pt larger (28.8 pt on iPad). The touch areas of adjacent buttons do not overlap.
- The size of the stick dot changes with the stick size.

Check results:

- Unit test: `internal/tests/common/test_touch_layout.c` passes. It checks 4 iPhone sizes, 4 iPad sizes, and 1 small window size: safe area, HUD area, no overlap of touch areas, button directions, and iPad height.
- iOS SDK syntax check: `App.m` has only two existing warnings.
- iPhone device (build 12): The log layout is `phone`, view 956×440, safe area 62/62/0/20. The screenshot agrees with the design.
- iPad device (build 12): The stick center is 254 pt from the bottom edge. The user reported that it is too high.
- 2026-10-18: The user examined the build 13 iPad layout and confirmed: it looks good.
- iPad device (build 13): The log layout is `tablet`, view 1133×744, safe area 0/0/0/20. The stick is at 486–654 pt. In the screenshot, the top-left HUD is not covered.
- No touch input was sent this time. The user confirmed the iPad build 13 layout. The feel on the iPhone is not confirmed yet.

Limits:

- A finger cannot slide from one button to an adjacent button. This is the same as in the old layout.
- The positions are fixed. They cannot be adjusted in the app.
- The iPhone build 12 check screenshot stopped at the game's crest menu. No input was sent this time. No later test was done on the iPhone. The iPhone installed build 13 at 02:17, and the app build number is 13. The app was not started after the install.

IPA: `IPA/silksong/Silksong-build13-20261018-020141-5eee3a.ipa`.
The iPad and the iPhone both installed build 13.
For details, see [the virtual controller record](../../../.touchable/work/silksong/reports/touch-layout-20261018/README.md).

## 2026-10-18: Build 11, high resolution and iPad full screen

Build 11 increases the output resolution on all devices.
The `environment` in `profile.json` sets `SS_RENDER_HEIGHT=1080`.
The shared code `internal/runtime/common/ss_display.c` reads this value. The permitted range is 720–1440. If it is not set, the value is 720.
The game window texture is fixed at 1920×1080, so the maximum output width is 1920. Wider screens get a smaller height.

| Device | Build 10 and earlier | Build 11 output | Build 11 world texture |
| --- | --- | --- | --- |
| iPad mini (3:2) | 1096×720 | 1644×1080 | 1316×864 |
| iPhone 16 Pro Max (2.17:1) | 1564×720 | 1916×882 | 1532×706 |
| 16:9 screen | 1280×720 | 1920×1080 | 1536×864 |

Build 11 also corrects the bands, the green and pink edges, and the ghosting of build 10 on iPad.
Two camera code paths in the game limit the aspect ratio to [1.6, 2.3917].
The upstream `dm/tools/patch_nso.py` registers two rewrites for `fc9ea4cc` (0x2c4bad8 and 0x2c4c800).
The rewrites only remove the 1.6 lower limit. The 2.3917 upper limit stays.
The new modules are in `.touchable/work/silksong/patched-v655360-fullview`. `patched_dir` in `game.json` selects them.
Only `main` changes, by 2 bytes in total. The old module directory `patched-v655360` is kept.
CPU checks: STRUCT passes (6 registered rewrites). EQUIV passes (0 differences, 100/100 mutations detected).

Check results:

- Unit test: `tests/test_display.c` passes. It checks the output height, the 1920 width limit, and invalid input.
- Unit test: `tests/test_viewport.py` passes. With the new modules, 45 simple camera cases and 15 main camera cases fill the phone and iPad screens. The original bytes still give bands at 3:2 (negative control).
- Mac host: At iPad size, 1644×1080 has no bands, no magenta edges, and no ghosting. Frame time p50 16.66 ms.
- Mac host: Rhythm Heaven starts and shows frames at iPad size. It does not set `SS_RENDER_HEIGHT`, so it stays at 720.
- iPad mini device: The screenshot fills 2266×1488. The edge rows have no green or pink pixels. For frames 4200–4800, in groups of 300 frames, p50 is 16.67 ms with 0 dropped frames.
- iPhone device (wireless install): The screenshot fills 2868×1320. For frames 3900–4500, in groups of 300 frames, p50 is 16.67 ms with 0 dropped frames.
- Both devices recorded 3 save commits. No touch input was sent this time.

GPU time and memory:

| Device | GPU p50 (build 10 or 9) | GPU p50 (build 11) | footprint | Distance to system limit |
| --- | --- | --- | --- | --- |
| iPad mini | 8.3 ms | 13.2–13.9 ms | 2.31–2.33 GB | 1.03–1.07 GB |
| iPhone 16 Pro Max | 6.1–6.9 ms | 11.7–12.2 ms | 2.30 GB | 1.06 GB |

Limits:

- GPU time is approximately 1.7 times the previous value. The device becomes hotter and uses more power. If necessary, set a smaller `SS_RENDER_HEIGHT` and build again.
- The iPhone output height is 882, not 1080. The cause is the 1920 width limit.
- The iPad shows a taller scene than 16:9. In some rooms, the bottom is a black area outside the scene. On the Mac, approximately 3.8% of the bottom of the bench room is solid black.
- The full flow is not verified. The provisioning profile expires on 2026-10-25.

Save backups before the install:

- iPad: `.touchable/saves/silksong/phone-before-install-20261018-012235-fb0a07`
- iPhone: `.touchable/saves/silksong/phone-before-install-20261018-012918-b36fb9`

IPA: `IPA/silksong/Silksong-build11-20261018-011835-b57260.ipa`.
For details, see [the build 11 record](../../../.touchable/work/silksong/reports/build11-hires-20261018/README.md).

## 2026-10-18: iPad mini build 10

Build 10 is installed on the iPad mini (iPad16,2, iPadOS 27.0.1).
Build 10 uses the shared iPad landscape and full-screen settings.
The game display code did not change.
The output size is 1096×720. The world texture is 876×576.
Correction: The build 10 image did not fill the 3:2 screen.
The game limits the aspect ratio to 1.6 or more, so the iPad has bands of approximately 2.4% at the top and at the bottom.
The bands show the undrawn edges of the render target (green and pink). The world layers are not aligned, and there is ghosting around the character.
Build 11 corrects these problems.
The running instance got to frame 1663. The p50 of 300-frame groups is 16.67 ms.
The memory footprint is 1.8 GB. 1.5 GB remains before the system limit.
No touch input was sent this time. The full flow is not verified.
The provisioning profile expires on 2026-10-25.
For details, see [the iPad mini record](../../../.touchable/work/silksong/reports/ipad-mini-20261018/README.md).

## 2026-10-17: Directory cleanup and rebuild

This rebuild uses build 9.
The current IPA is in `IPA/silksong/`.
The latest app path is recorded in `.touchable/build/games/silksong/latest.json`.
The original packages, keys, old apps, original IPA, and save data are kept.
The original IPA is moved to `.touchable/archive/ipa/`.
This time, the new items are the build, the launcher, and the launch check records.
Build 9 is installed on the iPhone through the launcher.
Before and after the install, the SHA-256 of 21 save-related files is the same.
A short launch on the device showed frames continuously. There were no missing shader errors.
The full gameplay flow was not verified again this time.
The older build numbers below belong to existing test records.
For details, see [the cleanup and verification record](../../../docs/developer/reorganization.md).

## Existing game records


Before the cleanup, the current game was v655360 (1.0.30000). The phone app was build 8.
The shared fixes from 2026-10-15 and 2026-10-16 have Mac records, but they did not replace the phone app.
The results below come from existing tests.
For the workspace maintenance rules, see [the maintenance notes](../../../docs/developer/workspace-maintenance.md).

2026-10-17: Old builds were removed as the user requested.
The latest local build at that time was in `.touchable/build/games/silksong/ios/`.
The checksum records of the old IPA and the old build logs are kept in `.touchable/build/games/silksong/logs/retired-builds-20261017/`.
The old paths are only for the history record.
For details, see [the build retention notes](../../../docs/developer/workspace-maintenance.md#latest-build-retention-2026-10-17).
The latest local build at that time included the later shared fixes and was not signed yet. The build 8 IPA keeps its original signature.

This game uses only Switch files that the user supplies. The paths below are relative to the project root. The reference project is [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile/blob/main/README.zh-CN.md). The pinned revision is `2eb5758771f110cd53082b999d88d242766610da`.

The Switch version of the game now runs on the iPhone. The phone app name is `丝之歌` (Silksong). It supports touch buttons and external controllers. On 2026-10-13, the user confirmed on iPhone 16 Pro Max, iOS 27.2: the menus and the game screen are correct. This version fixes pink flicker in menus, missing text, black frames in the game, and an upside-down image.

On the same day, version 4 fixed two problems: the main save could not be written, and the app crashed when the user quit through the menu. The SDK `clock_gettime` now reads the host clock. When a thread sleeps or yields, it registers a safe suspend marker for Unity resource collection. The user confirmed: the save data is kept, and quit through the menu and save reload work correctly. The phone save files from before the update are backed up.

Version 6 connects iPhone Core Haptics. The original amplitude, frequency profile, and stop signals of the game control the phone haptics. Output stops when the app goes to the background or when audio is interrupted. The phone has only one motor, so the output of the two Switch motors is merged. The frequency profile is expressed as Core Haptics sharpness.

The image is rendered at the aspect ratio of the phone view. iPhone 16 Pro Max uses 1564×720 output and a 1252×576 world texture. The game camera uses this to make the horizontal field of view wider and keep the vertical field of view. The menus also use the actual aspect ratio. The v0 modules are in `.touchable/work/silksong/patched`. The version 4 modules are kept in `patched.before-phone-features-v4`. The comparison of local camera readings with the same scene passes. On 2026-10-14, the user confirmed that the character proportions in version 6 are correct and that haptic feedback stops when haptics strength is 0. Full-screen display, haptics, and UI proportions also passed device tests.

On 2026-10-14, version 7 changed to the game update v655360 (display version 1.0.30000). Switch updates are cumulative, so only the base package and the latest `[v655360].nsp` are used. The `[v131072]` NSZ is an earlier update and is not necessary. The updated content is in `.touchable/work/silksong/v655360`. The rewritten modules are in `.touchable/work/silksong/patched-v655360`. `content_dir` and `patched_dir` in `game.json` select them. The v0 content and modules are kept unchanged.

- The update changed only `main` (build ID `fc9ea4cc…`). `sdk`, `subsdk0`, and `rtld` are byte-identical to v0. The Unity version does not change.
- The merged RomFS section passes all 6 levels (150,821 blocks) of the IVFC hash tree included in the update. The RomFS has 2,118 files, approximately 6.3 GiB.
- The fixed addresses were found again with masked instruction matching and the new IL2CPP metadata: Screen constructor `0xb21fb4` / `0xb21fd0`, cutscene getter `0x5b01060` / `0x5b01610`, resolution record `0x6679240` / `0x666efa0`, SetResolution slot `0x7cfaeb0`. `ss_display.c` checks them against the data and supports both the v0 and the v655360 layouts.
- In 1.0.30000, `ForceCameraAspectLite.AutoScaleViewport` itself clamps the aspect ratio to [1.6, 2.39]. The phone aspect ratio already fills the screen, so the v0 AutoScaleViewport cut is not necessary. `test_viewport.py` ran the original code in Unicorn to check this.
- `main` plus bss is approximately 132 MB. This is more than the ±128 MB range of the B instruction. For out-of-range branches, the rewriter uses "veneer words". They are placed between functions, at positions that were dead NOP padding, and are registered in `main.veneers.tsv` (14,332 entries). In-range branches do not change. The v0 rewrite result is byte-identical. `structck.py` checks each veneer word independently, and `equiv.py` executes through the veneers. The structure and instruction equivalence checks pass for all three modules (49,778 rewrite points in main, 0 differences, all 100 mutations detected).
- 398 new shaders (2,599 in total) were added. All translated successfully.
- The Mac host test used a copy of the phone save: the menu showed 1.0.30000. The original save loaded. Character movement, dialog prompts, the pause menu, save and quit, and return to the main menu and reload all worked correctly. The game wrote a separate `user1_1.0.30000.dat` and kept the old `user1_1.0.27776.dat`.
- The Documents folder (with save data) retrieved from the phone before the upgrade is in `.touchable/build/phone-saves-20261014-before-v655360`.

On 2026-10-14, version 8 decreased heat and made the frame rate stable. The version 7 phone log showed: almost every frame took 17.5 ms (approximately 57 fps; approximately 26 of each 300 frames were presented late). In gameplay, the GPU took 13–15 ms per frame. There were three causes. All are fixed:

- The Switch display vsync event was simulated with a `nanosleep(16.67 ms)` loop. Each tick added wake-up latency. The game presents frames from this event, so the phone got approximately 57 fps and the Mac approximately 48 fps. `internal/runtime/common/ss_platform.c` now uses a 60 Hz time-constraint thread scheduled at absolute times.
- On the real device, `nvnQueueWaitSync` is a GPU-side wait. But the host made the CPU wait until the GPU finished, so the CPU and the GPU could only work in turns. All queues and presents share the same in-order Metal queue, so no CPU wait is necessary (`internal/runtime/common/ss_nvn.inc`; `SS_QUEUE_WAIT_CPU=1` restores the old behavior for comparison).
- Before almost every draw, Unity uses `UpdateUniformBuffer` to change tens of bytes of constants. The translation layer wrote each update with a copy. A copy ends the render pass, so one frame was cut into approximately 340 passes. Each pass loaded and stored the full attachments. Now the CPU side records these constants in 256-byte blocks. Updates in a pass bind a snapshot instead. When the pass ends, one write-back occurs (`ubv_*` in `nvn_mtl.m`; `DM_NVN_UBV=0` disables it). This item is enabled with the TBDR optimization. Silksong sets `DM_NVN_TBDR=1` in the `environment` of `profile.json`. Other games still use the app default `0`.

Comparison on a Mac (M1) with the same save and the same gameplay operations:

| Per frame | Version 7 | Version 8 |
| --- | --- | --- |
| Frame time p50 / p99 | 19.3 / 31.9 ms | 16.66 / 17.4 ms |
| Render passes | 206 | 16 |
| Attachment load + store | approximately 2.2 GB | approximately 0.11 GB |
| GPU time p50 | 14.2 ms | 2.2 ms |

In the same process, screenshots of the pause screen were taken first with the old path and then with the new path. They are byte-identical (`DM_NVN_RULER=1`). In approximately 1 million absorbed updates, the shadow check against game memory made 0 corrections. Save load, movement, pause, save and quit, and return to the title and reload all worked correctly. A shared fix from the same day (another session) is also merged: retired textures and upload staging blocks could go into an autorelease pool that never drained. This is now fixed.

Memory: On the Mac, the game uses approximately 2.7 GB in gameplay. Of this, approximately 2.0 GB is the game's own Switch memory (including an approximately 870 MB GPU memory pool), and approximately 0.5 GB is Metal textures and buffers. The game controls the first part. The phone save backup from before the version 8 install is in `.touchable/build/phone-saves-20261014-before-build8`.

On 2026-10-15, a shared fix after version 8 (another session): When the game destroys a texture, `mtl_tex_release` in `nvn_mtl.m` now releases the cached views of that texture. Before, texture slots that the game no longer used kept the old textures. Mac host test (`DM_NVN_TBDR=1`, 130 seconds in gameplay, `DM_RES_CENSUS=2`): at frame 7500, retired but not destroyed textures decreased from 23 (38 MB) to 0. GPU allocation decreased from 1009 MB to 970 MB. Conflicts in inline uniform updates are still 0. Version 8 on the phone does not include this fix. The next build will include it.

Another shared change on the same day: the automatic block height in `nvn_tex.c` is now the GOB count of "height × 1.5", rounded down. Pre-laid-out textures use `pkg_block_h` and are not affected. Only render targets and textures created at runtime can change. Mac host test (current source, same save, same input as version 8): the pause menu, quit dialog, save and quit, title, save select, and reload all worked correctly, with 8 save commits. Compared with version 8, the screenshots differ only in animated parts. There are still the same 4 `【红】` (red) lines. Retired but not destroyed textures are 0 in all 29 census runs. In gameplay, frame time p50 is 16.65 ms and p99 is 18.7 ms, the same as version 8. The record is in `.touchable/work/silksong/reports/shared-fixes-20261015-mac-flow.json`.

On 2026-10-16, a shared graphics fix: `nvn_dirty.c` selects the memory pool from the full texture range and removes records by the original pool address.
This fix corrects wrong lookups when multiple 4 KiB-aligned pools share one 16 KiB host page.
In Rhythm Heaven Remix 8, this problem caused many duplicate uploads.
The Silksong Mac host and iOS app were built again.
On the Mac, with a separate save copy, a 75-second launch record and an 80-second menu and save load record were completed.
The run logs show no crash.
The 4 red diagnostics in the log are the same as in the 2026-10-15 regression log.
They include two unmapped shader control sections and one program without a vertex shader.
This check does not prove that the full game is correct.
Silksong build 8 on the phone was not replaced.
For the detailed record, see [the shared memory pool fix report](../../../.touchable/work/rhythm-heaven/reports/performance-20261016-134827/README.md).

This is a playable test version. The current version skips cutscene videos that are not adapted yet. The full game flow is not verified yet. The IPA is generated only locally. It uses the local development signing and the game resources that the user supplies.

Verification record of the v0 phase (history):

- The input is the v0 base NSP of title ID `010013c00e930000`. The content hashes of the four NCA files agree with the file names.
- ExeFS and RomFS are extracted. The RomFS has 2,099 files, approximately 6.65 GB. The original input is not changed.
- The game uses Unity `6000.0.50f1-uum-100966-branch1` and IL2CPP metadata version 31. All 12 segment hashes of the four NSO files pass the check.
- `main`, `subsdk0`, and `sdk` are rewritten. None of the three modules uses the D2R game-specific changes.
- The main module has 2 `br x18`, 1 `blr x18`, and 4 `mov sp, x18` that need new handler code. Each has a registration table. An unknown trap stops the process.
- On 2026-10-13, the device test on iPhone 16 Pro Max, iOS 27.2 was PASS. 4 threads completed 4,000 indirect branches, 4 stack switches, and 4 indirect calls.

Because ARM64 is the same, most CPU instructions can be used again. The iOS host must still supply the Switch thread, system service, graphics, and audio interfaces. The existing rewrite output needs the host adaptation of this project.

Current status of the game adaptation:

1. The startup code loads the three modules, registers the indirect branch tables, and configures signal stacks for game threads.
2. The host supplies file, thread, system service, input, audio, save, and app suspend interfaces. The full flow still needs tests.
3. The Switch shaders are converted to Metal. GPU parameter copies execute in order. When compute and unused stages are removed, the vertex and fragment programs are kept.
4. Special tests check 1,024 sets of overlapping parameter updates and shader stage bindings. 239 tests pass. The device menus and the real game screen passed user tests.
5. The clock special tests pass. A real Mach thread suspend test completed 17 suspends, context reads, resumes, and marker cleanups. Phone save, quit through the menu, and save reload passed user tests.

Main files:

| Path | Purpose |
| --- | --- |
| `internal/games/silksong/profile.json` | Pinned game version (v655360) and module hashes |
| `internal/tools/prepare_game.py` | Checks the NSP and extracts it with the local keys |
| `internal/tools/romfs_storage.py` | Expands the NCA compressed storage that this version uses |
| `internal/tools/patch_storage.py` | Reads update packages: indirect storage, AES-CTR-EX, IVFC and ExeFS hash checks |
| `.touchable/work/silksong/v655360` | ExeFS and RomFS of the base package plus the v655360 update |
| `.touchable/work/silksong/patched-v655360` | Rewritten modules for v655360, with `main.veneers.tsv` |
| `.touchable/work/silksong/patched-v655360-fullview` | Current modules. The 1.6 aspect ratio lower limit is removed |
| `.touchable/work/silksong/reports/il2cpp-v655360` | IL2CPP method table for v655360 |
| `internal/tools/analyze_game.py` | Verifies NSO files and lists module dependencies |
| `internal/tools/rewrite_game.py` | Runs the CPU rewrite and checks |
| `internal/patches/d2r-mobile.patch` | Rewriter, host, and graphics patch against the pinned upstream revision |
| `internal/tools/prepare_ios.py` | Generates the iOS project for the game |
| `internal/tools/package_ios.py` | Checks the signature and exports the IPA and SHA-256 |
| `internal/runtime/common/ss_indirect_branch.c` | Darwin ARM64 signal handler code |
| `internal/runtime/common/ss_time.c` | Switch clock interface |
| `internal/runtime/common/ss_thread_suspend.inc` | Thread suspend, context, and safe wait markers |
| `internal/games/silksong/runtime/ss_display.c` | Adaptive output, internal world texture, and native resolution requests |
| `internal/runtime/common/ss_haptics.c` | Switch rumble values and the mix of the two motors |
| `internal/platform/ios/Haptics.m` | iPhone Core Haptics output and lifecycle |
| `internal/platform/ios/probe/Probe.xcodeproj` | Device CPU test program |
| `.touchable/build/games/silksong/ios-project/Silksong.xcodeproj` | Game app |
| `.touchable/work/silksong/reports/iphone-cpu-probe.json` | Test results retrieved from the user's phone |
| `.touchable/work/silksong/reports/save-quit-verification.json` | Verification record of the save and quit fixes |
| `.touchable/work/silksong/reports/phone-features-verification.json` | Verification record of haptics, wide screen, actual camera readings, and the IPA |
| `.touchable/work/silksong/patched/verification.json` | CPU check results and output hashes |
| `.touchable/build/games/silksong/legacy/logs/rewrite/` | Structure check and instruction equivalence test logs |

The local machine needs an Apple Silicon Mac, Xcode 27, and Python 3.9 or later. Keep at least 20 GiB of free space before extraction. The scripts build tools in the workspace. They do not install system packages.

When the game resources and the signing configuration exist, you can rebuild the game and the IPA:

```sh
.touchable/venv/bin/python internal/tools/prepare_ios.py --game silksong
xcodebuild -project .touchable/build/games/silksong/ios-project/Silksong.xcodeproj -scheme Silksong \
  -configuration Debug -sdk iphoneos -derivedDataPath .touchable/build/games/silksong/ios \
  -allowProvisioningUpdates DEVELOPMENT_TEAM=YOUR_TEAM build
.touchable/venv/bin/python internal/tools/package_ios.py --game silksong --output IPA/silksong/Silksong.ipa
```

Replace `YOUR_TEAM` with the local development team ID. The signature applies only to the devices in the provisioning profile. The provisioning profile validity period also limits it.

When Xcode is not signed in to an Apple ID, automatic signing fails. Build first with `CODE_SIGNING_ALLOWED=NO`. Then sign with an existing provisioning profile. Version 7 was signed this way:

```sh
APP=.touchable/build/games/silksong/ios/Build/Products/Debug-iphoneos/Silksong.app
cp "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles/<PROFILE_UUID>.mobileprovision" "$APP/embedded.mobileprovision"
codesign --force --sign "Apple Development" --entitlements entitlements.plist --generate-entitlement-der "$APP"
```

`entitlements.plist` contains only `application-identifier`, `com.apple.developer.team-identifier`, and `get-task-allow`. This is the same as version 6.

Prepare the tools:

```sh
internal/tools/bootstrap.sh
```

The input is in `MyGameFiles`. `prod.keys` is used only to read encrypted NCA content. The scripts do not print the keys and do not copy the keys into the test app. Intermediate key files use `0600` permissions and are deleted when the procedure ends.

```sh
.touchable/venv/bin/python internal/tools/prepare_game.py inspect --game silksong \
  'MyGameFiles/silksong/Hollow Knight Silksong [010013C00E930000][v0].nsp'

.touchable/venv/bin/python internal/tools/prepare_game.py extract --game silksong \
  'MyGameFiles/silksong/Hollow Knight Silksong [010013C00E930000][v0].nsp' \
  --keys 'MyGameFiles/keys/ProdKeys.-v22.5.0/prod.keys'

.touchable/venv/bin/python internal/tools/analyze_game.py --game silksong
```

The extraction script does not overwrite existing output. If the extraction result already exists, do not run it again. `extract` processes only the base package. For update packages, use `extract-update`:

```sh
.touchable/venv/bin/python internal/tools/prepare_game.py extract-update --game silksong \
  'MyGameFiles/silksong/Hollow Knight Silksong [010013C00E930000][v0].nsp' \
  'MyGameFiles/silksong/Hollow Knight Silksong [010013C00E930800][v655360].nsp' \
  --keys 'MyGameFiles/keys/ProdKeys.-v22.5.0/prod.keys' --out .touchable/work/silksong/v655360
```

It uses `internal/tools/patch_storage.py` to merge the indirect storage and the AES-CTR-EX encrypted update data. Then it expands the compression layer. The merged result must pass the IVFC hash tree of the update. ExeFS must pass its SHA-256 layer.

Run the rewrite and checks in a new output directory:

```sh
.touchable/venv/bin/python internal/tools/rewrite_game.py --game silksong --out .touchable/work/silksong/rebuilt
```

Check the existing rewrite output:

```sh
.touchable/venv/bin/python internal/tools/rewrite_game.py --game silksong --verify-only
.touchable/venv/bin/python -m unittest discover -s internal/tests -p 'test_*.py' -v
```

The structure check uses independent LLVM disassembly. The instruction equivalence test uses Unicorn. In Unicorn, a model represents the register effects of the signal handler. The Mac and iPhone test programs verify the real C signal handler code. These checks cannot prove that the full game runs correctly.

Rebuild and install the CPU test program:

```sh
xcrun devicectl list devices
internal/tools/iphone_probe.sh DEVICE_ID DEVELOPMENT_TEAM
```

Replace the two parameters with the device ID and the development team ID from the local Xcode. Unlock the device and turn on Developer Mode. The app runs the test automatically when it starts. The result is saved in `Documents/cpu-probe.json` in the app container. Use this command to retrieve the result:

```sh
xcrun devicectl device copy from --device DEVICE_ID \
  --domain-type appDataContainer --domain-identifier com.mmmz.TouchableCPUProbe \
  --source Documents/cpu-probe.json \
  --destination .touchable/work/silksong/reports/iphone-cpu-probe.json
```

`MyGameFiles`, extracted files, keys, build output, and signing information are all listed in `.gitignore`. The upstream d2r-mobile uses the MIT license, and hactool uses the ISC license. The related source code and licenses are kept in the local source directories.

The app identity of this game is in `internal/games/silksong/game.json`. The fixed address changes are in `internal/games/silksong/runtime/ss_display.c`. The shared aspect ratio calculation is in `internal/runtime/common/ss_display.c`. To add a new game, see `docs/developer/add-game.md`.
