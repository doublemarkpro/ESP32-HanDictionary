"""Select the product CI matrix; full upstream builds require a manual event."""
import argparse
import json
import sys

PRODUCT_NAMES = ("m5stack-tab5-han-dictionary", "m5stack-tab5-han-dictionary-p4x")


def select(variants, event="push", scope="tab5"):
    if event not in ("push", "pull_request", "workflow_dispatch"):
        raise ValueError("Unsupported CI event")
    if scope not in ("tab5", "all"):
        raise ValueError("Unsupported CI scope")
    if scope == "all" and event != "workflow_dispatch":
        raise ValueError("Full matrix requires workflow_dispatch")
    if not isinstance(variants, list) or not variants:
        raise ValueError("Expected nonempty variant list")
    if scope == "tab5":
        selected = [v for v in variants if v.get("board") == "m5stack/tab5"
                    and v.get("name") in PRODUCT_NAMES]
        if sorted(v["name"] for v in selected) != sorted(PRODUCT_NAMES):
            raise ValueError("Expected exactly two unique Tab5 product variants")
    else:
        selected = variants
    return [{k: v[k] for k in ("board", "name", "full_name")} for v in selected]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event", default="push")
    parser.add_argument("--scope", default="tab5")
    args = parser.parse_args()
    try:
        print(json.dumps(select(json.load(sys.stdin), args.event, args.scope), separators=(",", ":")))
    except (ValueError, KeyError, TypeError, AttributeError) as exc:
        parser.exit(1, f"CI selection failed: {exc}\n")


if __name__ == "__main__":
    main()
