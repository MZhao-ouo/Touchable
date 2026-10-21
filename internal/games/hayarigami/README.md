# Hayarigami 1, 2 and 3

Status: not-adapted. This record does not establish host or iPhone playability.

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
