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
                         "wget", "timeout", "realpath", "ssh-keygen", "python3"):
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
        with tempfile.TemporaryDirectory() as directory:
            for role in ("client", "wan", "router"):
                (pathlib.Path(directory) / role).mkdir()
            result = bash(
                "allocate_ports(){ echo '1 2 3'; }; "
                "qemu_common(){ if [[ $2 = router ]]; then return 124; else sleep 2; fi; }; "
                f"execute_vm {directory!s}; test \"$qemu_status\" = 124")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_result_is_empty(self):
        result = bash(
            "mtype(){ return 1; }; mcopy(){ return 1; }; "
            "extract_results /tmp; test -z \"$guest_status\"")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_early_boot_output_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            (pathlib.Path(directory) / "router").mkdir()
            (pathlib.Path(directory) / "client").mkdir()
            (pathlib.Path(directory) / "wan").mkdir()
            log = pathlib.Path(directory) / "router" / "serial.log"
            log.write_text("\n".join(f"boot line {index}" for index in range(300)))
            result = bash(f"print_marked_diagnostics nftables {directory}")
        lines = result.stderr.splitlines()
        self.assertLessEqual(len(lines), 81)
        self.assertIn("no_guest_marker_seen", result.stderr)

    def test_peer_readiness_timeout(self):
        library = pathlib.Path(__file__).parent / "vm" / "guest-lib.sh"
        result = subprocess.run(
            ["/bin/bash", "-c",
             f"source {library!s}; ssh_ready(){{ return 1; }}; sleep(){{ :; }}; "
             "! wait_for_peer_ssh 192.0.2.2 3 0"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_either_peer_exiting_early_fails_and_cleans_up(self):
        for failed_peer, surviving_peer in (("client", "wan"), ("wan", "client")):
            with self.subTest(failed_peer=failed_peer), tempfile.TemporaryDirectory() as directory:
                for role in ("client", "wan", "router"):
                    (pathlib.Path(directory) / role).mkdir()
                result = bash(
                    "allocate_ports(){ echo '1 2 3'; }; "
                    f"qemu_common(){{ if [[ $2 = {failed_peer} ]]; then return 7; "
                    "else sleep 5; fi; }; "
                    f"execute_vm {directory!s}; status=$?; "
                    f"! kill -0 ${{{surviving_peer}_pid:-0}} 2>/dev/null; "
                    "test \"$qemu_status\" = 1")
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_extracts_artifacts_from_all_three_seeds(self):
        with tempfile.TemporaryDirectory() as directory:
            trace = pathlib.Path(directory) / "mcopy.trace"
            result = bash(
                "mtype(){ return 1; }; mcopy(){ printf '%s\\n' \"$*\" >>\"$TRACE\"; }; "
                "extract_results /work",
                PATH=os.environ.get("PATH", ""), TRACE=str(trace))
            copied = trace.read_text()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("/work/router/seed.img", copied)
        self.assertIn("/work/client/seed.img", copied)
        self.assertIn("/work/wan/seed.img", copied)

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
