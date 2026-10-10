#!/usr/bin/env python3
"""keen-pbr M0 experiment E load generator (LAN PC side, stdlib only).

Sends A queries for m0canary.test to --server:--port at --rate per second for
--duration seconds. Prints one line per second:
    t=<unix> sent=N ok=N timeout=N answers=<ip:count,...>
"""

import argparse
import random
import select
import socket
import struct
import sys
import time

QNAME_DEFAULT = "m0canary.test"


def build_query(qid, qname):
    header = struct.pack("!HHHHHH", qid, 0x0100, 1, 0, 0, 0)
    question = b"".join(
        bytes([len(label)]) + label.encode("ascii") for label in qname.split(".")
    ) + b"\x00"
    return header + question + struct.pack("!HH", 1, 1)


def skip_name(data, off):
    while True:
        if off >= len(data):
            raise ValueError("truncated name")
        ln = data[off]
        if ln & 0xC0 == 0xC0:
            return off + 2
        if ln == 0:
            return off + 1
        off += 1 + ln


def parse_a_answers(data):
    """Return (id, rcode, [ipv4 strings of type-A answers])."""
    if len(data) < 12:
        raise ValueError("short packet")
    qid, flags, qd, an, _ns, _ar = struct.unpack("!HHHHHH", data[:12])
    rcode = flags & 0x000F
    off = 12
    for _ in range(qd):
        off = skip_name(data, off)
        off += 4
    ips = []
    for _ in range(an):
        off = skip_name(data, off)
        if off + 10 > len(data):
            raise ValueError("truncated RR")
        rtype, _rclass, _ttl, rdlen = struct.unpack("!HHIH", data[off:off + 10])
        off += 10
        rdata = data[off:off + rdlen]
        off += rdlen
        if rtype == 1 and rdlen == 4:
            ips.append(socket.inet_ntoa(rdata))
    return qid, rcode, ips


def main():
    ap = argparse.ArgumentParser(description="M0 experiment E load generator")
    ap.add_argument("--server", required=True, help="dnsmasq LAN address")
    ap.add_argument("--port", type=int, default=53)
    ap.add_argument("--rate", type=float, default=100.0, help="queries per second")
    ap.add_argument("--duration", type=float, default=60.0, help="seconds")
    ap.add_argument("--timeout", type=float, default=1.0, help="per-query timeout (s)")
    ap.add_argument("--qname", default=QNAME_DEFAULT)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    dest = (args.server, args.port)

    pending = {}  # id -> send_time
    sent = ok = timeouts = 0
    answers = {}
    bucket_start = time.time()
    bucket_sent = bucket_ok = bucket_timeout = 0
    bucket_answers = {}

    def flush_bucket(now):
        nonlocal bucket_start, bucket_sent, bucket_ok, bucket_timeout, bucket_answers
        parts = ",".join("%s:%d" % (ip, c) for ip, c in sorted(bucket_answers.items()))
        print("t=%d sent=%d ok=%d timeout=%d answers=%s"
              % (int(now), bucket_sent, bucket_ok, bucket_timeout, parts or "-"),
              flush=True)
        bucket_start = now
        bucket_sent = bucket_ok = bucket_timeout = 0
        bucket_answers = {}

    start = time.monotonic()
    end = start + args.duration
    interval = 1.0 / args.rate if args.rate > 0 else 0.0
    next_send = start

    while True:
        now_m = time.monotonic()
        if now_m >= end and not pending:
            break

        # Send as many queries as the rate schedule requires.
        if now_m < end and now_m >= next_send:
            qid = random.randrange(0x10000)
            while qid in pending:
                qid = random.randrange(0x10000)
            try:
                sock.sendto(build_query(qid, args.qname), dest)
                pending[qid] = now_m
                sent += 1
                bucket_sent += 1
            except OSError as e:
                print("send error: %s" % e, file=sys.stderr)
            next_send += interval if interval else (end - now_m)
            if next_send < now_m - 1.0:
                next_send = now_m  # do not burst to catch up after a stall

        # Expire timed-out queries.
        for qid in [q for q, t in pending.items() if now_m - t >= args.timeout]:
            del pending[qid]
            timeouts += 1
            bucket_timeout += 1

        # Wait for a reply until the next send or the next expiry.
        wait = args.timeout
        if pending:
            wait = min(wait, min(pending.values()) + args.timeout - now_m)
        if now_m < end:
            wait = min(wait, max(0.0, next_send - now_m))
        wait = max(0.0, min(wait, 0.05))

        readable, _, _ = select.select([sock], [], [], wait)
        if readable:
            while True:
                try:
                    data, _addr = sock.recvfrom(4096)
                except BlockingIOError:
                    break
                try:
                    qid, rcode, ips = parse_a_answers(data)
                except (ValueError, struct.error):
                    continue
                if qid not in pending:
                    continue  # late reply after timeout, or unknown
                del pending[qid]
                if rcode == 0:
                    ok += 1
                    bucket_ok += 1
                for ip in ips:
                    answers[ip] = answers.get(ip, 0) + 1
                    bucket_answers[ip] = bucket_answers.get(ip, 0) + 1

        if time.time() - bucket_start >= 1.0:
            flush_bucket(time.time())

    if bucket_sent or bucket_ok or bucket_timeout:
        flush_bucket(time.time())

    summary = ",".join("%s:%d" % (ip, c) for ip, c in sorted(answers.items()))
    print("done sent=%d ok=%d timeout=%d answers=%s"
          % (sent, ok, timeouts, summary or "-"), flush=True)
    sock.close()


if __name__ == "__main__":
    main()
