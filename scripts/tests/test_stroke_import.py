import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("import_strokes", ROOT / "tools/import_strokes.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class StrokeImportTests(unittest.TestCase):
    def test_path_encoding_and_index_generation(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source"
            source.mkdir()
            record = {
                "strokes": ["M 10 20 L 30 40 Q 1 2 3 4 C 1 2 3 4 5 6 Z"],
                "medians": [[[10, 20], [30, 40]]],
                "radStrokes": [0],
            }
            (source / "汉.json").write_text(json.dumps(record), encoding="utf-8")
            order = Path(tmp) / "order.json"
            order.write_text(json.dumps({"汉": "j"}), encoding="utf-8")
            pack = Path(tmp) / "handict"
            shutil.copytree(ROOT / "content/sdcard/handict", pack)
            result = module.build(source, pack, order)
            self.assertEqual(result[0:2], (1, 1))
            self.assertEqual(module.content_pack.validate_stroke_index(pack), 1)
            manifest = json.loads((pack / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["stroke_dictionary"]["format"], "handict-strokes-v1")

    def test_malformed_path_is_rejected(self):
        with self.assertRaises(ValueError):
            module.encode_path("M 1")
        with self.assertRaises(ValueError):
            module.encode_path("A 1 2")


if __name__ == "__main__":
    unittest.main()
