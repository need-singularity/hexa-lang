# `anima_audio_worker`

_persistent python audio worker (raw 9 hexa-only)_

**Source:** [`stdlib/anima_audio_worker.hexa`](../../stdlib/anima_audio_worker.hexa)  

## Overview

raw 9 final closure (cycle 30 wave 4 follow-up, 2026-04-30): the on-disk
anima_audio_worker.py was the LAST .py surface in /Users/ghost/core/hexa-lang.
Per the Group A canonical embed pattern (cf. cycle 30 wave 4
btAI1_phi_over_n_sparsity, flatten_imports_py): the python source is now a
string literal inside this .hexa file, materialized to /tmp/_hexa_anima_audio_worker.py
on first call, and exec()d via pipe_spawn() from anima_audio.hexa.

raw 91 C3 honest disclosure:
  The worker is fundamentally a python execution sandbox: it exec()s
  arbitrary numpy + scipy + wave compute kernels received as base64 NDJSON
  over stdin from anima_audio.hexa. Porting numpy / scipy.signal (lfilter,
  butter, sosfilt, fftconvolve, resample_poly) + the wave PCM codec to pure
  hexa is out of scope for raw 9 closure (no equivalents in the hexa
  runtime). The hexa layer owns: source-of-truth ownership, materialization
  lifecycle, and import-graph cleanliness. The python3 backend (embedded
  below) owns the irreducible audio-DSP compute step.

raw 18 self-host fixpoint: byte-for-byte equivalent to the retired .py.
Original anima_audio_worker.py SHA-256 (200 LoC, 6345 bytes):
  b22855c1154b9d6aa6f7aff02f656faba5bc597019c811302e66db563eda8f34
Round-trip verified 2026-04-30 via escape + unescape walk; see commit message.
Any future edits must be made HERE in this .hexa, NOT in the materialized
/tmp/_hexa_anima_audio_worker.py shim (which is regenerated each hexa run).

Lifecycle (unchanged from the retired .py):
  - hexa caller (anima_audio.hexa) calls ensure_anima_audio_worker_script()
    which writes /tmp/_hexa_anima_audio_worker.py exactly once per process,
    then pipe_spawn("python3 /tmp/_hexa_anima_audio_worker.py")
  - worker reads NDJSON requests on stdin, writes NDJSON responses on stdout
  - on idle > IDLE_TIMEOUT_S (300s), exits cleanly
  - parent watchdog: polls kill(parent_pid,0) every <=5s; exits if parent dies

@no-sandbox  (writes /tmp/_hexa_anima_audio_worker.py and is exec()d as python3)

_No public functions detected._

---

← [Back to stdlib index](README.md)
