"""Build an indexed SD stroke-path database from Hanzi Writer Data.

The source may be an extracted npm package directory or a .tgz/.tar.gz archive.
Optional cnchar-order data adds compact per-stroke name codes.  No PNG frames are
generated: each character is stored as M/L/Q/C/Z path bytecode plus median points.
"""

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import tarfile
import zlib

import content_pack


SOURCE_ID = "hanzi-writer-data-2.0.1"
SOURCE_URL = "https://github.com/chanind/hanzi-writer-data"
LICENSE_NAME = "ARPHICPL.TXT"
PATH_TOKEN = re.compile(r"[A-Za-z]|-?\d+(?:\.\d+)?")
PATH_OPERANDS = {"M": 2, "L": 2, "Q": 4, "C": 6, "Z": 0}
PATH_OPCODE = {"M": 0, "L": 1, "Q": 2, "C": 3, "Z": 4}


def _coordinate(value):
    rounded = round(float(value))
    if not -32768 <= rounded <= 32767:
        raise ValueError(f"Stroke coordinate is outside int16: {value}")
    return rounded


def encode_path(path):
    tokens = PATH_TOKEN.findall(path)
    if not tokens:
        raise ValueError("Empty stroke path")
    output = bytearray()
    index = 0
    while index < len(tokens):
        command = tokens[index].upper()
        index += 1
        if command not in PATH_OPERANDS:
            raise ValueError(f"Unsupported stroke path command: {command}")
        count = PATH_OPERANDS[command]
        if index + count > len(tokens) or any(token.isalpha()
                                               for token in tokens[index:index + count]):
            raise ValueError("Malformed stroke path")
        output.append(PATH_OPCODE[command])
        for value in tokens[index:index + count]:
            output.extend(struct.pack("<h", _coordinate(value)))
        index += count
    return bytes(output)


