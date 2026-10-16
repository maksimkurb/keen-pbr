#!/usr/bin/env python3
"""Dependency-free TCP/UDP integration probe client and server."""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import selectors
import socket
import struct
import threading
import time
import uuid


def family_for(address: str) -> int:
    return socket.AF_INET6 if ipaddress.ip_address(address).version == 6 else socket.AF_INET


def ports(value: str) -> list[int]:
    result: list[int] = []
    for part in value.split(","):
        if "-" in part:
            first, last = (int(item) for item in part.split("-", 1))
            result.extend(range(first, last + 1))
        else:
            result.append(int(part))
    return result


class ObservationLog:
    def __init__(self, path: str) -> None:
        self.path = path
        self.lock = threading.Lock()
        open(path, "a", encoding="utf-8").close()

    def append(self, value: dict) -> None:
        value["observed_at"] = time.time()
        line = json.dumps(value, sort_keys=True)
        with self.lock, open(self.path, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")
            handle.flush()


def response(identity: str, request: dict, peer, proto: str) -> dict:
    return {
        "identity": identity,
        "outbound": identity,
        "token": request.get("token", ""),
        "peer": peer[0],
        "peer_port": peer[1],
        "proto": proto,
        "destination_port": request.get("destination_port"),
    }


def serve_tcp(listener: socket.socket, identity: str, observations: ObservationLog,
              delay: float = 0) -> None:
    while True:
        connection, peer = listener.accept()
        threading.Thread(target=handle_tcp,
                         args=(connection, peer, identity, observations, delay),
                         daemon=True).start()


def handle_tcp(connection: socket.socket, peer, identity: str,
               observations: ObservationLog, delay: float = 0) -> None:
    with connection:
        connection.settimeout(5)
        data = connection.recv(65536)
        if data.startswith((b"GET ", b"HEAD ")):
            first = data.splitlines()[0].decode(errors="replace")
            token = first.split(" ", 2)[1]
            value = response(identity, {"token": token}, peer, "http")
            value["payload_hex"] = data.hex()
            observations.append(value)
            time.sleep(delay)
            body = json.dumps(value, sort_keys=True).encode()
            connection.sendall(
                b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                + f"Content-Length: {len(body)}\r\nConnection: close\r\n\r\n".encode()
                + body
            )
            return
        try:
            request = json.loads(data.decode())
        except (UnicodeDecodeError, json.JSONDecodeError):
            # L7 cases deliberately send an actual ClientHello/HTTP payload,
            # not the JSON probe protocol.  Keep an observation and close the
            # connection; the packet itself is the assertion under test.
            observations.append({"identity": identity, "proto": "raw-tcp",
                                 "payload_hex": data.hex(), "peer": peer[0],
                                 "peer_port": peer[1]})
            return
        value = response(identity, request, peer, "tcp")
        observations.append(value)
        time.sleep(delay)
        connection.sendall(json.dumps(value, sort_keys=True).encode() + b"\n")


def serve_udp(listener: socket.socket, identity: str, observations: ObservationLog,
              delay: float = 0) -> None:
    while True:
        data, peer = listener.recvfrom(65536)
        try:
            request = json.loads(data.decode())
        except (UnicodeDecodeError, json.JSONDecodeError):
            observations.append({"identity": identity, "proto": "raw-udp",
                                 "payload_hex": data.hex(), "peer": peer[0],
                                 "peer_port": peer[1]})
            continue
        value = response(identity, request, peer, "udp")
        observations.append(value)
        time.sleep(delay)
        listener.sendto(json.dumps(value, sort_keys=True).encode(), peer)


def serve_udp_from_destination(listener: socket.socket, identity: str,
                               observations: ObservationLog,
                               delay: float = 0) -> None:
    """IPv4 UDP server that answers from the address the request was sent to,
    like a real multi-homed server (a wildcard socket would answer from the
    egress interface address, which is a different conntrack flow)."""
    listener.setsockopt(socket.IPPROTO_IP, socket.IP_PKTINFO, 1)
    while True:
        data, ancillary, _, peer = listener.recvmsg(65536, 1024)
        local = b"\0\0\0\0"
        for level, kind, value in ancillary:
            if level == socket.IPPROTO_IP and kind == socket.IP_PKTINFO:
                local = value[8:12]  # ipi_addr: destination of the request
        try:
            request = json.loads(data.decode())
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
        value = response(identity, request, peer, "udp")
        observations.append(value)
        time.sleep(delay)
        pktinfo = struct.pack("i4s4s", 0, local, b"\0\0\0\0")
        listener.sendmsg([json.dumps(value, sort_keys=True).encode()],
                         [(socket.IPPROTO_IP, socket.IP_PKTINFO, pktinfo)],
                         0, peer)


def server(args: argparse.Namespace) -> None:
    observations = ObservationLog(args.log)
    threads = []
    for family in (socket.AF_INET, socket.AF_INET6):
        host = "0.0.0.0" if family == socket.AF_INET else "::"
        for port in ports(args.ports):
            udp_target = (serve_udp_from_destination
                          if args.reply_from_destination
                          and family == socket.AF_INET else serve_udp)
            for socktype, target in ((socket.SOCK_STREAM, serve_tcp),
                                     (socket.SOCK_DGRAM, udp_target)):
                listener = socket.socket(family, socktype)
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                if family == socket.AF_INET6:
                    listener.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
                listener.bind((host, port))
                if socktype == socket.SOCK_STREAM:
                    listener.listen(64)
                thread = threading.Thread(target=target,
                                          args=(listener, args.identity, observations,
                                                args.delay_ms / 1000),
                                          daemon=True)
                thread.start()
                threads.append(thread)
    while True:
        time.sleep(3600)


def one_probe(args: argparse.Namespace) -> dict:
    family = family_for(args.destination)
    socktype = socket.SOCK_STREAM if args.proto == "tcp" else socket.SOCK_DGRAM
    with socket.socket(family, socktype) as probe:
        probe.settimeout(args.timeout)
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if args.dscp is not None:
            level = socket.IPPROTO_IP if family == socket.AF_INET else socket.IPPROTO_IPV6
            option = socket.IP_TOS if family == socket.AF_INET else socket.IPV6_TCLASS
            probe.setsockopt(level, option, args.dscp << 2)
        if args.mark:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_MARK, args.mark)
        if args.source or args.source_port:
            probe.bind((args.source or ("0.0.0.0" if family == socket.AF_INET else "::"),
                        args.source_port or 0))
        destination = (args.destination, args.destination_port)
        if args.payload_hex:
            payload = bytes.fromhex(args.payload_hex)
        else:
            request = {"token": args.token, "destination_port": args.destination_port}
            payload = json.dumps(request, sort_keys=True).encode() + b"\n"
        if socktype == socket.SOCK_STREAM:
            probe.connect(destination)
            probe.sendall(payload)
            if args.no_receive:
                return {"sent": len(payload), "proto": args.proto}
            data = probe.recv(65536)
        else:
            probe.sendto(payload, destination)
            if args.no_receive:
                return {"sent": len(payload), "proto": args.proto}
            data, _ = probe.recvfrom(65536)
    result = json.loads(data.decode())
    if result.get("token") != args.token:
        raise RuntimeError(f"probe token mismatch: {result!r}")
    return result


