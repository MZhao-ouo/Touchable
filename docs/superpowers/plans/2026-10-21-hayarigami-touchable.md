# Hayarigami on Touchable Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans in this session. Track each checked result. The user approved toolchain preparation and actual startup in the design review.

**Goal:** Run Hayarigami on Touchable and establish the actual path to an installable, playable iPhone app.

**Architecture:** Add a title adapter. Use Touchable shared services, Metal and iOS shell. Keep the old project unchanged.

**Tech Stack:** C, Objective-C, Python, ARM64 rewrite, Metal, VideoToolbox, AudioToolbox, Xcode.

**Spec:** `docs/superpowers/specs/2026-10-21-hayarigami-touchable-design.md`

## Global constraints

- New clone revision: `476fb39a05d4c5b56128ffe8299635a8e82036c5`.
- Work on `adapt/hayarigami` in the user-requested fresh clone.
- Use `.touchable/venv/bin/python` for project tools. Pass `--game hayarigami`.
- Use the pinned dependency and record patch changes.
- Use Bundle ID `com.guiyu.TouchableHayarigami` and `Documents/Saves/Hayarigami`.
- Keep game inputs, keys, saves, device records and signed output out of Git.
- Verify module identity. Do not execute `rtld`.
- Preserve the old app, source, saves and generated artifacts.
- Keep iOS 26.0 and macOS 26.0 as the initial floors.

## Review focus

- A wrong package or module must fail before a fixed-address adapter runs.
- A malformed embedded shader must not read outside the module.
- A pending media buffer must survive until its consumer finishes.
- A new game must not share app identity or save namespace with another game.
- A host timeout or successful build must not be reported as iPhone gameplay.

## Task 1: Pinned tools and title configuration

**Files:** `internal/games/hayarigami/{game.json,profile.json,README.md}`, `internal/tests/test_hayarigami.py`.
**Consumes:** The approved design and original NSP/NSO input.
**Produces:** `load_game('hayarigami')` with verified title and module identity.

- [x] Run reviewed bootstrap. Verify dependency revisions and applied patches.
- [x] Run the original registry and Python baseline. Preserve the output.
- [x] Add registry tests for unique app/save IDs, matching module lists, pinned module identities and an initial non-playable status. Watch the missing-game test fail.
- [x] Use `games.py new` to register the title. Fill the profile from newly checked original modules.
- [x] Place local input links under ignored `MyGameFiles`; stage extraction in a new local path.
- [x] Run `analyze_game.py --game hayarigami`. Verify all segment hashes and title evidence.
- [x] Run registry and Python tests. Commit the verified configuration and tests.

## Task 2: CPU rewrite and first real startup

**Files:** `internal/games/hayarigami/runtime/hy_game.c`, game manifest, game tests, game README.
**Consumes:** Task 1 profile and original modules.
**Produces:** New Touchable rewrite output and an observed native startup stop.

- [ ] Test that the title adapter uses the three intended modules and rejects a different module identity before any fixed-address change.
- [ ] Implement `touchable_game_install(void)` and `touchable_game_host_call(int, uint64_t *)`. Start without speculative fixed-address patches.
- [ ] Configure shared sources from the existing native-game path. Use the shared Switch touch layout.
- [ ] Set `in-progress` with explicit runtime, iOS, rewrite and shader adapter fields. Do not claim shader support that is not implemented.
- [ ] Run `rewrite_game.py --game hayarigami` into a new output directory. Read structure and equivalence logs.
- [ ] Build the Mac host. Run it with a separate empty save directory and a bounded timeout.
- [ ] Record the exact first unsupported operation. Reproduce it with a test before each runtime change.
- [ ] Commit verified source and evidence summaries. Keep raw logs local.

## Task 3: Embedded shader input and Metal output

**Files:** `internal/tools/extract_shaders.py`, `internal/games/hayarigami/profile.json`, shader parser tests and game record.
**Consumes:** Original module images and Task 2 observed graphics calls.
**Produces:** Touchable shader captures, chain and actual Metal frame evidence.

- [ ] Add synthetic parser tests for bounds, control/code lengths, stage mismatch and duplicate keys. Watch the new extraction path fail.
- [ ] Add `hayarigami-nvn` extraction using verified embedded stage records. Keep generic dispatch small.
- [ ] Use the existing `nvn` translator where inputs match. Add tests before any translator change.
- [ ] Extract and build Mac/iOS libraries. Keep unsupported stages explicit.
- [ ] Check the actual first draw and screenshot. Correct binding/origin only with ABI or pixel evidence.
- [ ] Test both existing games after any shared graphics change. Record unavailable assets.

## Task 4: Movies, input and saves

**Files:** Game runtime media bridge and tests; shared sources only where the behavior belongs to all games.
**Consumes:** Actual native startup and title/intro calls.
**Produces:** Story entry, repeated input, movie transition and save/reload evidence on the host.

- [ ] Reproduce each missing movie interface with a synthetic input/buffer test.
- [ ] Implement the smallest verified media bridge. Cover NV12 stride, timestamps, B-frames, AAC and lifetime.
- [ ] Test repeated button presses through intro exit and story advancement.
- [ ] Save to an empty slot. Quit and reload from a separate test save directory.
- [ ] Run applicable native tests and shared-game regressions. Record exact scope.

## Task 5: iPhone package and acceptance

**Files:** Game manifest/profile, shared generator only if required, game README and project summary.
**Consumes:** Verified host adapter, shaders and local signing identity.
**Produces:** A signed IPA whose exact build has passed iPhone checks.

- [ ] Generate the Xcode project with `prepare_ios.py --game hayarigami`.
- [ ] Build and sign with the authorized identity. Verify independent bundle and save paths.
- [ ] Install the exact build. Verify installed version and boot log.
- [ ] Test story entry, repeated virtual buttons, movie exit, audio and foreground recovery.
- [ ] Measure continuous frame timing without debugger pauses.
- [ ] Verify a manual save, cold launch and reload on that device build.
- [ ] Export with `package_ios.py --game hayarigami`. Verify signature, profile expiry, archive and hash.
- [ ] Update status only to the level supported by evidence. Run final review before integration.
