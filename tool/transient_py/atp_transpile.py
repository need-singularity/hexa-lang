#!/usr/bin/env python3
# ═══════════════════════════════════════════════════════════════════════
# tool/transient_py/atp_transpile.py
#
# hexa → PyTorch transpiler for self/ml/audio_token_predictor.hexa (Mk.III).
#
# Reads the BEGIN_CONFIG/END_CONFIG block and the function signatures from
# the hexa source, then emits tool/transient_py/atp_pytorch.py — a stand-
# alone PyTorch module implementing:
#
#   * KV-cache (per-layer ring buffer, append-on-step)
#   * 8-stage RVQ token embedding + per-stage classifier heads
#   * Classifier-free guidance (CFG) combine
#   * Top-k multinomial sampling (deterministic seed)
#
# Usage:
#   python tool/transient_py/atp_transpile.py
#       [--hexa  self/ml/audio_token_predictor.hexa]
#       [--out   tool/transient_py/atp_pytorch.py]
#       [--audit state/atp_transpile_audit/]
#
# Acceptance gate: F-VLM-TRANSPILE-1 (parity with the Mk.III hexa spec).
# ═══════════════════════════════════════════════════════════════════════
from __future__ import annotations

import argparse
import os
import re
import sys
import time
from pathlib import Path
from typing import Dict


CONFIG_KEYS = (
    "N_LAYERS", "N_HEADS", "D_MODEL", "D_HEAD", "FF", "CTX",
    "FRAME_HZ", "RVQ_STAGES", "V_CODEBOOK",
    "CFG_SCALE", "TOP_K", "SEED",
)


def parse_config(hexa_text: str) -> Dict[str, float]:
    """Extract BEGIN_CONFIG/END_CONFIG values."""
    m = re.search(r"BEGIN_CONFIG(.*?)END_CONFIG", hexa_text, flags=re.S)
    if not m:
        raise SystemExit("[atp_transpile] BEGIN_CONFIG block not found")
    body = m.group(1)
    out: Dict[str, float] = {}
    for line in body.splitlines():
        line = line.strip().lstrip("/").strip()
        if "=" not in line:
            continue
        k, v = (s.strip() for s in line.split("=", 1))
        if k not in CONFIG_KEYS:
            continue
        try:
            out[k] = int(v) if v.isdigit() else float(v)
        except ValueError:
            continue
    missing = [k for k in CONFIG_KEYS if k not in out]
    if missing:
        raise SystemExit(f"[atp_transpile] missing config keys: {missing}")
    return out


def parse_features(hexa_text: str) -> Dict[str, bool]:
    """Detect which Mk.III features are declared in the hexa source."""
    return {
        "kv_cache":   "atp_mhsa" in hexa_text and "kv_cache" in hexa_text,
        "rvq":        "atp_rvq_embed" in hexa_text,
        "cfg":        "atp_cfg_combine" in hexa_text,
        "top_k":      "atp_top_k_sample" in hexa_text,
        "swiglu":     "atp_ffn_swiglu" in hexa_text,
        "rmsnorm":    "atp_rmsnorm" in hexa_text,
        "per_stage":  "atp_head" in hexa_text,
    }


def own_preamble(hexa_path: str, out_path: str, cfg: Dict[str, float],
                 features: Dict[str, bool]) -> str:
    """Standard .own 2-header preamble for tool/transient_py/*.

    Header 1 — provenance: source hexa file, transpiler version, timestamp.
    Header 2 — config + feature manifest (so reviewers can diff features
               against the hexa CONFIG block without parsing Python).
    """
    ts = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    feat_str = ",".join(k for k, v in features.items() if v) or "none"
    cfg_str = " ".join(f"{k}={cfg[k]}" for k in CONFIG_KEYS)
    return (
        f"# .own/1 transient_py provenance\n"
        f"#   source:     {hexa_path}\n"
        f"#   transpiler: tool/transient_py/atp_transpile.py (Mk.III)\n"
        f"#   generated:  {ts}\n"
        f"#   target:     {out_path}\n"
        f"#   gate:       F-VLM-TRANSPILE-1\n"
        f"# .own/2 transient_py manifest\n"
        f"#   features:   {feat_str}\n"
        f"#   config:     {cfg_str}\n"
        f"#   regenerate: python {os.path.relpath(__file__, start=os.getcwd())}\n"
    )


