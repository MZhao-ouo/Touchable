"""Read synthetic NSO exports when the optional header extents are absent."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from analyze_game import load_nso
from game_formats import GameError


def nso(*, symbol_offset=0x300, count=2, terminate=True):
    image = bytearray(0x500)
    struct.pack_into('<I', image, 4, 0x40)
    struct.pack_into('<4si', image, 0x40, b'MOD0', 0x40)
    entries = [(4, 0x200), (5, 0x380), (6, symbol_offset), (10, 8), (11, 24)]
    if terminate:
        entries.append((0, 0))
    for i, pair in enumerate(entries):
        struct.pack_into('<QQ', image, 0x80 + i * 16, *pair)
    if not terminate:
        for offset in range(0x80 + len(entries) * 16, len(image), 16):
            struct.pack_into('<QQ', image, offset, 1, 1)
    else:
        struct.pack_into('<II', image, 0x200, 1, count)
        struct.pack_into('<IBBHQQ', image, 0x318, 1, 0x12, 0, 1, 0x100, 4)
        image[0x380:0x388] = b'\0nnMain\0'
    header = bytearray(0x100)
    header[:4] = b'NSO0'
    struct.pack_into('<I', header, 12, 0x38)
    for i, (offset, size) in enumerate([(0, 0x200), (0x200, 0x200), (0x400, 0x100)]):
        struct.pack_into('<III', header, 0x10 + i * 16, 0x100 + offset, offset, size)
        header[0xa0 + 32*i:0xc0 + 32*i] = hashlib.sha256(image[offset:offset+size]).digest()
    return header + image


class DynamicNsoTests(unittest.TestCase):
    def load(self, data):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'synthetic.nso'
            path.write_bytes(data)
            return load_nso(path)

    def test_missing_header_extents_use_mod0(self):
        result = self.load(nso())
        self.assertEqual([s['name'] for s in result['symbols']], ['nnMain'])
        self.assertTrue(result['symbols'][0]['defined'])
        self.assertEqual(result['symbols'][0]['value'], 0x100)

    def test_dynamic_symbol_extent_must_fit(self):
        for changes in [{'symbol_offset': 0x4f0}, {'count': 0xffffffff}]:
            with self.subTest(changes=changes), self.assertRaises(GameError):
                self.load(nso(**changes))

    def test_dynamic_table_must_terminate(self):
        with self.assertRaises(GameError):
            self.load(nso(terminate=False))


if __name__ == '__main__':
    unittest.main()
