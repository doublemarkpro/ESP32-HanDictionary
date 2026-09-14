import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("content_pack", ROOT / "tools/content_pack.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class ContentPackTests(unittest.TestCase):
    def test_timetable_legacy_and_weekend_example(self):
        pack.validate_timetable({"days": [[], [], [], [], []]})
        pack.validate_timetable(json.loads((ROOT / "docs/examples/timetable.example.json").read_text(encoding="utf-8")))

    def test_timetable_rejects_invalid_shape_and_strings(self):
        for record in [[], {}, {"days": []}, {"days": [None] * 5},
                       {"days": [["x"] * 9] * 5}, {"days": [["x" * 33]] * 5},
                       {"days": [["a\n"]] * 5}, {"days": [["\0"]] * 5},
                       {"days": [[True]] * 5}, {"days": [[]] * 5, "supplies": {}}]:
            with self.subTest(record=record), self.assertRaises(ValueError):
                pack.validate_timetable(record)

    def test_timetable_accepts_empty_lesson_placeholders(self):
        pack.validate_timetable({"days": [["", "语文"]] * 7, "supplies": [[]] * 7})

    def test_qweather_config_validation(self):
        valid = {
            "api_host": "abc123.xy.qweatherapi.com",
            "api_key": "ABCD1234EFGH",
            "city": "青岛",
            "latitude": 36.07,
            "longitude": 120.38,
        }
        pack.validate_qweather(valid)
        for changes in [
            {"api_host": "https://api.qweather.com/path"},
            {"api_key": "bad key"},
            {"city": ""},
            {"latitude": True},
            {"latitude": 91},
            {"longitude": -181},
        ]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                record = valid | changes
                pack.validate_qweather(record)

    def setUp(self):
        self.entry = json.loads((ROOT / "content/sdcard/handict/dictionary/entries/89C4.json").read_text(encoding="utf-8"))

    def test_starter_pack_has_no_printed_book_metadata(self):
        self.assertEqual(pack.validate(ROOT / "content/sdcard/handict"), 2)
        self.assertNotIn("reference", self.entry)
        manifest = json.loads((ROOT / "content/sdcard/handict/manifest.json").read_text(encoding="utf-8"))
        self.assertNotIn("isbn", manifest.get("dictionary", {}))

    def test_invalid_strokes_and_path_characters_are_rejected(self):
        for values in [dict(character="../"), dict(stroke_count=7), dict(stroke_count=True), dict(stroke_order=["横"] * 65)]:
            entry = copy.deepcopy(self.entry); entry.update(values)
            with self.assertRaises(ValueError): pack.validate_entry(entry)

    def test_prepare_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "card"
            self.assertEqual(pack.prepare(target), 2)
            with self.assertRaises(ValueError): pack.prepare(target)

    def test_import_duplicate_detected_before_output_created(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "input.json"
            source.write_text(json.dumps([self.entry, self.entry]), encoding="utf-8")
            target = Path(tmp) / "card"
            with self.assertRaises(ValueError): pack.prepare(target, source)
            self.assertFalse(target.exists())

    def test_stroke_png_validation(self):
        data = (ROOT / "assets/source/strokes/gui-1.png").read_bytes()
        pack.validate_stroke_png(data)
        for broken in [data[:-8], data + b"tail", data[:20] + b"\0" * 4 + data[24:], b"fake", data[:40] + b"bad" + data[43:]]:
            with self.assertRaises(ValueError): pack.validate_stroke_png(broken)

    def test_missing_stroke_frames_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            pack.prepare(tmp + "/card")
            (Path(tmp) / "card/handict/dictionary/strokes/89C4/01.png").unlink()
            with self.assertRaises(ValueError): pack.validate(Path(tmp) / "card/handict")


if __name__ == "__main__": unittest.main()
