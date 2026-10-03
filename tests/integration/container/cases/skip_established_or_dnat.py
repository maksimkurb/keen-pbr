from __future__ import annotations

import re
import uuid

from integration_context import REMOTE_CONTAINER_DIR, parse_probe

from .routing_common import apply, probe

# WAN fixtures listen on 18080,19000,19010,19011,19020 only.
PORT = 19500
NFT_TABLE = "kpbr_it_dnat"
LOG = "/tmp/kpbr_it_dnat.log"


def _install_port_forward(context):
    if context.backend == "iptables":
        context.run("iptables", "-t", "nat", "-A", "PREROUTING", "-i", "wan_direct",
                    "-p", "tcp", "--dport", str(PORT), "-j", "DNAT",
                    "--to-destination", f"192.0.2.2:{PORT}")
        return
    context.run("nft", "delete", "table", "ip", NFT_TABLE, check=False)
    context.run("nft", "add", "table", "ip", NFT_TABLE)
    context.run("nft", "add", "chain", "ip", NFT_TABLE, "prerouting",
                "{", "type", "nat", "hook", "prerouting", "priority", "-100", ";", "}")
    context.run("nft", "add", "rule", "ip", NFT_TABLE, "prerouting",
                "iifname", "wan_direct", "tcp", "dport", str(PORT),
                "dnat", "to", f"192.0.2.2:{PORT}")


def _remove_port_forward(context):
    if context.backend == "iptables":
        context.run("iptables", "-t", "nat", "-D", "PREROUTING", "-i", "wan_direct",
                    "-p", "tcp", "--dport", str(PORT), "-j", "DNAT",
                    "--to-destination", f"192.0.2.2:{PORT}", check=False)
    else:
        context.run("nft", "delete", "table", "ip", NFT_TABLE, check=False)


def _conntrack(context, *args):
    return context.run("conntrack", "-L", "-p", "tcp", *args, check=False).stdout


def register(registry):
    @registry.case("skip_established_or_dnat")
    def skip_established_or_dnat(context):
        try:
            apply(context, [{"outbound": "wan_pbr", "src_addr": "192.0.2.2/32"}])
            # The src rule is active: client-originated traffic leaves via wan_pbr.
            probe(context, "wan_pbr", destination_port=19000)
            # Control: that flow's connection carries keen-pbr's non-zero mark.
            marked = _conntrack(context, "--dport", "19000")
            assert re.search(r"mark=[1-9]\d*", marked), marked

            _install_port_forward(context)
            context.client("sh", "-c",
                           f"nohup python3 {REMOTE_CONTAINER_DIR}/probe.py server "
                           f"--identity client --log {LOG} --ports {PORT} "
                           ">/dev/null 2>&1 </dev/null &")
            context.run("conntrack", "-F", check=False)

            # Port-forward from wan_direct to the client. Without the DNAT skip
            # the client's reply would be classified by the src rule and sent
            # into wan_pbr; with it the reply returns through wan_direct.
            token = uuid.uuid4().hex
            result = None
            for _ in range(10):  # the listener starts asynchronously
                result = context.wan(
                    "direct", "python3", f"{REMOTE_CONTAINER_DIR}/probe.py", "client",
                    "--proto", "tcp", "--destination", "10.10.0.1",
                    "--destination-port", str(PORT), "--token", token,
                    "--timeout", "2", check=False, timeout=10)
                if result.returncode == 0:
                    break
            assert result.returncode == 0, result.stderr
            assert parse_probe(result.stdout, token)["identity"] == "client"

            # The forwarded connection is DNAT'd and was never marked.
            forwarded = _conntrack(context, "--dport", str(PORT))
            assert re.search(r"mark=0\b", forwarded), forwarded
            assert not re.search(r"mark=[1-9]", forwarded), forwarded
        finally:
            context.client("pkill", "-f", "probe.py server --identity client",
                           check=False)
            _remove_port_forward(context)
            context.run("conntrack", "-F", check=False)
