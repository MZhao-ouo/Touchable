# Hayarigami 1, 2 and 3

Status: in-progress. Development builds are enabled. This record does not establish iPhone playability.

## Input

- Title ID: `010095b01af94000`.
- Base package version: 0.
- Nintendo SDK: 14.3.0.
- Executable modules: `main`, `subsdk0`, `sdk`.
- `rtld` is an input for identity checks. The host does not execute it.
- App Bundle ID: `com.guiyu.TouchableHayarigami`.
- iPhone save path: `Documents/Saves/Hayarigami`.

The profile pins the original module SHA-256 and Build IDs.
The main module's Build ID is all zeroes. Its SHA-256 remains pinned.
Do not use another game's fixed-address adapter.

## 2026-10-21 input and analysis checks

The work starts from Touchable revision `476fb39`.
The pinned d2r-mobile dependency and its Touchable patch were prepared in this clone.
The original registry check and 48 Python tests passed.

The normal full extraction command stopped at its free-space precheck.
It requires eight times the 4.44 GB package size. The disk had about 24 GiB free.
No protected package or prior app was deleted.
Original ExeFS and RomFS files were staged as independent APFS copies of the previous extraction.
SHA-256 was checked for all 35 copied files.
The current NSP container bounds and decrypted NCA title headers were checked again.
This is not a fresh full extraction or a new content-signature check.

Touchable analysis checked all 12 NSO segment hashes.
It found 30 RomFS files, 4,423,579,523 bytes, and no Unity assets.
After a MOD0 symbol-table correction, the analysis finds all boot exports and no unresolved strong imports.
Matching names do not prove ABI compatibility.

The correction reads bounded MOD0 dynamic symbol tables when optional NSO header extents are absent.
Synthetic tests cover an export, an out-of-range symbol table, a large symbol count and an unterminated dynamic table.
All 53 Python tests pass.

Local evidence is under `.touchable/work/hayarigami/reports/`.
Game inputs, keys, extracted files, logs and builds remain outside Git.

## Current acceptance scope

Build 7 is installed and launched on iPhone 16 after device unlock.
The user confirmed a visible title/menu and an A-triggered page change.
Build 6 rendered the collection title during the user test.
The user confirmed that touch A opens the three-game selector.
The Mac host also reached part 1 story text. This does not establish complete-game support.
The source build 7 adds an original icon and a pipeline-cache stability fix.
The user authorized updating the phone after the regression checks.

Remaining checks are manual save and cold reload, part 2/3 story flow, audio/video synchronization, background/resume on device and long sessions.
One extracted geometry shader is unsupported. No test requests it so far.
Do not mark the adapter `adapted` until its acceptance checks are recorded.

See [the approved design](../../../docs/superpowers/specs/2026-10-21-hayarigami-touchable-design.md).

## Native rewrite, shaders and iOS build

Touchable generated new rewrites of main, subsdk0 and sdk.
All structure and instruction-equivalence checks passed, with 100 mutations per module.
The new embedded shader extractor validates 128 records in main.
It builds 127 vertex/fragment records and reports one unsupported geometry record.
A request to use that unsupported stage is not supported.
Mac and iOS libraries were built through Touchable, with chain `2a45343d04c6`.
The shared runtime and the new title adapter compiled into the Mac host.

The first real Mac launch exited with -6 in dyld.
The machine runs macOS 15.7.5. The backend requires macOS 26 Metal 4 classes.
The log names missing `MTL4ArgumentTableDescriptor`. No guest code ran.
The host result does not test game startup or graphics.

The generated iOS 26 app passed unsigned compilation and linking.
After the user signed in to Xcode, development signing also passed.
Its Bundle ID is `com.guiyu.TouchableHayarigami`, build 1.
Deep and strict signature verification passed. The profile expires on 2026-10-28 at 10:07:08 UTC.
Device installation and execution checks are in progress.

Code review found an interrupted shader extraction could leave a completed control marker without code.
The extractor now writes code first. An interrupted-write/retry regression failed before the fix and passes after it.
All 58 Python tests pass.

## First iPhone startup and six-channel reverb

