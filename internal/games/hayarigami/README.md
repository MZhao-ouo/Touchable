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

## Remaining work

Implement the title adapter and verify a new CPU rewrite.
Run the Touchable Mac host and record the actual next stop.
Add embedded NVN shader extraction and confirm bindings.
Verify movie decoding, repeated input, story play and save/reload.
Build, sign, install and test the independent iPhone app.
Export an IPA only with its actual validation scope recorded.

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
