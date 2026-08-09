#!/usr/bin/env python3
"""Patch the NXP LPC boot-ROM vector-table checksum into a flat binary.

The LPC8xx boot ROM refuses to treat an image as valid application code
unless the sum of the first 8 vector-table words (indices 0-7, byte
offsets 0x00-0x1C), taken as 32-bit values, is exactly zero. Word 7
(offset 0x1C) is reserved for this checksum: it must hold the two's
complement of the sum of words 0-6.

Usage: fix_lpc_checksum.py <binary-file>

Patches the file in place and prints the checksum value written.
"""
import struct
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <binary-file>", file=sys.stderr)
        return 1

    path = sys.argv[1]
    with open(path, "r+b") as f:
        data = bytearray(f.read())
        if len(data) < 32:
            print(f"error: {path} is smaller than 32 bytes, no vector table found", file=sys.stderr)
            return 1

        words = struct.unpack_from("<7I", data, 0)
        checksum = (-sum(words)) & 0xFFFFFFFF
        struct.pack_into("<I", data, 0x1C, checksum)

        f.seek(0)
        f.write(data)

    print(f"patched {path}: LPC boot checksum at 0x1C = 0x{checksum:08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
