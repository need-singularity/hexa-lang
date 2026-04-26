#!/usr/bin/env python3
# anima_audio_worker.py — persistent python worker for anima_audio.hexa
#
# Design ref: /Users/ghost/core/anima/state/omega_audio_2_persistent_worker_design.json
#
# Lifecycle:
#   - spawned lazily by hexa _run_py() on first call when ANIMA_AUDIO_WORKER=1
#   - reads NDJSON requests on stdin, writes NDJSON responses on stdout
#   - on idle > IDLE_TIMEOUT_S, exits cleanly
#   - inherits process group from hexa parent => SIGHUP cascades on parent exit
#
# Protocol (one line per message, NDJSON):
#   request : {"id": <int>, "kind": "exec"|"ping"|"shutdown",
#              "code_b64": <str>, "expect_path": <str>, "timeout_ms": <int>}
#   response: {"id": <int>, "ok": <bool>, "elapsed_ms": <float>,
#              "bytes_out": <int>, "err": <str>}
#
# State contamination mitigation (per design R3):
#   - each exec runs in a fresh local namespace
#   - np.seterr restored in finally
#   - rng auto-reseeded per request (each code blob calls np.random.default_rng() itself today)

import sys
import os
import json
import time
import base64
import signal
import select
import traceback

# ── pre-import heavy modules (the whole point of the worker) ─────
import wave
import numpy as np
from scipy.signal import lfilter, lfiltic, butter, sosfilt, fftconvolve, resample_poly

# ── config ──────────────────────────────────────────────────────
IDLE_TIMEOUT_S = 300.0
VERSION = 1
# Hexa parent pid is passed via env (set by helper script). Falls back to
# getppid() for ad-hoc test runs. We watch this pid: when it dies, the
# worker exits. We do NOT use os.getppid() in the loop because the helper
# shell that spawns us exits immediately, reparenting us to launchd.
PARENT_PID = int(os.environ.get("ANIMA_AUDIO_PARENT_PID", str(os.getppid())))

# carry-over globals available inside exec() namespaces
_BASE_GLOBALS = {
    "np": np,
    "numpy": np,
    "wave": wave,
    "lfilter": lfilter,
    "lfiltic": lfiltic,
    "butter": butter,
    "sosfilt": sosfilt,
    "fftconvolve": fftconvolve,
    "resample_poly": resample_poly,
    "json": json,
    "os": os,
    "time": time,
}


def _emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def _file_size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


def _parent_alive():
    # Watch the hexa pid we were told about. kill -0 returns 0 iff alive.
    try:
        os.kill(PARENT_PID, 0)
        return True
    except OSError:
        return False


def _handle_exec(req):
    code_b64 = req.get("code_b64", "")
    expect_path = req.get("expect_path", "")
    try:
        code = base64.b64decode(code_b64).decode("utf-8")
    except Exception as e:
        return {
            "id": req.get("id"),
            "ok": False,
            "elapsed_ms": 0.0,
            "bytes_out": 0,
            "err": "b64_decode: " + str(e)[:480],
        }

    # fresh namespace per request (R3 contamination mitigation)
    ns = dict(_BASE_GLOBALS)
    ns["__builtins__"] = __builtins__

    saved_errstate = np.geterr()
    t0 = time.perf_counter()
    err = ""
    ok = True
    try:
        exec(compile(code, "<worker>", "exec"), ns, ns)
    except Exception:
        ok = False
        err = traceback.format_exc()[-512:]
    finally:
        np.seterr(**saved_errstate)
    elapsed_ms = (time.perf_counter() - t0) * 1000.0

    bytes_out = _file_size(expect_path) if expect_path else 0
    return {
        "id": req.get("id"),
        "ok": ok,
        "elapsed_ms": round(elapsed_ms, 3),
        "bytes_out": bytes_out,
        "err": err,
    }


def _handle_ping(req):
    return {
        "id": req.get("id"),
        "ok": True,
        "elapsed_ms": 0.0,
        "bytes_out": 0,
        "err": "",
    }


def main():
    # signal handlers — clean exit on SIGTERM/SIGHUP
    def _sigterm(signum, frame):
        sys.exit(0)
    signal.signal(signal.SIGTERM, _sigterm)
    signal.signal(signal.SIGHUP, _sigterm)
    signal.signal(signal.SIGINT, _sigterm)

    _emit({"event": "ready", "pid": os.getpid(), "ppid": PARENT_PID, "version": VERSION})

    last_activity = time.time()
    debug = os.environ.get("ANIMA_AUDIO_WORKER_DEBUG") == "1"
    def _log(msg):
        if debug:
            sys.stderr.write("[worker] " + msg + "\n")
            sys.stderr.flush()
    while True:
        # poll stdin with idle timeout
        remaining = IDLE_TIMEOUT_S - (time.time() - last_activity)
        if remaining <= 0:
            _log("idle_timeout exit")
            break
        timeout = min(remaining, 5.0)  # also wake every 5s to check parent
        rl, _, _ = select.select([sys.stdin], [], [], timeout)
        if not _parent_alive():
            _log("parent_died exit")
            break
        if not rl:
            continue
        line = sys.stdin.readline()
        if not line:
            _log("readline_empty exit (EOF on stdin)")
            break
        line = line.strip()
        if not line:
            _log("empty_line skip")
            continue
        _log("got_request: " + line[:120])
        last_activity = time.time()
        try:
            req = json.loads(line)
        except Exception as e:
            _emit({"id": None, "ok": False, "elapsed_ms": 0.0, "bytes_out": 0,
                   "err": "json_parse: " + str(e)[:480]})
            continue
        kind = req.get("kind", "exec")
        if kind == "shutdown":
            _emit({"id": req.get("id"), "ok": True, "elapsed_ms": 0.0,
                   "bytes_out": 0, "err": ""})
            break
        elif kind == "ping":
            _emit(_handle_ping(req))
        else:
            _emit(_handle_exec(req))


if __name__ == "__main__":
    main()
