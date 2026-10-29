# Coding agent workflow

Run commands from the project root. Use the current manifests instead of paths copied from old reports. Set `TASK_GAME` to `silksong` or `rhythm-heaven` in command examples.

## Task sequence

1. Read [AGENTS.md](../../AGENTS.md), [the project summary](project-summary.md), and the selected game record.
2. Read the game's manifest and profile.
3. Define the expected behavior and the affected files.
4. Check the current configuration and the applicable test baseline.
5. Reproduce the fault with a bounded test when the task is a fix.
6. Change the source that owns the behavior.
7. Run the required checks for that behavior.
8. Record the result and any test limits.
9. Update the relevant documentation.
10. Give the user the changed files, results, and remaining work.

Record the runtime test's game ID, package version, module hashes, app build, save fixture, and platform. Keep the host result separate from the phone result.

## Select the change path

| Task | Source to inspect first | Required evidence |
| --- | --- | --- |
| Documentation | `README.md`, `docs/`, game records | Valid links, accurate file references, and valid command syntax |
| Workspace maintenance | `.gitignore`, `internal/requirements.txt`, game records, [maintenance rules](workspace-maintenance.md) | Source baseline, both game configurations, dependency checks, document checks, and protected-file comparison |
| Game configuration or tool paths | `internal/tools/workspace.py`, `internal/games/<id>/game.json`, `profile.json` | Registry check and applicable Python tests |
| Extraction or update storage | `internal/tools/prepare_game.py`, `game_formats.py`, `patch_storage.py`, `nca_header.py`, `romfs_storage.py` | Format tests, content hash checks, correct Title ID, and preserved source packages |
| CPU rewrite or indirect branch handling | `internal/tools/rewrite_game.py`, upstream rewrite and verification tools, `internal/runtime/common/ss_indirect_branch.c` | Structure and equivalence checks; applicable native signal tests |
| Shared files, memory, time, or threads | `internal/runtime/common/ss_fs.c`, `ss_memory.inc`, `ss_time.c`, `ss_thread.c`, `ss_thread_suspend.inc` | Applicable C tests and host regression tests for both games |
| Silksong resolution or camera | `internal/games/silksong/runtime/ss_display.c`, shared display calculation | Display C test, active-version viewport test, and gameplay checks at the affected aspect ratios |
| Graphics or shader translation | `internal/runtime/common/ss_nvn.inc`, upstream `dm/gfx/`, shader tools and profiles | Applicable graphics tests, shader rebuild when needed, and both game hosts |
| Rhythm Heaven audio | `internal/runtime/common/ss_audren.c`, `ss_audio_renderer.c` | Mixer C test, audio output check, and rhythm timing checks on the target platform |
| iPhone or iPad app, touch, or haptics | `internal/platform/ios/`, game `ios` settings | Applicable runtime test, generated project, app build, and target device result when available |
| App language or iOS per-app localization | `internal/platform/ios/App.m`, `internal/runtime/common/ss_platform.c`, game `ios.localizations` | Settings entry screenshot, app build, target language checks, fallback check, and both host regressions |
| New game | `internal/tools/games.py`, [add-game.md](add-game.md) | Isolated identities, pinned modules, implemented adapters, and recorded game tests |
| Game update | Extraction tools, profile, fixed-address adapter, shaders | New content hashes, relocated addresses, CPU verification, and save/load regression |

Run only the checks that apply to the task. A documentation task does not require game extraction or an app rebuild. A shared behavior change needs checks in both games.

## Check the workspace

```sh
TASK_GAME=silksong
.touchable/venv/bin/python internal/tools/games.py list --json
.touchable/venv/bin/python internal/tools/games.py check
.touchable/venv/bin/python -m unittest discover -s internal/tests -p 'test_*.py' -v
```

`games.py check` checks all registered identities. It also checks adapter configuration and source files. It does not establish game compatibility.

If local preparation tools are missing, inspect the bootstrap scripts before you run them:

```sh
bash internal/tools/bootstrap.sh
bash internal/tools/bootstrap_shader.sh
```

The scripts fetch pinned sources and build local tools. The first script applies project patches and installs `internal/requirements.txt` into `.touchable/venv`. Preserve existing upstream edits before bootstrap. The dependency file includes `UnityPy` for Silksong shader extraction.

## Maintain the workspace

Use [workspace maintenance](workspace-maintenance.md) before cleanup.
Check the root Git status first.
Keep upstream changes in the pinned patches.
Keep current game state separate from historical build records.
Inventory each cleanup target before deletion.
Preserve game packages, keys, saves, apps, IPA files, and test evidence.
Use the applicable generator to restore generated files.

