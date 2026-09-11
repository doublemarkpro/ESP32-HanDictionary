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
    def setUp(self):
        self.entry = json.loads((ROOT / "content/sdcard/handict/dictionary/entries/89C4.json").read_text(encoding="utf-8"))

    def test_starter_pack_has_no_invented_page(self):
        self.assertEqual(pack.validate(ROOT / "content/sdcard/handict"), 1)
        self.assertIsNone(self.entry["reference"]["page"])

    def test_wrong_edition_cannot_supply_page(self):
        self.entry["reference"].update(isbn="9780000000000", page=123, verified=True)
        with self.assertRaises(ValueError): pack.validate_entry(self.entry)

    def test_unverified_or_boolean_page_is_rejected(self):
        for page, verified in [(123, False), (True, True), (0, True), (None, True)]:
            self.entry["reference"].update(page=page, verified=verified)
            with self.assertRaises(ValueError): pack.validate_entry(self.entry)

    def test_invalid_strokes_and_path_characters_are_rejected(self):
        for values in [dict(character="../"), dict(stroke_count=7), dict(stroke_count=True), dict(stroke_order=["横"] * 65)]:
            entry = copy.deepcopy(self.entry); entry.update(values)
            with self.assertRaises(ValueError): pack.validate_entry(entry)

    def test_prepare_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "card"
            self.assertEqual(pack.prepare(target), 1)
            with self.assertRaises(ValueError): pack.prepare(target)

    def test_import_duplicate_detected_before_output_created(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "input.json"
            source.write_text(json.dumps([self.entry, self.entry]), encoding="utf-8")
            target = Path(tmp) / "card"
            with self.assertRaises(ValueError): pack.prepare(target, source)
            self.assertFalse(target.exists())

    def test_verified_page_round_trips(self):
        # Synthetic fixture verifies storage only; this value is not a real dictionary page.
        self.entry["reference"].update(page=123, verified=True)
        self.assertEqual(pack.validate_entry(self.entry), "规")


if __name__ == "__main__": unittest.main()
