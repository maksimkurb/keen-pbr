#!/usr/bin/env python3
"""Small instrumented authoritative DNS fixture supporting UDP and TCP."""

from __future__ import annotations

import argparse
import ipaddress
import json
import socket
import struct
import threading
import time


def question_end(packet: bytes, offset: int = 12) -> int:
    while packet[offset]:
        offset += packet[offset] + 1
    return offset + 5


def qname(packet: bytes, offset: int = 12) -> str:
    labels = []
    while packet[offset]:
        length = packet[offset]
        offset += 1
        labels.append(packet[offset:offset + length].decode(errors="replace"))
        offset += length
    return ".".join(labels).lower()


class Fixture:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.lock = threading.Lock()

    def answer(self, packet: bytes, peer) -> bytes:
        end = question_end(packet)
        name = qname(packet)
        qtype = struct.unpack("!H", packet[end - 4:end - 2])[0]
        address = self.args.a if qtype == 1 else self.args.aaaa if qtype == 28 else None
        with self.lock, open(self.args.log, "a", encoding="utf-8") as handle:
            handle.write(json.dumps({"identity": self.args.identity, "qname": name,
                                     "qtype": qtype, "peer": peer[0],
                                     "observed_at": time.time()}, sort_keys=True) + "\n")
        flags = 0x8180
        header = packet[:2] + struct.pack("!HHHHH", flags, 1, 1 if address else 0, 0, 0)
        result = header + packet[12:end]
        if address:
            packed = ipaddress.ip_address(address).packed
            result += b"\xc0\x0c" + struct.pack("!HHIH", qtype, 1, 0, len(packed)) + packed
        return result


def udp_loop(sock: socket.socket, fixture: Fixture) -> None:
    while True:
        packet, peer = sock.recvfrom(65536)
        sock.sendto(fixture.answer(packet, peer), peer)


def tcp_loop(sock: socket.socket, fixture: Fixture) -> None:
    while True:
        connection, peer = sock.accept()
        with connection:
            size_data = connection.recv(2)
            if len(size_data) != 2:
                continue
            size = struct.unpack("!H", size_data)[0]
            packet = b""
            while len(packet) < size:
                packet += connection.recv(size - len(packet))
            answer = fixture.answer(packet, peer)
            connection.sendall(struct.pack("!H", len(answer)) + answer)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--identity", required=True)
    parser.add_argument("--listen", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--log", required=True)
    parser.add_argument("--a", required=True)
    parser.add_argument("--aaaa", required=True)
    args = parser.parse_args()
    open(args.log, "a", encoding="utf-8").close()
    fixture = Fixture(args)
    family = socket.AF_INET6 if ipaddress.ip_address(args.listen).version == 6 else socket.AF_INET
    udp = socket.socket(family, socket.SOCK_DGRAM)
    tcp = socket.socket(family, socket.SOCK_STREAM)
    for sock in (udp, tcp):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if family == socket.AF_INET6:
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        sock.bind((args.listen, args.port))
    tcp.listen(32)
    threading.Thread(target=udp_loop, args=(udp, fixture), daemon=True).start()
    tcp_loop(tcp, fixture)


if __name__ == "__main__":
    main()
