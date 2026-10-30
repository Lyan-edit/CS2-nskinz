"""Independently compare an exported nSkinz catalog with installed CS2 KeyValues/VPK data.

Export through NSKINZCS2_CATALOG_AUDIT_OUTPUT in the native catalog regression.
No game files are modified and no third-party Python modules are required.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re

from inspect_vpk_entries import parse_directory

TOKEN = re.compile(r'//[^\n]*|"(?:\\.|[^"\\])*"|[{}]|[^\s{}"]+')


def keyvalues(path: Path):
    tokens = iter(m.group() for m in TOKEN.finditer(path.read_text(encoding="utf-8-sig"))
                  if not m.group().startswith("//"))

    def unquote(value):
        return value[1:-1].replace('\\"', '"').replace('\\\\', '\\') if value.startswith('"') else value

    def object_():
        entries = []
        for key in tokens:
            if key == "}":
                return entries
            if key.startswith("[$"):
                continue
            value = next(tokens)
            entries.append((unquote(key), object_() if value == "{" else unquote(value)))
        return entries

    return object_()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--items-game", required=True, type=Path)
    parser.add_argument("--vpk", required=True, type=Path)
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    root = dict(keyvalues(args.items_game))["items_game"]

    def section(name):
        # Preserve repeated sections; a generic dict(root) would lose most paints.
        return {key: dict(value) for section_name, entries in root if section_name == name for key, value in entries}

    items = {int(k): v for k, v in section("items").items() if k.isdigit()}
    paints = {int(k): v for k, v in section("paint_kits").items() if k.isdigit()}
    prefabs = section("prefabs")

    def inherited(entry, visited=()):
        result = {}
        for name in entry.get("prefab", "").split():
            if name in prefabs and name not in visited:
                result.update(inherited(prefabs[name], visited + (name,)))
        result.update(entry)
        return result

    gun_types = {"#CSGO_Type_" + name for name in ("Pistol", "Rifle", "SMG", "Machinegun", "Shotgun", "SniperRifle")}
    guns = {i for i, entry in items.items() if entry.get("name", "").startswith("weapon_")
            and inherited(entry).get("item_type_name") in gun_types}
    knives = {i for i, entry in items.items() if entry.get("prefab") == "melee_unusual"}
    gloves = {i for i, entry in items.items() if entry.get("prefab") in ("hands", "hands_paintable")}
    names = {entry["name"]: i for i, entry in items.items() if entry.get("name")}
    paint_names = defaultdict(set)
    for i, entry in paints.items():
        paint_names[entry.get("name", "")].add(i)
    pairs = defaultdict(set)

    def walk(entries):
        for key, value in entries:
            match = re.fullmatch(r"\[([^]]+)\](.+)", key)
            if match and match[2] in names:
                pairs[names[match[2]]].update(paint_names[match[1]])
            if isinstance(value, list):
                walk(value)

    for name, entries in root:
        if name in ("item_sets", "client_loot_lists"):
            walk(entries)
    _, _, vpk_entries = parse_directory(args.vpk)
    ordered_names = sorted(names, key=lambda name: -len(name))
    for entry in vpk_entries:
        if not entry.path.startswith("panorama/images/econ/default_generated/"):
            continue
        filename = entry.path.rsplit("/", 1)[-1]
        for suffix in ("_light_png.vtex_c", "_medium_png.vtex_c", "_heavy_png.vtex_c"):
            if not filename.endswith(suffix):
                continue
            filename = filename[:-len(suffix)]
            for name in ordered_names:
                if filename.startswith(name + "_"):
                    pairs[names[name]].update(paint_names.get(filename[len(name) + 1:], ()))
                    break
            break
    catalog = json.loads(args.catalog.read_text(encoding="utf-8-sig"))
    actual_pairs = {entry["definition"]: set(entry["paints"]) for entry in catalog["weapon_paint_compatibility"]}
    actual_gloves = {entry["paint"]: entry["definition"] for entry in catalog["glove_paint_compatibility"] if entry["paint"]}
    report = {"scope": "Installed catalog/list/association/visual-metadata coverage, not rendered gameplay acceptance",
              "items_sha256": hashlib.sha256(args.items_game.read_bytes()).hexdigest(), "modules": {}}

    def check(name, expected_ids, field, expected_pairs=None):
        listed = {entry["id"] for entry in catalog[field]}
        result = {"definitions": len(expected_ids), "missing_ids": sorted(expected_ids - listed)}
        if expected_pairs is not None:
            visible = {i: actual_pairs.get(i, set()) & {p["id"] for p in catalog["weapon_paint_kits"]}
                       if i in listed else set() for i in expected_ids}
            result["pairs"] = sum(len(pairs[i]) for i in expected_ids)
            result["missing_pairs"] = {str(i): sorted(pairs[i] - visible[i]) for i in expected_ids if pairs[i] - visible[i]}
            result["extra_pairs"] = {str(i): sorted(visible[i] - pairs[i]) for i in expected_ids if visible[i] - pairs[i]}
        report["modules"][name] = result

    check("firearms", guns, "base_items", True)
    check("knives", knives, "knife_items", True)
    expected_gloves = {paint: definition for definition in gloves for paint in pairs[definition]}
    glove_paints = {p["id"] for p in catalog["glove_paint_kits"]}
    report["modules"]["gloves"] = {
        "finishes": len(expected_gloves),
        "model_counts": {str(i): len(pairs[i]) for i in sorted(gloves) if pairs[i]},
        "missing_ids": sorted(set(expected_gloves) - glove_paints),
        "wrong_pairs": {str(p): {"expected": d, "actual": actual_gloves.get(p)} for p, d in expected_gloves.items()
                        if actual_gloves.get(p) != d},
        "leaked_to_firearms": sorted(set(expected_gloves) & {p["id"] for p in catalog["weapon_paint_kits"]}),
    }
    stickers = {int(i) for i, entry in section("sticker_kits").items()
                if (entry.get("item_name") or entry.get("name"))
                and not entry.get("item_name", "").startswith(("#SprayKit", "#GraffitiKit"))
                and not entry.get("name", "").startswith(("spray_", "graffiti_"))}
    check("stickers", stickers, "sticker_kits")
    charms = {int(i): entry for i, entry in section("keychain_definitions").items()
              if int(i) > 0 and (entry.get("loc_name") or entry.get("name"))}
    check("keychains", set(charms), "keychain_kits")
    report["modules"]["keychains"]["missing_model_metadata"] = sorted(set(charms) -
        {v["definition"] for v in catalog["keychain_visuals"] if v["resolved_model_resource_path"]})
    # Zeus is equipment, outside the existing firearm module. Report rather
    # than pretend its paints were audited as supported or silently enable it.
    report["outside_supported_scope"] = [{"definition": i, "name": items[i]["name"], "paints": sorted(pairs[i])}
        for i in sorted(names.values()) if i not in guns | knives | gloves and pairs[i]
        and items[i].get("name", "").startswith("weapon_")]
    fields = ("missing_ids", "missing_pairs", "extra_pairs", "wrong_pairs", "leaked_to_firearms", "missing_model_metadata")
    report["passed"] = all(not result.get(field) for result in report["modules"].values() for field in fields)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
