"""Check embedded NVN stages with synthetic bytes only."""
from pathlib import Path
import struct
import sys
import unittest
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import extract_shaders as shaders
from game_formats import GameError


def stage(selector=0):
    control=bytearray(0x880)
    struct.pack_into('<9I',control,0,0x98761234,1,0x10,0x120,0xb,0x878,0,0,0x880)
    struct.pack_into('<5I',control,0x6f8,0x50,16,0x100,0x200,0x30)
    struct.pack_into('<I',control,0x714,selector)
    code=bytearray(0x200)
    struct.pack_into('<I',code,0,0x12345678)
    struct.pack_into('<I',code,0x30,{0:1,1:5,2:4}[selector]<<10)
    return control+code


class EmbeddedShaderTests(unittest.TestCase):
    def test_contiguous_stages_and_duplicate_records(self):
        result=shaders.embedded_stages(stage()+stage(1)+stage(2)+stage())
        self.assertEqual([r['stage'] for r in result],[0,1,2,0])
        self.assertEqual(result[0]['control'],result[3]['control'])
        self.assertEqual(len(result[0]['code']),0x200)

    def test_truncated_or_missing_shader_is_rejected(self):
        for blob in [b'',stage()[:0x300],stage()[:-1]]:
            with self.subTest(size=len(blob)),self.assertRaises(GameError):
                shaders.embedded_stages(blob)

    def test_invalid_lengths_header_and_stage_are_rejected(self):
        for offset,value in [(4,2),(0x700,0x80),(0x704,0xffffffff),(0x714,1),(0x880,0)]:
            blob=stage();struct.pack_into('<I',blob,offset,value)
            with self.subTest(offset=offset),self.assertRaises(GameError):
                shaders.embedded_stages(blob)

    def test_interrupted_code_write_can_retry(self):
        record=stage()
        module={'image':record,'sha256':'a'*64,'build_id':'b'*64}
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);output=root/'capture'
            config=SimpleNamespace(id='hayarigami',module_names=['main'],content_dir=root,
                compile_defines=['SS_SHADER_KEY_CODE'],profile={'shader_exefs':'main','modules':{'main':module}})
            original=Path.write_bytes
            def interrupted(path,data):
                if path.suffix=='.code':raise OSError('simulated interrupted code write')
                return original(path,data)
            with patch('analyze_game.load_nso',return_value=module):
                with patch.object(Path,'write_bytes',interrupted),self.assertRaises(OSError):
                    shaders.extract_embedded(config,output)
                self.assertEqual(list(output.glob('*.control')),[])
                shaders.extract_embedded(config,output)
            self.assertEqual(len(list(output.glob('*.control'))),1)
            self.assertEqual(len(list(output.glob('*.code'))),1)


if __name__=='__main__':unittest.main()
