#!/usr/bin/env python3
from __future__ import annotations

import os
import pathlib
import re
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parent
RUNNER = ROOT / "scripts" / "run-netns-suite.sh"
NETNS = ROOT / "netns"


def source(fragment: str, **environment: str) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env.update(environment)
    return subprocess.run(
        ["/bin/bash", "-c", f"source {RUNNER!s}; {fragment}"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )


class RootlessNetnsHarnessTest(unittest.TestCase):
    def test_invalid_backend(self):
        result = source("select_backends invalid")
        self.assertEqual(result.returncode, 2)
        self.assertIn("INTEGRATION_BACKEND", result.stderr)

    def test_invalid_case_syntax(self):
        result = source("select_backends all", INTEGRATION_CASES="bad case")
        self.assertEqual(result.returncode, 2)
        self.assertIn("INTEGRATION_CASES", result.stderr)

    def test_launcher_creates_user_network_mount_and_pid_namespaces(self):
        text = RUNNER.read_text(encoding="utf-8")
        for option in ("--user", "--map-root-user", "--mount", "--net", "--pid",
                       "--mount-proc", "--uts", "--ipc", "--kill-child"):
            self.assertIn(option, text)
        self.assertIn("setpriv --no-new-privs", text)

    def test_launcher_has_no_privileged_fallback(self):
        text = RUNNER.read_text(encoding="utf-8")
        self.assertNotRegex(text, re.compile(r"(^|[;&|])\s*(sudo|doas)\s", re.MULTILINE))
        self.assertIn("do not run the netns harness as root", text)
        self.assertIn("no privileged fallback", text)

    def test_missing_tools_are_reported_together_before_namespace_setup(self):
        result = source(
            """
command() {
    if [[ "$1" == "-v" && ( "$2" == "conntrack" || "$2" == "ipset" || "$2" == "ps" || "$2" == "unshare" ) ]]; then
        return 1
    fi
    builtin command "$@"
}
main all
"""
        )
        self.assertEqual(result.returncode, 2)
        missing = next(line for line in result.stderr.splitlines()
                       if "missing_executables=" in line)
        self.assertIn("conntrack", missing)
        self.assertIn("ipset", missing)
        self.assertIn("ps", missing)
        self.assertIn("unshare", missing)
        self.assertIn("sudo pacman -S --needed", result.stderr)
        self.assertNotIn("KPBR_IT_BEGIN", result.stdout)

    def test_sandbox_guards_host_user_and_network_namespaces(self):
        text = (NETNS / "sandbox.sh").read_text(encoding="utf-8")
        self.assertIn("/proc/self/uid_map", text)
        self.assertIn("/proc/self/gid_map", text)
        self.assertIn("/proc/self/ns/net", text)
        self.assertIn("/proc/self/ns/user", text)
        self.assertIn("host root filesystem is not read-only", text)

    def test_sandbox_reports_failed_stage_and_command(self):
        env = {
            "KPBR_SANDBOX_SOURCE_ONLY": "1",
            "KPBR_SOURCE_ROOT": "/tmp",
            "KPBR_SOURCE_BIN": "/tmp/keen-pbr",
            "KPBR_HOST_UID": "1000",
            "KPBR_HOST_GID": "1000",
            "KPBR_HOST_NETNS": "net:[1]",
            "KPBR_HOST_USERNS": "user:[1]",
        }
        result = subprocess.run(
            ["/bin/bash", "-c",
             f"source {NETNS / 'sandbox.sh'}; run_stage topology topology.sh false"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, **env},
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("stage=topology", result.stderr)
        self.assertIn("command=topology.sh", result.stderr)
        self.assertIn("exit=1", result.stderr)

    def test_service_start_waits_for_runtime_ready(self):
        text = (NETNS / "service-control.sh").read_text(encoding="utf-8")
        self.assertIn(
            'grep -Eq \'"runtime_state"[[:space:]]*:[[:space:]]*"running"\'',
            text,
        )

    def test_sandbox_imports_repo_and_remounts_it_read_only(self):
        text = (NETNS / "sandbox.sh").read_text(encoding="utf-8")
        self.assertIn('mount -t tmpfs -o mode=0755,nosuid,nodev tmpfs "$sandbox_repo"',
                      text)
        self.assertIn('cp -a --no-preserve=ownership "$source_root/tests/integration/."',
                      text)
        self.assertIn('mount -o remount,ro tmpfs "$sandbox_repo"', text)
        self.assertIn('mount -o remount,bind,ro / /', text)
        self.assertIn('mount -o remount,bind,ro "$target" "$target"', text)
        self.assertIn('mount_tmpfs /var/lib/docker 0755', text)
        self.assertIn('mount -o remount,ro tmpfs /var/lib/docker', text)
        self.assertNotIn('mount --bind "$source_root" "$sandbox_repo"', text)
        self.assertNotIn('mount --bind / /', text)

    def test_all_shell_scripts_parse(self):
        scripts = [RUNNER, *NETNS.glob("*.sh"), *NETNS.joinpath("shims").iterdir()]
        for script in scripts:
            if not script.is_file():
                continue
            with self.subTest(script=script.name):
                result = subprocess.run(["bash", "-n", str(script)], text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_host_global_module_loading_is_blocked_in_path(self):
        shim = NETNS / "shims" / "modprobe"
        self.assertTrue(shim.exists())
        self.assertIn("disabled", shim.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
