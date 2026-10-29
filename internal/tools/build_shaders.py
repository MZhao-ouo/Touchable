#!/usr/bin/env python3
"""Translate locally captured Switch shaders to macOS and iOS Metal libraries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from workspace import ROOT, DEPENDENCIES, UPSTREAM, GameError, load_game
from shader_keys import functional_key

GFX = UPSTREAM / 'dm/gfx'
XLATE = GFX / 'tools/dmsh_xlate/bin/Release/net10.0'
SPIRV = DEPENDENCIES / 'SPIRV-Cross/spirv-cross'


def run(args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'{args[0]} failed: {result.stdout}\n{result.stderr}')
    return result.stdout


def metal_target_options(sdk, minimum="26.0"):
    # Match the runtime's OS 26 floor even when building with a newer Xcode.
    deployment = {'macosx': '-mmacosx-version-min=', 'iphoneos': '-mios-version-min='}
    return ['-std=metal4.0', deployment[sdk] + minimum]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# Each SDK compiles for its own OS version unless a minimum is given. The device rejects a
# library built for a newer OS, and every draw that uses it is skipped: the screen stays black.
def metal_targets(config):
    return {'macosx': config.minimum_macos_version, 'iphoneos': config.minimum_ios_version}


def version_tuple(text):
    return tuple(int(part) for part in str(text).split('.'))


def metallib_target(path):
    """Return the SDK name and minimum OS version recorded in a metallib header."""
    data = Path(path).read_bytes()[:16]
    if len(data) < 16 or data[:4] != b'MTLB':
        raise ValueError(f'Not a Metal library: {path}')
    sdk = {1: 'macosx', 2: 'iphoneos'}.get(data[11] & 0x7f)
    if sdk is None:
        raise ValueError(f'Unknown Metal library platform 0x{data[11]:02x}: {path}')
    major, minor = struct.unpack_from('<HH', data, 12)
    return sdk, (major, minor)


def check_metallib(path, sdk, minimum):
    found, version = metallib_target(path)
    if found != sdk or version > version_tuple(minimum):
        raise ValueError(f'{path} targets {found} {version[0]}.{version[1]}, not {sdk} {minimum}.')


def tool_fingerprint(config):
    fingerprint = {p.name: digest(p) for p in [Path(__file__), Path(__file__).with_name('extract_shaders.py'),
                   Path(__file__).with_name('shader_keys.py'), SPIRV, XLATE/'dmsh_xlate.dll', XLATE/'Ryujinx.Graphics.Shader.dll']}
    for sdk in ('macosx', 'iphoneos'):
        fingerprint[sdk] = run(['xcrun', '-sdk', sdk, 'metal', '--version']).strip()
    fingerprint['targets'] = json.dumps(metal_targets(config), sort_keys=True)
    options = {key: config.profile[key] for key in ('nvn_window_origin', 'vertex_attribute_types', 'nvn_bindings', 'shader_functional_aliases', 'shader_exefs', 'nvn_alpha_test') if key in config.profile}
    if options:
        fingerprint['options'] = json.dumps(options, sort_keys=True)
    return fingerprint


def shader_job(control, work, code_key=False):
    key = control.stem
    data = control.read_bytes()
    code = control.with_suffix('.code').read_bytes()
    # The key rule matches runtime/common/ss_shader_capture.c for this game.
    expected = hashlib.sha256(data + code if code_key else data).hexdigest()[:16]
    if len(data) != 0x880 or expected != key:
        raise ValueError(f'Invalid shader control: {key}')
    constant_start, end, start = struct.unpack_from('<III', data, 0x700)
    stage_id = struct.unpack_from('<I', data, 0x714)[0]
    stage = {0: 'vs', 1: 'ps', 5: 'cs'}.get(stage_id)
    if stage is None or not 0 <= start < constant_start <= end <= len(code):
        raise ValueError(f'Unsupported shader layout: {key}, stage={stage_id}')
    if stage != 'cs':
        sph_stage = (struct.unpack_from('<I', code, start)[0] >> 10) & 15
        if sph_stage != {'vs': 1, 'ps': 5}[stage]:
            raise ValueError(f'Shader stage mismatch: {key}')
    path = work / f'{key}.bin'
    path.write_bytes(code[start:constant_start])
    job = dict(key=key, stage=stage, code=str(path), c1=code[constant_start:end].hex(),
               tex={}, img={}, attr={}, fout={}, debug=True, reduced=False)
    if stage == 'cs':
        local = struct.unpack_from('<III', data, 0x748)
        if any(n < 1 or n > 1024 for n in local) or local[0]*local[1]*local[2] > 1024:
            raise ValueError(f'Invalid compute workgroup: {key}: {local}')
        job['local'] = local
    return job


# Byte ranges in NVN constant bank 2 for bound image, texture, and separate sampler handles.
# This default matches the Unity and D2R shader compilers. A game pins another layout in
# profile.nvn_bindings; a kind it omits is rejected instead of guessed.
DEFAULT_LAYOUT = {'image': (0x120, 0x168), 'texture': (0x168, 0x568), 'sampler': (0x568, 0x968)}
LAYOUT = dict(DEFAULT_LAYOUT)


def binding_layout(profile):
    custom = profile.get('nvn_bindings')
    if custom is None:
        return dict(DEFAULT_LAYOUT)
    if not isinstance(custom, dict) or set(custom) - {'image', 'texture', 'sampler', 'evidence'}:
        raise GameError('profile.nvn_bindings must map image, texture, or sampler to [start, end].')
    layout = {}
    for kind in ('image', 'texture', 'sampler'):
        value = custom.get(kind)
        if value is None:
            layout[kind] = None
            continue
        start, end = (int(item, 16) for item in value)
        if start % 8 or end % 8 or end <= start:
            raise GameError('profile.nvn_bindings.' + kind + ' must be an aligned [start, end] range.')
        layout[kind] = (start, end)
    return layout


def texture_slots(name):
    match = re.match(r'[a-z]+_[ti]\w*?_(?:cb(\d+)|tcb)_([0-9A-F]+)', name)
    if not match:
        raise ValueError(f'Unrecognized shader resource: {name}')
    packed = int(match[1]) if match[1] else 2
    handle = int(match[2], 16)
    tex_bank = packed & 0xffff
    sampler_bank = (packed >> 16)-1 if packed >> 16 else tex_bank
    first, second, kind = handle & 0x3fff, (handle >> 14) & 0x3fff, handle >> 28
    words = [first, second] if kind else [first]
    def slot(span, bank):
        if span is None: return None
        base, limit = span
        return next(((w*4-base)//8 for w in words if bank == 2 and base <= w*4 < limit and (w*4-base)%8 == 0), None)
    if re.match(r'[a-z]+_i', name):
        return slot(LAYOUT['image'], tex_bank), None, kind
    return slot(LAYOUT['texture'], tex_bank), slot(LAYOUT['sampler'], sampler_bank), kind


def reflection(job, source, resources, chain):
    # Resource ABI follows the upstream MIT-licensed dmsh_build.py.
    body = re.search(r'struct spvDescriptorSetBuffer0\s*\{(.*?)\};', source, re.S)
    buffers = sorted(resources['sbufs'], key=lambda b: b['binding'])
    bindings = []
    for match in re.finditer(r'^\s*(.+?)\s+(\w+)\s*\[\[id\((\d+)\)\]\];', body[1] if body else '', re.M):
        type_name, name, aid = match[1].strip(), match[2], int(match[3])
        binding = dict(msl_name=name, msl_type=type_name, arg_id=aid)
        cbuf = re.fullmatch(r'[a-z]+_c(\d+)', name)
        sbuf = re.fullmatch(r'[a-z]+_s(\d+)', name)
        if name == 'support_buffer':
            binding.update(kind='support')
        elif cbuf:
            bank = int(cbuf[1])
            binding.update(kind='cbuf', reg=bank-3) if 3 <= bank <= 16 else binding.update(kind='c1' if bank == 1 else 'drvcb', bank=bank)
        elif sbuf:
            buffer = buffers[int(sbuf[1])]
            offset = buffer['cb_offset']*4
            base = {'vs': 0x110, 'ps': 0x510, 'cs': 0x310}[job['stage']]
            kind = 'sbuf'
            if job['stage'] == 'cs' and 0x210 <= offset < 0x310:
                base, kind = 0x210, 'ubo_ptr'
            if buffer['cb_slot'] != 0 or not 0 <= offset-base < 256 or (offset-base)%16:
                raise ValueError(f'Unsupported storage buffer: {name}: {buffer}')
            binding.update(kind=kind, reg=(offset-base)//16, rw=bool(buffer['flags'] & 2))
        elif name.endswith('Smplr'):
            if LAYOUT['image'] == DEFAULT_LAYOUT['image'] and re.fullmatch(r'[a-z]+_t_tcb_4[89A-F]Smplr', name):
                # TXQ on an image produces an unused sampler declaration.
                binding.update(kind='samp', reg=0)
            else:
                tex, samp, kind = texture_slots(name[:-5])
                binding.update(kind='samp', reg=samp) if kind else binding.update(kind='samp_tex', reg=tex)
        else:
            image_query = LAYOUT['image'] == DEFAULT_LAYOUT['image'] and re.fullmatch(r'[a-z]+_t_tcb_(4[89A-F])', name)
            tex = (int(image_query[1], 16)*4-0x120)//8 if image_query else texture_slots(name)[0]
            binding.update(kind='img' if image_query or re.match(r'[a-z]+_i', name) else 'tbuf' if 'texture_buffer' in type_name else 'tex', reg=tex)
        if 'reg' in binding and binding['reg'] is None:
            raise ValueError(f'Unknown NVN resource slot: {name}')
        bindings.append(binding)
    reads = {}
    for bank in (0, 2):
        indices = re.findall(r'[a-z]+_c%d\)\._m0\[([^\]]+)\]' % bank, source)
        if indices:
            reads[str(bank)] = dict(vec4_offsets=sorted({int(i.rstrip('u'))*16 for i in indices if i.rstrip('u').isdigit()}),
                                   dynamic=any(not i.rstrip('u').isdigit() for i in indices))
    entry = re.search(r'(vertex|fragment|kernel)\s+\S+\s+(\w+)\s*\(', source)
    if not entry:
        raise ValueError(f'No Metal entry point: {job["key"]}')
    return dict(key=job['key']+'_b', route='B', chain=chain, dm_ctl=job['key'],
                source='Switch NVN capture', stage=job['stage'], entry=entry[2], msl_entry_kind=entry[1],
                c1=job['c1'], threadgroup_size=job.get('local'), bindings=bindings,
                vertex_inputs=[dict(location=int(m[2]), msl_type=m[1]) for m in re.finditer(r'(\w+)\s+\w+\s*\[\[attribute\((\d+)\)\]\]', source)],
                drv_reads=reads, notes=resources['notes'])


def alpha_test_source(source):
    """Add NVN alpha comparison to a SPIRV-Cross fragment output, before return."""
    entry = re.search(r'fragment\s+(\w+)\s+main0\((.*?)\)\s*\{', source, re.S)
    if not entry or re.search(r'\[\[buffer\(30\)\]\]', source):
        raise ValueError('Unsupported fragment alpha-test interface.')
    output = re.search(r'struct\s+' + re.escape(entry[1]) + r'\s*\{(.*?)\};', source, re.S)
    if not output:
        raise ValueError('Fragment alpha test requires an output structure.')
    color = re.search(r'float4\s+(\w+)\s*\[\[color\(0\)\]\]', output[1])
    body = source[entry.end():]
    if 'return out;' not in body:
        raise ValueError('Fragment alpha test requires a named output return.')
    comparison = 'ss_alpha_pass(out.' + color[1] + '.w, uint(ss_alpha.x), ss_alpha.y)' if color else '(uint(ss_alpha.x) != 1u)'
    body = body.replace('return out;', 'if (!(' + comparison + ')) discard_fragment();\n    return out;')
    signature = source[:entry.end(2)] + (', ' if entry[2].strip() else '') + 'constant float2& ss_alpha [[buffer(30)]]' + source[entry.end(2):entry.end()]
    predicate = """
