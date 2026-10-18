#!/usr/bin/env python3
"""Build the offline font subset and its coverage metadata in an output directory.

Run with fonttools==4.60.1 and brotli==1.1.0. The source stays outside the repo;
the output includes the exact supported set and source/output hashes.
"""

import argparse
import hashlib
import json
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def supported_codepoints():
    points = set(range(32, 127)) | set(map(ord, "，。！？：；、“”‘’（）《》…—"))
    # GB2312's level-one/two Hanzi zones, excluding undefined cells.
    for lead in range(0xB0, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                points.add(ord(bytes([lead, trail]).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    return points


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--expect-sha256", required=True)
    args = parser.parse_args()
    digest = sha256(args.source)
    if digest != args.expect_sha256:
        parser.error("source hash differs; inspect the font version before changing the pin")
    font = TTFont(args.source, recalcTimestamp=False)
    if "fvar" in font:
        font = instantiateVariableFont(font, {"wght": 400}, inplace=True)
    points = supported_codepoints()
    missing = sorted(points - set(font.getBestCmap()))
    if missing:
        parser.error(f"missing codepoints: {missing}")
    options = subset.Options()
    options.hinting = False
    options.layout_features = []
    options.name_IDs = [1, 2, 3, 4, 5, 6]
    options.notdef_outline = True
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=points)
    subsetter.subset(font)
    if not points <= set(font.getBestCmap()):
        parser.error("subsetting removed supported codepoints")
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output / "lightboard.woff2"
    font.flavor = "woff2"
    font.save(output)
    units = font["head"].unitsPerEm
    advances = {cp: font["hmtx"][name][0] for cp, name in font.getBestCmap().items()
                if cp in points}
    metrics = {
        "source_sha256": digest,
        "font_revision": font["head"].fontRevision,
        "output_sha256": sha256(output),
        "woff2_bytes": output.stat().st_size,
        "supported_count": len(points),
        "codepoints": sorted(points),
        "units_per_em": units,
        "max_advance_at_64px": max(advances.values()) * 64 / units,
    }
    (args.output / "font-probe.json").write_text(json.dumps(metrics, indent=2) + "\n")
    print(json.dumps({k: v for k, v in metrics.items() if k != "codepoints"}, indent=2))


if __name__ == "__main__":
    main()
