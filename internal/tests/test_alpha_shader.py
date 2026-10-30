"""Alpha-test transformation must reject unsupported shader interfaces."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import build_shaders

class AlphaShaderTests(unittest.TestCase):
    def test_requires_a_transform(self):
        self.assertTrue(callable(getattr(build_shaders,'alpha_test_source',None)),
                        'Fragment alpha comparison is missing')

    def test_rejects_unknown_fragment_interface(self):
        transform=getattr(build_shaders,'alpha_test_source',None)
        self.assertIsNotNone(transform,'Fragment alpha comparison is missing')
        with self.assertRaises(ValueError):
            transform('fragment float4 main0() { return float4(1); }')

    def test_rejects_reserved_buffer_collision(self):
        with self.assertRaises(ValueError):
            build_shaders.alpha_test_source('struct Out { float4 color [[color(0)]]; }; fragment Out main0(constant float& v [[buffer(30)]]) { Out out; return out; }')

    def test_empty_fragment_argument_list_is_valid(self):
        source='struct Out { float4 color [[color(0)]]; }; fragment Out main0() { Out out; return out; }'
        transformed=build_shaders.alpha_test_source(source)
        # A real Metal compile validates the parameter interface. Rendering is checked by the native GPU test.
        import subprocess,tempfile
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'alpha.metal'
            path.write_text('#include <metal_stdlib>\nusing namespace metal;\n'+transformed)
            subprocess.run(['xcrun','-sdk','macosx','metal','-c',str(path),'-o',str(Path(directory)/'alpha.air')],check=True,capture_output=True)
