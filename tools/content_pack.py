"""Validate and prepare a bounded microSD content pack (Python standard library only)."""
import argparse
import json
import math
from pathlib import Path
import re
import shutil
import struct
import unicodedata
import zlib

ROOT = Path(__file__).resolve().parents[1]
INDEX_MAGIC = b"HDX1"
INDEX_VERSION = 1
INDEX_FIRST = 0x4E00
INDEX_SLOTS = 0x9FFF - INDEX_FIRST + 1
INDEX_HEADER = struct.Struct("<4s7I")
INDEX_SLOT = struct.Struct("<II")
INDEX_ENTRY_LIMIT = 4096
STROKE_INDEX_MAGIC = b"HST1"
STROKE_INDEX_VERSION = 1
STROKE_INDEX_HEADER = struct.Struct("<4s7I")
STROKE_RECORD_MAGIC = b"HSG1"
STROKE_RECORD_VERSION = 1
STROKE_RECORD_HEADER = struct.Struct("<4sBBH")
STROKE_DIRECTORY = struct.Struct("<IIIHBB")
STROKE_RECORD_LIMIT = 64 * 1024
PINYIN_MAGIC = b"HPY1"
PINYIN_VERSION = 1
PINYIN_HEADER = struct.Struct("<4s5I")
PINYIN_RECORD = struct.Struct("<8sIHH")
PINYIN_MAX_RECORDS = 2048
PINYIN_DATA_LIMIT = 256 * 1024


def normalize_pinyin(value):
    value = value.strip().lower().replace("u:", "v")
    value = value.translate(str.maketrans({"ü": "v", "ǖ": "v", "ǘ": "v", "ǚ": "v", "ǜ": "v"}))
    value = "".join(char for char in unicodedata.normalize("NFD", value)
                    if unicodedata.category(char) != "Mn")
    value = re.sub(r"[1-5]$", "", value)
    return value if re.fullmatch(r"[a-z]{1,7}", value) else ""