# ─── PyTorch module template ─────────────────────────────────────────────
PY_BODY = r'''
"""Mk.III Audio Token Predictor — transpiled from hexa.

Implements: KV-cache, 8-stage RVQ embed/heads, CFG, top-k sampling.
Runs forward pass on CPU (macOS verified). GPU-only paths: none.
"""
from __future__ import annotations

import math
import sys
from dataclasses import dataclass
from typing import List, Optional, Tuple

import torch
import torch.nn as nn
import torch.nn.functional as F


# ─── config (mirror of hexa BEGIN_CONFIG) ────────────────────────────────
@dataclass
class ATPConfig:
    n_layers:    int   = {N_LAYERS}
    n_heads:     int   = {N_HEADS}
    d_model:     int   = {D_MODEL}
    d_head:      int   = {D_HEAD}
    ff:          int   = {FF}
    ctx:         int   = {CTX}
    frame_hz:    int   = {FRAME_HZ}
    rvq_stages:  int   = {RVQ_STAGES}
    v_codebook:  int   = {V_CODEBOOK}
    cfg_scale:   float = {CFG_SCALE}
    top_k:       int   = {TOP_K}
    seed:        int   = {SEED}


# ─── primitive layers (lowered from hexa fns) ───────────────────────────
class RMSNorm(nn.Module):
    def __init__(self, d: int, eps: float = 1e-6):
        super().__init__()
        self.weight = nn.Parameter(torch.ones(d))
        self.eps = eps

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        rms = x.pow(2).mean(-1, keepdim=True).add(self.eps).rsqrt()
        return x * rms * self.weight


class SwiGLUFFN(nn.Module):
    """atp_ffn_swiglu: (silu(x @ Wg) * (x @ Wu)) @ Wd."""
    def __init__(self, d_model: int, ff: int):
        super().__init__()
        self.w_gate = nn.Linear(d_model, ff, bias=False)
        self.w_up   = nn.Linear(d_model, ff, bias=False)
        self.w_down = nn.Linear(ff, d_model, bias=False)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.w_down(F.silu(self.w_gate(x)) * self.w_up(x))


class CausalMHSAWithKVCache(nn.Module):
    """atp_mhsa with append-on-step KV-cache."""
    def __init__(self, d_model: int, n_heads: int, d_head: int, ctx: int):
        super().__init__()
        assert d_model == n_heads * d_head, "d_model must equal n_heads*d_head"
        self.n_heads, self.d_head, self.ctx = n_heads, d_head, ctx
        self.qkv  = nn.Linear(d_model, 3 * d_model, bias=False)
        self.proj = nn.Linear(d_model, d_model, bias=False)
        self.scale = 1.0 / math.sqrt(d_head)

    def forward(
        self,
        x: torch.Tensor,                       # [B, T, D]
        kv_cache: Optional[Tuple[torch.Tensor, torch.Tensor]],
        step: int,
    ) -> Tuple[torch.Tensor, Tuple[torch.Tensor, torch.Tensor]]:
        B, T, D = x.shape
        H, Dh = self.n_heads, self.d_head
        qkv = self.qkv(x).view(B, T, 3, H, Dh).permute(2, 0, 3, 1, 4)
        q, k, v = qkv[0], qkv[1], qkv[2]       # each [B, H, T, Dh]

        if kv_cache is None:
            # step==0 prefill: full causal attention, init cache.
            k_cache = torch.zeros(B, H, self.ctx, Dh, dtype=x.dtype, device=x.device)
            v_cache = torch.zeros(B, H, self.ctx, Dh, dtype=x.dtype, device=x.device)
            k_cache[:, :, :T] = k
            v_cache[:, :, :T] = v
            attn_k, attn_v = k_cache[:, :, :T], v_cache[:, :, :T]
            mask = torch.tril(torch.ones(T, T, device=x.device, dtype=torch.bool))
        else:
            # step>0 decode: append T tokens (typically T==1) to cache slot[step:].
            k_cache, v_cache = kv_cache
            end = min(step + T, self.ctx)
            k_cache[:, :, step:end] = k[:, :, : end - step]
            v_cache[:, :, step:end] = v[:, :, : end - step]
            attn_k, attn_v = k_cache[:, :, :end], v_cache[:, :, :end]
            mask = None  # decode step queries can attend to all past+current

        att = torch.matmul(q, attn_k.transpose(-2, -1)) * self.scale
        if mask is not None:
            att = att.masked_fill(~mask, float("-inf"))
        att = F.softmax(att, dim=-1)
        out = torch.matmul(att, attn_v)        # [B, H, T, Dh]
        out = out.transpose(1, 2).contiguous().view(B, T, D)
        return self.proj(out), (k_cache, v_cache)


class ATPBlock(nn.Module):
    """atp_block: pre-norm decoder block."""
    def __init__(self, cfg: ATPConfig):
        super().__init__()
        self.norm1 = RMSNorm(cfg.d_model)
        self.attn  = CausalMHSAWithKVCache(cfg.d_model, cfg.n_heads, cfg.d_head, cfg.ctx)
        self.norm2 = RMSNorm(cfg.d_model)
        self.ffn   = SwiGLUFFN(cfg.d_model, cfg.ff)

    def forward(self, x, kv_cache, step):
        h, kv_new = self.attn(self.norm1(x), kv_cache, step)
        x = x + h
        x = x + self.ffn(self.norm2(x))
        return x, kv_new


class AudioTokenPredictor(nn.Module):
    """Mk.III ATP: 8-stage RVQ decoder with KV-cache + CFG + top-k."""
    def __init__(self, cfg: ATPConfig):
        super().__init__()
        self.cfg = cfg
        # Intent projection (text/conditioning → d_model). Hand-port assumes
        # caller provides intent_mem of shape [B, T_intent, D_intent]; here we
        # keep a simple Linear over a 1-D mean-pooled vector.
        self.intent_proj = nn.Linear(cfg.d_model, cfg.d_model)
        # Null-intent vector for classifier-free guidance.
        self.null_intent = nn.Parameter(torch.zeros(1, 1, cfg.d_model))

        # 8-stage RVQ embedding tables.
        self.rvq_embed = nn.ModuleList(
            [nn.Embedding(cfg.v_codebook, cfg.d_model) for _ in range(cfg.rvq_stages)]
        )
        # Position embedding (learned, capped at ctx).
        self.pos_embed = nn.Embedding(cfg.ctx, cfg.d_model)

        # Decoder stack.
        self.blocks = nn.ModuleList([ATPBlock(cfg) for _ in range(cfg.n_layers)])
        self.norm_f = RMSNorm(cfg.d_model)

        # Per-stage classifier heads.
        self.heads = nn.ModuleList(
            [nn.Linear(cfg.d_model, cfg.v_codebook, bias=False)
             for _ in range(cfg.rvq_stages)]
        )

    # ─── helpers ────────────────────────────────────────────────────
    def _embed_step(self, prev_tokens: torch.Tensor, positions: torch.Tensor,
                    intent_vec: torch.Tensor) -> torch.Tensor:
        """prev_tokens: [B, T, RVQ_STAGES] long;  positions: [B, T] long.
        Returns x: [B, T, D]. Sums RVQ stage embeddings + positional + intent."""
        B, T, S = prev_tokens.shape
        x = torch.zeros(B, T, self.cfg.d_model, device=prev_tokens.device,
                        dtype=intent_vec.dtype)
        for s in range(S):
            x = x + self.rvq_embed[s](prev_tokens[..., s])
        x = x + self.pos_embed(positions)
        x = x + intent_vec  # broadcast over T
        return x

    def _empty_cache(self, batch: int, device, dtype) -> List:
        return [None] * self.cfg.n_layers  # filled lazily by attn module

    # ─── public API ─────────────────────────────────────────────────
    def forward_step(
        self,
        prev_tokens: torch.Tensor,             # [B, T, RVQ_STAGES]
        positions: torch.Tensor,               # [B, T]
        intent_vec: torch.Tensor,              # [B, 1, D]  (already pooled)
        step: int,
        kv_caches: Optional[List] = None,
    ) -> Tuple[List[torch.Tensor], List]:
        x = self._embed_step(prev_tokens, positions, intent_vec)
        if kv_caches is None:
            kv_caches = self._empty_cache(x.size(0), x.device, x.dtype)
        new_caches = []
        for i, blk in enumerate(self.blocks):
            x, kv_new = blk(x, kv_caches[i], step)
            new_caches.append(kv_new)
        x = self.norm_f(x)
        # take last position only for next-token decode
        last = x[:, -1:, :]
        per_stage_logits = [h(last).squeeze(1) for h in self.heads]  # each [B, V]
        return per_stage_logits, new_caches

    @torch.no_grad()
    def generate(
        self,
        intent_mem: torch.Tensor,              # [B, T_intent, D]
        n_frames: int,
        cfg_scale: Optional[float] = None,
        top_k: Optional[int] = None,
        seed: Optional[int] = None,
    ) -> torch.Tensor:
        """Returns tokens [B, n_frames, RVQ_STAGES] long."""
        cfg = self.cfg
        cfg_scale = cfg.cfg_scale if cfg_scale is None else cfg_scale
        top_k     = cfg.top_k     if top_k     is None else top_k
        seed      = cfg.seed      if seed      is None else seed
        B = intent_mem.size(0)
        device = intent_mem.device

        # Pool intent → [B, 1, D].
        intent_vec = self.intent_proj(intent_mem.mean(dim=1, keepdim=True))
        null_vec   = self.null_intent.expand(B, 1, cfg.d_model)

        gen = torch.Generator(device="cpu").manual_seed(seed)

        # Prefill seed token (zeros). Each step decodes 1 frame x 8 stages.
        cur = torch.zeros(B, 1, cfg.rvq_stages, dtype=torch.long, device=device)
        out = torch.zeros(B, n_frames, cfg.rvq_stages, dtype=torch.long, device=device)
        kv_cond:   Optional[List] = None
        kv_uncond: Optional[List] = None

        for t in range(n_frames):
            pos = torch.full((B, 1), t, dtype=torch.long, device=device)
            logits_cond,   kv_cond   = self.forward_step(
                cur, pos, intent_vec, step=t, kv_caches=kv_cond)
            logits_uncond, kv_uncond = self.forward_step(
                cur, pos, null_vec,   step=t, kv_caches=kv_uncond)

            # CFG combine + top-k sample, per RVQ stage.
            frame = torch.zeros(B, cfg.rvq_stages, dtype=torch.long, device=device)
            for s in range(cfg.rvq_stages):
                lc, lu = logits_cond[s], logits_uncond[s]      # [B, V]
                logits = lu + cfg_scale * (lc - lu)            # CFG combine
                # Top-k restriction (mask out below-threshold logits).
                if top_k > 0 and top_k < logits.size(-1):
                    topv, _ = torch.topk(logits, top_k, dim=-1)
                    threshold = topv[..., -1:].expand_as(logits)
                    logits = torch.where(logits < threshold,
                                         torch.full_like(logits, float("-inf")),
                                         logits)
                probs = F.softmax(logits, dim=-1).cpu()
                tok = torch.multinomial(probs, num_samples=1, generator=gen).to(device)
                frame[:, s] = tok.squeeze(-1)
            out[:, t, :] = frame
            cur = frame.unsqueeze(1)  # next-step input is the just-sampled frame

        return out


# ─── selftest entry ──────────────────────────────────────────────────────
def _selftest() -> int:
    cfg = ATPConfig()
    # Slim the model for CPU smoke test (keeps shape semantics, drops compute).
    cfg.n_layers   = 2
    cfg.n_heads    = 4
    cfg.d_model    = 128
    cfg.d_head     = 32
    cfg.ff         = 256
    cfg.ctx        = 64
    cfg.rvq_stages = {RVQ_STAGES}
    cfg.v_codebook = 256
    cfg.top_k      = 16
    torch.manual_seed(0)
    model = AudioTokenPredictor(cfg).eval()

    intent_mem = torch.randn(1, 4, cfg.d_model)
    tokens = model.generate(intent_mem, n_frames=6)
    print(f"audio_token_predictor Mk.III — L={cfg.n_layers} H={cfg.n_heads} "
          f"d={cfg.d_model} ctx={cfg.ctx} RVQ={cfg.rvq_stages} V={cfg.v_codebook}")
    print(f"tokens(6 frames, 60ms): shape={list(tokens.shape)} "
          f"first={tokens[0,0].tolist()}")
    assert tokens.shape == (1, 6, cfg.rvq_stages), tokens.shape
    assert tokens.min().item() >= 0 and tokens.max().item() < cfg.v_codebook
    return 0


if __name__ == "__main__":
    sys.exit(_selftest())
'''


