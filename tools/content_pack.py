"""Validate and prepare a bounded microSD content pack (Python standard library only)."""
import argparse
import json
from pathlib import Path
import re
import shutil

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
        days = read_json(timetable, 8192).get("days")
        if not isinstance(days, list) or len(days) != 5:
            raise ValueError("Timetable needs five weekday arrays")
        for day in days:
            if not isinstance(day, list) or len(day) > 8:
                raise ValueError("At most eight lessons per day")
            for lesson in day:
                text({"lesson": lesson}, "lesson", 24, True)
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
    return count


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