def client(args: argparse.Namespace) -> None:
    print(json.dumps(one_probe(args), sort_keys=True))


def stream(args: argparse.Namespace) -> None:
    sequence = 0
    base_token = args.token
    while True:
        sequence += 1
        args.token = f"{base_token}-{sequence}-{uuid.uuid4().hex}"
        try:
            value = one_probe(args)
            value["ok"] = True
        except Exception as error:  # stream records failures instead of stopping
            value = {"token": args.token, "ok": False,
                     "error": f"{type(error).__name__}: {error}"}
        with open(args.output, "a", encoding="utf-8") as handle:
            handle.write(json.dumps(value, sort_keys=True) + "\n")
        time.sleep(args.interval)


def burst(args: argparse.Namespace) -> None:
    """Send one UDP datagram per source port: one new flow (conntrack entry,
    statistic-cascade decision) per datagram, at a deterministic pace."""
    family = family_for(args.destination)
    source = args.source or ("0.0.0.0" if family == socket.AF_INET else "::")
    sent = 0
    for index, port in enumerate(range(args.first_port, args.first_port + args.count)):
        with socket.socket(family, socket.SOCK_DGRAM) as probe:
            probe.bind((source, port))
            probe.sendto(b"balance-split", (args.destination, args.destination_port))
        sent += 1
        if args.pace_every and index % args.pace_every == args.pace_every - 1:
            time.sleep(args.pace_ms / 1000)
    print(json.dumps({"sent": sent}))