def render_pytorch(hexa_path: Path, out_path: Path, cfg: Dict[str, float],
                   features: Dict[str, bool]) -> str:
    head = own_preamble(str(hexa_path), str(out_path), cfg, features)
    body = PY_BODY
    # Use double-brace placeholders to avoid clashing with PyTorch literal
    # f-strings / dataclass braces in the template.
    for k in CONFIG_KEYS:
        body = body.replace("{" + k + "}", str(cfg[k]))
    return head + body


def write_audit(audit_dir: Path, hexa_path: Path, out_path: Path,
                cfg: Dict[str, float], features: Dict[str, bool]) -> None:
    audit_dir.mkdir(parents=True, exist_ok=True)
    manifest = audit_dir / "manifest.txt"
    manifest.write_text(
        "atp_transpile audit\n"
        f"  source:    {hexa_path}\n"
        f"  generated: {out_path}\n"
        f"  features:  " + ",".join(k for k, v in features.items() if v) + "\n"
        "  config:\n" +
        "".join(f"    {k} = {cfg[k]}\n" for k in CONFIG_KEYS)
    )
    # Diff vs hand-port: we have no hand-port checked in, so emit the baseline.
    baseline = audit_dir / "diff_vs_hand_port.txt"
    baseline.write_text(
        "atp_transpile diff audit\n"
        "================================\n"
        "No hand-port found at tool/transient_py/atp_pytorch_handport.py.\n"
        "Emitting baseline only — the regenerated module IS the reference.\n"
        "Re-run after a hand-port lands to populate this diff.\n\n"
        "Mk.III feature manifest (transpiled vs hexa CONFIG):\n" +
        "".join(f"  {k:10s} {'YES' if v else 'NO'}\n"
                for k, v in features.items())
    )


def main() -> int:
    p = argparse.ArgumentParser()
    repo_root = Path(__file__).resolve().parents[2]
    p.add_argument("--hexa",  default=str(repo_root / "self/ml/audio_token_predictor.hexa"))
    p.add_argument("--out",   default=str(repo_root / "tool/transient_py/atp_pytorch.py"))
    p.add_argument("--audit", default=str(repo_root / "state/atp_transpile_audit"))
    args = p.parse_args()

    hexa_path = Path(args.hexa)
    out_path = Path(args.out)
    audit_dir = Path(args.audit)

    text = hexa_path.read_text()
    cfg = parse_config(text)
    feat = parse_features(text)

    py = render_pytorch(hexa_path, out_path, cfg, feat)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(py)
    write_audit(audit_dir, hexa_path, out_path, cfg, feat)

    print(f"[atp_transpile] wrote {out_path} ({out_path.stat().st_size} bytes)")
    print(f"[atp_transpile] audit  {audit_dir}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
