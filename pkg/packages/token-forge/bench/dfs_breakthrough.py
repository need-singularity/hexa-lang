#!/usr/bin/env python3
"""DFS Breakthrough — 4-strategy compression benchmark (rebuilt 2026-04-10).

Rebuilt from cache report ``reports/breakthrough_1775473383.json`` after the
original ``dfs_breakthrough.py`` was lost. The best performer on the cached
run was **A_hybrid** (평균 유사도 70%, PASS 3/5) — keep the last N turns
verbatim while summarising the older prefix.

Strategies (match the JSON keys of the cached report):

    A_hybrid        : last 3 turns raw + older turns compressed
    B_code_protect  : aggressive whole-history compression, code blocks fenced
    C_twostage      : summarise → then compress the summary
    D_combined      : A_hybrid over B_code_protect output (two-level)

This script follows the same overall shape as ``dfs_100pass_v2.py``
(session discovery via ``Path.home().glob('.claude*/projects/**/*.jsonl')``,
``tf.utils.llm_call``, depth-based DFS).

tf/ 모듈 부재 상태라 실제 LLM 호출은 import-guard 로 막고, ``--dry-run`` 에서는
strategy 정의/시그니처만 출력한다. tf 패키지가 복구되면 ``run()`` 경로가 그대로
동작하도록 인터페이스를 유지했다.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Callable

# ---------------------------------------------------------------------------
# tf.utils.llm_call — optional import (matches dfs_100pass_v2.py pattern)
# ---------------------------------------------------------------------------

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

_TF_IMPORT_ERROR = ""
try:
    from tf.utils import llm_call  # type: ignore
    _TF_AVAILABLE = True
except Exception as _e:  # pragma: no cover - tf may be absent
    _TF_AVAILABLE = False
    _TF_IMPORT_ERROR = repr(_e)

    def llm_call(prompt: str, backend: str = "claude-cli") -> str:  # type: ignore
        raise RuntimeError(
            f"tf.utils.llm_call unavailable ({_TF_IMPORT_ERROR}); "
            "use --dry-run or restore the tf package"
        )


# ---------------------------------------------------------------------------
# Tunables (mirrors dfs_100pass_v2.py defaults so results stay comparable)
# ---------------------------------------------------------------------------

KEEP_RECENT = 3            # A_hybrid: 최근 3턴 보존
COMPRESS_THRESHOLD = 2000  # 이하는 압축 생략
MAX_CTX = 4000
MAX_TURN_CHARS = 300


Message = dict  # {"role": "user"|"assistant", "text": str}


# ---------------------------------------------------------------------------
# Session loading (same pattern as dfs_100pass_v2.parse_session/find_session)
# ---------------------------------------------------------------------------

def parse_session(path: str) -> list[Message]:
    msgs: list[Message] = []
    for line in open(path):
        try:
            d = json.loads(line)
        except Exception:
            continue
        role = d.get("type", "")
        if role not in ("user", "assistant"):
            continue
        content = d.get("message", {}).get("content", "")
        if isinstance(content, list):
            content = "\n".join(
                b.get("text", "") if isinstance(b, dict) and b.get("type") == "text"
                else b if isinstance(b, str) else ""
                for b in content
            )
        if isinstance(content, str) and len(content.strip()) > 10:
            msgs.append({"role": role, "text": content[:1000]})
    return msgs


def find_session() -> str:
    best = (0, "")
    for p in Path.home().glob(".claude*/projects/**/*.jsonl"):
        try:
            s = p.stat().st_size
            if 10000 < s < 50_000_000 and s > best[0]:
                best = (s, str(p))
        except Exception:
            pass
    return best[1]


def _render(history: list[Message]) -> str:
    return "\n".join(
        f"[{m['role'].upper()}] {m['text'][:MAX_TURN_CHARS]}" for m in history
    )


# ---------------------------------------------------------------------------
# Compression primitives
# ---------------------------------------------------------------------------

def _compress_block(text: str) -> str:
    """Single LLM compression pass, preserves decisions/filenames/code tokens."""
    if len(text) < COMPRESS_THRESHOLD:
        return text
    prompt = (
        "Compress for LLM. CRITICAL: preserve ALL filenames, functions, "
        "decisions, code, errors, paths. Only remove filler/greetings. "
        "Keep [USER]/[ASST] markers.\n---\n"
        f"{text[:MAX_CTX]}\n---\nCompressed:"
    )
    return llm_call(prompt, backend="claude-cli")


def _fence_code(text: str) -> str:
    """Mark code-like lines so the compressor leaves them alone (B strategy)."""
    out: list[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if (
            stripped.startswith(("def ", "class ", "import ", "from "))
            or stripped.endswith((":", "{", "}"))
            or "```" in stripped
        ):
            out.append(f"<CODE>{line}</CODE>")
        else:
            out.append(line)
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Strategies — each takes list[Message], returns list[Message]
# ---------------------------------------------------------------------------

def strategy_A_hybrid(history: list[Message]) -> list[Message]:
    """최근 3턴 원문 + 과거 단일 압축 블록.

    Cached result: 평균 유사도 70%, PASS 3/5 — best overall.
    """
    if len(history) <= KEEP_RECENT:
        return list(history)
    old, recent = history[:-KEEP_RECENT], history[-KEEP_RECENT:]
    compressed = _compress_block(_render(old))
    return [{"role": "system", "text": f"[PRIOR — compressed]\n{compressed}"}, *recent]


def strategy_B_code_protect(history: list[Message]) -> list[Message]:
    """코드 블록 보존한 채 전체 이력을 공격적으로 축약.

    Cached result: 평균 유사도 ~37%, PASS 0/5 — 가장 공격적, 손실 큼.
    """
    fenced = _fence_code(_render(history))
    compressed = _compress_block(fenced)
    return [{"role": "system", "text": f"[COMPRESSED+CODE]\n{compressed}"}]


def strategy_C_twostage(history: list[Message]) -> list[Message]:
    """2단계: 1) 요약 → 2) 요약을 다시 압축."""
    raw = _render(history)
    summary_prompt = (
        "Summarise this conversation into bullet decisions (max 30 lines).\n"
        f"---\n{raw[:MAX_CTX]}\n---"
    )
    summary = llm_call(summary_prompt, backend="claude-cli")
    compressed = _compress_block(summary)
    return [{"role": "system", "text": f"[TWO-STAGE]\n{compressed}"}]


def strategy_D_combined(history: list[Message]) -> list[Message]:
    """A_hybrid 골격 + 과거 블록에 B_code_protect 적용."""
    if len(history) <= KEEP_RECENT:
        return list(history)
    old, recent = history[:-KEEP_RECENT], history[-KEEP_RECENT:]
    fenced = _fence_code(_render(old))
    compressed = _compress_block(fenced)
    return [{"role": "system", "text": f"[PRIOR — code-protect]\n{compressed}"}, *recent]


STRATEGIES: dict[str, Callable[[list[Message]], list[Message]]] = {
    "A_hybrid":       strategy_A_hybrid,
    "B_code_protect": strategy_B_code_protect,
    "C_twostage":     strategy_C_twostage,
    "D_combined":     strategy_D_combined,
}


# ---------------------------------------------------------------------------
# Evaluation helpers (mirror dfs_100pass_v2.ask/similarity)
# ---------------------------------------------------------------------------

def ask(ctx: str, query: str) -> str:
    return llm_call(
        f"Context:\n{ctx[:MAX_CTX]}\n\nRequest: {query[:500]}\n\nRespond concisely.",
        backend="claude-cli",
    )


def similarity(a: str, b: str) -> int:
    r = llm_call(
        f"Rate similarity 0-100:\nA: {a[:600]}\nB: {b[:600]}\nOutput ONLY number:",
        backend="claude-cli",
    ).strip()
    for t in r.split():
        try:
            return int(t)
        except ValueError:
            pass
    return -1


def token_overlap(a: str, b: str) -> float:
    """Cheap fallback similarity (0-1) when no LLM judge is available."""
    ta, tb = set(a.lower().split()), set(b.lower().split())
    if not ta or not tb:
        return 0.0
    return len(ta & tb) / len(ta | tb)


# ---------------------------------------------------------------------------
# Benchmark driver
# ---------------------------------------------------------------------------

def run_strategy(name: str, fn: Callable, msgs: list[Message], depth: int) -> list[dict]:
    users = [m for m in msgs if m["role"] == "user"]
    assts = [m for m in msgs if m["role"] == "assistant"]
    results: list[dict] = []
    history: list[Message] = []

    for d in range(depth):
        query = users[d + 1]["text"] if d + 1 < len(users) else users[-1]["text"]
        history.append({"role": "user", "text": users[d]["text"]})
        if d < len(assts):
            history.append({"role": "assistant", "text": assts[d]["text"]})

        base_ctx = _render(history)
        try:
            comp_msgs = fn(history)
            comp_ctx = _render(comp_msgs)
            reduction = 1 - len(comp_ctx) / max(len(base_ctx), 1)

            base_resp = ask(base_ctx, query)
            comp_resp = ask(comp_ctx, query)
            sim = similarity(base_resp, comp_resp)
            verdict = "PASS" if isinstance(sim, int) and sim >= 70 else "FAIL"
        except Exception as e:
            sim, reduction, verdict = -1, 0, f"ERR:{type(e).__name__}"

        results.append({
            "depth": d + 1,
            "sim": sim,
            "reduction": reduction,
            "verdict": verdict,
        })
        print(f"  [{name}] depth={d+1} sim={sim} red={reduction:+.2f} {verdict}")

    return results


def run(depth: int = 5) -> None:
    if not _TF_AVAILABLE:
        print(f"tf.utils.llm_call unavailable — aborting run. ({_TF_IMPORT_ERROR})")
        print("Use --dry-run to inspect strategy definitions without calling the LLM.")
        sys.exit(2)

    path = find_session()
    msgs = parse_session(path)
    print(f"session: {Path(path).name} ({len(msgs)} msgs, depth={depth})\n")

    report: dict = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "session": path,
        "depth": depth,
        "results": {},
    }
    for name, fn in STRATEGIES.items():
        print(f"\n>>> {name}")
        report["results"][name] = run_strategy(name, fn, msgs, depth)

    rp = Path(__file__).resolve().parent.parent / "reports" / f"dfs_breakthrough_{int(time.time())}.json"
    rp.parent.mkdir(exist_ok=True)
    rp.write_text(json.dumps(report, indent=2, ensure_ascii=False))
    print(f"\nreport: {rp}")


# ---------------------------------------------------------------------------
# Dry-run / CLI
# ---------------------------------------------------------------------------

def dry_run() -> None:
    print("DFS Breakthrough — strategy catalog (dry-run)")
    print(f"tf.utils available : {_TF_AVAILABLE}")
    print(f"keep_recent        : {KEEP_RECENT}")
    print(f"compress_threshold : {COMPRESS_THRESHOLD}")
    print()
    for name, fn in STRATEGIES.items():
        doc = (fn.__doc__ or "").strip().splitlines()[0] if fn.__doc__ else ""
        print(f"  {name:<16} -> {fn.__name__}  // {doc}")
    print()
    print("Cached baseline (reports/breakthrough_1775473383.json):")
    print("  A_hybrid       : avg sim 70%, PASS 3/5  <- best")
    print("  B_code_protect : avg sim 37%, PASS 0/5")
    print("  C_twostage     : avg sim 65%, PASS 3/5")
    print("  D_combined     : avg sim 64%, PASS 3/5")


def main() -> None:
    ap = argparse.ArgumentParser(description="DFS Breakthrough 4-strategy benchmark")
    ap.add_argument("--dry-run", action="store_true", help="print strategy catalog only")
    ap.add_argument("--depth", type=int, default=5, help="DFS depth (default 5)")
    args = ap.parse_args()
    if args.dry_run:
        dry_run()
        return
    run(args.depth)


if __name__ == "__main__":
    main()
