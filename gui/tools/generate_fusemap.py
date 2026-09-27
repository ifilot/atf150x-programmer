#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 ATF1502 programmer contributors

## @file
# Generates the GUI fuse-map descriptions from the pinned Project Bureau
# database.
#
# The GUI only needs to know which functional region and signal each JEDEC
# fuse belongs to, so this script reduces the 1.2 MB upstream database to a
# compact JSON document per supported device. Run it from the repository root:
#
#     python3 gui/tools/generate_fusemap.py
#
# An existing download can be supplied with --database to work offline.

import argparse
import hashlib
import json
import pathlib
import urllib.request

COMMIT = "8b8a97122ec238b6a09868a4bc7def20acef798c"
URL = ("https://raw.githubusercontent.com/whitequark/prjbureau/"
       f"{COMMIT}/database.json")
SHA256 = "ecf4e47b65afdf2e5cb747e30a26a423688097541fd4474615a6bedd72a509cd"
DEVICES = ("ATF1502AS", "ATF1504AS")
PACKAGE = "PLCC44"
OUTPUT = pathlib.Path(__file__).resolve().parent.parent / "resources" / "fusemap"


## Loads and authenticates the upstream database.
# @param path Optional local copy; downloaded from GitHub when omitted.
# @return Parsed database dictionary.
def load_database(path):
    if path:
        data = pathlib.Path(path).read_bytes()
    else:
        with urllib.request.urlopen(URL, timeout=60) as response:
            data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        raise SystemExit(f"database.json SHA-256 mismatch: {digest}")
    return json.loads(data)


## Records one descriptive label for a fuse, merging aliases.
# @param labels Mapping from fuse index to a list of labels.
# @param fuses Iterable of JEDEC fuse indices.
# @param text Label to attach.
def add_label(labels, fuses, text):
    for fuse in fuses:
        entries = labels.setdefault(fuse, [])
        if text not in entries:
            entries.append(text)


## Collects labels for option dictionaries of the form {"fuses": [...]}.
# @param labels Mapping from fuse index to a list of labels.
# @param prefix Owner name prepended to each option name.
# @param options Dictionary of named options.
def add_options(labels, prefix, options):
    for name, option in options.items():
        if isinstance(option, dict) and "fuses" in option:
            add_label(labels, option["fuses"], f"{prefix} {name}".strip())


## Reduces one device entry to the fields the GUI renders.
# @param name Device name such as ATF1502AS.
# @param device Upstream device dictionary.
# @return JSON-serializable fuse-map description.
def convert(name, device):
    ranges = device["ranges"]
    pins = device["pins"][PACKAGE]
    labels = {}

    macrocells = {}
    pterms = []
    for mc_name, macrocell in device["macrocells"].items():
        pad = macrocell["pad"]
        macrocells[mc_name] = {
            "block": macrocell["block"],
            "pin": pins.get(pad, ""),
        }
        for pt_name, (first, end) in macrocell["pterm_ranges"].items():
            pterms.append([first, end, macrocell["block"], mc_name, pt_name])
        add_options(labels, mc_name, macrocell)

    blocks = {}
    for block_name, block in device["blocks"].items():
        points = block["pterm_points"]
        inputs = [""] * len(points)
        for signal, offset in points.items():
            inputs[offset] = signal
        blocks[block_name] = {"inputs": inputs}

    for switch_name, switch in device["switches"].items():
        add_label(labels, switch["mux"]["fuses"],
                  f"{switch_name} input mux (block {switch['block']})")

    for global_name, options in device["globals"].items():
        add_options(labels, global_name, options)

    config = device["config"]
    for pin_name, options in config.get("pins", {}).items():
        add_options(labels, pin_name, options)
    add_options(labels, "", {k: v for k, v in config.items() if k != "pins"})

    first_user = ranges["user"][0]
    for fuse in range(first_user, ranges["user"][1]):
        add_label(labels, [fuse], f"User signature fuse {fuse - first_user}")

    pterms.sort()
    return {
        "device": name,
        "source": f"whitequark/prjbureau@{COMMIT[:12]}",
        "package": PACKAGE,
        "fuse_count": ranges["reserved"][1],
        "regions": [[region, first, end]
                    for region, (first, end) in ranges.items()],
        "blocks": blocks,
        "macrocells": macrocells,
        "pterms": pterms,
        "labels": [[fuse, " / ".join(text)]
                   for fuse, text in sorted(labels.items())],
    }


## Command-line entry point.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", help="local copy of database.json")
    args = parser.parse_args()
    database = load_database(args.database)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name in DEVICES:
        path = OUTPUT / f"{name.lower()}.json"
        text = json.dumps(convert(name, database[name]),
                          separators=(",", ":"))
        path.write_text(text + "\n", encoding="utf-8")
        print(f"Wrote {path} ({len(text)} bytes)")


if __name__ == "__main__":
    main()
