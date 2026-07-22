#!/bin/sh
set -eu

if [ ! -x ./darkstat ]; then
  echo "darkstat binary not found; run make first" >&2
  exit 1
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/darkstat-hosttrim.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT INT TERM

pcap="$tmpdir/hosts.pcap"
db="$tmpdir/out.db"
log="$tmpdir/darkstat.log"

python3 - "$pcap" <<'PY'
import socket
import struct
import sys

path = sys.argv[1]

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
    total_len = 20 + len(payload)
    header = struct.pack("!BBHHHBBH4s4s",
                         0x45, 0, total_len, ident, 0,
                         64, proto, 0, src_ip, dst_ip)
    header = struct.pack("!BBHHHBBH4s4s",
                         0x45, 0, total_len, ident, 0,
                         64, proto, checksum(header), src_ip, dst_ip)
    return header + payload

def udp_packet(src_ip, dst_ip, sport, dport, payload, ident):
    udp_len = 8 + len(payload)
    return ipv4_packet(src_ip, dst_ip, socket.IPPROTO_UDP,
                       struct.pack("!HHHH", sport, dport, udp_len, 0) + payload,
                       ident)

with open(path, "wb") as f:
    f.write(struct.pack("<IHHIIII", 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1))
    for i in range(20):
        src = ipv4(10, 1, 0, i + 1)
        dst = ipv4(172, 16, 0, i + 1)
        payload = b"host-" + str(i).encode("ascii")
        pkt = udp_packet(src, dst, 10000 + i, 53, payload, i + 1)
        eth = mac(0x001122330000 + i) + mac(0xaabbccdd0000 + i) + struct.pack("!H", 0x0800)
        frame = eth + pkt
        f.write(struct.pack("<IIII", i, i, len(frame), len(frame)))
        f.write(frame)
PY

./darkstat -r "$pcap" --verbose --hosts-max 4 --hosts-keep 2 --test-reduce --export "$db" >"$log" 2>&1

grep -Fq 'Total packets: 20, bytes:' "$log"

python3 - "$db" <<'PY'
import struct
import sys

path = sys.argv[1]
with open(path, 'rb') as f:
    data = f.read()

count = struct.unpack('!I', data[8:12])[0]
if count > 4:
    raise SystemExit(f"expected trimmed host count <= 4, got {count}")
PY
