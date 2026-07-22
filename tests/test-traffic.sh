#!/bin/sh
set -eu

if [ ! -x ./darkstat ]; then
  echo "darkstat binary not found; run make first" >&2
  exit 1
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/darkstat-traffic.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT INT TERM

pcap="$tmpdir/traffic.pcap"
log="$tmpdir/darkstat.log"

python3 - "$pcap" <<'PY'
import random
import socket
import struct
import sys

path = sys.argv[1]
random.seed(1337)

def mac(n):
    return bytes([(n >> 40) & 0xff, (n >> 32) & 0xff, (n >> 24) & 0xff,
                  (n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff])

def ipv4(a, b, c, d):
    return bytes([a, b, c, d])

def checksum(data):
    if len(data) % 2:
        data += b"\x00"
    s = 0
    for i in range(0, len(data), 2):
        s += (data[i] << 8) + data[i + 1]
        s = (s & 0xffff) + (s >> 16)
    return (~s) & 0xffff

def ipv4_packet(src_ip, dst_ip, proto, payload, ident):
    ver_ihl = 0x45
    total_len = 20 + len(payload)
    header = struct.pack("!BBHHHBBH4s4s",
                         ver_ihl, 0, total_len, ident, 0,
                         64, proto, 0, src_ip, dst_ip)
    header = struct.pack("!BBHHHBBH4s4s",
                         ver_ihl, 0, total_len, ident, 0,
                         64, proto, checksum(header), src_ip, dst_ip)
    return header + payload

def udp_packet(src_ip, dst_ip, sport, dport, payload, ident):
    udp_len = 8 + len(payload)
    header = struct.pack("!HHHH", sport, dport, udp_len, 0)
    return ipv4_packet(src_ip, dst_ip, socket.IPPROTO_UDP, header + payload, ident)

def tcp_packet(src_ip, dst_ip, sport, dport, payload, ident):
    seq = random.randint(1, 100000)
    offset_flags = (5 << 12) | 0x18
    header = struct.pack("!HHLLHHHH", sport, dport, seq, 0, offset_flags, 4096, 0, 0)
    return ipv4_packet(src_ip, dst_ip, socket.IPPROTO_TCP, header + payload, ident)

with open(path, "wb") as f:
    f.write(struct.pack("<IHHIIII", 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1))
    for i in range(8):
        src = ipv4(10, 0, 0, i + 1)
        dst = ipv4(192, 168, 1, 10 + i)
        if i % 2 == 0:
            payload = b"hello-" + bytes([48 + i])
            pkt = udp_packet(src, dst, 10000 + i, 53, payload, i + 1)
        else:
            payload = b"GET / HTTP/1.0\r\n\r\n"
            pkt = tcp_packet(src, dst, 20000 + i, 80, payload, i + 1)
        eth = mac(0x001122334400 + i) + mac(0xaabbccddeeff - i) + struct.pack("!H", 0x0800)
        frame = eth + pkt
        f.write(struct.pack("<IIII", i, i, len(frame), len(frame)))
        f.write(frame)
PY

./darkstat -r "$pcap" --verbose >"$log" 2>&1

grep -Fq 'Total packets: 8, bytes:' "$log"
