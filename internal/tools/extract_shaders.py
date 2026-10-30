#!/usr/bin/env python3
"""Read Switch shader programs from local Unity assets for a registered game."""
import argparse
from pathlib import Path
import hashlib
import json
import struct
import sys

from workspace import GameError, load_game


def scan(blob, source, name, data_dir, output, records, verified, added):
    position = 0
    while True:
        start = blob.find(bytes.fromhex('34127698'), position)
        if start < 0: return
        position = start + 4
        if start + 0x880 > len(blob): continue
        control = blob[start:start+0x880]
        constant, size, sph = struct.unpack_from('<III', control, 0x700)
        stage = struct.unpack_from('<I', control, 0x714)[0]
        if stage not in (0, 1, 5) or not 0 <= sph < constant <= size <= 4*1024*1024: continue
        if start + 0x880 + size > len(blob): continue
        code = blob[start+0x880:start+0x880+size]
        if stage != 5 and (struct.unpack_from('<I', code, sph)[0] >> 10) & 15 != {0: 1, 1: 5}[stage]: continue
        key = hashlib.sha256(control).hexdigest()[:16]
        path = output/(key+'.control')
        if path.exists():
            if path.read_bytes() != control or path.with_suffix('.code').read_bytes() != code:
                raise GameError('Shader capture differs from asset: '+key)
            verified.add(key)
        else:
            path.with_suffix('.code').write_bytes(code)
            path.write_bytes(control)
            added.add(key)
        records.append(dict(source=str(source.relative_to(data_dir)), name=name, offset=start, key=key, stage=stage))
        position = start + 0x880 + size


def shader_key(config, control, code):
    """Match runtime/common/ss_shader_capture.c for the selected game."""
    data = control + code if 'SS_SHADER_KEY_CODE' in config.compile_defines else control
    return hashlib.sha256(data).hexdigest()[:16]


def bnsh_stages(blob):
    """Return (control, code) pairs from an nn::gfx BNSH file.

    Each NVN stage record is {u64 control offset, u64 code offset, u32 code size, u32 control size}.
    A record is accepted only when it points at a valid 0x880-byte control block whose own code size,
    constant offset, and stage agree with the record.
    """
    if len(blob) < 0x20 or blob[:4] != b'BNSH' or struct.unpack_from('<H', blob, 0xC)[0] != 0xFEFF:
        raise GameError('Not a little-endian BNSH file.')
    if struct.unpack_from('<I', blob, 0x1C)[0] != len(blob):
        raise GameError('The BNSH size field does not match the file.')
    stages = {}
    for position in range(0, len(blob) - 0x18 + 1, 8):
        control_offset, code_offset, code_size, control_size = struct.unpack_from('<QQII', blob, position)
        if control_size != 0x880 or control_offset > len(blob) - 0x880 or not code_size or code_offset > len(blob) - code_size:
            continue
        control = blob[control_offset:control_offset + 0x880]
        if control[:4] != bytes.fromhex('34127698'):
            continue
        constant, size, sph = struct.unpack_from('<III', control, 0x700)
        stage = struct.unpack_from('<I', control, 0x714)[0]
        if size != code_size or stage not in (0, 1, 5) or not 0 <= sph < constant <= size:
            raise GameError(f'BNSH stage record at {position:#x} does not match its control block.')
        code = blob[code_offset:code_offset + code_size]
        if stage != 5 and (struct.unpack_from('<I', code, sph)[0] >> 10) & 15 != {0: 1, 1: 5}[stage]:
            raise GameError(f'BNSH stage record at {position:#x} has a mismatched shader header.')
        stages[(control_offset, code_offset)] = (control, code, stage)
    if not stages:
        raise GameError('The BNSH file has no NVN shader stages.')
    return [stages[key] for key in sorted(stages)]


