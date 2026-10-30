#!/usr/bin/env python3
"""Build the development host in the selected game's build directory."""
import argparse
from datetime import datetime
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

from prepare_ios import write_game_header
from workspace import ROOT, DEPENDENCIES, UPSTREAM, WorkspaceError, load_game, validate_registry


def make_quote(value):
    return str(value).replace('$', '$$').replace('#', r'\#').replace(' ', r'\ ')


def write_if_changed(path, text):
    if not path.exists() or path.read_text() != text: path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', default='silksong', help='Game ID in games/<id>/game.json')
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--configure-only', action='store_true', help='Write the build configuration without compiling')
    args = parser.parse_args()
    if args.jobs < 1: parser.error('--jobs must be positive')
    try:
        game = load_game(args.game)
        validate_registry(game.root)
        game.require_adapted()
        host = UPSTREAM / 'dm/host'
        gfx = UPSTREAM / 'dm/gfx'
        sources = list(game.common_runtime_sources) + list(game.runtime_sources)
        sources += [host / name for name in ('loader.c', 'hle.c', 'hle_svc.c', 'hle_thread.c', 'hle_os.c',
                                             'hle_sdkinit.c', 'hle_table.c', 'hle_count.c', 'hle_account.c', 'entry.S')]
        for pattern in ('*.c', 'nvn/*.c', 'nvn/*.m', 'nvn/*.S', 'tex/*.c', 'tex/*.m'):
            sources.extend(sorted(gfx.glob(pattern)))
        sources = list(dict.fromkeys(sources))
        for path in sources:
            if not path.is_file(): raise WorkspaceError('Source does not exist: ' + str(path))
        game.host_dir.mkdir(parents=True, exist_ok=True)
        game.logs_dir.mkdir(parents=True, exist_ok=True)
        write_game_header(game, game.host_dir)
        includes = (game.host_dir, ROOT / 'runtime/common', game.game_dir / 'runtime', host, gfx / 'nvn/include', gfx / 'tex')
        flags = ['-arch', 'arm64', '-mmacosx-version-min=' + game.minimum_macos_version,
                 '-O1', '-g', '-Wall', '-Wno-unused-function', '-Wno-deprecated-declarations',
                 '-Werror=unguarded-availability-new', '-fobjc-arc', '-DSS_HOST_CLI']
        flags += ['-D' + name for name in game.compile_defines] + ['-I' + str(path) for path in includes]
        config = game.host_dir / 'config.mk'
        relative_host = Path(os.path.relpath(game.host_dir, ROOT))
        relative_sources = [Path(os.path.relpath(path, ROOT)) for path in sources]
        objects = [relative_host / 'obj' / (hashlib.sha256(str(path).encode()).hexdigest()[:16] + '.o')
                   for path in relative_sources]
        text = '# Generated from games/' + game.id + '/game.json.\n'
        text += 'GAME := ' + game.id + '\n'
        text += 'GAME_FRAMEWORKS := ' + ' '.join(game.frameworks) + '\n'
        text += 'HOST_OUTPUT := ' + make_quote(relative_host / (game.id + '-host')) + '\n'
        text += 'OBJECT_DIR := ' + make_quote(relative_host / 'obj') + '\n'
        text += 'GAME_HEADER := ' + make_quote(relative_host / 'touchable_game_config.h') + '\n'
        text += 'OBJECTS := ' + ' '.join(make_quote(path) for path in objects) + '\n'
        text += 'FLAGS := ' + shlex.join(flags).replace('$', '$$').replace('#', r'\#') + '\n'
        for source, obj in zip(relative_sources, objects):
            text += make_quote(obj) + ': ' + make_quote(source) + ' tools/host.mk $(CONFIG) $(GAME_HEADER)\n'
            text += '\t@mkdir -p "$(@D)"\n\t$(CC) $(FLAGS) -MMD -MP -c "$<" -o "$@"\n'
        write_if_changed(config, text)
        if args.configure_only:
            print('Configured', config)
            return 0
        log = game.logs_dir / 'host-build.log'
        if log.exists():
            shutil.copy2(log, log.with_name('host-build-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.log'))
        with log.open('w') as stream:
            result = subprocess.run(['make', '-f', 'tools/host.mk', 'CONFIG=' + os.path.relpath(config, ROOT),
                                     '-j', str(args.jobs)], cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode: raise WorkspaceError('Host build failed. See ' + str(log))
        print('Built', game.host_dir / (game.id + '-host'))
        print('Log:', log)
        return 0
    except (WorkspaceError, OSError, subprocess.CalledProcessError) as error:
        print('Error:', error, file=sys.stderr)
        return 1


if __name__ == '__main__': sys.exit(main())
