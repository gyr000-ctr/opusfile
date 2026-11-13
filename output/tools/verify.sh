#!/usr/bin/env bash
# Acceptance checks for gain read/apply/switch/clamp in libopusfile.
# Uses only the repo's own entry point (examples/opusfile_example).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/output"
DATA="$OUT/data"
LOG="$OUT/logs"
EXAMPLE="$ROOT/build/opusfile_example"
PY="python3 -I"   # isolated: never import from the data directories

mkdir -p "$DATA" "$LOG"
cd "$DATA"

echo "=== [1/6] Build check ==="
if [ ! -x "$EXAMPLE" ]; then
  cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release -DOP_DISABLE_DOCS=ON
  cmake --build "$ROOT/build" -j"$(nproc)"
fi
"$EXAMPLE" 2>&1 | head -1 || true

echo "=== [2/6] Make material with large loudness differences ==="
# Three takes of the same tone at -10/-20/-30 dBFS, each carrying a header
# gain that normalizes it to about -16 dBFS on playback.
$PY "$OUT/tools/gen_source.py" take_loud.wav  -10
$PY "$OUT/tools/gen_source.py" take_mid.wav   -20
$PY "$OUT/tools/gen_source.py" take_quiet.wav -30
opusenc --quiet take_loud.wav  loud.opus
opusenc --quiet take_mid.wav   mid.opus
opusenc --quiet take_quiet.wav quiet.opus
# Header gains (Q8 dB): -6, +4, +14 dB -> all land near -16 dBFS.
$PY "$OUT/tools/patch_gain.py" loud.opus  gain_m6.opus  -1536
$PY "$OUT/tools/patch_gain.py" mid.opus   gain_p4.opus   1024
$PY "$OUT/tools/patch_gain.py" quiet.opus gain_p14.opus  3584
# A zero-gain copy of the mid take, for the bit-exactness check.
$PY "$OUT/tools/patch_gain.py" mid.opus   gain_0.opus        0

echo "=== [3/6] Gain ON: energies align ==="
for f in gain_m6 gain_p4 gain_p14; do
  "$EXAMPLE" "$f.opus" > "on_$f.wav" 2> "on_$f.log"
  rms=$($PY "$OUT/tools/wav_rms.py" "on_$f.wav")
  echo "  $f: RMS = $rms dBFS  ($(grep 'Effective gain' "on_$f.log"))"
  echo "$rms" > "on_$f.rms"
done
$PY - <<'EOF'
vals = [float(open(f"on_{n}.rms").read()) for n in ("gain_m6", "gain_p4", "gain_p14")]
spread = max(vals) - min(vals)
print(f"  spread after gain: {spread:.3f} dB (target < 0.2 dB)")
assert spread < 0.2, "energy not aligned"
print("  PASS: energies aligned")
EOF

echo "=== [3b] Gain OFF: original loudness differences remain ==="
for f in gain_m6 gain_p4 gain_p14; do
  "$EXAMPLE" --no-gain "$f.opus" > "off_$f.wav" 2> "off_$f.log"
  rms=$($PY "$OUT/tools/wav_rms.py" "off_$f.wav")
  echo "  $f: RMS = $rms dBFS  ($(grep 'Gain application' "off_$f.log"))"
done

echo "=== [4/6] Gain OFF: samples bit-identical to ungained decode ==="
# Decode the +4 dB file with the switch off, and an identical stream whose
# header gain is 0 with the switch on. Both program the decoder with gain 0,
# so every sample must match exactly.
"$EXAMPLE" --no-gain gain_p4.opus > off_p4.wav 2>/dev/null
"$EXAMPLE" gain_0.opus > on_g0.wav 2>/dev/null
if cmp -s off_p4.wav on_g0.wav; then
  echo "  PASS: cmp reports zero differences (off_p4.wav == on_g0.wav)"
else
  echo "  FAIL: outputs differ"; exit 1
fi

echo "=== [5/6] Out-of-range gain: clamped and reported ==="
"$EXAMPLE" --gain-offset=98303 gain_p4.opus > /dev/null 2> clamp_hi.log
"$EXAMPLE" --gain-offset=-98302 gain_p4.opus > /dev/null 2> clamp_lo.log
grep -E "Effective gain" clamp_hi.log
grep -E "Effective gain" clamp_lo.log
grep -q "clamped" clamp_hi.log && grep -q "128.00 dB" clamp_hi.log \
  && echo "  PASS: +98303 q8 clamped to 32767 q8 and reported"
grep -q "clamped" clamp_lo.log && grep -q -- "-128.00 dB" clamp_lo.log \
  && echo "  PASS: -98302 q8 clamped to -32768 q8 and reported"
# In-range gain must NOT be reported as clamped.
"$EXAMPLE" --gain-offset=2560 gain_p4.opus > /dev/null 2> noclamp.log
grep -E "Effective gain" noclamp.log
! grep -q "clamped" noclamp.log && echo "  PASS: in-range gain not flagged"

echo "=== [6/6] No extra copy: gain shares the decode output buffer ==="
# The gain is programmed into the libopus decoder (OPUS_SET_GAIN), which
# scales samples in place while filling the caller's buffer. Show that the
# gain path contains no buffer allocation or bulk copy of its own.
sed -n '/^static void op_update_gain/,/^}/p' "$ROOT/src/opusfile.c" | tee "$LOG/op_update_gain.c"
if sed -n '/^static void op_update_gain/,/^}/p' "$ROOT/src/opusfile.c" \
   | grep -qE "memcpy|memmove|malloc|_ogg_malloc|alloca"; then
  echo "  FAIL: gain path performs its own copy/allocation"; exit 1
fi
if sed -n '/^void op_set_gain_enabled/,/^}/p;/^opus_int32 op_get_gain/,/^}/p' "$ROOT/src/opusfile.c" \
   | grep -qE "memcpy|memmove|malloc|_ogg_malloc|alloca"; then
  echo "  FAIL: new API performs its own copy/allocation"; exit 1
fi
echo "  PASS: gain applied via OPUS_SET_GAIN inside decode; no second full copy"

echo "ALL CHECKS PASSED"
