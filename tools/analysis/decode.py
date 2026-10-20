#!/usr/bin/env python3
"""Decode a capture from the rf-tool `q <ch>` command into one line per button press.

usage: decode.py capture.log

Each press arrives as a burst of identical frames; frames are CRC-checked, descrambled
and deduplicated, then printed in time order with the decoded address and payload.
"""
import re
import sys

SCRAMBLE = [0xE3, 0xB1, 0x4B, 0xEA, 0x85, 0xBC, 0xE5, 0x66, 0x0D, 0xAE, 0x8C, 0x88, 0x12, 0x69, 0xEE, 0x1F]
CRC_XOROUT = 0xF494  # for a 5-byte address and 11-byte payload; see crcsolve.py for other layouts

COMMANDS = {
    0x30: "wake", 0x31: "power", 0x32: "backlight", 0x35: "brighter", 0x36: "dimmer",
    0x39: "cooler", 0x3A: "warmer", 0x3E: "auto off", 0x40: "auto on",
}


def bit_reverse(b):
    return int(f"{b:08b}"[::-1], 2)


def crc16(data, crc=0xB5D2):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def main(path):
    text = open(path, errors="ignore").read()
    seen, order = {}, []
    for t, h in re.findall(r"^(\d+) ([0-9A-F]{36})", text, re.M):
        raw = bytes.fromhex(h)
        if crc16(raw[:16]) ^ CRC_XOROUT != int.from_bytes(raw[16:18], "big"):
            continue
        addr = bytes(raw[i] ^ SCRAMBLE[i] for i in range(5))[::-1]
        payload = bytes(bit_reverse(raw[i] ^ SCRAMBLE[i]) for i in range(5, 16))
        key = (addr, payload)
        if key not in seen:
            seen[key] = [int(t), 0]
            order.append(key)
        seen[key][1] += 1
    if not order:
        print("no CRC-valid frames found")
        return
    t0 = seen[order[0]][0]
    for addr, p in order:
        t, count = seen[(addr, p)]
        checksum = "ok" if sum(p[:10]) & 0xFF == p[10] else "BAD"
        print(
            f"+{(t - t0) / 1000:6.1f}s x{count:<3} addr {addr.hex(' ')} | id {p[:4].hex(' ')} | "
            f"cmd {p[4]:02x} {COMMANDS.get(p[4], '?'):<9} | bright {p[5]:02x} cct {p[6]:02x} "
            f"backlight {p[7]:02x} mode {p[8]:02x} | ctr {p[9]:02x} sum {p[10]:02x} {checksum}"
        )


if __name__ == "__main__":
    main(sys.argv[1])
