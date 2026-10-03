#!/usr/bin/env python3
"""Small instrumented authoritative DNS fixture supporting UDP and TCP."""

from __future__ import annotations

import argparse
import ipaddress
import json
import socket
import struct
import sys
import threading
import time
import uuid


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


def encoded_name(name: str) -> bytes:
    return b"".join(bytes((len(label),)) + label.encode() for label in name.split(".")) + b"\0"


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
        cname = (self.args.cname_target
                 if self.args.cname_domain and name == self.args.cname_domain.lower()
                 else None)
        answer_count = (1 if address else 0) + (1 if cname and address else 0)
        header = packet[:2] + struct.pack("!HHHHH", flags, 1, answer_count, 0, 0)
        result = header + packet[12:end]
        if cname and address:
            result += (b"\xc0\x0c" + struct.pack("!HHIH", 5, 1, 30,
                                                     len(encoded_name(cname))) +
                       encoded_name(cname))
            owner = encoded_name(cname)
            packed = ipaddress.ip_address(address).packed
            result += owner + struct.pack("!HHIH", qtype, 1, 30, len(packed)) + packed
        elif address:
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


def query(args: argparse.Namespace) -> None:
    """Issue one UDP query and print the received wire packet."""
    packet = (struct.pack("!HHHHHH", uuid.uuid4().int & 0xffff, 0x0100, 1, 0, 0, 0) +
              encoded_name(args.name.rstrip(".")) + struct.pack("!HH", args.qtype, 1))
    family = socket.AF_INET6 if ipaddress.ip_address(args.server).version == 6 else socket.AF_INET
    address = ((args.server, args.port, 0, 0) if family == socket.AF_INET6
               else (args.server, args.port))
    with socket.socket(family, socket.SOCK_DGRAM) as sock:
        sock.settimeout(args.timeout)
        sock.sendto(packet, address)
        response, peer = sock.recvfrom(65536)
    print(json.dumps({"bytes": len(response), "peer": peer[0],
                      "qname": qname(response), "qtype": args.qtype,
                      "packet_hex": response.hex()}, sort_keys=True))


def main() -> None:
    if sys.argv[1:2] == ["query"]:
        parser = argparse.ArgumentParser()
        parser.add_argument("--server", required=True)
        parser.add_argument("--port", type=int, default=53)
        parser.add_argument("--name", required=True)
        parser.add_argument("--qtype", type=int, default=1)
        parser.add_argument("--timeout", type=float, default=8)
        query(parser.parse_args(sys.argv[2:]))
        return
    parser = argparse.ArgumentParser()
    parser.add_argument("--identity", required=True)
    parser.add_argument("--listen", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--log", required=True)
    parser.add_argument("--a", required=True)
    parser.add_argument("--aaaa", required=True)
    parser.add_argument("--cname-domain")
    parser.add_argument("--cname-target")
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