inline bool ss_alpha_pass(float a, uint op, float r) {
    switch (op) {
        case 1: return false;
        case 2: return a < r;
        case 3: return a == r;
        case 4: return a <= r;
        case 5: return a > r;
        case 6: return a != r;
        case 7: return a >= r;
        default: return true;
    }
}
"""
    return signature[:entry.start()] + predicate + signature[entry.start():] + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', default='silksong', help='Registered game ID.')
    parser.add_argument('--capture', type=Path, help='Default: work/<game>/shaders/capture.')
    parser.add_argument('--out', type=Path, help='Default: work/<game>/shaders.')
    parser.add_argument('--ios', action='store_true')
    args = parser.parse_args()
    config = load_game(args.game)
    if config.require_adapted('shader_build') not in ('unity-nvn', 'nvn'):
        raise GameError('No shader build implementation is registered for ' + config.id + '.')
    LAYOUT.update(binding_layout(config.profile))
    capture = (args.capture or config.capture_dir).resolve()
    out = (args.out or config.shaders_dir).resolve()
    controls = sorted(capture.glob('*.control'))
    if not controls:
        raise GameError('No captured shaders were found: ' + str(capture))
    work = out / 'translate'
    work.mkdir(parents=True, exist_ok=True)
    fingerprint = tool_fingerprint(config)
    origin = config.profile.get('nvn_window_origin', 'upper-left')
    chain = hashlib.sha256(json.dumps(fingerprint, sort_keys=True).encode()).hexdigest()[:12]
    (out/'chain.json').write_text(json.dumps(dict(game_id=config.id, chain=chain, tools=fingerprint), indent=2)+'\n')
    if origin not in ('upper-left', 'lower-left'):
        raise GameError('profile.nvn_window_origin must be upper-left or lower-left.')
    lower_left = origin == 'lower-left'
    if lower_left != ('SS_NVN_LOWER_LEFT_ORIGIN' in config.compile_defines):
        raise GameError('profile.nvn_window_origin lower-left requires the SS_NVN_LOWER_LEFT_ORIGIN compile define, and only it.')
    alpha_test = config.profile.get('nvn_alpha_test', False)
    if not isinstance(alpha_test, bool) or alpha_test != ('SS_NVN_ALPHA_TEST' in config.compile_defines):
        raise GameError('profile.nvn_alpha_test must match SS_NVN_ALPHA_TEST.')
    code_key = 'SS_SHADER_KEY_CODE' in config.compile_defines
    jobs = [shader_job(path, work, code_key) for path in controls]
    # Integer vertex inputs must be declared with their type, or Metal rejects the pipeline.
    attribute_types = config.profile.get('vertex_attribute_types', {})
    if any(not re.fullmatch(r'\d+', k) or v not in ('float', 'sint', 'uint') for k, v in attribute_types.items()):
        raise GameError('profile.vertex_attribute_types maps a location to float, sint, or uint.')
    for job in jobs:
        if job['stage'] == 'vs': job['attr'] = dict(attribute_types)
    jobs_file = work/'jobs.json'
    jobs_file.write_text(json.dumps(jobs, indent=2)+'\n')
    print(run([DEPENDENCIES/'dotnet/dotnet', XLATE/'dmsh_xlate.dll', jobs_file, work]).strip(), flush=True)
    # TEXS/SULD/SUST encode dimensions. Reuse those dimensions for image TXQ.
    changed = []
    for job in jobs:
        resources = json.loads((work/f'{job["key"]}.res.json').read_text())
        image_types = {f'{r["handle"]:X}': r['type'] for r in resources['images']
                       if r['cbuf_slot'] == 2 and 0x120 <= (r['handle'] & 0x3fff)*4 < 0x168}
        if image_types:
            job['handle_types'] = image_types
            changed.append(job)
    if changed:
        followup = work/'image-query-jobs.json'
        followup.write_text(json.dumps(changed, indent=2)+'\n')
        print(run([DEPENDENCIES/'dotnet/dotnet', XLATE/'dmsh_xlate.dll', followup, work]).strip(), flush=True)
    def build(job):
        key = job['key']
        targets = [out / name / f'b-{chain}' / (key + '_b')
                   for name in (('mac', 'ios') if args.ios else ('mac',))]
        if all(target.with_suffix(suffix).is_file() for target in targets
               for suffix in ('.metallib', '.json', '.metal')):
            return key
        msl = work/f'{key}.metal'
        # Lower-left window origin (NVN default, GL convention): flip clip-space Y so that render target
        # row 0 is the bottom, as the game expects when it samples its own render targets.
        flip = ['--flip-vert-y'] if job['stage'] == 'vs' and lower_left else []
        run([SPIRV, '--msl', '--msl-version', '20300', '--msl-argument-buffers', '--msl-texture-buffer-native',
             '--msl-pad-fragment-output', *flip, work/f'{key}.spv', '--output', msl])
        source = msl.read_text()
        if alpha_test and job['stage'] == 'ps':
            source = alpha_test_source(source)
            msl.write_text(source)
        resources = json.loads((work/f'{key}.res.json').read_text())
        refl = reflection(job, source, resources, chain)
        targets = metal_targets(config)
        for sdk, name in [('macosx', 'mac')] + ([('iphoneos', 'ios')] if args.ios else []):
            target = out/name/f'b-{chain}'
            target.mkdir(parents=True, exist_ok=True)
            air = work/f'{key}.{name}.air'
            run(['xcrun', '-sdk', sdk, 'metal', *metal_target_options(sdk, targets[sdk]), '-c', msl, '-o', air])
            run(['xcrun', '-sdk', sdk, 'metallib', air, '-o', target/f'{key}_b.metallib'])
            check_metallib(target/f'{key}_b.metallib', sdk, targets[sdk])
            (target/f'{key}_b.json').write_text(json.dumps(refl, indent=2)+'\n')
            (target/f'{key}_b.metal').write_text(source)
        return key
    with ThreadPoolExecutor(max_workers=6) as pool:
        built = list(pool.map(build, jobs))
    # The runtime matches these stage values in shader_map.tsv. Do not translate them.
    names = dict(vs='顶点', ps='像素', cs='计算')
    rows = []
    for job in jobs:
        key = job['key']
        alias = functional_key((capture/(key+'.control')).read_bytes(), (capture/(key+'.code')).read_bytes()) if config.profile.get('shader_functional_aliases') else ''
        rows.append(f'capture\t{key}\t{names[job["stage"]]}\t{key}\t{alias}\n')
    (out/'shader_map.tsv').write_text('package\tname\tstage\tkey\tfunctional_key\n' + ''.join(rows))
    config.shader_chain_header.parent.mkdir(parents=True, exist_ok=True)
    config.shader_chain_header.write_text(f'/* Generated by tools/build_shaders.py for {config.id}. */\n#define DMSH_CHAIN "{chain}"\n')
    print(f'Built {len(built)} Switch shaders. Chain: {chain}')


if __name__ == '__main__':
    try:
        main()
    except (GameError, OSError, ValueError, RuntimeError) as error:
        sys.exit(str(error))
