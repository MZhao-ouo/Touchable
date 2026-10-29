#!/usr/bin/env python3
"""Print the official game titles from the control.nacp of a Switch package.

The titles give the localized app names in game.json (ios.display_names). The tool reads the Control
NCA of the package, so it needs prod.keys. It writes nothing outside a temporary directory.

    .touchable/venv/bin/python internal/tools/game_titles.py --game rhythm-heaven \
      --keys MyGameFiles/keys/ProdKeys.-v22.5.0/prod.keys "MyGameFiles/rhythm-heaven/<package>.nsp"
"""
import argparse
import json
from pathlib import Path
import re
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_game import (GameError, copy_ncas, package_ncas, package_tickets, run_hactool,
                          write_nca_keyset)
from workspace import DEPENDENCIES, load_game

# Title entries of control.nacp: 16 entries of 0x300 bytes (name 0x200, publisher 0x100), in this order.
NACP_LANGUAGES = ['en-US', 'en-GB', 'ja', 'fr', 'de', 'es-419', 'es', 'it', 'nl', 'fr-CA', 'pt', 'ru', 'ko',
                  'zh-Hant', 'zh-Hans', 'pt-BR']


def nacp_titles(data):
    """Return {language: title} for the non-empty title entries of control.nacp."""
    if len(data) < 0x3000:
        raise GameError('control.nacp is too short.')
    titles = {}
    for index, language in enumerate(NACP_LANGUAGES):
        name = data[0x300 * index:0x300 * index + 0x200].split(b'\0', 1)[0].decode('utf-8', 'replace').strip()
        if name:
            titles[language] = name
    return titles


def read_titles(package, keys, hactool):
    entries, ncas = package_ncas(package)
    tickets = package_tickets(package, entries)
    with tempfile.TemporaryDirectory(prefix='.titles-') as temporary:
        temp = Path(temporary)
        nca_keys = temp / 'nca.keys'
        header_key = write_nca_keyset(keys, nca_keys)
        nca_dir = temp / 'nca'
        nca_dir.mkdir()
        copy_ncas(package, ncas, nca_dir, header_key)
        for entry in ncas:
            nca = nca_dir / entry['name']
            info = run_hactool(hactool, nca_keys, ['-i', '-t', 'nca', nca])
            content = re.search(r'^Content Type:\s*(\S+)', info, re.M)
            if not content or content[1] != 'Control':
                continue
            rights = re.search(r'^Rights ID:\s*([0-9a-fA-F]{32})', info, re.M)
            title_key = tickets.get(rights[1].lower()) if rights else None
            out = temp / 'control'
            run_hactool(hactool, nca_keys, ['-t', 'nca', '--romfsdir=' + str(out), nca], title_key)
            nacp = out / 'control.nacp'
            if not nacp.is_file():
                raise GameError('The Control NCA has no control.nacp.')
            return nacp_titles(nacp.read_bytes())
    raise GameError('The package has no Control NCA.')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('package', type=Path, help='Base or update NSP of the game.')
    parser.add_argument('--game', default='silksong', help='Registered game ID.')
    parser.add_argument('--keys', type=Path, required=True)
    parser.add_argument('--hactool', type=Path, default=DEPENDENCIES / 'hactool/hactool')
    args = parser.parse_args()
    load_game(args.game)
    try:
        titles = read_titles(args.package, args.keys, args.hactool)
    except GameError as error:
        parser.error(str(error))
    print(json.dumps(titles, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