def encode_record(character, record, order=""):
    strokes = record.get("strokes")
    medians = record.get("medians")
    radicals = record.get("radStrokes", [])
    if not isinstance(strokes, list) or not isinstance(medians, list) or not strokes:
        raise ValueError(f"Missing stroke arrays for {character}")
    if len(strokes) != len(medians) or len(strokes) > 64:
        raise ValueError(f"Stroke array mismatch for {character}")
    if not isinstance(radicals, list) or any(type(value) is not int for value in radicals):
        raise ValueError(f"Invalid radical strokes for {character}")
    radical_set = set(radicals)
    if any(value < 0 or value >= len(strokes) for value in radical_set):
        raise ValueError(f"Radical stroke is out of range for {character}")
    if order and (len(order) != len(strokes) or not order.isascii() or not order.isalpha()):
        order = ""

    path_data = []
    median_data = []
    for stroke, points in zip(strokes, medians):
        if not isinstance(stroke, str) or not isinstance(points, list) or len(points) > 1024:
            raise ValueError(f"Invalid stroke geometry for {character}")
        path_data.append(encode_path(stroke))
        encoded_points = bytearray()
        for point in points:
            if (not isinstance(point, list) or len(point) != 2 or
                    isinstance(point[0], bool) or isinstance(point[1], bool)):
                raise ValueError(f"Invalid median point for {character}")
            encoded_points.extend(struct.pack("<hh", _coordinate(point[0]),
                                              _coordinate(point[1])))
        median_data.append(bytes(encoded_points))

    header_size = (content_pack.STROKE_RECORD_HEADER.size +
                   len(strokes) * content_pack.STROKE_DIRECTORY.size)
    payload = bytearray()
    directory = bytearray()
    for index, (path, median) in enumerate(zip(path_data, median_data)):
        path_offset = header_size + len(payload)
        payload.extend(path)
        median_offset = header_size + len(payload)
        payload.extend(median)
        name_code = ord(order[index].lower()) if order else 0
        flags = 1 if index in radical_set else 0
        directory.extend(content_pack.STROKE_DIRECTORY.pack(
            path_offset, len(path), median_offset, len(median) // 4, name_code, flags))
    output = bytearray(content_pack.STROKE_RECORD_HEADER.pack(
        content_pack.STROKE_RECORD_MAGIC, content_pack.STROKE_RECORD_VERSION,
        len(strokes), header_size))
    output.extend(directory)
    output.extend(payload)
    if len(output) > content_pack.STROKE_RECORD_LIMIT:
        raise ValueError(f"Encoded stroke record is too large for {character}")
    return bytes(output)


def _directory_records(path):
    license_data = None
    license_path = path / LICENSE_NAME
    if license_path.is_file():
        license_data = license_path.read_bytes()
    records = []
    for source in path.glob("*.json"):
        character = source.stem
        if len(character) == 1 and content_pack.INDEX_FIRST <= ord(character) <= 0x9FFF:
            if source.stat().st_size > 64 * 1024:
                raise ValueError(f"Stroke JSON is too large: {source}")
            records.append((character, json.loads(source.read_text(encoding="utf-8"))))
    return records, license_data


def _archive_records(path):
    records = []
    license_data = None
    with tarfile.open(path, "r:*") as archive:
        for member in archive.getmembers():
            if not member.isfile() or member.size > 64 * 1024:
                continue
            name = PurePosixPath(member.name).name
            if name == LICENSE_NAME:
                stream = archive.extractfile(member)
                license_data = stream.read() if stream else None
                continue
            if not name.endswith(".json"):
                continue
            character = name[:-5]
            if len(character) != 1 or not content_pack.INDEX_FIRST <= ord(character) <= 0x9FFF:
                continue
            stream = archive.extractfile(member)
            if stream is None:
                raise ValueError(f"Cannot read archive member: {member.name}")
            records.append((character, json.loads(stream.read().decode("utf-8"))))
    return records, license_data


def read_source(path):
    path = Path(path)
    records, license_data = (_directory_records(path) if path.is_dir()
                             else _archive_records(path))
    if not records:
        raise ValueError("No BMP CJK Hanzi Writer records found")
    return records, license_data


def read_orders(path):
    if path is None:
        return {}
    value = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("cnchar order data must be a JSON object")
    return {key: item for key, item in value.items()
            if isinstance(key, str) and len(key) == 1 and isinstance(item, str)}


def build(source, pack, order_path=None, order_license=None):
    pack = Path(pack).resolve()
    manifest_path = pack / "manifest.json"
    if not manifest_path.is_file():
        raise ValueError("Target must be an existing handict content directory")
    dictionary = pack / "dictionary"
    index_path = dictionary / "strokes.idx"
    data_path = dictionary / "strokes.dat"
    if index_path.exists() or data_path.exists():
        raise ValueError("Stroke index already exists; use a fresh content-pack directory")

    incoming, license_data = read_source(source)
    orders = read_orders(order_path)
    slots = bytearray(content_pack.INDEX_SLOTS * content_pack.INDEX_SLOT.size)
    data = bytearray()
    seen = set()
    named = 0
    for character, record in sorted(incoming, key=lambda item: ord(item[0])):
        if character in seen:
            raise ValueError(f"Duplicate stroke character: {character}")
        seen.add(character)
        encoded = encode_record(character, record, orders.get(character, ""))
        if orders.get(character, "") and len(orders[character]) == len(record.get("strokes", [])):
            named += 1
        slot = ord(character) - content_pack.INDEX_FIRST
        content_pack.INDEX_SLOT.pack_into(slots, slot * content_pack.INDEX_SLOT.size,
                                          len(data), len(encoded))
        data.extend(encoded)

    header = content_pack.STROKE_INDEX_HEADER.pack(
        content_pack.STROKE_INDEX_MAGIC, content_pack.STROKE_INDEX_VERSION,
        content_pack.INDEX_FIRST, content_pack.INDEX_SLOTS, len(seen), len(data),
        zlib.crc32(data), zlib.crc32(slots))
    dictionary.mkdir(parents=True, exist_ok=True)
    index_path.write_bytes(header + slots)
    data_path.write_bytes(data)
    # A vector pack does not need the old cumulative per-step PNG directories.
    # Removing them also prevents an SD update from silently carrying both formats.
    legacy_frames = dictionary / "strokes"
    if legacy_frames.is_dir():
        shutil.rmtree(legacy_frames)

    licenses = pack / "licenses"
    licenses.mkdir(exist_ok=True)
    if license_data:
        (licenses / LICENSE_NAME).write_bytes(license_data)
    if order_license:
        shutil.copyfile(order_license, licenses / "CNCHAR-MIT.txt")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["stroke_dictionary"] = {
        "format": "handict-strokes-v1",
        "entries": len(seen),
        "named_entries": named,
        "source_id": SOURCE_ID,
        "source_url": SOURCE_URL,
        "license": "Arphic Public License",
        "license_file": f"licenses/{LICENSE_NAME}",
        "index_bytes": len(header) + len(slots),
        "data_bytes": len(data),
        "data_crc32": zlib.crc32(data),
    }
    if order_path:
        manifest["stroke_dictionary"]["stroke_names_source"] = (
            "https://github.com/theajack/cnchar/tree/master/src/cnchar/plugin/order")
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
    content_pack.validate(pack)
    return len(seen), named, len(header) + len(slots), len(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True,
                        help="hanzi-writer-data package directory or npm .tgz")
    parser.add_argument("--pack", type=Path, required=True,
                        help="Existing handict directory to extend")
    parser.add_argument("--order", type=Path,
                        help="Optional cnchar stroke-order-jian.json")
    parser.add_argument("--order-license", type=Path,
                        help="Optional cnchar MIT LICENSE to copy into the pack")
    args = parser.parse_args()
    try:
        entries, named, index_bytes, data_bytes = build(
            args.source, args.pack, args.order, args.order_license)
    except (ValueError, OSError, json.JSONDecodeError, tarfile.TarError) as exc:
        parser.exit(1, f"Stroke import error: {exc}\n")
    print(f"Converted and validated {entries} vector stroke characters ({named} named)")
    print(f"strokes.idx: {index_bytes} bytes; strokes.dat: {data_bytes} bytes")


if __name__ == "__main__":
    main()