Build 1 installed on the iPhone with its independent Bundle ID.
The actual log shows relocation, SDK initialization, constructors and `BOOT nnMain`.
It opened AudioRenderer at 48 kHz, then aborted at `Cannot add an I3DL2 reverb with 6 channels`.
The mixer already has six-channel routing buffers. Its effect-allocation guard still limited reverb to two channels.
The guard now uses `SS_AR_CHANNELS` as its limit. The existing approximate reverb algorithm is unchanged.
A synthetic regression checks six distinct PCM channels through disabled and enabled dry reverb routing to stereo.
It fails at effect allocation before the fix and passes after it.
Existing stereo tests, ASan/UBSan, TSan and all 58 Python tests pass.
This test does not establish exact console wet-reverb behavior or audible phone output.
Build 2 signed and installed successfully. It passed the reverb allocation point.
It then crashed in SDK `AddDelay`, at `EffectManager::IsInitialized`.
The native SDK cannot use the host AudioRenderer configuration as its own manager.

The clone has no Silksong or Rhythm Heaven packages. Their actual host regressions could not run.
This Mac also cannot run their Metal 4 backend. No gameplay regression pass is claimed for either game.

## Delay support

The shared renderer now handles the imported delay allocation, routing, time, gain, enable and removal calls.
The delay supports up to six channels and the observed eight-second maximum buffer.
Synthetic PCM tests check delay time, feedback, six-channel routing, removal and slot reuse.
Review found that slot reuse could change effect execution order. A failing chain regression reproduced it.
The renderer now preserves allocation order separately from storage slots. The regression passes.
ASan/UBSan and TSan checks pass. Existing mixer tests and all 58 Python tests pass.
The low-pass and wet signal are approximations. Console-identical defaults and phone audio remain unverified.
Build 3 is the next device candidate. No title screen or playable scene has been verified in this framework.

Shader staging no longer copies source file attributes. This avoids failure when iCloud compression flags cannot be set on generated files. A synthetic attribute-copy failure test fails before the change and passes after it. All 59 Python tests pass. All 381 iOS shader files were read locally before rebuilding.

## 2026-10-29 continuation: display resolution services

The referenced ChatGPT conversation has no project attachment or URL.
This repository is the independent Touchable adaptation from the approved design.
Its real path is `/Users/vim/LocalDocuments/Development/hayarigami-ios/Touchable`.
The existing `adapt/hayarigami` branch was clean at revision `2693867`.
The user confirmed ownership and authorized local use of the existing game inputs.
All 35 extracted files match the prior provenance SHA-256 records.
All four NSO hashes and Build IDs match the profile. All 12 segment hashes pass.
The resources include 22 `PS_FS_V1` archives and MP4 movies in the three FOAF directories.
No Unity assets were found. A specific commercial engine name is not established.
This is not a fresh NSP extraction or a fresh content-signature check.

The build 3 device log stops at `sdk+0x3a5050` after window initialization.
The pinned SDK symbol identifies `nn::oe::GetDefaultDisplayResolutionChangeEvent`.
That function dereferences the absent console service returned through its PLT.
The shared platform now intercepts it and binds a caller-owned, auto-clear event.
A fresh Mac run passes that point and stops in the companion resolution query at `sdk+0x3a4fdc`.
The platform now intercepts that query too. It reports the fixed 1280x720 handheld display.
This does not change the game's 1920x1080 render textures or the iOS view size.
The fixed output does not signal resolution changes.
These changes use the existing kernel event bridge. They add no fixed-address patches.

The new native regressions fail without each takeover and pass with both.
They check symbol registration, caller storage, clear mode, query output bounds and guest write preparation.
The tests replace the kernel-event boundary with a recorder. They do not execute guest SDK events.
AddressSanitizer and UndefinedBehaviorSanitizer pass. All 59 Python tests pass.
The touch layout test passes 12 fixtures, including the logged iPhone 16 view and safe area.
These layout checks do not establish touch response during gameplay.

