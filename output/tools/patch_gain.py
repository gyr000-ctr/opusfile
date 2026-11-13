#!/usr/bin/env python3
"""Patch the OpusHead output_gain field of an .opus file and fix the Ogg CRC.

The ID header packet ("OpusHead") carries a little-endian signed 16-bit
output_gain at packet offset 16, in 1/256ths of a dB. Changing it alters the
gain the decoder applies without touching the encoded audio, which is exactly
how files with different loudness metadata arrive in the wild.
"""
import struct
import sys

# Ogg CRC-32: polynomial 0x04C11DB7, MSB-first, init 0, no final XOR.
_CRC_TABLE = []
for _i in range(256):
    _r = _i << 24
    for _ in range(8):
        _r = ((_r << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if _r & 0x80000000 \
            else (_r << 1) & 0xFFFFFFFF
    _CRC_TABLE.append(_r)


def ogg_crc(data: bytes) -> int:
    crc = 0
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) & 0xFF) ^ b]
    return crc


def main() -> None:
    if len(sys.argv) != 4:
        sys.exit(f"usage: {sys.argv[0]} <in.opus> <out.opus> <gain_q8>")
    src, dst, gain_q8 = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
    if not -32768 <= gain_q8 <= 32767:
        sys.exit("gain_q8 out of range [-32768, 32767]")
    blob = bytearray(open(src, "rb").read())

    pos, patched = 0, False
    while pos < len(blob):
        if blob[pos:pos + 4] != b"OggS":
            sys.exit(f"bad Ogg capture pattern at offset {pos}")
        nseg = blob[pos + 26]
        seg_table = blob[pos + 27:pos + 27 + nseg]
        body_off = pos + 27 + nseg
        body_len = sum(seg_table)
        body = blob[body_off:body_off + body_len]
        idx = body.find(b"OpusHead")
        if idx != -1:
            gain_off = body_off + idx + 16
            blob[gain_off:gain_off + 2] = struct.pack("<h", gain_q8)
            # Zero the CRC field, recompute over the whole page, write back.
            crc_off = pos + 22
            blob[crc_off:crc_off + 4] = b"\x00\x00\x00\x00"
            page = bytes(blob[pos:body_off + body_len])
            blob[crc_off:crc_off + 4] = struct.pack("<I", ogg_crc(page))
            patched = True
        pos = body_off + body_len
    if not patched:
        sys.exit("no OpusHead packet found")
    open(dst, "wb").write(bytes(blob))
    print(f"patched output_gain={gain_q8} q8 ({gain_q8 / 256.0:+.2f} dB) -> {dst}")


if __name__ == "__main__":
    main()
