#!/usr/bin/env python3
"""Energy analysis for the gain layer. Run with: python3 -I analyze.py"""
import math, struct, sys, os

FX = os.path.join(os.path.dirname(__file__))

def read_f32(name):
    with open(os.path.join(FX, name), "rb") as f:
        data = f.read()
    return struct.unpack("<%df" % (len(data) // 4), data)

def rms(x):
    return math.sqrt(sum(v * v for v in x) / len(x))

def db(x):
    return 20.0 * math.log10(x) if x > 0 else float("-inf")

# --- Part 1: a single, identical gain on both materials scales RMS by the
#     exact linear factor the gain layer is supposed to use. ---
G_Q8 = 6 * 256
expect_factor = 10.0 ** (G_Q8 / (20.0 * 256.0))
print("Part 1: unified +6 dB gain (q8=%d), expected linear factor %.7f"
      % (G_Q8, expect_factor))
for stem in ("quiet", "loud"):
    r0 = rms(read_f32(stem + "_0.raw"))
    rg = rms(read_f32(stem + "_g.raw"))
    got = rg / r0
    print("  %-5s RMS(0dB)=%.6f RMS(+6dB)=%.6f factor=%.7f %s"
          % (stem, r0, rg, got,
             "OK" if abs(got - expect_factor) < 1e-6 else "MISMATCH"))

# --- Part 2: energy alignment.
#     Materials have very different native loudness. Read each one's level,
#     derive the gain that moves it to a common target, and confirm that after
#     those (different) gains are applied through the API, output energy is
#     aligned while the no-gain outputs stay far apart. ---
TARGET = 0.25
print("\nPart 2: align both materials to target RMS %.4f" % TARGET)
rows = []
for stem in ("quiet", "loud"):
    r0 = rms(read_f32(stem + "_0.raw"))
    need_db = db(TARGET / r0)
    rows.append((stem, r0, need_db))
for stem, r0, need_db in rows:
    print("  %-5s native RMS=%.6f (%.2f dBFS) -> required gain %+.3f dB"
          % (stem, r0, db(r0), need_db))
native_gap = abs(db(rows[0][1]) - db(rows[1][1]))
print("  native loudness gap: %.2f dB" % native_gap)
print("  (required gains written to gains.txt for the apply step)")
with open(os.path.join(FX, "gains.txt"), "w") as f:
    for stem, r0, need_db in rows:
        f.write("%s %d\n" % (stem, round(need_db * 256.0)))