def sink(args: argparse.Namespace) -> None:
    """Count UDP datagrams per IP family until SIGTERM, then dump JSON."""
    import signal
    counts = {"ipv4": 0, "ipv6": 0}
    ports = {"ipv4": set(), "ipv6": set()}
    selector = selectors.DefaultSelector()
    for family, name, host in ((socket.AF_INET, "ipv4", "0.0.0.0"),
                               (socket.AF_INET6, "ipv6", "::")):
        listener = socket.socket(family, socket.SOCK_DGRAM)
        if family == socket.AF_INET6:
            listener.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 16 * 1024 * 1024)
        listener.bind((host, args.port))
        listener.setblocking(False)
        selector.register(listener, selectors.EVENT_READ, name)
    stop = []
    signal.signal(signal.SIGTERM, lambda *_: stop.append(True))
    open(args.output + ".ready", "w").close()
    while not stop:
        for key, _ in selector.select(timeout=0.2):
            while True:
                try:
                    _, peer = key.fileobj.recvfrom(2048)
                except BlockingIOError:
                    break
                counts[key.data] += 1
                ports[key.data].add(peer[1])
    result = {name: {"datagrams": counts[name], "flows": len(ports[name])}
              for name in counts}
    with open(args.output + ".tmp", "w", encoding="utf-8") as handle:
        json.dump(result, handle)
    os.rename(args.output + ".tmp", args.output)


def add_client_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--proto", choices=("tcp", "udp"), default="tcp")
    parser.add_argument("--destination", required=True)
    parser.add_argument("--destination-port", type=int, required=True)
    parser.add_argument("--source")
    parser.add_argument("--source-port", type=int)
    parser.add_argument("--dscp", type=int)
    parser.add_argument("--mark", type=lambda value: int(value, 0))
    parser.add_argument("--token", required=True)
    parser.add_argument("--timeout", type=float, default=4)
    parser.add_argument("--payload-hex")
    parser.add_argument("--no-receive", action="store_true")


def main() -> None:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    server_parser = commands.add_parser("server")
    server_parser.add_argument("--identity", required=True)
    server_parser.add_argument("--log", required=True)
    server_parser.add_argument("--ports", required=True)
    server_parser.add_argument("--delay-ms", type=int, default=0)
    server_parser.add_argument("--reply-from-destination", action="store_true")
    server_parser.set_defaults(handler=server)
    client_parser = commands.add_parser("client")
    add_client_arguments(client_parser)
    client_parser.set_defaults(handler=client)
    stream_parser = commands.add_parser("stream")
    add_client_arguments(stream_parser)
    stream_parser.add_argument("--output", required=True)
    stream_parser.add_argument("--interval", type=float, default=0.1)
    stream_parser.set_defaults(handler=stream)
    burst_parser = commands.add_parser("burst")
    burst_parser.add_argument("--destination", required=True)
    burst_parser.add_argument("--destination-port", type=int, required=True)
    burst_parser.add_argument("--source")
    burst_parser.add_argument("--first-port", type=int, required=True)
    burst_parser.add_argument("--count", type=int, required=True)
    burst_parser.add_argument("--pace-every", type=int, default=50)
    burst_parser.add_argument("--pace-ms", type=float, default=1)
    burst_parser.set_defaults(handler=burst)
    sink_parser = commands.add_parser("sink")
    sink_parser.add_argument("--port", type=int, required=True)
    sink_parser.add_argument("--output", required=True)
    sink_parser.set_defaults(handler=sink)
    args = parser.parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()
