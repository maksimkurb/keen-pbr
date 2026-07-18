#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import types
import unittest


SCRIPT = Path(__file__).with_name("analyze_crash.py")
SPEC = importlib.util.spec_from_file_location("analyze_crash", SCRIPT)
assert SPEC and SPEC.loader
analyze_crash = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analyze_crash)


class KpbrCrashParsingTest(unittest.TestCase):
    REPORT = """=== KPBR-CRASH v1 BEGIN ===
meta version=3.0.7 build=20260705144235 commit=abc branch=main target=openwrt os_version=24.10.6 arch=aarch64_cortex-a53 variant=full
registers arch=aarch64 pc=0x7f001004 sp=0x7fff0000 x30=0x7f002008
maps-begin
7f000000-7f010000 r-xp 00000000 00:01 2 /usr/sbin/keen-pbr
maps-end
frame index=0 kind=fault pc=0x7f001004 sp=0x7fff0000
frame index=1 kind=return pc=0x7f002008 sp=0x7fff0040
=== KPBR-CRASH v1 END ===
"""

    def test_metadata_is_read_from_v1_report(self):
        metadata = analyze_crash.parse_kpbr_metadata(self.REPORT)
        self.assertEqual(metadata["build"], "20260705144235")
        self.assertEqual(metadata["arch"], "aarch64_cortex-a53")
        self.assertEqual(metadata["target"], "openwrt")

    def test_frames_preserve_fault_and_return_semantics(self):
        frames = [
            item for item in analyze_crash.parse_log(self.REPORT, [])
            if str(item["label"]).startswith("frame[")
        ]
        self.assertEqual(len(frames), 2)
        self.assertFalse(frames[0]["is_return_address"])
        self.assertTrue(frames[1]["is_return_address"])

    def test_embedded_maps_are_parsed(self):
        mappings = analyze_crash.parse_maps(None, self.REPORT)
        self.assertEqual(len(mappings), 1)
        self.assertEqual(mappings[0]["path"], "/usr/sbin/keen-pbr")

    def test_artifact_names_cover_both_platforms(self):
        common = dict(version="3.0.7", build="20260705144235", arch="aarch64_cortex-a53", variant="full")
        openwrt = types.SimpleNamespace(platform="openwrt", target_version="24.10.6", **common)
        self.assertEqual(
            analyze_crash.artifact_name(openwrt),
            "keen-pbr_3.0.7-20260705144235_openwrt_24.10.6_aarch64_cortex-a53_full.debug",
        )
        keenetic = types.SimpleNamespace(platform="keenetic", target_version="4.3", **common)
        self.assertEqual(
            analyze_crash.artifact_name(keenetic),
            "keen-pbr_3.0.7-20260705144235_keenetic_aarch64_cortex-a53_full.debug",
        )


if __name__ == "__main__":
    unittest.main()
