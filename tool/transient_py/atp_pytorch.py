# .own/1 transient_py provenance
#   source:     /Users/ghost/core/hexa-lang/.claude/worktrees/agent-abd57c2b3b22c19df/self/ml/audio_token_predictor.hexa
#   transpiler: tool/transient_py/atp_transpile.py (Mk.III)
#   generated:  2026-05-03T21:54:21Z
#   target:     /Users/ghost/core/hexa-lang/.claude/worktrees/agent-abd57c2b3b22c19df/tool/transient_py/atp_pytorch.py
#   gate:       F-VLM-TRANSPILE-1
# .own/2 transient_py manifest
#   features:   kv_cache,rvq,cfg,top_k,swiglu,rmsnorm,per_stage
#   config:     N_LAYERS=12 N_HEADS=12 D_MODEL=768 D_HEAD=64 FF=3072 CTX=1536 FRAME_HZ=100 RVQ_STAGES=8 V_CODEBOOK=1024 CFG_SCALE=3.0 TOP_K=50 SEED=0
#   regenerate: python tool/transient_py/atp_transpile.py

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
    n_layers:    int   = 12
    n_heads:     int   = 12
    d_model:     int   = 768
    d_head:      int   = 64
    ff:          int   = 3072
    ctx:         int   = 1536
    frame_hz:    int   = 100
    rvq_stages:  int   = 8
    v_codebook:  int   = 1024
    cfg_scale:   float = 3.0
    top_k:       int   = 50
    seed:        int   = 0


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
    cfg.rvq_stages = 8
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
