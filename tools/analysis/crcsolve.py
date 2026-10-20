#!/usr/bin/env python3
"""Find where the CRC sits in captured XN297 frames, without knowing the frame layout.

usage: crcsolve.py capture.log

CRC-16 is affine: for two frames of equal length, crc(A) ^ crc(B) == fieldA ^ fieldB no matter
what init value or final XOR is used. Testing that for every candidate length L across many
pairs of distinct, cleanly repeated frames reveals the covered length; the final XOR
(relative to the XN297 init 0xB5D2) then follows from any single frame.
"""
import collections
import itertools
import re
import sys


def bits(h):
    return "".join(f"{int(h[i:i + 2], 16):08b}" for i in range(0, len(h), 2))


def crc_bits(s, crc=0):
    for ch in s:
        feedback = ((crc >> 15) & 1) ^ (ch == "1")
        crc = (crc << 1) & 0xFFFF
        if feedback:
            crc ^= 0x1021
    return crc


def main(path):
    # The tail of each 32-byte capture is noise after the packet ends; 20 bytes covers the frame.
    frames = collections.Counter(re.findall(r"^\d+ ([0-9A-F]{40})", open(path, errors="ignore").read(), re.M))
    good = [h for h, c in frames.most_common() if c >= 4]
    print(f"{len(good)} frames seen at least 4 times")
    B = [bits(h) for h in good]
    for L in range(24, 160 - 16):
        pairs = [(a, b) for a, b in itertools.combinations(range(len(B)), 2) if B[a][:L] != B[b][:L]]
        fields = {b[L:L + 16] for b in B}
        if len(pairs) < 10 or len(fields) < 4:
            continue
        ok = sum(
            crc_bits(B[a][:L]) ^ crc_bits(B[b][:L]) == int(B[a][L:L + 16], 2) ^ int(B[b][L:L + 16], 2)
            for a, b in pairs
        )
        if ok == len(pairs):
            xorout = crc_bits(B[0][:L], 0xB5D2) ^ int(B[0][L:L + 16], 2)
            print(f"CRC covers the first {L} bits ({L / 8:g} bytes); final XOR 0x{xorout:04X}")
            return
    print("no consistent CRC position found")


if __name__ == "__main__":
    main(sys.argv[1])
