#!/usr/bin/env python3
"""Generate a 48 kHz stereo 16-bit WAV test tone at a given level."""
import math
import struct
import sys
import wave


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <out.wav> <level_dBFS>")
    out_path, level_db = sys.argv[1], float(sys.argv[2])
    rate, seconds, freq = 48000, 3, 440.0
    amp = 10.0 ** (level_db / 20.0)
    with wave.open(out_path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for n in range(rate * seconds):
            v = int(amp * 32767 * math.sin(2.0 * math.pi * freq * n / rate))
            frames += struct.pack("<hh", v, v)
        w.writeframes(bytes(frames))


if __name__ == "__main__":
    main()