## Build and test an existing game on Mac

Reuse the configured content, rewritten modules, and shader libraries when those inputs have not changed.

```sh
bash internal/tools/build_host.sh --game "$TASK_GAME"
mkdir -p ".touchable/work/$TASK_GAME/temp"
TASK_SAVE_DIR=$(mktemp -d ".touchable/work/$TASK_GAME/temp/agent-saves.XXXXXX")
.touchable/venv/bin/python internal/tools/run_host.py --game "$TASK_GAME" --seconds 30 \
  --save-dir "$TASK_SAVE_DIR"
```

This test starts with an empty save directory. Copy the required save fixture into `TASK_SAVE_DIR` before launch if the task needs an existing save. Keep the source save unchanged.

The build log is `.touchable/build/games/<id>/logs/host-build.log`. The runtime log is `.touchable/build/games/<id>/logs/host-boot.log`. Archive each relevant log under a task-specific name before another run. The tool replaces the runtime log and resets the input file on each launch.

`run_host.py` reports the guest exit code or the time limit. Its own successful exit does not prove that the guest ran correctly. Read the complete log. Check the specific menu, scene, input, audio, or save behavior required by the task.

Send input from another terminal while the host runs:

```sh
.touchable/venv/bin/python internal/tools/input_host.py --game "$TASK_GAME" A --seconds 0.15
```

For a save regression, load the copied save, play, open pause, save and quit, return to the title menu, and reload. A 30-second boot alone does not cover this sequence. Select a suitable time limit and record the input sequence.

## Verify or replace CPU rewrite output

To check the active output:

```sh
.touchable/venv/bin/python internal/tools/rewrite_game.py --game "$TASK_GAME" --verify-only
```

This command checks module hashes and the pinned upstream revision. It runs structural checks and Unicorn equivalence checks with 100 mutations per module. It updates `verification.json` and the rewrite logs. Archive earlier evidence before the check if it is needed for comparison.

To test a changed rewriter, use a new directory:

```sh
TASK_REWRITE_DIR=".touchable/work/$TASK_GAME/patched-agent-$(date +%Y%m%d-%H%M%S)"
.touchable/venv/bin/python internal/tools/rewrite_game.py --game "$TASK_GAME" --out "$TASK_REWRITE_DIR"
```

The output directory must not exist. Review the verification report before you select it as the active `patched_dir`. The host reads `patched_dir` from the manifest. The iOS generator can test another directory with `--modules "$TASK_REWRITE_DIR"`.

Keep `content_dir`, `patched_dir`, module hashes, Build IDs, and fixed-address layouts consistent. Stop at a hash mismatch and determine its cause. Do not change a hash merely to bypass the check.

Signal effects in Unicorn use a model. Changes to the native signal bridge also need the Mac branch probe or the phone CPU probe. [iphone_probe.sh](../../internal/tools/iphone_probe.sh) builds, installs, and launches the phone probe. Use it when the task includes device tests. The probe has its own app identity and no game content.

## Change a game package version

1. Inspect the new package with `internal/tools/prepare_game.py inspect` and the explicit game ID.
2. Select a new content directory inside `.touchable/work/<id>/`.
3. Extract the base package, or merge the base and update with `extract-update`.
4. Require the extraction hash checks to pass.
5. Analyze the new content with `internal/tools/analyze_game.py` and a separate report directory.
6. Record the module hashes, Build IDs, and runtime module list in the profile.
7. Relocate each fixed-address change against the new modules.
8. Rewrite into a new output directory.
9. Select the verified content and modules in the manifest.
10. Extract and build the shaders for the selected new content.
11. Test the game with a copy of the prior save.
12. Record the package version and new evidence.

Use `--input` for explicit rewrite input. Use the positional input path for analysis. See [add-game.md](add-game.md) for extraction commands. Preserve the prior package content, rewritten modules, saves, and IPA.

## Change graphics or shaders

Use profile fields for per-game shader bindings, vertex attribute types, and window origin. Keep `SS_SHADER_KEY_CODE` and `SS_NVN_LOWER_LEFT_ORIGIN` consistent with the selected shader path.

Rebuild shader inputs only when the task changes game content, shader capture, translation tools, or shader options:

```sh
.touchable/venv/bin/python internal/tools/extract_shaders.py --game "$TASK_GAME"
.touchable/venv/bin/python internal/tools/build_shaders.py --game "$TASK_GAME" --ios
```