The current host is macOS 27.0.1 with Xcode 27.1. The earlier Metal 4 host limit is historical.
A fresh host build runs the game and passes both display service calls.
It then aborts at `nvnTextureBuilderGetSparseTileLayout`, which the graphics layer does not implement.
This is an actual unsupported texture operation. No title screen or story scene is verified.
The existing layout code has no sparse tile-width representation. Returning an empty success is not a fix.
Sparse support needs consistent query, allocation, layout and texture-transfer checks.

Build 4 passes full iOS compilation, linking and development signing.
Strict/deep signature verification passes. The profile expires at 2026-11-05 07:27:08 UTC.
The build contains existing Metal encoder-type warnings. No warning-free build is claimed.
The first installation failed with `MIInstallerErrorDomain` error 13 because the free development profile had three apps.
The user authorized removal of `com.local.optionslab.OptionsLabUITests.xctrunner` only.
The runner was removed. OptionsLab and D2R remain installed.
Build 4 then installed successfully. The device app inventory confirms version 0.1, build 4.
The first launch was denied because the phone was locked. The user unlocked it.
A new launch on iPhone 16, iOS 26.2.1, reached `nnMain`, audio setup and window initialization.
It passed both display service crash points, then aborted at `nvnTextureBuilderGetSparseTileLayout`.
The device exit code is 134. The game frame is `main+0x281668`.
This verifies the display fixes on the phone. It does not verify title rendering or gameplay.
The device log is `reports/20261029/device-build4-game.log` under the local game work directory.
No final or playable IPA is exported. No game files or keys are included in the source patch.
Story entry, movies, audio, save/reload and all three games remain acceptance checks.
Other game packages are absent in this clone. Their gameplay regressions were not run.

Evidence: `.touchable/work/hayarigami/reports/20261029/` and the game build log directory.

## 2026-10-29 continuation: title, movie and story rendering

Resident sparse 2D layouts now use matching query, allocation, CPU transfer and GPU transfer rules.
Packaged texture layout reads two bytes. It preserves horizontal and vertical GOB dimensions.
Unsupported sparse forms return zero size and fail texture initialization.
The shared backend records tiled-cache flushes and GPU fences. A fence waits for its submission.
This conservative wait can be slower than an exact in-stream fence.
Timestamp conversion uses a wide intermediate. Queue usage reset has no host arena counters to reset.

The title enables per-draw fragment alpha comparison at buffer 30.
All eight NVN comparisons and dynamic references have actual GPU pixel coverage.
The game profile and compile define must agree. Other games keep the existing shader path.
The SDK CommitSaveData alias uses the shared atomic save transaction.
The polling AudioOut overload does not require an event. PCM consumption and buffer release have native coverage.

The movie adapter replaces only movie::Decoder construction.
The original extractor, metadata and buffer calls remain guest code.
Apple VideoToolbox decodes AVC. AudioConverter decodes AAC.
Worker stacks, a retained keyframe window, presentation-order output and client-capacity copying are tested.
Temporary decoder unavailability after suspension retries the same packet after foreground recovery.
Direct-return and callback-error regressions failed before that fix and pass after it.
The synthetic fixtures contain color bars, a red frame and a sine wave. They contain no game media.

Build 5 installed after the user authorized reinstalling this app for storage space.
Its iPhone output was black. Its log reports unsupported Metal language 4.1 libraries.
The shader compiler now pins Metal 4.0 and the OS 26 deployment floor.
Actual emitted compiler metadata is checked. The current shader chain is `033e5e61f36e`.
Build 6 compiled, signed and installed. The actual iPhone collection title renders.
The user confirmed A opens the selector and reported normal initial operation.
Build 7 later installed as an update after the user authorized it.
No uninstall was needed. After unlock, the user confirmed a title/menu and an A-triggered page change.
The captured log reaches frame 10,200 without a fatal error.
It opens CoreAudio output and configures 1920x1080 AVC and 48 kHz stereo AAC decoders.
Resident samples are about 326 MB in the menu and 617 MB after movie entry.
This is a short startup/input check. It does not establish movie synchronization or long-session stability.
Diagnostic frame files were not saved because the update changed the data-container path.
The visual result is a user confirmation. Runtime metrics come from the copied device log.

