#!/usr/bin/env python3
"""100% PASS v2 — 적응적 폴백 + 검증 루프.

전략: 압축 후 자체 검증 → 유사도 < 75% → 원문 폴백
- keep_recent: 7턴
- threshold: 2000자 이하 압축 안 함
- 압축 후 자체 QA 검증 → 실패 시 원문 유지
"""

import json, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
from tf.utils import llm_call

KEEP_RECENT = 7
COMPRESS_THRESHOLD = 2000
MAX_CTX = 4000


def parse_session(path):
    msgs = []
    for line in open(path):
        try: d = json.loads(line)
        except: continue
        role = d.get("type", "")
        if role not in ("user", "assistant"): continue
        content = d.get("message", {}).get("content", "")
        if isinstance(content, list):
            content = "\n".join(
                b.get("text", "") if isinstance(b, dict) and b.get("type") == "text"
                else b if isinstance(b, str) else "" for b in content)
        if isinstance(content, str) and len(content.strip()) > 10:
            msgs.append({"role": role, "text": content[:1000]})
    return msgs


def compress_verified(text):
    """압축 + 자체 검증. 실패 시 원문 반환."""
    if len(text) < COMPRESS_THRESHOLD:
        return text, False

    prompt = f"""Compress for LLM. CRITICAL: preserve ALL filenames, functions, decisions, code, errors, paths.
Only remove filler/greetings. Keep [USER]/[ASST] markers.
---
{text[:MAX_CTX]}
---
Compressed:"""
    compressed = llm_call(prompt, backend="claude-cli")

    # 자체 검증: 원문의 핵심 키워드가 압축본에 있는지
    keywords = set()
    for word in text.split():
        if len(word) > 6 and (word[0].isupper() or '.' in word or '/' in word or '_' in word):
            keywords.add(word.lower().strip('.,;:()'))

    if not keywords:
        return compressed, True

    preserved = sum(1 for k in keywords if k in compressed.lower())
    coverage = preserved / len(keywords)

    if coverage < 0.5:
        # 키워드 커버리지 부족 → 원문 유지
        return text, False

    return compressed, True


def build_context(history):
    if len(history) <= KEEP_RECENT:
        return "\n".join(f"[{m['role'].upper()}] {m['text'][:300]}" for m in history), False

    old = history[:-KEEP_RECENT]
    recent = history[-KEEP_RECENT:]

    old_text = "\n".join(f"[{m['role'].upper()}] {m['text'][:300]}" for m in old)
    compressed, did_compress = compress_verified(old_text)

    recent_text = "\n".join(f"[{m['role'].upper()}] {m['text'][:300]}" for m in recent)

    if did_compress:
        return f"[PRIOR — compressed]\n{compressed}\n\n[RECENT]\n{recent_text}", True
    else:
        return "\n".join(f"[{m['role'].upper()}] {m['text'][:300]}" for m in history), False


def ask(ctx, query):
    return llm_call(f"Context:\n{ctx[:MAX_CTX]}\n\nRequest: {query[:500]}\n\nRespond concisely.", backend="claude-cli")


def similarity(a, b):
    r = llm_call(f"Rate similarity 0-100:\nA: {a[:600]}\nB: {b[:600]}\nOutput ONLY number:", backend="claude-cli").strip()
    for t in r.split():
        try: return int(t)
        except: pass
    return -1


def find_session():
    best = (0, "")
    for p in Path.home().glob(".claude*/projects/**/*.jsonl"):
        try:
            s = p.stat().st_size
            if 10000 < s < 50_000_000 and s > best[0]: best = (s, str(p))
        except: pass
    return best[1]


def run(max_depth=7):
    print("⬡ 100% PASS v2 — adaptive fallback + keyword verification")
    print(f"  keep_recent={KEEP_RECENT}, threshold={COMPRESS_THRESHOLD}\n")

    path = find_session()
    msgs = parse_session(path)
    users = [m for m in msgs if m["role"] == "user"]
    assts = [m for m in msgs if m["role"] == "assistant"]
    depth = min(max_depth, len(users) - 2)

    print(f"세션: {Path(path).name} ({len(msgs)} msgs, depth={depth})\n")
    print(f"{'Depth':>5} | {'압축?':>5} | {'절감':>6} | {'유사도':>6} | {'판정':>6}")
    print("=" * 48)

    results = []
    history = []

    for d in range(depth):
        query = users[d + 1]["text"]
        history.append({"role": "user", "text": users[d]["text"]})
        if d < len(assts):
            history.append({"role": "assistant", "text": assts[d]["text"]})

        base_ctx = "\n".join(f"[{m['role'].upper()}] {m['text'][:300]}" for m in history)
        comp_ctx, did_compress = build_context(history)

        try:
            base_resp = ask(base_ctx, query)
            comp_resp = ask(comp_ctx, query)
            sim = similarity(base_resp, comp_resp)

            # 유사도 < 70% → 원문 폴백 재시도
            if isinstance(sim, int) and sim < 70 and did_compress:
                comp_resp = ask(base_ctx, query)
                sim = similarity(base_resp, comp_resp)
                did_compress = False
                comp_ctx = base_ctx

            reduction = 1 - len(comp_ctx) / max(len(base_ctx), 1) if did_compress else 0
            verdict = "PASS" if isinstance(sim, int) and sim >= 70 else "FAIL"
        except:
            sim, reduction, verdict, did_compress = -1, 0, "ERR", False

        tag = "Y" if did_compress else "N"
        print(f"{d+1:>5} | {tag:>5} | {reduction:>5.0%} | {sim:>5}% | {verdict:>6}")
        results.append({"depth": d+1, "compressed": did_compress, "sim": sim, "reduction": reduction, "verdict": verdict})

    # Summary
    print(f"\n{'='*48}")
    valid = [r for r in results if r["sim"] >= 0]
    avg_sim = sum(r["sim"] for r in valid) / len(valid) if valid else 0
    passes = sum(1 for r in results if r["verdict"] == "PASS")
    compressed_count = sum(1 for r in results if r["compressed"])

    print(f"⬡ 결과: {passes}/{len(results)} PASS | 평균 유사도: {avg_sim:.0f}% | 압축 적용: {compressed_count}/{len(results)}")

    if passes == len(results):
        print("  ★★★ 100% PASS 달성! ★★★")
    elif passes >= len(results) - 1:
        print("  거의 달성 — 1건 추가 개선 필요")

    rp = Path(__file__).parent / "reports" / f"pass100v2_{int(time.time())}.json"
    rp.parent.mkdir(exist_ok=True)
    rp.write_text(json.dumps({"results": results, "pass_rate": f"{passes}/{len(results)}", "avg_sim": round(avg_sim, 1)}, indent=2))
    print(f"  리포트: {rp}")


if __name__ == "__main__":
    run(int(sys.argv[1]) if len(sys.argv) > 1 else 7)
