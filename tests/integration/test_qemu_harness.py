#!/usr/bin/env python3
import os
import pathlib
import subprocess
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).parent / "scripts" / "run-qemu-suite.sh"


def bash(fragment: str, **environment: str) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env.update(environment)
    return subprocess.run(
        ["/bin/bash", "-c", f"source {SCRIPT!s}; {fragment}"],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)


class QemuHarnessTest(unittest.TestCase):
    def test_invalid_backend(self):
        result = bash("select_backends invalid")
        self.assertEqual(result.returncode, 2)
        self.assertIn("INTEGRATION_BACKEND", result.stderr)

    def test_invalid_case_syntax(self):
        result = bash("select_backends all", INTEGRATION_CASES="bad case")
        self.assertEqual(result.returncode, 2)
        self.assertIn("INTEGRATION_CASES", result.stderr)

    def test_missing_prerequisite(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ("qemu-system-x86_64", "mkfs.vfat", "mcopy", "mtype",
                         "wget", "timeout", "realpath"):
                os.symlink("/bin/true", pathlib.Path(directory) / name)
            result = bash("require_prerequisites", PATH=directory)
        self.assertEqual(result.returncode, 2)
        self.assertIn("qemu-img", result.stderr)

    def test_inaccessible_docker_mentions_prebuilt_package(self):
        result = bash(
            "package_input_hash(){ echo hash; }; docker(){ return 1; }; "
            "resolve_package '' 1", PATH=os.environ.get("PATH", ""))
        self.assertEqual(result.returncode, 2)
        self.assertIn("INTEGRATION_DEB", result.stderr)

    def test_timeout_status_is_preserved(self):
        result = bash("timeout(){ return 124; }; execute_vm /tmp; test \"$qemu_status\" = 124")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_result_is_empty(self):
        result = bash(
            "mtype(){ return 1; }; mcopy(){ return 1; }; "
            "extract_results /tmp; test -z \"$guest_status\"")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_early_boot_output_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            log = pathlib.Path(directory) / "serial.log"
            log.write_text("\n".join(f"boot line {index}" for index in range(300)))
            result = bash(f"print_marked_diagnostics nftables {directory}")
        lines = result.stderr.splitlines()
        self.assertLessEqual(len(lines), 81)
        self.assertIn("no_guest_marker_seen", result.stderr)

    def test_all_backends_run_after_failure(self):
        result = bash(
            "require_prerequisites(){ :; }; resolve_package(){ :; }; "
            "prepare_base_image(){ :; }; "
            "run_backend(){ echo ran:$1; test \"$1\" = nftables; }; main all '' 0")
        self.assertEqual(result.returncode, 1)
        self.assertIn("ran:iptables", result.stdout)
        self.assertIn("ran:nftables", result.stdout)
        self.assertIn("status=fail", result.stdout)


if __name__ == "__main__":
    unittest.main()
