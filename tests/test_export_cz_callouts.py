import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path


spec = importlib.util.spec_from_file_location(
    "export_cz_callouts", Path(__file__).resolve().parents[1] / "tools/export_cz_callouts.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def sample_nav(bsp_size=16):
    data = bytearray(struct.pack("<IIIH", 0xFEEDFACE, 5, bsp_size, 1))
    data += struct.pack("<H", 10) + b"BombsiteA\0"
    data += struct.pack("<I", 1)  # one area
    data += struct.pack("<IB6f2f", 1, 0, 0, 0, 0, 100, 200, 20, 0, 0)
    data += struct.pack("<4I", 0, 0, 0, 0)  # connections
    data += struct.pack("<BBIH", 0, 0, 0, 1)  # hiding, approach, encounters, place
    return bytes(data)


class ExportTests(unittest.TestCase):
    def test_reads_named_region_center(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bsp = root / "de_test.bsp"
            nav = root / "de_test.nav"
            bsp.write_bytes(b"x" * 16)
            nav.write_bytes(sample_nav())
            self.assertEqual(module.read_places(nav, bsp), {"BombsiteA": (50.0, 100.0, 10.0)})

    def test_rejects_wrong_map_and_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bsp = root / "de_test.bsp"
            nav = root / "de_test.nav"
            bsp.write_bytes(b"x" * 15)
            nav.write_bytes(sample_nav())
            with self.assertRaises(module.NavError):
                module.read_places(nav, bsp)
            bsp.write_bytes(b"x" * 16)
            nav.write_bytes(sample_nav()[:-2])
            with self.assertRaises(module.NavError):
                module.read_places(nav, bsp)


if __name__ == "__main__":
    unittest.main()
