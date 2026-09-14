"""Convert the Guoxuedashi Xinhua TSV/ZIP export into the Tab5 SD dictionary index."""

import argparse
from contextlib import contextmanager
from html.parser import HTMLParser
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unicodedata
import zipfile
import zlib

import content_pack


SOURCE_ID = "guoxuedashi-xinhua-community"
SOURCE_URL = "https://github.com/lxs602/Chinese-Mandarin-Dictionaries"
CBIN_CONVERTER_COMMIT = "c420999fe79adb0bc2a480c4a64fd33fc6e34519"
DEFAULT_FONT = (
    content_pack.ROOT
    / "managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
)
PINYIN_RE = re.compile(
    r"[A-Za-züÜāáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜńňǹḿ]+(?:[ '\-]"
    r"[A-Za-züÜāáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜńňǹḿ]+)*"
)
CJK_RE = r"\u4e00-\u9fff"
COMMON_CHARACTERS = (
    "的一是不了人我在有他这为之大来以个中上们到说国和地也子时道出而要于就下得可你"
    "年生自会那后能对着事其里所去行过家十用发天如然作方成者多日都三小军二无同么"
    "经法当起与好看学进种将还分此心前面又定见只主没公从知全工己使情明性汉规矩"
)
COMMON_RANK = {character: rank for rank, character in enumerate(COMMON_CHARACTERS)}
CNCHAR_SOURCE_URL = "https://github.com/theajack/cnchar"