def extract_bnsh(config, romfs, output):
    names = config.profile.get('shader_files')
    if not names:
        raise GameError('The BNSH shader adapter needs profile.shader_files.')
    output.mkdir(parents=True, exist_ok=True)
    records, verified, added = [], set(), set()
    for name in names:
        path = (romfs / name).resolve()
        if not path.is_relative_to(romfs) or not path.is_file():
            raise GameError('Shader file must exist inside RomFS: ' + name)
        for control, code, stage in bnsh_stages(path.read_bytes()):
            key = shader_key(config, control, code)
            target = output / (key + '.control')
            if target.exists():
                if target.read_bytes() != control or target.with_suffix('.code').read_bytes() != code:
                    raise GameError('Shader capture differs from asset: ' + key)
                verified.add(key)
            else:
                target.with_suffix('.code').write_bytes(code)
                target.write_bytes(control)
                added.add(key)
            records.append(dict(source=name, key=key, stage=stage))
    (output.parent/'asset-shaders.json').write_text(json.dumps(
        dict(game_id=config.id, verified=sorted(verified), added=sorted(added), records=records), indent=2)+'\n')
    print('Added:', len(added), 'Verified:', len(verified), 'Total:', len(verified | added))


def embedded_stages(blob):
    """Validate Hayarigami's contiguous control, instruction and constant records."""
    records, position = [], 0
    while True:
        offset = blob.find(bytes.fromhex('34127698'), position)
        if offset < 0:
            break
        if len(blob)-offset < 0x880:
            raise GameError('Truncated embedded shader control.')
        control = blob[offset:offset+0x880]
        if struct.unpack_from('<9I', control) != (0x98761234, 1, 0x10, 0x120, 0xb, 0x878, 0, 0, 0x880):
            raise GameError('Unsupported embedded shader control header.')
        instructions, constants, constant_start, total, start = struct.unpack_from('<5I', control, 0x6f8)
        if (start != 0x30 or instructions < 0x50 or instructions % 8 or
                constant_start != (start+instructions+255) & ~255 or
                total != (constant_start+constants+255) & ~255 or total > 4*1024*1024):
            raise GameError('Invalid embedded shader lengths.')
        if total > len(blob)-offset-0x880:
            raise GameError('Truncated embedded shader code.')
        code = blob[offset+0x880:offset+0x880+total]
        selector = struct.unpack_from('<I', control, 0x714)[0]
        sph = (struct.unpack_from('<I', code, start)[0] >> 10) & 15
        if code[:4] != bytes.fromhex('78563412') or {0: 1, 1: 5, 2: 4}.get(selector) != sph:
            raise GameError('Embedded shader stage or code header mismatch.')
        records.append(dict(control=control, code=code, stage=selector, offset=offset))
        position = offset+0x880+total
    if not records:
        raise GameError('No embedded NVN shaders found.')
    return records


def extract_embedded(config, output):
    from analyze_game import load_nso
    name = config.profile.get('shader_exefs')
    if name not in config.module_names:
        raise GameError('profile.shader_exefs must name an executable module.')
    module = load_nso(config.content_dir/'ExeFS'/name, include_image=True)
    expected = config.profile['modules'][name]
    if any(module[k] != expected[k] for k in ('sha256', 'build_id')):
        raise GameError('Embedded shader module does not match its profile.')
    records = embedded_stages(module['image'])
    output.mkdir(parents=True, exist_ok=True)
    summary, added, unsupported = [], set(), []
    for record in records:
        key = shader_key(config, record['control'], record['code'])
        row = dict(source=name, offset=record['offset'], key=key, stage=record['stage'])
        if record['stage'] == 2:
            row['reason'] = 'Geometry translation is unsupported. Do not bind this stage.'
            unsupported.append(row)
            continue
        target = output/(key+'.control')
        if target.exists():
            if target.read_bytes() != record['control'] or target.with_suffix('.code').read_bytes() != record['code']:
                raise GameError('Embedded shader key collision: '+key)
        else:
            target.with_suffix('.code').write_bytes(record['code'])
            target.write_bytes(record['control'])
            added.add(key)
        summary.append(row)
    report = dict(game_id=config.id, module_sha256=module['sha256'], records=summary, unsupported=unsupported)
    (output.parent/'asset-shaders.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Embedded shader records:',len(records),'supported:',len(summary),'unsupported:',len(unsupported),'added:',len(added))


