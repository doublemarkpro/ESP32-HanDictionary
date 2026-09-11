"""Validate and prepare a bounded microSD content pack (Python standard library only)."""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
ISBN = "9787100168076"
EDITION = "新华字典第12版"


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
    ref = entry.get("reference", {})
    if not isinstance(ref, dict):
        raise ValueError("reference must be an object")
    if ref.get("edition") != EDITION or ref.get("isbn") != ISBN:
        raise ValueError(f"Page references must target {EDITION}, ISBN {ISBN}")
    page = ref.get("page")
    verified = ref.get("verified", False)
    if type(verified) is not bool:
        raise ValueError("verified must be a boolean")
    if page is not None and (type(page) is not int or not 1 <= page < 10000 or not verified):
        raise ValueError("A printed page requires an integer and verified=true")
    if verified and page is None:
        raise ValueError("A verified page must be supplied")
    encoded = json.dumps(entry, ensure_ascii=False).encode("utf-8")
    if len(encoded) > 16384:
        raise ValueError("Entry exceeds firmware read limit")
    return character


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


def validate(folder):
    folder = Path(folder)
    manifest = read_json(folder / "manifest.json", 4096)
    if type(manifest.get("schema_version")) is not int or manifest["schema_version"] != 1:
        raise ValueError("Unsupported content schema")
    if manifest.get("dictionary", {}).get("isbn") != ISBN:
        raise ValueError("Manifest ISBN does not match the selected book")
    count = 0
    for path in (folder / "dictionary/entries").glob("*.json"):
        character = validate_entry(read_json(path, 16384))
        if path.name != f"{ord(character):04X}.json":
            raise ValueError(f"Entry filename mismatch: {path.name}")
        count += 1
    timetable = folder / "timetable.json"
    if timetable.exists():
        validate_timetable(read_json(timetable, 8192))
    weather = folder / "weather.json"
    if weather.exists():
        record = read_json(weather, 4096)
        for key, limit in [("city", 96), ("summary", 512), ("updated_at", 64)]:
            text(record, key, limit)
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
    print(f"Validated {count} entries; ISBN {ISBN}. Unknown pages remain null.")


if __name__ == "__main__":
    main()
