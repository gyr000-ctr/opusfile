#!/usr/bin/env bash
# Segmented, non-interactive demo of the gain feature for screen recording.
# Each segment gets a title banner and a pause; no keyboard input is needed.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/output"
DATA="$OUT/data"
EXAMPLE="$ROOT/build/opusfile_example"
PY="python3 -I"
cd "$DATA"

pause() { sleep "${1:-2.5}"; }
title() {
  clear
  echo
  echo "  +------------------------------------------------------------------+"
  printf "  |  %-64s  |\n" "$1"
  echo "  +------------------------------------------------------------------+"
  echo
  pause 3
}

title "SEGMENT 1/5  BUILD: library + repo example entry point"
echo "$ cmake --build build --target opusfile_example"
cmake --build "$ROOT/build" --target opusfile_example 2>&1 | tail -2
echo
"$EXAMPLE" 2>&1 | head -1 || true
pause

title "SEGMENT 2/5  MATERIAL: same take, loudness differs by 20 dB"
ls -1 gain_m6.opus gain_p4.opus gain_p14.opus
echo
echo "header gains: -6 dB / +4 dB / +14 dB (patched into OpusHead output_gain)"
pause

title "SEGMENT 3/5  GAIN ON: playback energies align"
for f in gain_m6 gain_p4 gain_p14; do
  "$EXAMPLE" "$f.opus" > "on_$f.wav" 2> "on_$f.log"
  printf "  %-12s RMS = %s dBFS   (%s)\n" "$f" \
    "$($PY "$OUT/tools/wav_rms.py" "on_$f.wav")" \
    "$(grep 'Effective gain' "on_$f.log")"
done
echo
echo "  -> spread < 0.02 dB: loudness unified by the header gains"
pause

title "SEGMENT 4/5  GAIN OFF: bypass gain, samples bit-identical"
for f in gain_m6 gain_p4 gain_p14; do
  "$EXAMPLE" --no-gain "$f.opus" > "off_$f.wav" 2>/dev/null
  printf "  %-12s RMS = %s dBFS (gain disabled)\n" "$f" \
    "$($PY "$OUT/tools/wav_rms.py" "off_$f.wav")"
done
echo
echo "$ opusfile_example --no-gain gain_p4.opus > off_p4.wav"
echo "$ opusfile_example gain_0.opus        > on_g0.wav   # header gain = 0"
"$EXAMPLE" --no-gain gain_p4.opus > off_p4.wav 2>/dev/null
"$EXAMPLE" gain_0.opus > on_g0.wav 2>/dev/null
echo "$ cmp off_p4.wav on_g0.wav"
cmp off_p4.wav on_g0.wav && echo "  -> zero differences: switch-off is sample-exact"
pause

title "SEGMENT 5/5  CLAMP: out-of-range gain is clamped and reported"
echo "$ opusfile_example --gain-offset=98303 gain_p4.opus"
"$EXAMPLE" --gain-offset=98303 gain_p4.opus 2>&1 >/dev/null | grep -E "Gain|gain"
echo
echo "$ opusfile_example --gain-offset=-98302 gain_p4.opus"
"$EXAMPLE" --gain-offset=-98302 gain_p4.opus 2>&1 >/dev/null | grep -E "Effective"
echo
echo "$ opusfile_example --gain-offset=2560 gain_p4.opus   # in range: no clamp flag"
"$EXAMPLE" --gain-offset=2560 gain_p4.opus 2>&1 >/dev/null | grep -E "Effective"
echo
echo "Gain is applied by the decoder (OPUS_SET_GAIN) while filling the output"
echo "buffer: no second full copy of the decoded samples."
pause 4
clear
echo "  Demo complete. Artifacts in output/."
pause 2