`--ios` builds Mac and iOS libraries. The build writes `chain.json`, `shader_map.tsv`, and the game's generated shader chain header. Regenerate the iOS project after the chain changes. Preserve old shader chains needed for comparisons.

For render pass changes, compare the same scene and input with the previous behavior. `DM_NVN_RULER=1` supports an in-process image comparison. `DM_NVN_UBV=0` disables uniform snapshots for comparison. `DM_NVN_CLEAR_KEEP=0` lands pending clears in their own pass again when the same targets are set twice. `SS_QUEUE_WAIT_CPU=1` restores the prior queue wait behavior.

The letterbox ambient bars use `DM_NVN_AMBIENT_BARS`.
The value is a 0 to 1 strength.
0 is off.
The present pass samples the picture edge and draws only the top/bottom or left/right bars.
Set it in `profile.environment` for one game.
Check a 16:9 full-screen case and a non-16:9 window.
Check a bright white screen, not only a dark scene.
Use `DM_NVN_SHOT_OUT=<width>x<height>` with a windowless host to capture the bars at a chosen size.

The display rate uses `SS_DISPLAY_HZ` in `profile.environment`.
The value is 60 (default) or 120.
Set 120 only for a game whose game time follows real time.
Compare the same host steps at 60 Hz and 120 Hz first.
On an iPhone, check the `Display event rate` lines in `Documents/Logs/game.log`.

Use `DM_RES_CENSUS=2` for retained Metal resource diagnosis. Record memory across repeated level entry, play, completion, and return to menus. Check both games after a shared resource lifetime change.

Build only the upstream graphics test targets that apply to the change. For example:

```sh
make -C .touchable/dependencies/d2r-mobile/dm/gfx/tests t_reorder_chain
MTL_DEBUG_LAYER=1 .touchable/dependencies/d2r-mobile/dm/gfx/tests/t_reorder_chain
```

Consult that directory's Makefile for the complete suite. The existing build 8 report lists sample-data and abort limits for two tests. Record actual failures and compare them with the prior baseline.

## Run native runtime checks

The Python discovery command excludes the C tests. Use the relevant C test's build instructions when provided. Otherwise, inspect its include files and link dependencies. Create its output directory first.

Examples for the audio mixer and Silksong display:

```sh
mkdir -p .touchable/build/checks/common .touchable/build/games/silksong/tests
xcrun clang -Wall -Wextra -pthread -Iinternal/runtime/common \
  internal/tests/common/test_audren.c internal/runtime/common/ss_audren.c \
  -o .touchable/build/checks/common/test_audren
.touchable/build/checks/common/test_audren
xcrun clang -Wall -Wextra -Werror \
  -Iinternal/runtime/common -I.touchable/dependencies/d2r-mobile/dm/host \
  internal/games/silksong/tests/test_display.c -o .touchable/build/games/silksong/tests/test_display
.touchable/build/games/silksong/tests/test_display
.touchable/venv/bin/python internal/games/silksong/tests/test_viewport.py
```

Select the clock, thread suspension, haptics, memory, or branch probe checks when those components change.

When the Switch-style touch layout changes, run the layout check. It covers phone, tablet, and small iPad window sizes:

```sh
xcrun clang -Wall -Wextra -Werror -Iinternal/platform/ios \
  internal/tests/common/test_touch_layout.c -o .touchable/build/checks/common/test_touch_layout
.touchable/build/checks/common/test_touch_layout
```

When the app-language mapping changes, run the language check:

```sh
xcrun clang -Wall -Wextra -Werror -Iinternal/runtime/common \
  internal/tests/common/test_language.c -o .touchable/build/checks/common/test_language
.touchable/build/checks/common/test_language
```

`test_world_projection.py` is a v0 regression test. Its default `.touchable/work/silksong/phone-features/` path is a compatibility link to the retained v0 modules. An explicit command is `.touchable/venv/bin/python internal/games/silksong/tests/test_world_projection.py --modules .touchable/work/silksong/patched`. Do not count it as v655360 projection evidence. New camera changes need checks for the exact active module layout.

## Generate, build, and package the iPhone or iPad app

Read `product_name`, `bundle_id`, and `build_version` from the selected manifest. For this example, set `TASK_PRODUCT` to `Silksong`. For Rhythm Heaven, use `RhythmHeaven` and set `TASK_GAME` to `rhythm-heaven`.

With verified modules and iOS shaders available:

```sh
TASK_PRODUCT=Silksong
.touchable/venv/bin/python internal/tools/prepare_ios.py --game "$TASK_GAME"
xcodebuild -project ".touchable/build/games/$TASK_GAME/ios-project/$TASK_PRODUCT.xcodeproj" \
  -scheme "$TASK_PRODUCT" -configuration Debug -sdk iphoneos \
  -derivedDataPath ".touchable/build/games/$TASK_GAME/ios" \
  -allowProvisioningUpdates DEVELOPMENT_TEAM=YOUR_TEAM build
.touchable/venv/bin/python internal/tools/package_ios.py --game "$TASK_GAME"
```

Replace `YOUR_TEAM` with the local development team. Check the actual signing identity, profile expiry, and registered device. If automatic signing is unavailable, use the selected game's recorded manual signing procedure.

The generator stages module tables and the current shader chain. It removes stale generated tables and chains. Change source configuration or generator code when the output is wrong.

The packager verifies the signed app and Bundle ID. It creates an IPA, a JSON report, and a SHA-256 file. It refuses to replace an existing IPA. Increase `build_version` for a new distributable build, or select a new `--output` path for a local packaging trial. Packaging records profile expiry and optional device membership; inspect the report before installation.

The same project supports iPhone and iPad. When you change the app shell, check the iPad orientation keys, the full-screen setting, and the touch layout on an iPad. The simulator SDK can lack the Metal 4 headers that the runtime needs. A simulator failure does not replace a device build result. Record both outcomes.

The system press response of Liquid Glass controls runs only for real touches. Setting `highlighted` in code does not show it. To check a press look in a simulator, send real touches with an XCUITest and record the screen with `xcrun simctl io <device> recordVideo`. Put an Apple reference control next to the changed control. The tools are in `.touchable/work/silksong/reports/glass-press-20261019/tools/`.

Declare game-supported languages in `ios.localizations`.
The generator writes the list to `CFBundleLocalizations`.
Read the app language from `NSLocale.preferredLanguages`.
Map each language tag to a game language code.
Check the Settings entry on the target device.
Test representative languages and one unsupported language.
A launch-argument test is not a Settings tap test.
Record both results.

Set `ios.controller_haptics` to `true` for a game that needs controller vibration.
The generator writes `TouchableControllerHaptics` to the Info.plist.
Use a controller with haptics.
Do not treat accepted haptic API calls as physical feedback.
Record the controller model and the user's physical result.

If the task includes installation, back up the relevant application saves before replacement. Use the selected game identity. Read `Documents/Logs/game.log` after the device test. Record the app build, device, OS, tested behavior, and save result.

## Keep upstream changes reproducible

Check the existing upstream diff before you edit it:

```sh
git -C .touchable/dependencies/d2r-mobile status --short
git -C .touchable/dependencies/d2r-mobile diff --stat
```

The existing modified files contain project changes. Preserve them. After an upstream change, update `internal/patches/d2r-mobile.patch` against the pinned revision. Include new source files in the patch. A plain `git diff` omits untracked files. Review the patch before replacement and check that it applies to a clean copy of the pinned source.

Use the same procedure for `internal/patches/hactool.patch` when hactool changes. Do not reset the upstream working tree to obtain a clean test copy.

## Record completion

Store detailed local evidence in `.touchable/work/<id>/reports/`. Keep source documentation free of keys, save contents, and signing secrets.

Use this completion record in the task report or relevant game record:

```text
Task and date:
Game ID, package version, and app build:
Changed source files:
Input module identities and save fixture:
Checks run and observed results:
Mac, iPhone, or iPad platform and device:
App language choices and fallback:
Controller model and physical haptics result:
Evidence paths:
Checks not run and known limits:
Next required action, if any:
```

For documentation, check all local links and referenced scripts. For implementation, include the observed behavior that establishes the fix. Keep untested device behavior explicit. Update the game record, project summary, or workflow when the task changes their facts.

### Display service boundary regression

Run from the project root:

```sh
mkdir -p .touchable/build/checks
xcrun clang -Wall -Wextra -Werror -Wno-unused-function -pthread \
  -I.touchable/dependencies/d2r-mobile/dm/host \
  internal/tests/common/test_platform_event.c \
  -o .touchable/build/checks/test_platform_event
.touchable/build/checks/test_platform_event
```

Add `-fsanitize=address,undefined` for the native sanitizer check.
This tests service registration and output behavior. It uses an event-boundary recorder.
It does not replace a real guest SDK or device startup test.
