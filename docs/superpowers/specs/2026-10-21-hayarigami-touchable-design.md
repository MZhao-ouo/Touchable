# Hayarigami adaptation on Touchable

Date: 2026-10-21
Status: Approved by the user on 2026-10-21. Implementation is in progress.

## User request

Use a fresh clone of `https://github.com/guiyu/Touchable`.
Continue the Hayarigami iOS adaptation in this project.
Do not change the previous Hayarigami framework.
The existing acceptance target remains an installable IPA that can enter the game.

## Verified starting point

- Touchable revision: `476fb39a05d4c5b56128ffe8299635a8e82036c5`.
- The clone has no local product code changes.
- The project has separate game manifests, profiles, adapters, build paths and save paths.
- The project uses its patched d2r-mobile dependency for the loader and Metal graphics.
- The bootstrap pins d2r-mobile to `2eb5758771f110cd53082b999d88d242766610da`.
- The runtime executes rewritten ARM64 modules. It does not execute `rtld`.
- The shader extraction dispatcher accepts Unity assets and BNSH files.
- The upstream patch reports that its console movie decoder has no host hardware support.
- These source facts do not establish Hayarigami compatibility.

## Selected approach

Add `hayarigami` as a third game in Touchable.
Use the shared Touchable loader, services, Metal graphics and iOS app.
Keep title-specific behavior in `internal/games/hayarigami/`.
Do not place the old `hy_*` runtime behind the Touchable app shell.
Do not copy fixed addresses or engine hooks from Silksong or Rhythm Heaven.

The old project's source remains unchanged.
Use the user's original game package and authorized local keys as inputs.
Treat earlier reports as clues. Verify each module identity again.
Create new rewrite output and shader output with Touchable tools.
Do not reuse the old signed Guest framework or compiled Metal libraries as Touchable output.

A shell-only port was considered and rejected.
It would retain the old framework and would not satisfy this request.
A fork of the whole shared runtime was also rejected.
The existing game adapter boundary is sufficient for initial investigation.

## Identity and local data

- Game ID: `hayarigami`.
- Title ID to verify from the package: `010095b01af94000`.
- Product name: `Hayarigami`.
- Proposed Bundle ID: `com.guiyu.TouchableHayarigami`.
- Phone save directory: `Documents/Saves/Hayarigami`.
- Game files and keys: `MyGameFiles/`, ignored by Git.
- Extracted content and reports: `.touchable/work/hayarigami/`.
- Builds and logs: `.touchable/build/games/hayarigami/`.
- Local test saves: `.touchable/saves/hayarigami/` with a separate directory for each test.

The proposed Bundle ID permits side-by-side comparison with the old app.
Do not migrate or overwrite the old app's saves in this work.
Use the previously authorized signing identity only when the offline build is ready.
Keep all signing material and device records local.

## Adaptation boundaries

### Configuration and CPU

Create `game.json`, `profile.json` and a game record.
Keep the initial status `not-adapted`.
Record package version, module SHA-256 and Build IDs from the actual input.
Expect `main`, `subsdk0` and `sdk`; confirm this before selecting the runtime modules.
Use the generic `switch-native` rewrite procedure only after its identity checks pass.
Run structure and instruction-equivalence checks on the new output.
Implement `touchable_game_install()` and `touchable_game_host_call()` for this title.
Set `in-progress` only when the configured adapter permits development builds.

### Graphics and shaders

Inspect this title's embedded NVN control and code layout.
Add a dedicated extraction adapter to `internal/tools/extract_shaders.py`.
Test bounds, stage agreement, lengths, duplicate keys and malformed input.
Use Touchable's shader key and chain format.
Record CB, texture, sampler, vertex layout and window-origin evidence.
Use the existing `nvn` build adapter where its supported inputs match.
Reject an unsupported stage or binding explicitly.
Do not mark all stages compatible because one draw succeeds.
Begin with the project's default render path. Enable optional pass optimizations only after image comparison.

### Media and audio

Check the actual movie factory and decoder interfaces from this title.
Add a title-specific media bridge if the shared runtime cannot execute them.
Use VideoToolbox and AudioToolbox only with verified buffer and timestamp contracts.
Test caller-owned output, NV12 stride, B-frame order, AAC, packet lifetime and background recovery.
Use host worker stacks for Apple media callbacks when the guest stack is unsuitable.
Do not treat a skipped movie as successful movie support.

### Input, saves and iOS

Use the shared Switch touch layout first.
Use the shared controller and audio-session paths.
Do not add title-specific touch UI, aspect changes or 120 Hz support in the first pass.
Verify shared save service behavior against real calls.
Generate the Xcode project with `prepare_ios.py`.
Export the signed archive with `package_ios.py`.
Do not edit generated projects.

## Validation order

1. Check both existing game configurations and the Python test baseline.
2. Check package identity and each module's integrity.
3. Verify the new CPU rewrite with structure and equivalence tests.
4. Start a bounded Mac host run with a separate save directory.
5. Record the first actual unsupported operation. Add a regression before each fix.
6. Verify shader conversion, actual Metal output and pixel layout.
7. Enter the collection title, first game title and story on the host.
8. Verify movie skip, repeated input, audio and save/reload.
9. Generate and sign the independent iOS app.
10. Install the exact build on iPhone. Repeat the same flow without debugger pauses.
11. Measure frame timing on the device. Check sustained play and foreground recovery.
12. Check the IPA signature, provisioning scope, expiry and archive integrity.

A shared runtime or graphics change requires both existing game regressions.
If their game files are unavailable, report that gap. Do not claim those regressions passed.
Each result must identify the game, module version, build, platform and save fixture.
Keep host, compile, installation and gameplay evidence separate.
Set `adapted` only after recorded game tests support that status.

## First implementation milestone

Prepare Touchable's pinned local toolchain.
Register this title with its verified profile.
Run the new CPU rewrite and a bounded native startup.
Deliver the exact next unsupported operation and its log.
This milestone is not an IPA or a playability claim.

## Risks and completion gate

Shared SDK exports can have different ABI layouts.
The existing movie failure path cannot be assumed sufficient for this title.
The old project's shader output cannot be assumed compatible with Touchable's chain.
The old project's device results do not validate the new app.

The final gate is actual iPhone story play, repeated virtual input, movie transition,
manual save, cold launch and save reload in the same build as the delivered IPA.
Keep the build a candidate until these checks pass.
