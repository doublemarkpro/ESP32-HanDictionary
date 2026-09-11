"""Check actual flash payloads against the selected build's partition CSV (no board required)."""
import argparse
import csv
import json
from pathlib import Path
import re


def number(value):
    value = value.strip().lower()
    scale = {"k": 1024, "m": 1024 * 1024}.get(value[-1:], 1)
    return int(value[:-1] if scale != 1 else value, 0) * scale


def report(build, minimum=512 * 1024):
    build = Path(build).resolve()
    project = json.loads((build / "project_description.json").read_text(encoding="utf-8"))
    config = Path(project["config_file"]).read_text(encoding="utf-8")
    filename = re.search(r'^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="([^"]+)"', config, re.M)
    if not filename: raise ValueError("Custom partition CSV not found in build configuration")
    csvpath = Path(project["project_path"]) / filename.group(1)
    parts, end = {}, 0
    for row in csv.reader(csvpath.read_text(encoding="utf-8").splitlines()):
        if not row or row[0].lstrip().startswith("#"): continue
        name, kind, _, offset, size = (s.strip() for s in row[:5])
        align = 0x10000 if kind == "app" else 0x1000
        offset = number(offset) if offset else (end + align - 1) // align * align
        size = number(size)
        if offset < end: raise ValueError("Overlapping partition layout")
        end = offset + size
        parts[offset] = {"name": name, "type": kind, "capacity": size}
    flash = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
    flash_size = number(flash["flash_settings"]["flash_size"].replace("MB", "m"))
    if end > flash_size: raise ValueError("Partition layout exceeds physical flash setting")
    rows = []
    for address, filename in flash["flash_files"].items():
        address = int(address, 0)
        file = (build / filename).resolve()
        if not file.is_relative_to(build): raise ValueError("Flash path escapes build directory")
        used = file.stat().st_size
        if address + used > flash_size: raise ValueError("Flash payload exceeds configured size")
        if address not in parts: continue  # bootloader and partition table precede CSV partitions
        part = parts[address]
        free = part["capacity"] - used
        if free < 0: raise ValueError("Partition overflow: " + part["name"])
        rows.append({**part, "used": used, "free": free, "free_percent": round(100*free/part["capacity"], 1)})
        if part["type"] == "app" and free < minimum: raise ValueError("Application headroom below configured safety floor")
    if not any(p["type"] == "app" for p in rows): raise ValueError("No application payload checked")
    return {"flash_bytes": flash_size, "partitions": rows, "runtime_free_ram": None,
            "note": "OTA slot is reserved, not additional content storage. Runtime RAM requires hardware measurement."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path(__file__).resolve().parents[1]/"build")
    parser.add_argument("--min-app-free", type=int, default=512*1024)
    args = parser.parse_args()
    try:
        print(json.dumps(report(args.build, args.min_app_free), ensure_ascii=False, indent=2))
    except (ValueError, OSError, KeyError) as exc:
        parser.exit(1, f"Capacity check failed: {exc}\n")


if __name__ == "__main__": main()