def extract(config, data_dir, output):
    adapter = config.require_adapted('shader_extract')
    if adapter == 'hayarigami-nvn':
        return extract_embedded(config, output)
    if adapter == 'nn-gfx-bnsh':
        return extract_bnsh(config, config.content_dir / 'RomFS', output)
    if adapter != 'unity-switch':
        raise GameError('No shader extraction implementation is registered for ' + config.id + '.')
    import lz4.block
    import UnityPy
    version = config.profile.get('unity_version')
    if not version:
        raise GameError('The Unity shader adapter needs profile.unity_version.')
    UnityPy.config.FALLBACK_UNITY_VERSION = version.split('-', 1)[0]
    names = config.profile.get('shader_assets', [
        'resources.assets', 'globalgamemanagers.assets', 'Resources/unity_builtin_extra',
        'Resources/unity default resources'])
    patterns = config.profile.get('shader_asset_globs', [
        'StreamingAssets/aa/Switch/*shader*.bundle',
        'StreamingAssets/aa/Switch/*unitybuiltinassets.bundle'])
    sources = []
    for name in names:
        path = (data_dir / name).resolve()
        if not path.is_relative_to(data_dir): raise GameError('Shader asset path must stay inside the Data directory.')
        if path.is_file(): sources.append(path)
    for pattern in patterns:
        if Path(pattern).is_absolute() or '..' in Path(pattern).parts:
            raise GameError('Shader asset glob must stay inside the Data directory.')
        for path in sorted(data_dir.glob(pattern)):
            if not path.resolve().is_relative_to(data_dir): raise GameError('Shader asset link leaves the Data directory.')
            if path.is_file(): sources.append(path)
    sources = list(dict.fromkeys(sources))
    if not sources: raise GameError('No Unity shader assets were found: ' + str(data_dir))
    output.mkdir(parents=True, exist_ok=True)
    records, verified, added = [], set(), set()
    for source in sources:
        environment = UnityPy.load(str(source))
        for obj in environment.objects:
            if obj.type.name == 'ComputeShader':
                scan(obj.get_raw_data(), source, 'ComputeShader', data_dir, output, records, verified, added)
            elif obj.type.name == 'Shader':
                shader = obj.read(check_read=False)
                name = shader.m_ParsedForm.m_Name
                for offsets, lengths, sizes in zip(shader.offsets, shader.compressedLengths, shader.decompressedLengths):
                    if not isinstance(offsets, list): offsets, lengths, sizes = [offsets], [lengths], [sizes]
                    for offset, length, size in zip(offsets, lengths, sizes):
                        if not length: continue
                        chunk = bytes(shader.compressedBlob[offset:offset+length])
                        scan(lz4.block.decompress(chunk, uncompressed_size=size), source, name,
                             data_dir, output, records, verified, added)
        print(source.name, 'unique', len(verified | added), flush=True)
    if config.profile.get('shader_exefs'):
        from analyze_game import load_nso
        for module in config.module_names:
            source = config.content_dir / 'ExeFS' / module
            nso = load_nso(source, include_image=True)
            expected = config.profile['modules'][module]
            if any(nso[key] != expected[key] for key in ('sha256', 'build_id')):
                raise GameError('Unsupported shader source module: ' + module)
            scan(nso['image'], source, 'NSO', config.content_dir, output, records, verified, added)
            print('ExeFS/' + module, 'unique', len(verified | added), flush=True)
    (output.parent/'asset-shaders.json').write_text(json.dumps(
        dict(game_id=config.id, verified=sorted(verified), added=sorted(added), records=records), indent=2)+'\n')
    print('Added:', len(added), 'Verified:', len(verified), 'Total:', len(verified | added))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', default='silksong', help='Registered game ID.')
    parser.add_argument('--data', type=Path, help='Default: the configured content RomFS/Data.')
    parser.add_argument('--out', type=Path, help='Default: work/<game>/shaders/capture.')
    args = parser.parse_args()
    try:
        config = load_game(args.game)
        extract(config, (args.data or config.content_dir/'RomFS/Data').resolve(),
                (args.out or config.capture_dir).resolve())
    except (GameError, OSError, ValueError, struct.error) as error:
        print('Error: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__': sys.exit(main())
