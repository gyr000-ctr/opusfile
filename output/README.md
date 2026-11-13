# Software gain layer — build, verification, and recording

All paths are relative to the repository root. Nothing here reads the keyboard.

## What changed (small, multi-site edits behind one common layer)

- `src/gain.h`, `src/gain.c` — new common gain layer:
  - `op_gain_clamp()` — clamps a Q8 dB gain to [-32768, 32767] and records the
    clamp (count + requested/applied values).
  - `op_gain_to_linear()` — single dB->linear conversion shared by the float and
    fixed-point paths.
  - `op_gain_apply()` — scales `op_sample` buffers **in place**; contains no
    allocation and no copy.
- `src/internal.h` — gain layer state fields on `OggOpusFile`
  (`gain_enabled`, `gain_applied_q8`, `gain_clamp_count`,
  `gain_last_requested_q8`, `gain_last_applied_q8`).
- `src/opusfile.c` —
  - `op_update_gain()` routes the gain to the decoder (layer off, legacy,
    bit-identical) or zeroes the decoder gain and applies it in software (on),
    via the common clamp routine.
  - `op_decode()` is the single funnel where every read path applies the gain
    in place on the decoder's own output buffer (`_nsamples`, so decode
    callbacks returning 0 on success are handled).
  - new public API: `op_set_gain_enabled()`, `op_gain_clamp_count()`,
    `op_gain_clamp_last()`.
- `include/opusfile.h` — declarations and docs for the new API.
- `CMakeLists.txt`, `Makefile.am` — add `src/gain.c`/`src/gain.h`.

## Build

    cmake -S . -B build -DOP_DISABLE_HTTP=ON -DOP_DISABLE_DOCS=ON
    cmake --build build -j4
    # fixed-point variant:
    cmake -S . -B build-fixed -DOP_DISABLE_HTTP=ON -DOP_DISABLE_DOCS=ON \
      -DOP_DISABLE_EXAMPLES=ON -DOP_FIXED_POINT=ON
    cmake --build build-fixed -j4

## Verification (repo entry points + dedicated harnesses)

- `output/test/gain_test.c` — identity / clamp / apply modes.
- `output/test/nocopy_test.c` — runtime pointer-identity + in-place proof.
- `output/test/smallbuf_test.c` — forces the internal `od_buffer` path.
- `output/test/fixed_test.c` — fixed-point Q16 branch.
- `output/test/default_test.c` — built against both the pristine baseline lib
  and the modified lib to prove the default path is unchanged.
- `output/test/analyze.py`, `energy_check.py` — energy math (`python3 -I`).

Results:
- Toggle off vs on at 0 dB: **zero sample difference**.
- Out-of-range gain: clamped to +/-32768/32767 and **reported**; in-range does
  not clamp.
- Energy alignment: native loudness gap 25.099 dB -> 0.001 dB at a common RMS.
- No extra copy: caller buffer is the same allocation the decoder filled.
- Default path: byte-identical output to the unmodified baseline.
- Fixed-point Q16: +6 dB energy ratio 3.98096 vs theory 3.98107.

## Recording

`output/record/demo_gain_layer.cast` is an asciinema v2 cast with titled
sections and ~1.4 s pauses between them. Replay:

    asciinema play output/record/demo_gain_layer.cast

Re-generate non-interactively:

    asciinema rec --overwrite -c "bash output/record/run_demo.sh" \
      output/record/demo_gain_layer.cast

`output/record/demo_output.txt` holds the same transcript as plain text.

## Untrusted inputs

Generated fixtures live only under `output/fixtures/`; test scripts live under
`output/test/` and take paths as arguments; Python is run with `-I`.