class TableCells(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.cells = []
        self.current = None

    def handle_starttag(self, tag, attrs):
        if tag.lower() == "td":
            self.current = []
        elif tag.lower() in ("br", "p", "div") and self.current is not None:
            self.current.append("\n")

    def handle_endtag(self, tag):
        if tag.lower() == "td" and self.current is not None:
            self.cells.append("".join(self.current))
            self.current = None

    def handle_data(self, data):
        if self.current is not None:
            self.current.append(data)


def normalize_lines(value):
    value = value.replace("\ufffd", "").replace("\r", "\n")
    value = re.sub(r"[\t\u00a0\u3000 ]+", " ", value)
    return [line.strip() for line in value.split("\n") if line.strip()]


def truncate_utf8(value, limit):
    encoded = value.encode("utf-8")
    if len(encoded) <= limit:
        return value
    suffix = "…"
    data = encoded[:limit - len(suffix.encode("utf-8"))]
    return data.decode("utf-8", errors="ignore").rstrip() + suffix


def extract_words(character, definition):
    words = []
    patterns = [
        (rf"～([{CJK_RE}]{{1,3}})", lambda match: character + match.group(1)),
        (rf"([{CJK_RE}]{{1,3}})～", lambda match: match.group(1) + character),
    ]
    for pattern, build in patterns:
        for match in re.finditer(pattern, definition):
            word = build(match)
            if word not in words and len(word.encode("utf-8")) <= 12:
                words.append(word)
                if len(words) == 12:
                    return words
    return words


def parse_entry(character, markup):
    if len(character) != 1 or not content_pack.INDEX_FIRST <= ord(character) <= 0x9FFF:
        return None
    parser = TableCells()
    try:
        parser.feed(markup)
        parser.close()
    except Exception:
        return None
    basic = None
    for index, cell in enumerate(parser.cells[:-1]):
        label = "".join(normalize_lines(cell))
        if "基本解释" in label or "Basic explanation" in label:
            basic = parser.cells[index + 1]
            break
    if basic is None:
        return None
    lines = normalize_lines(basic)
    if not lines:
        return None
    pinyin_index = None
    matches = []
    for index, line in enumerate(lines[:3]):
        matches = PINYIN_RE.findall(line.replace(character, " ", 1))
        if matches:
            pinyin_index = index
            break
    pinyin = " ".join(matches[:4]).strip()
    if not pinyin or pinyin_index is None or pinyin_index + 1 >= len(lines):
        return None
    definition = truncate_utf8("\n".join(lines[pinyin_index + 1:]), 2000)
    if not definition:
        return None
    return {
        "character": character,
        "pinyin": truncate_utf8(pinyin, 80),
        "radical": "",
        "stroke_count": 0,
        "structure": "",
        "definition": definition,
        "words": extract_words(character, definition),
        "stroke_order": [],
        "source": SOURCE_ID,
    }


@contextmanager
def open_tsv(path):
    path = Path(path)
    archive = None
    raw = None
    if path.suffix.lower() == ".zip":
        archive = zipfile.ZipFile(path)
        names = [name for name in archive.namelist()
                 if Path(name).suffix.lower() in (".tab", ".tsv", ".txt")]
        if len(names) != 1:
            archive.close()
            raise ValueError("ZIP must contain exactly one TAB/TSV/TXT dictionary file")
        raw = archive.open(names[0])
    else:
        raw = path.open("rb")
    stream = io.TextIOWrapper(raw, encoding="utf-8-sig", errors="replace", newline="")
    try:
        yield stream
    finally:
        stream.close()
        if archive:
            archive.close()


def read_entries(path, limit=None):
    entries = {}
    skipped = 0
    replacements = 0
    with open_tsv(path) as stream:
        for line_number, line in enumerate(stream, 1):
            replacements += line.count("\ufffd")
            line = line.rstrip("\r\n")
            if "\t" not in line:
                skipped += 1
                continue
            character, markup = line.split("\t", 1)
            entry = parse_entry(character.strip(), markup)
            if entry is None:
                skipped += 1
                continue
            if entry["character"] in entries:
                raise ValueError(f"Duplicate character on input line {line_number}: {entry['character']}")
            entries[entry["character"]] = entry
            if limit and len(entries) >= limit:
                break
    if not entries:
        raise ValueError("No usable BMP Chinese entries found")
    return entries, skipped, replacements


def read_cnchar_metadata(radicals_path, structures_path):
    radicals = json.loads(Path(radicals_path).read_text(encoding="utf-8"))
    structures = json.loads(Path(structures_path).read_text(encoding="utf-8"))
    if not isinstance(radicals, dict) or not isinstance(structures, dict):
        raise ValueError("cnchar radical and structure data must be JSON objects")
    metadata = {}
    for radical, encoded in radicals.items():
        if not isinstance(radical, str) or not isinstance(encoded, str):
            raise ValueError("Invalid cnchar radical data")
        if radical == "*":
            for match in re.finditer(r"(.)([a-z*])(\d{1,2})", encoded):
                character, structure_id = match.group(1), match.group(2)
                if content_pack.INDEX_FIRST <= ord(character) <= 0x9FFF:
                    metadata[character] = (character, structures.get(structure_id, ""))
            continue
        separator = encoded.find(":")
        payload = encoded[separator + 1:] if separator >= 0 else ""
        if not payload or len(payload) % 2:
            raise ValueError(f"Invalid cnchar radical group: {radical}")
        for index in range(0, len(payload), 2):
            character, structure_id = payload[index], payload[index + 1]
            if content_pack.INDEX_FIRST <= ord(character) <= 0x9FFF:
                metadata[character] = (radical, structures.get(structure_id, ""))
    if not metadata:
        raise ValueError("cnchar radical data contains no usable BMP Chinese entries")
    return metadata


def encode_index(entries):
    slots = bytearray(content_pack.INDEX_SLOTS * content_pack.INDEX_SLOT.size)
    records = bytearray()
    for character in sorted(entries, key=ord):
        record = json.dumps(entries[character], ensure_ascii=False,
                            separators=(",", ":")).encode("utf-8")
        if len(record) > content_pack.INDEX_ENTRY_LIMIT:
            raise ValueError(f"Converted entry exceeds device limit: {character}")
        slot = ord(character) - content_pack.INDEX_FIRST
        content_pack.INDEX_SLOT.pack_into(slots, slot * content_pack.INDEX_SLOT.size,
                                          len(records), len(record))
        records.extend(record)
    header = content_pack.INDEX_HEADER.pack(
        content_pack.INDEX_MAGIC, content_pack.INDEX_VERSION, content_pack.INDEX_FIRST,
        content_pack.INDEX_SLOTS, len(entries), len(records), zlib.crc32(records),
        zlib.crc32(slots))
    return header + slots, records


def pinyin_syllables(value):
    syllables = []
    for raw in re.findall(r"[A-Za-züÜāáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜńňǹḿ:]+[1-5]?", value):
        normalized = content_pack.normalize_pinyin(raw)
        if normalized and normalized not in syllables:
            syllables.append(normalized)
    return syllables


def pinyin_keys(value):
    keys = []
    tone_marks = {"\u0304": 1, "\u0301": 2, "\u030c": 3, "\u0300": 4}
    for raw in re.findall(r"[A-Za-züÜāáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜńňǹḿ:]+[1-5]?", value):
        base = content_pack.normalize_pinyin(raw)
        if not base:
            continue
        explicit = re.search(r"([1-5])$", raw)
        tone = (0 if explicit.group(1) == "5" else int(explicit.group(1))) if explicit else 0
        if not explicit:
            for mark in unicodedata.normalize("NFD", raw):
                if mark in tone_marks:
                    tone = tone_marks[mark]
                    break
        for key in (base, f"{base}{tone}"):
            if key not in keys:
                keys.append(key)
    return keys


def encode_pinyin_index(entries):
    groups = {}
    for character, entry in entries.items():
        for syllable in pinyin_keys(entry["pinyin"]):
            groups.setdefault(syllable, set()).add(character)
    directory = bytearray()
    data = bytearray()
    for syllable in sorted(groups):
        characters = "".join(sorted(groups[syllable],
                                    key=lambda character: (COMMON_RANK.get(character, 10000),
                                                           ord(character)))).encode("utf-8")
        count = len(characters) // 3
        if not count or count > 1024:
            raise ValueError(f"Too many candidates for pinyin syllable: {syllable}")
        directory.extend(content_pack.PINYIN_RECORD.pack(
            syllable.encode("ascii").ljust(8, b"\0"), len(data), count, 0))
        data.extend(characters)
    if not directory or len(groups) > content_pack.PINYIN_MAX_RECORDS or \
            len(data) > content_pack.PINYIN_DATA_LIMIT:
        raise ValueError("Generated pinyin index exceeds device limits")
    payload = directory + data
    header = content_pack.PINYIN_HEADER.pack(
        content_pack.PINYIN_MAGIC, content_pack.PINYIN_VERSION, len(groups), len(directory),
        len(data), zlib.crc32(payload))
    return header + payload, len(groups)


def font_converter():
    candidates = [
        content_pack.ROOT /
        "managed_components/78__xiaozhi-fonts/build/tools/lv_font_conv/lv_font_conv.js",
        content_pack.ROOT / "build/dictionary-font-converter/lv_font_conv.js",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    destination = candidates[-1].parent
    if destination.exists():
        raise ValueError(f"Incomplete font converter directory: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    git = shutil.which("git")
    npm = shutil.which("npm.cmd") or shutil.which("npm")
    if not git or not npm:
        raise ValueError("Generating the SD font requires git, Node.js and npm")
    subprocess.run([git, "clone", "--quiet", "https://github.com/78/lv_font_conv.git",
                    str(destination)], check=True)
    subprocess.run([git, "-C", str(destination), "checkout", "--quiet",
                    CBIN_CONVERTER_COMMIT], check=True)
    subprocess.run([npm, "ci", "--omit=dev", "--ignore-scripts"], cwd=destination, check=True)
    return candidates[-1]


def build_dictionary_font(output, font):
    font = Path(font).resolve()
    if not font.is_file():
        raise ValueError(f"Dictionary font not found: {font}")
    node = shutil.which("node")
    if not node:
        raise ValueError("Generating the SD font requires Node.js")
    subprocess.run([
        node, str(font_converter()), "--no-compress", "--no-prefilter", "--no-kerning",
        "--font", str(font), "--format", "cbin", "--bpp", "1", "--size", "28",
        "-r", "0x20-0x2ff,0x4e00-0x9fff", "-o", str(output),
    ], check=True)
    size = output.stat().st_size
    if not 128 * 1024 <= size <= 4 * 1024 * 1024:
        raise ValueError("Generated dictionary font size is outside the device limit")
    return size, zlib.crc32(output.read_bytes())


def convert(input_path, output, limit=None, font=None, radicals=None, structures=None,
            radical_license=None):
    output = Path(output).resolve()
    if output.exists():
        raise ValueError("Output already exists; use a new directory to preserve previous files")
    entries, skipped, replacements = read_entries(input_path, limit)
    if bool(radicals) != bool(structures):
        raise ValueError("cnchar radical and structure files must be provided together")
    metadata_count = 0
    if radicals:
        metadata = read_cnchar_metadata(radicals, structures)
        for character, entry in entries.items():
            if character in metadata:
                entry["radical"], entry["structure"] = metadata[character]
                metadata_count += 1
    index, records = encode_index(entries)
    pinyin_index, pinyin_count = encode_pinyin_index(entries)
    shutil.copytree(content_pack.ROOT / "content/sdcard", output)
    if radical_license:
        licenses = output / "handict/licenses"
        licenses.mkdir(exist_ok=True)
        shutil.copyfile(radical_license, licenses / "CNCHAR-MIT.txt")
    dictionary = output / "handict/dictionary"
    (dictionary / "index.bin").write_bytes(index)
    (dictionary / "data.bin").write_bytes(records)
    (dictionary / "pinyin.idx").write_bytes(pinyin_index)
    font_size = 0
    font_crc = 0
    if font:
        font_size, font_crc = build_dictionary_font(dictionary / "font-28-1.bin", font)
    manifest_path = output / "handict/manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["indexed_dictionary"] = {
        "format": "handict-index-v1",
        "entries": len(entries),
        "source_id": SOURCE_ID,
        "source_url": SOURCE_URL,
        "attribution": "Adapted from the Guoxuedashi community Xinhua dataset",
        "license_status": "Upstream redistribution terms must be verified",
        "notice": "Community offline Chinese learning data; not an official printed dictionary",
        "pinyin_index": {
            "path": "dictionary/pinyin.idx",
            "format": "handict-pinyin-v1",
            "syllables": pinyin_count,
            "bytes": len(pinyin_index),
        },
    }
    if radicals:
        manifest["indexed_dictionary"]["character_metadata"] = {
            "source_id": "cnchar-radical",
            "source_url": CNCHAR_SOURCE_URL,
            "entries": metadata_count,
            "fields": ["radical", "structure"],
            "license": "MIT",
            "license_file": "licenses/CNCHAR-MIT.txt" if radical_license else "",
        }
    if font_size:
        manifest["indexed_dictionary"]["font"] = {
            "path": "dictionary/font-28-1.bin",
            "size": 28,
            "bpp": 1,
            "bytes": font_size,
            "crc32": font_crc,
        }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
    content_pack.validate(output / "handict")
    return len(entries), len(index), len(records), font_size, skipped, replacements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True,
                        help="Guoxuedashi .tab/.tsv file or ZIP containing it")
    parser.add_argument("--output", type=Path, required=True,
                        help="New SD-card directory to create")
    parser.add_argument("--limit", type=int, help="Convert only N entries for development tests")
    parser.add_argument("--font", type=Path, default=DEFAULT_FONT,
                        help="CJK TTF/OTF used to generate the SD dictionary font")
    parser.add_argument("--radicals", type=Path,
                        help="cnchar radical plugin radicals.json")
    parser.add_argument("--structures", type=Path,
                        help="cnchar radical plugin struct.json")
    parser.add_argument("--radical-license", type=Path,
                        help="cnchar MIT LICENSE file to include in the content pack")
    args = parser.parse_args()
    if args.limit is not None and args.limit <= 0:
        parser.error("--limit must be positive")
    try:
        count, index_bytes, data_bytes, font_bytes, skipped, replacements = convert(
            args.input, args.output, args.limit, args.font, args.radicals, args.structures,
            args.radical_license)
    except (ValueError, OSError, json.JSONDecodeError, zipfile.BadZipFile,
            subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Dictionary import error: {exc}\n")
    print(f"Converted and validated {count} entries")
    print(f"index.bin: {index_bytes} bytes; data.bin: {data_bytes} bytes")
    print(f"font-28-1.bin: {font_bytes} bytes")
    print(f"Skipped input rows: {skipped}; removed invalid UTF-8 markers: {replacements}")
    print(f"Copy {args.output.resolve() / 'handict'} to SD:/handict and restart the device.")


if __name__ == "__main__":
    main()
