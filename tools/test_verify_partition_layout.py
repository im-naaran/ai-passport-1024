#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

from verify_partition_layout import (
    LayoutError,
    parse_partitions,
    verify_layout,
    verify_normal_upload,
)


VALID = """\
nvs,data,nvs,0x9000,0x6000,
phy_init,data,phy,0xf000,0x1000,
factory,app,factory,0x10000,0x300000,
badge_data,data,0x40,0x310000,0x40000,
cardid,data,nvs,0x356000,0x4000,
custom_data,data,0x41,0x35a000,0x100000,
lightboard_data,data,0x42,0x45a000,0x380000,
"""


class PartitionLayoutTest(unittest.TestCase):
    def parse(self, text=VALID):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / "partitions.csv"
        path.write_text(text, encoding="utf-8")
        return parse_partitions(path)

    def test_valid_fixed_layout(self):
        result = verify_layout(self.parse())
        self.assertEqual(0x35A000, result["custom_data"].offset)
        self.assertEqual(0x45A000, result["custom_data"].end)
        self.assertEqual(0x7DA000, result["lightboard_data"].end)

    def test_missing_and_duplicate_partitions_fail(self):
        with self.assertRaisesRegex(LayoutError, "missing required partition: custom_data"):
            verify_layout(self.parse(VALID.replace(VALID.splitlines()[-2] + "\n", "")))
        with self.assertRaisesRegex(LayoutError, "duplicate partition"):
            verify_layout(self.parse(VALID + "nvs,data,nvs,0x500000,0x1000,\n"))

    def test_wrong_fixed_range_and_alignment_fail(self):
        with self.assertRaisesRegex(LayoutError, "unexpected custom_data range"):
            verify_layout(self.parse(VALID.replace("0x100000", "0x0ff000")))
        with self.assertRaisesRegex(LayoutError, "not 4 KiB aligned"):
            verify_layout(self.parse(VALID.replace("0x100000", "0x100001")))

    def test_overlap_and_flash_overflow_fail(self):
        overlap = VALID + "extra,data,0x42,0x359000,0x2000,\n"
        with self.assertRaisesRegex(LayoutError, "partitions overlap"):
            verify_layout(self.parse(overlap))
        overflow = VALID + "extra,data,0x42,0x7ff000,0x2000,\n"
        with self.assertRaisesRegex(LayoutError, "exceeds 8 MiB"):
            verify_layout(self.parse(overflow))

    def test_normal_upload_accepts_only_boot_partition_table_and_app(self):
        by_name = verify_layout(self.parse())
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        build = Path(temporary.name)
        (build / "bootloader.bin").write_bytes(b"b" * 0x7000)
        (build / "partitions.bin").write_bytes(b"p" * 0xC00)
        (build / "firmware.bin").write_bytes(b"a" * 0x200000)
        manifest = {
            "flash_files": {
                "0x0": "bootloader/bootloader.bin",
                "0x8000": "partition_table/partition-table.bin",
                "0x10000": "AI-Passport-Badge.bin",
            }
        }
        (build / "flasher_args.json").write_text(json.dumps(manifest), encoding="utf-8")
        verify_normal_upload(build, by_name)

    def test_unexpected_or_protected_upload_range_fails(self):
        by_name = verify_layout(self.parse())
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        build = Path(temporary.name)
        (build / "data.bin").write_bytes(b"x" * 16)
        (build / "flasher_args.json").write_text(
            json.dumps({"flash_files": {"0x310000": "data.bin"}}), encoding="utf-8"
        )
        with self.assertRaisesRegex(LayoutError, "unexpected normal-upload offset"):
            verify_normal_upload(build, by_name)

        (build / "firmware.bin").write_bytes(b"x" * 0x301000)
        (build / "flasher_args.json").write_text(
            json.dumps({"flash_files": {"0x10000": "firmware.bin"}}), encoding="utf-8"
        )
        with self.assertRaisesRegex(LayoutError, "overlaps protected partition badge_data"):
            verify_normal_upload(build, by_name)


if __name__ == "__main__":
    unittest.main()
