import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("import_xinhua", ROOT / "tools/import_xinhua.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def markup(character, pinyin, definition):
    return (
        "<table><tr><td>基本解释<br>Basic explanation</td><td>"
        f"{character}　　{pinyin}<br>{definition}</td></tr>"
        "<tr><td>中文输入法 Input Methods</td><td>TEST</td></tr></table>"
    )


class DictionaryImportTests(unittest.TestCase):
    def test_cnchar_radicals_add_radical_and_structure(self):
        with tempfile.TemporaryDirectory() as tmp:
            radicals = Path(tmp) / "radicals.json"
            structures = Path(tmp) / "struct.json"
            radicals.write_text(json.dumps({"*": "一c1", "口": "3:嗝a"}, ensure_ascii=False),
                                 encoding="utf-8")
            structures.write_text(json.dumps({"a": "左右结构", "c": "独体结构"},
                                              ensure_ascii=False), encoding="utf-8")
            metadata = module.read_cnchar_metadata(radicals, structures)
            self.assertEqual(metadata["嗝"], ("口", "左右结构"))
            self.assertEqual(metadata["一"], ("一", "独体结构"))

    def test_html_cleaning_extracts_basic_definition_and_words(self):
        entry = module.parse_entry("规", markup("规", "guī", "1. 法则：～则。<br>2. 谋划：～划。"))
        self.assertEqual(entry["pinyin"], "guī")
        self.assertEqual(entry["definition"], "1. 法则：～则。\n2. 谋划：～划。")
        self.assertEqual(entry["words"], ["规则", "规划"])
        self.assertEqual(entry["stroke_count"], 0)
        self.assertNotIn("reference", entry)

    def test_pinyin_can_follow_character_on_the_next_line(self):
        html = ("<table><tr><td>基本解释</td><td>垂<br>chuí ㄔㄨㄟˊ<br>"
                "东西一头挂下。</td></tr></table>")
        entry = module.parse_entry("垂", html)
        self.assertEqual(entry["pinyin"], "chuí")
        self.assertEqual(entry["definition"], "东西一头挂下。")

    def test_conversion_builds_direct_lookup_index_from_zip(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source.zip"
            rows = [
                "About\tignored",
                "规\t" + markup("规", "guī", "法则：～则。"),
                "矩\t" + markup("矩", "jǔ", "画直角的工具：～尺。"),
                "汉\t" + markup("汉", "hàn", "汉族；汉语。"),
            ]
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("dictionary.tab", "\n".join(rows).encode("utf-8"))
            output = Path(tmp) / "card"
            count, index_size, data_size, font_size, skipped, replacements = module.convert(source, output)
            self.assertEqual((count, skipped, replacements), (3, 1, 0))
            self.assertEqual(font_size, 0)
            self.assertEqual(index_size, module.content_pack.INDEX_HEADER.size +
                             module.content_pack.INDEX_SLOTS * module.content_pack.INDEX_SLOT.size)
            self.assertGreater(data_size, 0)
            self.assertEqual(module.content_pack.validate_index(output / "handict"), 3)
            self.assertEqual(module.content_pack.validate_pinyin_index(output / "handict"), 3)
            manifest = json.loads((output / "handict/manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["indexed_dictionary"]["source_id"], module.SOURCE_ID)
            self.assertEqual(manifest["indexed_dictionary"]["pinyin_index"]["syllables"], 3)

            pinyin = (output / "handict/dictionary/pinyin.idx").read_bytes()
            header = module.content_pack.PINYIN_HEADER.unpack_from(pinyin)
            _, _, count, directory_size, _, _ = header
            candidates = {}
            for record in range(count):
                key, offset, candidate_count, _ = module.content_pack.PINYIN_RECORD.unpack_from(
                    pinyin, module.content_pack.PINYIN_HEADER.size +
                    record * module.content_pack.PINYIN_RECORD.size)
                key = key.rstrip(b"\0").decode("ascii")
                begin = module.content_pack.PINYIN_HEADER.size + directory_size + offset
                candidates[key] = pinyin[begin:begin + candidate_count * 3].decode("utf-8")
            self.assertEqual(candidates["han"], "汉")
            self.assertEqual(candidates["gui"], "规")
            self.assertEqual(candidates["ju"], "矩")

            index = (output / "handict/dictionary/index.bin").read_bytes()
            data = (output / "handict/dictionary/data.bin").read_bytes()
            slot = ord("汉") - module.content_pack.INDEX_FIRST
            offset, length = module.content_pack.INDEX_SLOT.unpack_from(
                index, module.content_pack.INDEX_HEADER.size +
                slot * module.content_pack.INDEX_SLOT.size)
            entry = json.loads(data[offset:offset + length].decode("utf-8"))
            self.assertEqual(entry["character"], "汉")
            self.assertEqual(entry["definition"], "汉族；汉语。")

    def test_pinyin_normalization_accepts_tones_umlaut_and_numbers(self):
        self.assertEqual(module.pinyin_syllables("hàn guī lǜ lu:4"),
                         ["han", "gui", "lv"])

    def test_existing_output_and_corrupt_data_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source.tab"
            source.write_text("规\t" + markup("规", "guī", "规则。"), encoding="utf-8")
            output = Path(tmp) / "card"
            module.convert(source, output)
            with self.assertRaises(ValueError):
                module.convert(source, output)
            data = output / "handict/dictionary/data.bin"
            value = bytearray(data.read_bytes())
            value[-1] ^= 1
            data.write_bytes(value)
            with self.assertRaisesRegex(ValueError, "checksum"):
                module.content_pack.validate_index(output / "handict")


if __name__ == "__main__":
    unittest.main()
