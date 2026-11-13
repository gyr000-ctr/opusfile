#!/usr/bin/env python3
"""Standalone energy-alignment check for the recorded demo. Run with -I."""
import math, struct, sys, os

d = sys.argv[1] if len(sys.argv) > 1 else "."

def rms(name):
    with open(os.path.join(d, name), "rb") as f:
        data = f.read()
    x = struct.unpack("<%df" % (len(data) // 4), data)
    return math.sqrt(sum(v * v for v in x) / len(x))

def db(v):
    return 20.0 * math.log10(v)

q0, l0 = rms("quiet_0.raw"), rms("loud_0.raw")
qa, la = rms("quiet_aligned.raw"), rms("loud_aligned.raw")
print("native  loudness gap: %.3f dB" % abs(db(q0) - db(l0)))
print("aligned loudness gap: %.3f dB (target RMS 0.25)" % abs(db(qa) - db(la)))
print("aligned RMS: quiet=%.5f loud=%.5f" % (qa, la))
if abs(db(qa) - db(la)) >= 0.01:
    sys.exit("ENERGY ALIGNMENT FAILED")
print("RESULT: energies aligned by per-file gains applied through the API")