The Mac host rendered part 1 title, prologue selection, AVC/AAC film and story text.
The frame `play8-14160.png` shows the first story line after bounded input.
An earlier film loop grew to 24 GB and exited through the memory watchdog.
The heap report found thousands of Metal pipelines and retained output data.
The decoded frame queue itself stayed bounded.
Recorded keys differ in write masks for unbound color attachments.
The cache now excludes those unused nibbles for normal passes. Merged logical masks remain intact.
The real pipeline test changed from 64 extra objects to zero. Active write masks still select different pipelines.
The output-copy call also needs its own autorelease pool on the long-lived guest thread.
The full acquire/copy/release regression failed before that change and passes for 120 HD frames after it.
A fresh two-minute film run keeps the pipeline count at three and resident memory near 546–565 MB.
The earlier run reached about 9 GB over the same interval.
This bounded check does not establish hour-long stability.
Build 7 contains both fixes and the [original app icon](ios/Artwork/README.md).
Its iOS build and strict/deep signature check pass.
The phone inventory confirms version 0.1, build 7.

The integrated Python suite has 77 checks. Native results include NVN 108, GPU deswizzle 19,
clear/cache 29 and alpha 48 pixel comparisons. Separate tests cover display services,
save commit/rollback, polling PCM, B-frame recovery and 120 HD frame lifetimes.
These are unit or bounded host results. They do not replace the remaining device acceptance checks.
Other game inputs are absent in this clone. Their gameplay regressions could not run.
Evidence remains local under `reports/20261029-sparse/` in the game work directory.

## Local build and signing

Supply your own legally held package and local keys. Follow the approved preparation workflow.
Run all commands from the repository root. Keep inputs, keys and saves outside Git.

```sh
.touchable/venv/bin/python internal/tools/games.py check
.touchable/venv/bin/python -m unittest discover -s internal/tests
.touchable/venv/bin/python internal/tools/build_shaders.py --game hayarigami --ios
.touchable/venv/bin/python internal/tools/build_host.py --game hayarigami --jobs 4
.touchable/venv/bin/python internal/tools/prepare_ios.py --game hayarigami
```

Open `.touchable/build/games/hayarigami/ios-project/Hayarigami.xcodeproj` in Xcode.
Select your own development team and the Hayarigami scheme. Select an iOS 26+ device.
Build and run. Keep the existing Bundle ID to retain this app's save namespace.
A free development profile needs renewal after its displayed expiry and has an app-count limit.
Device installation can require space for a staged second app copy.
The current user's profile expiry is 2026-11-05 07:27:08 UTC. A fresh user's profile can differ.
This repository contains code, synthetic tests and original icon artwork.
It does not contain game packages, extracted assets, keys, signing material, saves or an asset-bearing IPA.
The local-use authorization does not grant rights to publish the game's assets.

## Public main integration

The feature branch includes public main revision `8b22a49`.
It preserves the upstream TV mode, iOS settings, copy-image stride and asynchronous translation queue.
Command fences are published at enqueue time, before the worker can run.
A nested command-stream walk handles copied command buffers and jump segments.
The backend does not overwrite the published fence during older replay.
A controlled translation pause reproduced premature fresh and reused sync waits before the fix.
After the fix, both waits block and all 16,384 sampled source bytes per case stay correct.
The full NVN test reports 108 passing checks.
The launcher compares the dependency tree with pinned HEAD, including intent-to-add files.
Its plain-apply and intent-to-add regressions both pass on this Git version.
The Hayarigami manifest includes the shared health-bar data module required by the current common iOS shell.
No Hayarigami health-bar feature is enabled.


## Build 9: screen input bridge and controls outside the picture (2026-10-30)

Hayarigami selects `ios.touch_layout = native`. A dedicated UIKit surface sends one finger's down, move and up events to the original `GetTouchScreenState` APIs. It uses the 1280 x 720 touch coordinate space. The 40-byte contact and 16-byte state header follow [libnx HID definitions](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/hid.h). Short taps retain both edges between game polls. Cancellation clears queued and held contacts. Controller buttons do not generate screen touches.

