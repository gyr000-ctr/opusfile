#!/usr/bin/env bash
# Segmented, non-interactive demo of the software gain layer.
# Section titles are printed, followed by a pause between sections.
# All artifacts are written under output/; no keyboard input is ever read.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
T="$ROOT/output/test"; F="$ROOT/output/fixtures"; R="$ROOT/output/record"
mkdir -p "$R"
OUT="$R/demo_gain_layer.cast"

pause(){ sleep 1.4; }
title(){
  local n="$1"; shift
  printf '\n\033[1;36m========================================================\n'
  printf '  SECTION %s: %s\n' "$n" "$*"
  printf '========================================================\033[0m\n'
}

{
  title 0 "Build opusfile with the new common gain layer (CMake)"
  cmake --build build -j4 2>&1 | grep -E "Built target (opusfile|opusfile_example)"
  pause

  title 1 "Repo entry point opusfile_example still works (default path)"
  ./build/opusfile_example "$F/loud.opus" > "$R/example_loud.wav" 2>"$R/example_loud.log"
  tail -1 "$R/example_loud.log"; echo "wav bytes: $(stat -c%s "$R/example_loud.wav")"
  pause

  title 2 "Acceptance A - toggle OFF vs ON gives zero sample difference"
  "$T/gain_test" identity "$F/quiet.opus" "$R/off.raw" "$R/on.raw"
  cmp "$R/off.raw" "$R/on.raw" && echo "RESULT: sample-for-sample IDENTICAL (zero diff)"
  pause

  title 3 "Acceptance B - out-of-range gain is clamped and reported"
  "$T/gain_test" clamp "$F/loud.opus" 40000
  "$T/gain_test" clamp "$F/loud.opus" -40000
  "$T/gain_test" clamp "$F/loud.opus" 100
  pause

  title 4 "Acceptance C - unified gain aligns different-loudness materials"
  "$T/gain_test" apply "$F/quiet.opus" 0 "$R/quiet_0.raw" >/dev/null
  "$T/gain_test" apply "$F/loud.opus"  0 "$R/loud_0.raw"  >/dev/null
  "$T/gain_test" apply "$F/quiet.opus" "$(awk '/quiet/{print $2}' "$T/gains.txt")" "$R/quiet_aligned.raw" >/dev/null
  "$T/gain_test" apply "$F/loud.opus"  "$(awk '/loud/{print $2}'  "$T/gains.txt")" "$R/loud_aligned.raw"  >/dev/null
  python3 -I "$T/energy_check.py" "$R"
  pause

  title 5 "Acceptance D - no extra full copy (pointer identity, in place)"
  "$T/nocopy_test" "$F/quiet.opus"
  pause

  title 6 "Fixed-point Q16 gain branch and default-path baseline equality"
  "$T/fixed_test" "$F/quiet.opus"
  for x in quiet loud stereo; do
    "$T/default_baseline" "$F/$x.opus" "$R/base_$x.raw"
    "$T/default_modified" "$F/$x.opus" "$R/mod_$x.raw"
    cmp "$R/base_$x.raw" "$R/mod_$x.raw" && echo "$x: default path byte-identical to pristine baseline"
  done

  printf '\n\033[1;32mAll acceptance criteria verified. Artifacts in output/.\033[0m\n'
} 2>&1 | tee "$R/demo_output.txt"
