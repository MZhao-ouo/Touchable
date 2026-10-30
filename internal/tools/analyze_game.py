#!/usr/bin/env python3
"""Check NSO integrity and inventory dependencies. This does not execute the game."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

import lz4.block

from game_formats import GameError
from workspace import ROOT, DEPENDENCIES, UPSTREAM, load_game


def dynamic_symbol_extents(image):
    """Read the mandatory dynamic tables when optional NSO header extents are absent."""
    if len(image) < 8:
        raise GameError("Missing MOD0 pointer.")
    mod = struct.unpack_from('<I', image, 4)[0]
    if mod > len(image) - 8 or image[mod:mod+4] != b'MOD0':
        raise GameError("Invalid MOD0 header.")
    offset = mod + struct.unpack_from('<i', image, mod+4)[0]
    dynamic = {}
    while 0 <= offset <= len(image) - 16:
        tag, value = struct.unpack_from('<qQ', image, offset)
        offset += 16
        if not tag:
            break
        dynamic[tag] = value
    else:
        raise GameError("Unterminated NSO dynamic table.")
    if not all(tag in dynamic for tag in (4, 5, 6, 10)) or dynamic.get(11, 24) != 24:
        raise GameError("Unsupported NSO dynamic symbol tables.")
    if dynamic[4] > len(image) - 8:
        raise GameError("Invalid NSO dynamic hash table.")
    buckets, count = struct.unpack_from('<II', image, dynamic[4])
    if not buckets or not count or (2 + buckets + count)*4 > len(image) - dynamic[4]:
        raise GameError("Invalid NSO dynamic symbol count.")
    return dynamic[5], dynamic[10], dynamic[6], count*24


def load_nso(path, include_image=False):
    data = path.read_bytes()
    if len(data) < 256 or data[:4] != b"NSO0":
        raise GameError("Not an NSO: " + path.name)
    flags = struct.unpack_from("<I", data, 12)[0]
    segments = [struct.unpack_from("<III", data, 0x10 + 16 * i) for i in range(3)]
    end = max(memory + size for _, memory, size in segments)
    if end > 1024**3:
        raise GameError("NSO image exceeds the analysis limit.")
    image = bytearray(end)
    hashes = []
    for index, (offset, memory, size) in enumerate(segments):
        compressed = bool(flags & (1 << index))
        stored = struct.unpack_from("<I", data, 0x60 + 4 * index)[0] if compressed else size
        if offset < 256 or offset + stored > len(data):
            raise GameError("NSO segment extends beyond the file: " + path.name)
        segment = data[offset:offset + stored]
        if compressed:
            segment = lz4.block.decompress(segment, uncompressed_size=size)
        if len(segment) != size:
            raise GameError("NSO segment has an invalid size: " + path.name)
        digest = hashlib.sha256(segment).digest()
        verified = bool(flags & (1 << (index + 3)))
        if verified and digest != data[0xa0 + 32 * index:0xc0 + 32 * index]:
            raise GameError("NSO section hash mismatch: " + path.name)
        hashes.append({"segment": ("text", "rodata", "data")[index], "sha256": digest.hex(), "header_hash_checked": verified})
        image[memory:memory + size] = segment
    # NSO dynamic string and symbol extents are relative to rodata.
    strings_offset, strings_size = struct.unpack_from("<II", data, 0x90)
    symbols_offset, symbols_size = struct.unpack_from("<II", data, 0x98)
    rodata = segments[1][1]
    strings_offset += rodata
    symbols_offset += rodata
    if not strings_size or not symbols_size:
        strings_offset, strings_size, symbols_offset, symbols_size = dynamic_symbol_extents(image)
    if strings_offset + strings_size > len(image) or symbols_offset + symbols_size > len(image) or symbols_size % 24:
        raise GameError("Invalid NSO symbol table: " + path.name)
    strings = image[strings_offset:strings_offset + strings_size]
    symbols = []
    for offset in range(symbols_offset, symbols_offset + symbols_size, 24):
        name_offset, info, other, section, value, size = struct.unpack_from("<IBBHQQ", image, offset)
        if not name_offset:
            continue
        end_name = strings.find(0, name_offset)
        if end_name < 0:
            raise GameError("Unterminated symbol name: " + path.name)
        name = strings[name_offset:end_name].decode("utf-8")
        symbols.append({"name": name, "defined": bool(section), "weak": (info >> 4) == 2,
                        "binding": info >> 4, "type": info & 15, "value": value, "size": size})
    result = {
        "name": path.name, "sha256": hashlib.sha256(data).hexdigest(),
        "build_id": data[0x40:0x60].hex(), "file_bytes": len(data),
        "image_bytes": len(image), "bss_bytes": struct.unpack_from("<I", data, 0x3c)[0],
        "segments": hashes, "symbols": symbols,
    }
    if include_image:
        result['image'] = bytes(image)
    return result


def analyze(game, upstream, output, config=None):
    modules = [load_nso(p) for p in sorted((game / "ExeFS").iterdir()) if p.name in ("main", "sdk", "rtld") or re.fullmatch(r"subsdk\d+", p.name)]
    if not any(m["name"] == "main" for m in modules):
        raise GameError("ExeFS/main was not found.")
    exports = {}
    for module in modules:
        for symbol in module["symbols"]:
            if symbol["defined"] and symbol["binding"] in (1, 2):
                exports.setdefault(symbol["name"], []).append(module["name"])
    hle, forced = set(), set()
    for directory in (upstream / "dm/host", upstream / "dm/gfx", upstream / "dm/gfx/nvn"):
        for path in directory.glob("*.c"):
            source = path.read_text()
            hle.update(re.findall(r'\bHLE\(\s*"([^"\n]+)"', source))
            forced.update(re.findall(r'\bHLE_FORCE\(\s*"([^"\n]+)"', source))
    libc = (upstream / "dm/host/hle_table.c").read_text()
    hle.update(n for n in re.findall(r'\bL\((\w+)\)', libc) if n != "n")
    if config:
        for path in (*config.common_runtime_sources, *config.runtime_sources):
            if not path.is_file(): continue
            source = path.read_text()
            hle.update(re.findall(r'\bHLE\(\s*"([^"\n]+)"', source))
            forced.update(re.findall(r'\bHLE_FORCE\(\s*"([^"\n]+)"', source))
    imports, missing = [], []
    for module in modules:
        for symbol in module["symbols"]:
            if symbol["defined"]:
                continue
            name = symbol["name"]
            row = {"module": module["name"], "symbol": name, "weak": symbol["weak"],
                   "guest_providers": exports.get(name, []), "upstream_hle_name_match": name in hle}
            imports.append(row)
            if not row["guest_providers"] and not row["upstream_hle_name_match"] and not symbol["weak"]:
                missing.append(row)
    romfs = game / "RomFS"
    assets = [p for p in romfs.rglob("*") if p.is_file()]
    managers = romfs / "Data/globalgamemanagers"
    version_match = None
    if managers.is_file():
        with managers.open("rb") as stream:
            version_match = re.search(rb'(?:20\d\d|[456]\d{0,3})\.\d+\.\d+[^\x00]{0,60}', stream.read(256))
    metadata = romfs / "Data/Managed/Metadata/global-metadata.dat"
    il2cpp_version = None
    if metadata.is_file():
        with metadata.open("rb") as stream:
            magic, version = struct.unpack("<II", stream.read(8))
        if magic == 0xfab11baf:
            il2cpp_version = version
    boot_names = set(re.findall(r'\breq\("([^"\n]+)"\)', (upstream / "dm/host/main.c").read_text()))
    report = {
        "game_id": config.id if config else None,
        "title_id": config.title_id if config else None, "game_directory": str(game),
        "unity_version": version_match[0].decode() if version_match else None,
        "il2cpp_metadata_version": il2cpp_version,
        "managed_dll_count": sum(p.suffix.lower() == ".dll" for p in assets),
        "asset_file_count": len(assets), "asset_bytes": sum(p.stat().st_size for p in assets),
        "modules": modules, "imports": imports, "unresolved_strong_imports": missing,
        "upstream_boot_symbols_missing": sorted(boot_names - exports.keys()),
        "upstream_force_names_present": sorted(forced & exports.keys()),
        "status": "analysis only; no host execution or iPhone compatibility test",
        "limitation": "Symbol name matches do not prove ABI, platform-service, or graphics compatibility.",
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "game.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    with (output / "imports.tsv").open("w") as stream:
        stream.write("module\tsymbol\tweak\tguest_providers\tupstream_hle_name_match\n")
        for row in imports:
            stream.write("\t".join((row["module"], row["symbol"], str(row["weak"]), ",".join(row["guest_providers"]), str(row["upstream_hle_name_match"]))) + "\n")
    summary = {k: report[k] for k in ("unity_version", "il2cpp_metadata_version", "managed_dll_count", "asset_file_count", "asset_bytes", "upstream_boot_symbols_missing")}
    summary["module_count"] = len(modules)
    summary["import_count"] = len(imports)
    summary["unresolved_strong_import_count"] = len(missing)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, nargs="?", help="Extracted directory. Default: the configured content directory.")
    parser.add_argument("--game", default="silksong", help="Registered game ID.")
    parser.add_argument("--upstream", type=Path, default=UPSTREAM)
    parser.add_argument("--out", type=Path, help="Report directory. Default: work/<game>/reports.")
    args = parser.parse_args()
    try:
        config = load_game(args.game)
        analyze((args.input or config.content_dir).resolve(), args.upstream.resolve(),
                (args.out or config.reports_dir).resolve(), config)
    except (GameError, OSError, ValueError, struct.error, lz4.block.LZ4BlockError) as exc:
        print("Error: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
