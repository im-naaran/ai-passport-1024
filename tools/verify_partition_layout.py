#!/usr/bin/env python3
"""Validate the fixed 8 MiB badge layout and optional normal-upload ranges."""

from __future__ import annotations

import argparse
import csv
import json
from dataclasses import dataclass
from pathlib import Path


FLASH_SIZE = 0x800000
ALIGNMENT = 0x1000
EXPECTED = {
    "nvs": (0x9000, 0x6000),
    "phy_init": (0xF000, 0x1000),
    "factory": (0x10000, 0x300000),
    "badge_data": (0x310000, 0x40000),
    "cardid": (0x356000, 0x4000),
    "custom_data": (0x35A000, 0x100000),
    "lightboard_data": (0x45A000, 0x380000),
}
PROTECTED_UPLOAD_PARTITIONS = ("nvs", "badge_data", "cardid", "custom_data", "lightboard_data")
NORMAL_UPLOAD_OFFSETS = {0x0, 0x8000, 0x10000}


class LayoutError(ValueError):
    pass


@dataclass(frozen=True)
class Partition:
    name: str
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


def _number(text: str) -> int:
    value = text.strip().lower()
    multiplier = 1
    if value.endswith("k"):
        value, multiplier = value[:-1], 1024
    elif value.endswith("m"):
        value, multiplier = value[:-1], 1024 * 1024
    return int(value, 0) * multiplier


def parse_partitions(path: Path) -> list[Partition]:
    partitions: list[Partition] = []
    with path.open(newline="", encoding="utf-8") as source:
        for row in csv.reader(source):
            if not row or not row[0].strip() or row[0].lstrip().startswith("#"):
                continue
            if len(row) < 5:
                raise LayoutError(f"invalid partition row: {row!r}")
            try:
                partitions.append(Partition(row[0].strip(), _number(row[3]), _number(row[4])))
            except ValueError as error:
                raise LayoutError(f"invalid numeric value for {row[0].strip()}") from error
    return partitions


def verify_layout(partitions: list[Partition]) -> dict[str, Partition]:
    by_name: dict[str, Partition] = {}
    for partition in partitions:
        if partition.name in by_name:
            raise LayoutError(f"duplicate partition: {partition.name}")
        if partition.offset % ALIGNMENT or partition.size % ALIGNMENT:
            raise LayoutError(f"partition is not 4 KiB aligned: {partition.name}")
        if partition.size <= 0 or partition.offset < 0 or partition.end > FLASH_SIZE:
            raise LayoutError(f"partition exceeds 8 MiB flash: {partition.name}")
        by_name[partition.name] = partition

    ordered = sorted(partitions, key=lambda item: item.offset)
    for previous, current in zip(ordered, ordered[1:]):
        if previous.end > current.offset:
            raise LayoutError(f"partitions overlap: {previous.name} and {current.name}")

    for name, expected in EXPECTED.items():
        actual = by_name.get(name)
        if actual is None:
            raise LayoutError(f"missing required partition: {name}")
        if (actual.offset, actual.size) != expected:
            raise LayoutError(
                f"unexpected {name} range: 0x{actual.offset:x}/0x{actual.size:x}"
            )
    return by_name


def _resolve_flash_file(build_dir: Path, relative: str) -> Path:
    direct = build_dir / relative
    if direct.is_file():
        return direct
    # ESP-IDF records component-relative paths while PlatformIO copies the
    # final upload images into the environment build directory.
    flattened = build_dir / Path(relative).name
    if flattened.is_file():
        return flattened
    aliases = {
        "AI-Passport-Badge.bin": "firmware.bin",
        "partition-table.bin": "partitions.bin",
    }
    alias = aliases.get(Path(relative).name)
    if alias and (build_dir / alias).is_file():
        return build_dir / alias
    raise LayoutError(f"upload image is missing: {relative}")


def verify_normal_upload(build_dir: Path, by_name: dict[str, Partition]) -> None:
    manifest = build_dir / "flasher_args.json"
    if not manifest.is_file():
        raise LayoutError(f"missing upload manifest: {manifest}")
    data = json.loads(manifest.read_text(encoding="utf-8"))
    flash_files = data.get("flash_files")
    if not isinstance(flash_files, dict) or not flash_files:
        raise LayoutError("upload manifest has no flash_files")

    protected = [by_name[name] for name in PROTECTED_UPLOAD_PARTITIONS]
    for offset_text, relative in flash_files.items():
        offset = _number(offset_text)
        if offset not in NORMAL_UPLOAD_OFFSETS:
            raise LayoutError(f"unexpected normal-upload offset: 0x{offset:x}")
        image = _resolve_flash_file(build_dir, str(relative))
        end = offset + image.stat().st_size
        if end > FLASH_SIZE:
            raise LayoutError(f"upload image exceeds flash: {image.name}")
        for partition in protected:
            if offset < partition.end and end > partition.offset:
                raise LayoutError(
                    f"upload image {image.name} overlaps protected partition {partition.name}"
                )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("partitions", type=Path)
    parser.add_argument("build_dir", nargs="?", type=Path)
    args = parser.parse_args()
    try:
        by_name = verify_layout(parse_partitions(args.partitions))
        if args.build_dir is not None:
            verify_normal_upload(args.build_dir, by_name)
    except (LayoutError, OSError, json.JSONDecodeError) as error:
        parser.exit(1, f"layout verification failed: {error}\n")
    print("partition layout verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
