#!/usr/bin/env python3
"""Print the RMS level of a 16-bit PCM WAV file in dBFS."""
import array
import math
import sys
import wave


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} <file.wav>")
    with wave.open(sys.argv[1], "rb") as w:
        frames = w.readframes(w.getnframes())
        width = w.getsampwidth()
    if width != 2:
        sys.exit("16-bit PCM WAV required")
    samples = array.array("h")
    samples.frombytes(frames)
    if sys.byteorder == "big":
        samples.byteswap()
    if not samples:
        print("-inf")
        return
    mean_sq = sum(s * s for s in samples) / len(samples)
    db = 10.0 * math.log10(mean_sq / 32768.0**2) if mean_sq else -math.inf
    print(f"{db:.3f}")


if __name__ == "__main__":
    main()