def read_json(path, limit):
    if path.stat().st_size > limit:
        raise ValueError(f"File exceeds {limit} bytes: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def text(obj, key, limit, required=False):
    value = obj.get(key, "")
    if not isinstance(value, str) or len(value.encode("utf-8")) > limit or "\0" in value:
        raise ValueError(f"Invalid {key}")
    if required and not value:
        raise ValueError(f"Missing {key}")
    return value


def validate_entry(entry):
    if not isinstance(entry, dict):
        raise ValueError("Entry must be an object")
    character = text(entry, "character", 4, True)
    if len(character) != 1 or not 0x4E00 <= ord(character) <= 0x9FFF:
        raise ValueError("Starter firmware supports one BMP CJK character per entry")
    for key, limit in [("pinyin", 80), ("radical", 24), ("structure", 48),
                       ("definition", 2048), ("source", 128)]:
        text(entry, key, limit, key in ("pinyin", "definition", "source"))
    count = entry.get("stroke_count")
    if type(count) is not int or not 1 <= count <= 64:
        raise ValueError("stroke_count must be an integer in 1..64")
    for key, maximum in [("words", 12), ("stroke_order", 64)]:
        values = entry.get(key)
        if not isinstance(values, list) or len(values) > maximum:
            raise ValueError(f"Invalid {key}")
        for value in values:
            text({key: value}, key, 96, True)
    if len(entry["stroke_order"]) != count:
        raise ValueError("Stroke names must match stroke_count")
    encoded = json.dumps(entry, ensure_ascii=False).encode("utf-8")
    if len(encoded) > 16384:
        raise ValueError("Entry exceeds firmware read limit")
    return character


def validate_indexed_entry(entry, expected_character):
    if not isinstance(entry, dict):
        raise ValueError("Indexed entry must be an object")
    character = text(entry, "character", 4, True)
    if character != expected_character:
        raise ValueError(f"Indexed entry character mismatch: {character!r}")
    for key, limit in [("pinyin", 80), ("radical", 24), ("structure", 48),
                       ("definition", 2048), ("source", 128)]:
        text(entry, key, limit, key in ("pinyin", "definition", "source"))
    count = entry.get("stroke_count", 0)
    if type(count) is not int or not 0 <= count <= 64:
        raise ValueError("Indexed stroke_count must be an integer in 0..64")
    for key, maximum in [("words", 12), ("stroke_order", 64)]:
        values = entry.get(key)
        if not isinstance(values, list) or len(values) > maximum:
            raise ValueError(f"Invalid indexed {key}")
        for value in values:
            text({key: value}, key, 96, True)
    if len(entry["stroke_order"]) != count:
        raise ValueError("Indexed stroke names must match stroke_count")


def validate_index(folder):
    folder = Path(folder)
    index_path = folder / "dictionary/index.bin"
    data_path = folder / "dictionary/data.bin"
    if not index_path.exists() and not data_path.exists():
        return 0
    if not index_path.is_file() or not data_path.is_file():
        raise ValueError("Indexed dictionary needs both index.bin and data.bin")
    index = index_path.read_bytes()
    expected_size = INDEX_HEADER.size + INDEX_SLOTS * INDEX_SLOT.size
    if len(index) != expected_size:
        raise ValueError("Indexed dictionary index size is invalid")
    magic, version, first, slots, count, data_size, data_crc, slots_crc = \
        INDEX_HEADER.unpack_from(index)
    if (magic, version, first, slots) != (INDEX_MAGIC, INDEX_VERSION, INDEX_FIRST, INDEX_SLOTS):
        raise ValueError("Indexed dictionary header is invalid")
    slot_data = index[INDEX_HEADER.size:]
    if zlib.crc32(slot_data) != slots_crc:
        raise ValueError("Indexed dictionary slot checksum mismatch")
    if data_path.stat().st_size != data_size or data_size > 64 * 1024 * 1024:
        raise ValueError("Indexed dictionary data size is invalid")
    checksum = 0
    with data_path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            checksum = zlib.crc32(block, checksum)
    if checksum != data_crc:
        raise ValueError("Indexed dictionary data checksum mismatch")
    found = 0
    expected_offset = 0
    with data_path.open("rb") as stream:
        for slot in range(INDEX_SLOTS):
            offset, length = INDEX_SLOT.unpack_from(slot_data, slot * INDEX_SLOT.size)
            if length == 0:
                if offset != 0:
                    raise ValueError("Empty indexed dictionary slot has an offset")
                continue
            if length > INDEX_ENTRY_LIMIT or offset != expected_offset or offset + length > data_size:
                raise ValueError("Indexed dictionary record range is invalid")
            stream.seek(offset)
            try:
                entry = json.loads(stream.read(length).decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise ValueError("Indexed dictionary record is not valid UTF-8 JSON") from exc
            validate_indexed_entry(entry, chr(INDEX_FIRST + slot))
            expected_offset += length
            found += 1
    if found != count or expected_offset != data_size:
        raise ValueError("Indexed dictionary record count or packing is invalid")
    return found


def validate_stroke_record(data):
    if len(data) < STROKE_RECORD_HEADER.size or len(data) > STROKE_RECORD_LIMIT:
        raise ValueError("Vector stroke record size is invalid")
    magic, version, count, header_size = STROKE_RECORD_HEADER.unpack_from(data)
    expected_header = STROKE_RECORD_HEADER.size + count * STROKE_DIRECTORY.size
    if magic != STROKE_RECORD_MAGIC or version != STROKE_RECORD_VERSION:
        raise ValueError("Vector stroke record header is invalid")
    if not 1 <= count <= 64 or header_size != expected_header or header_size > len(data):
        raise ValueError("Vector stroke directory is invalid")
    for stroke in range(count):
        values = STROKE_DIRECTORY.unpack_from(
            data, STROKE_RECORD_HEADER.size + stroke * STROKE_DIRECTORY.size)
        path_offset, path_length, median_offset, median_count, name_code, flags = values
        if (path_length == 0 or path_offset < header_size or
                path_offset + path_length > len(data) or
                median_offset < path_offset + path_length or
                median_offset + median_count * 4 > len(data) or
                (name_code and not chr(name_code).isalpha()) or flags & ~1):
            raise ValueError("Vector stroke range is invalid")
        cursor = path_offset
        while cursor < path_offset + path_length:
            opcode = data[cursor]
            cursor += 1
            operands = (2, 2, 4, 6, 0)[opcode] if opcode <= 4 else -1
            if operands < 0 or cursor + operands * 2 > path_offset + path_length:
                raise ValueError("Vector stroke bytecode is invalid")
            cursor += operands * 2
        if cursor != path_offset + path_length:
            raise ValueError("Vector stroke bytecode is truncated")


def validate_stroke_index(folder):
    folder = Path(folder)
    index_path = folder / "dictionary/strokes.idx"
    data_path = folder / "dictionary/strokes.dat"
    if not index_path.exists() and not data_path.exists():
        return 0
    if not index_path.is_file() or not data_path.is_file():
        raise ValueError("Vector strokes need both strokes.idx and strokes.dat")
    index = index_path.read_bytes()
    expected_size = STROKE_INDEX_HEADER.size + INDEX_SLOTS * INDEX_SLOT.size
    if len(index) != expected_size:
        raise ValueError("Vector stroke index size is invalid")
    magic, version, first, slots, count, data_size, data_crc, slots_crc = \
        STROKE_INDEX_HEADER.unpack_from(index)
    if ((magic, version, first, slots) !=
            (STROKE_INDEX_MAGIC, STROKE_INDEX_VERSION, INDEX_FIRST, INDEX_SLOTS)):
        raise ValueError("Vector stroke index header is invalid")
    slot_data = index[STROKE_INDEX_HEADER.size:]
    if zlib.crc32(slot_data) != slots_crc:
        raise ValueError("Vector stroke slot checksum mismatch")
    if data_path.stat().st_size != data_size or data_size > 32 * 1024 * 1024:
        raise ValueError("Vector stroke data size is invalid")
    data = data_path.read_bytes()
    if zlib.crc32(data) != data_crc:
        raise ValueError("Vector stroke data checksum mismatch")
    found = 0
    expected_offset = 0
    for slot in range(INDEX_SLOTS):
        offset, length = INDEX_SLOT.unpack_from(slot_data, slot * INDEX_SLOT.size)
        if length == 0:
            if offset != 0:
                raise ValueError("Empty vector stroke slot has an offset")
            continue
        if length > STROKE_RECORD_LIMIT or offset != expected_offset or offset + length > data_size:
            raise ValueError("Vector stroke record range is invalid")
        validate_stroke_record(data[offset:offset + length])
        expected_offset += length
        found += 1
    if found != count or expected_offset != data_size:
        raise ValueError("Vector stroke record count or packing is invalid")
    return found


def validate_pinyin_index(folder):
    path = Path(folder) / "dictionary/pinyin.idx"
    if not path.exists():
        return 0
    raw = path.read_bytes()
    if len(raw) < PINYIN_HEADER.size:
        raise ValueError("Pinyin index is truncated")
    magic, version, count, directory_size, data_size, payload_crc = \
        PINYIN_HEADER.unpack_from(raw)
    if magic != PINYIN_MAGIC or version != PINYIN_VERSION:
        raise ValueError("Pinyin index header is invalid")
    if not 1 <= count <= PINYIN_MAX_RECORDS or directory_size != count * PINYIN_RECORD.size:
        raise ValueError("Pinyin index directory is invalid")
    if data_size > PINYIN_DATA_LIMIT or len(raw) != PINYIN_HEADER.size + directory_size + data_size:
        raise ValueError("Pinyin index data size is invalid")
    payload = raw[PINYIN_HEADER.size:]
    if zlib.crc32(payload) != payload_crc:
        raise ValueError("Pinyin index checksum mismatch")
    data = payload[directory_size:]
    previous = ""
    expected_offset = 0
    for record in range(count):
        key_raw, offset, candidates, reserved = PINYIN_RECORD.unpack_from(
            payload, record * PINYIN_RECORD.size)
        try:
            key = key_raw.rstrip(b"\0").decode("ascii")
        except UnicodeDecodeError as exc:
            raise ValueError("Pinyin index key is not ASCII") from exc
        base = key[:-1] if key[-1:] in "01234" else key
        if normalize_pinyin(base) != base or len(key) > 8 or key <= previous or reserved != 0:
            raise ValueError("Pinyin index keys are invalid or unsorted")
        byte_count = candidates * 3
        if not 1 <= candidates <= 1024 or offset != expected_offset or byte_count > len(data) - offset:
            raise ValueError("Pinyin candidate range is invalid")
        seen = set()
        for cursor in range(offset, offset + byte_count, 3):
            try:
                character = data[cursor:cursor + 3].decode("utf-8")
            except UnicodeDecodeError as exc:
                raise ValueError("Pinyin candidate is not valid UTF-8") from exc
            if len(character) != 1 or not INDEX_FIRST <= ord(character) <= 0x9FFF or character in seen:
                raise ValueError("Pinyin candidate is invalid or duplicated")
            seen.add(character)
        previous = key
        expected_offset += byte_count
    if expected_offset != data_size:
        raise ValueError("Pinyin candidate data is not tightly packed")
    return count


def validate_index_font(folder, manifest):
    indexed = manifest.get("indexed_dictionary")
    if not isinstance(indexed, dict):
        return
    if "font" in indexed:
        font = indexed["font"]
        profiles = {
            "dictionary/font-28-1.bin": 1,
            "dictionary/font-28-2.bin": 2,
        }
        if not isinstance(font, dict) or font.get("path") not in profiles:
            raise ValueError("Indexed dictionary font metadata is invalid")
        if font.get("size") != 28 or font.get("bpp") != profiles[font["path"]]:
            raise ValueError("Indexed dictionary font profile is unsupported")
        path = Path(folder) / font["path"]
        if not path.is_file() or not 128 * 1024 <= path.stat().st_size <= 4 * 1024 * 1024:
            raise ValueError("Indexed dictionary font size is invalid")
        if (font.get("bytes") != path.stat().st_size or
                font.get("crc32") != zlib.crc32(path.read_bytes())):
            raise ValueError("Indexed dictionary font checksum mismatch")
    if "candidate_font" in indexed:
        font = indexed["candidate_font"]
        profiles = {
            "dictionary/font-40-1.bin": (40, 4 * 1024 * 1024),
            "dictionary/font-56-kai-1.bin": (56, 8 * 1024 * 1024),
            "dictionary/font-56-heavy-1.bin": (56, 8 * 1024 * 1024),
        }
        if (not isinstance(font, dict) or font.get("path") not in profiles or
                font.get("size") != profiles[font["path"]][0] or font.get("bpp") != 1):
            raise ValueError("Indexed dictionary candidate font metadata is invalid")
        path = Path(folder) / font["path"]
        if (not path.is_file() or
                not 128 * 1024 <= path.stat().st_size <= profiles[font["path"]][1]):
            raise ValueError("Indexed dictionary candidate font size is invalid")
        if (font.get("bytes") != path.stat().st_size or
                font.get("crc32") != zlib.crc32(path.read_bytes())):
            raise ValueError("Indexed dictionary candidate font checksum mismatch")
    if "candidate_scalable_font" in indexed:
        font = indexed["candidate_scalable_font"]
        if (not isinstance(font, dict) or
                font.get("path") != "dictionary/NotoSansSC-Medium.ttf" or
                font.get("format") != "truetype" or font.get("size") != 56):
            raise ValueError("Indexed dictionary scalable candidate font metadata is invalid")
        path = Path(folder) / font["path"]
        if not path.is_file() or not 1024 * 1024 <= path.stat().st_size <= 32 * 1024 * 1024:
            raise ValueError("Indexed dictionary scalable candidate font size is invalid")
        if (font.get("bytes") != path.stat().st_size or
                font.get("crc32") != zlib.crc32(path.read_bytes())):
            raise ValueError("Indexed dictionary scalable candidate font checksum mismatch")
    if "scalable_font" in indexed:
        font = indexed["scalable_font"]
        if (not isinstance(font, dict) or
                font.get("path") != "dictionary/SourceHanSansSC-Normal.otf" or
                font.get("format") != "opentype"):
            raise ValueError("Indexed dictionary scalable font metadata is invalid")
        path = Path(folder) / font["path"]
        if not path.is_file() or not 1024 * 1024 <= path.stat().st_size <= 32 * 1024 * 1024:
            raise ValueError("Indexed dictionary scalable font size is invalid")
        if (font.get("bytes") != path.stat().st_size or
                font.get("crc32") != zlib.crc32(path.read_bytes())):
            raise ValueError("Indexed dictionary scalable font checksum mismatch")


def validate_timetable(record):
    if not isinstance(record, dict) or "days" not in record:
        raise ValueError("Timetable needs days")
    for key in ("days", "supplies"):
        if key not in record:
            continue
        days = record[key]
        if not isinstance(days, list) or len(days) not in (5, 7):
            raise ValueError(f"{key} needs five or seven day arrays, Monday first")
        for day in days:
            if not isinstance(day, list) or len(day) > 8:
                raise ValueError("At most eight lessons/items per day")
            for value in day:
                text({key: value}, key, 32)
                if any(c in value for c in "\r\n\t"):
                    raise ValueError("Timetable text must be one line")


def validate_qweather(record):
    if not isinstance(record, dict):
        raise ValueError("QWeather config must be an object")
    host = text(record, "api_host", 128, True)
    key = text(record, "api_key", 160, True)
    text(record, "city", 48, True)
    if (host.startswith(".") or host.endswith(".") or
            not re.fullmatch(r"[A-Za-z0-9.-]+", host)):
        raise ValueError("QWeather api_host must be a host name without scheme or path")
    if any(ord(char) <= 0x20 or ord(char) >= 0x7F for char in key):
        raise ValueError("QWeather api_key must contain printable ASCII without spaces")
    for name, minimum, maximum in (("latitude", -90, 90), ("longitude", -180, 180)):
        value = record.get(name)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError(f"QWeather {name} must be a finite number")
        if not minimum <= value <= maximum:
            raise ValueError(f"QWeather {name} is out of range")


def validate_weather_page_graphics(folder):
    graphics = folder / "ui/graphics/weather-page"
    expected = {
        "qingdao-hero.png": (760, 344),
        "qingdao-hero-original.png": (1997, 787),
        "air-quality.png": (76, 76),
        "precipitation.png": (76, 76),
        "sunrise-sunset.png": (76, 76),
        "lifestyle-index.png": (76, 76),
        **{f"condition-{name}.png": (192, 192) for name in
           ("sunny", "partly-cloudy", "cloudy", "rain", "thunderstorm", "snow", "fog", "wind")},
    }
    if not graphics.is_dir():
        raise ValueError("Weather page SD graphics are missing")
    files = {path.name for path in graphics.iterdir() if path.is_file()}
    if files != set(expected):
        raise ValueError("Weather page SD graphics set is incomplete or contains unknown files")
    total = 0
    for name, dimensions in expected.items():
        data = (graphics / name).read_bytes()
        total += len(data)
        if not 45 <= len(data) <= 2 * 1024 * 1024 or data[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"Invalid weather page PNG: {name}")
        if data[12:16] != b"IHDR" or struct.unpack(">II", data[16:24]) != dimensions:
            raise ValueError(f"Unexpected weather page PNG dimensions: {name}")
    if total > 3 * 1024 * 1024:
        raise ValueError("Weather page SD graphics exceed 3 MiB")


def validate_alarm_page_graphics(folder):
    graphics = folder / "ui/graphics/alarm-page"
    expected = {"alarm-sunrise.png": (512, 512)}
    if not graphics.is_dir():
        raise ValueError("Alarm page SD graphics are missing")
    files = {path.name for path in graphics.iterdir() if path.is_file()}
    if files != set(expected):
        raise ValueError("Alarm page SD graphics set is incomplete or contains unknown files")
    for name, dimensions in expected.items():
        data = (graphics / name).read_bytes()
        if not 45 <= len(data) <= 2 * 1024 * 1024 or data[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"Invalid alarm page PNG: {name}")
        if data[12:16] != b"IHDR" or struct.unpack(">II", data[16:24]) != dimensions:
            raise ValueError(f"Unexpected alarm page PNG dimensions: {name}")


def validate(folder):
    folder = Path(folder)
    manifest = read_json(folder / "manifest.json", 4096)
    if type(manifest.get("schema_version")) is not int or manifest["schema_version"] != 1:
        raise ValueError("Unsupported content schema")
    validate_index_font(folder, manifest)
    count = 0
    for path in (folder / "dictionary/entries").glob("*.json"):
        character = validate_entry(read_json(path, 16384))
        if path.name != f"{ord(character):04X}.json":
            raise ValueError(f"Entry filename mismatch: {path.name}")
        count += 1
    count = max(count, validate_index(folder))
    validate_stroke_index(folder)
    validate_pinyin_index(folder)
    validate_weather_page_graphics(folder)
    validate_alarm_page_graphics(folder)
    timetable = folder / "timetable.json"
    if timetable.exists():
        validate_timetable(read_json(timetable, 8192))
    qweather = folder / "qweather.json"
    if qweather.exists():
        validate_qweather(read_json(qweather, 2048))
    weather = folder / "weather.json"
    if weather.exists():
        record = read_json(weather, 48 * 1024)
        text(record, "city", 96)
        text(record, "updated_at", 64)
        if record.get("schema_version") == 2:
            if not isinstance(record.get("current"), dict):
                raise ValueError("Weather cache current data must be an object")
            if not isinstance(record.get("days", []), list) or len(record.get("days", [])) > 4:
                raise ValueError("Weather cache supports at most four forecast days")
        else:
            text(record, "summary", 512)
            if record.get("summary") and not record.get("updated_at"):
                raise ValueError("Weather cache must have an update timestamp")
    for path in (folder / "phonetics").rglob("*.ogg"):
        if not 0 < path.stat().st_size <= 256 * 1024:
            raise ValueError(f"Audio size out of bounds: {path}")
        with path.open("rb") as stream:
            header = stream.read(128)
        if header[:4] != b"OggS" or b"OpusHead" not in header:
            raise ValueError(f"Expected Ogg/Opus: {path}")
    for directory in (folder / "dictionary/strokes").glob("*"):
        if not directory.is_dir() or not re.fullmatch(r"[0-9A-F]{4}", directory.name):
            raise ValueError("Invalid stroke directory")
        entry = read_json(folder / "dictionary/entries" / (directory.name + ".json"), 16384)
        expected = {f"{i:02d}.png" for i in range(1, entry["stroke_count"] + 1)}
        if {p.name for p in directory.iterdir()} != expected:
            raise ValueError("Stroke frames must exactly match entry count")
        for filename in expected:
            frame = directory / filename
            if frame.stat().st_size > 128 * 1024: raise ValueError("Stroke frame too large")
            validate_stroke_png(frame.read_bytes())
    return count


def validate_stroke_png(data):
    if not 45 <= len(data) <= 128 * 1024 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Invalid stroke PNG size/signature")
    if data[8:16] != b"\x00\x00\x00\rIHDR" or data[16:29] != struct.pack(">IIBBBBB", 300, 300, 8, 6, 0, 0, 0):
        raise ValueError("Stroke PNG must be non-interlaced 300x300 RGBA8")
    offset, ended, seen_data = 8, False, False
    while offset + 12 <= len(data):
        size = int.from_bytes(data[offset:offset+4], "big")
        end = offset + 12 + size
        if end > len(data): raise ValueError("Truncated PNG chunk")
        chunk = data[offset+4:offset+8]
        if offset > 8 and chunk == b"IHDR": raise ValueError("Duplicate IHDR")
        crc = int.from_bytes(data[end-4:end], "big")
        if zlib.crc32(data[offset+4:end-4]) != crc: raise ValueError("PNG checksum mismatch")
        if chunk == b"IDAT": seen_data = True
        if chunk == b"IEND":
            ended = size == 0 and end == len(data)
            break
        offset = end
    if not ended or not seen_data: raise ValueError("Incomplete PNG")


def prepare(output, entries=None):
    output = Path(output).resolve()
    if output.exists():
        raise ValueError("Output already exists; use a new directory to preserve previous files")
    imported = []
    if entries:
        path = Path(entries)
        incoming = read_json(path, 64 * 1024 * 1024)
        if not isinstance(incoming, list):
            raise ValueError("Imported dictionary must be an array of entries")
        seen = set()
        for entry in incoming:
            char = validate_entry(entry)
            if char in seen:
                raise ValueError(f"Duplicate character: {char}")
            seen.add(char)
            imported.append((char, entry))
    shutil.copytree(ROOT / "content/sdcard", output)
    for char, entry in imported:
        path = output / "handict/dictionary/entries" / f"{ord(char):04X}.json"
        path.write_text(json.dumps(entry, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return validate(output / "handict")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("validate")
    check.add_argument("folder", type=Path)
    create = sub.add_parser("prepare")
    create.add_argument("--output", type=Path, required=True)
    create.add_argument("--entries", type=Path)
    args = parser.parse_args()
    try:
        count = validate(args.folder) if args.command == "validate" else prepare(args.output, args.entries)
    except (ValueError, OSError, json.JSONDecodeError) as exc:
        parser.exit(1, f"Content pack error: {exc}\n")
    print(f"Validated {count} dictionary entries.")


if __name__ == "__main__":
    main()