The complete game picture keeps a 16:9 fit. Four direction keys and all ten controller keys use opaque backgrounds. They sit in two safe-area side rails. Short windows use bottom rows. No glass container, blur, or dimming layer is created for this layout. The picture is smaller to leave room for the controls. Other games keep their configured layouts. Build 9 keeps the Bundle ID and save directory.

Validation: native screen ABI, short tap edges, drag, cancel, all 16 capacity variants, queue overflow, concurrent producer/reader and pad isolation pass with ASan/UBSan. Ten geometry fixtures check 14 controls, safe bounds, no control/picture overlap and coordinate edges. The prior 12 controller layout fixtures and the native haptics test pass. All 77 Python tests pass. Mac host build and a 30-second isolated-save startup pass. The iOS build and strict/deep signature verification pass. Device screenshots show the opaque rails outside the title picture. Device logs record a direct screen contact. Menu selection by direct touch still needs a device acceptance check. The initial collection title says "PRESS ANY BUTTON" and did not advance from that one direct contact; A remains available in the right rail.

The final build also registers real-controller disconnect notifications in native mode. Removing the last controller restores the buttons. That hardware path has not been exercised with a real controller.

Local evidence: `.touchable/work/hayarigami/reports/20261030-touch/`. No game screenshots or user data are included in Git.

Build 8 failed device acceptance: direct cover taps did not advance the title, and releasing A raised `NSInternalInconsistencyException` because the inherited glass press reset set the button configuration to nil while a configuration update handler was active. Build 9 skips that reset for plain buttons. Build 9 compiles, passes strict/deep signature verification, and is installed on iPhone 16. The user confirmed that A works normally and accepted this version for now. Direct cover/menu tapping remains unverified and is not claimed as working.

## Build 11: continuous audio buffers (2026-10-30)

The guest imports `GetReleasedWaveBuffer` and `GetVoicePlayState`. The host now implements both. Previously, those calls used SDK object state even though host acquisition stored a host voice pointer. The initial four buffers drained, and the guest did not refill them. A device capture of about 371 seconds contained only zero samples.

Consumed buffers now retain their original guest pointer until the guest retrieves them. The tagged outstanding count bounds storage to four buffers. Stop returns both queued buffers and buffers submitted before the same update. A sanitizer regression recycles 100 buffers, checks pointer identity and nonzero output, and checks stop recovery. The existing mixer tests pass. A bounded host capture after the fix has 1,677,903 nonzero samples and peak 10,872. All 77 Python tests pass. The iOS build passes. Device testing recovered initial collection audio, but the user still reported silence in story scenes; the additional Build 12 fixes are described below.


## Build 12: story audio attachment and DSP ADPCM

Device testing of Build 11 confirmed collection audio followed by silence inside the selected game. Its mixed PCM became zero after about 35 seconds; a diagnostic run showed no allocated story voices with master volume 1. The guest at `main+0x2ba2ac` waits for `GetMemoryPoolState` to return Attached (1) before acquiring a story voice. The original SDK getter reads offset 0x10 of our host pool record, interprets the attach flag as its internal state, and returns 3 instead. The host now supplies the matching pool state, release and copy operations.

Removing that wait reproduced a second failure: the guest requests sample format 6 (DSP ADPCM), while the mixer previously supported only PCM16. Mono ADPCM now uses the sixteen supplied predictor coefficients and optional six-byte history context, retains history across buffers, and restores it for loops. PCM16 behavior remains unchanged. The public [libnx audio structures](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/audren.h) document the coefficients and context; [vgmstream's DSP decoder](https://github.com/vgmstream/vgmstream/blob/master/src/coding/ngc_dsp_decoder.c) provides an independent format reference.

Validation: the pre-fix ADPCM allocation regression fails; synthetic nibble vectors, frame boundaries, both predictor histories, context/loop restoration, stop reset, signed saturation and truncated input pass with ASan/UBSan. The existing mixer and 100-cycle buffer recycling regressions pass. All 77 Python tests and the iOS Build 12 compilation pass. Build 12 is installed and launches on iPhone 16. Actual story audibility is awaiting user confirmation; this is not a complete three-game playthrough claim. Diagnostic traces and game audio stay local and are excluded from the repository.
