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
from typing import Dict, List, Tuple, Optional, Any


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


# ─── hexa body parser (recursive descent) ───────────────────────────────
# Scope (intentionally minimal — covers the ATP module vocabulary):
#   stmts:  let NAME = expr ;  return expr
#   exprs:  arithmetic + - * / , unary -, calls f(arg, ...), parens, idents,
#           int/float literals
# No closures, no generics, no if/while/for. Larger features bail to a
# stub `return x` and the caller falls back to the hand-written template.

# AST node tuples (lightweight; no dataclass overhead):
#   ("num", value)
#   ("name", id)
#   ("call", name, [args])
#   ("binop", op, lhs, rhs)
#   ("unary", op, operand)
#   ("let", name, expr)
#   ("return", expr)


_TOKEN_RE = re.compile(
    r"\s*(?:"
    r"(//[^\n]*)"             # 1: line comment
    r"|([A-Za-z_][A-Za-z0-9_]*)"   # 2: ident/keyword
    r"|(\d+\.\d+(?:[eE][+\-]?\d+)?|\d+(?:[eE][+\-]?\d+)?)"  # 3: number (incl. sci notation)
    r"|([+\-*/(){},=])"       # 4: punctuation
    r")"
)


def _tokenize(src: str) -> List[Tuple[str, str]]:
    """Return [(kind, text)] tokens. Kind in {ID, NUM, PUNCT}."""
    out: List[Tuple[str, str]] = []
    i = 0
    while i < len(src):
        m = _TOKEN_RE.match(src, i)
        if not m:
            # skip stray char (newline already eaten by \s*)
            i += 1
            continue
        i = m.end()
        if m.group(1) is not None:
            continue  # comment
        if m.group(2) is not None:
            out.append(("ID", m.group(2)))
        elif m.group(3) is not None:
            out.append(("NUM", m.group(3)))
        elif m.group(4) is not None:
            out.append(("PUNCT", m.group(4)))
    return out


class _ParseError(Exception):
    pass


class _Parser:
    """Recursive-descent parser over the token list."""
    def __init__(self, toks: List[Tuple[str, str]]):
        self.toks = toks
        self.i = 0

    def _peek(self, ahead: int = 0) -> Optional[Tuple[str, str]]:
        j = self.i + ahead
        return self.toks[j] if j < len(self.toks) else None

    def _eat(self, kind: str, text: Optional[str] = None) -> Tuple[str, str]:
        t = self._peek()
        if t is None or t[0] != kind or (text is not None and t[1] != text):
            raise _ParseError(f"expected {kind} {text!r}, got {t!r}")
        self.i += 1
        return t

    # ─── expressions (Pratt-ish) ───────────────────────────────────
    def parse_expr(self) -> Any:
        return self._parse_addsub()

    def _parse_addsub(self) -> Any:
        lhs = self._parse_muldiv()
        while True:
            t = self._peek()
            if t and t == ("PUNCT", "+"):
                self.i += 1
                lhs = ("binop", "+", lhs, self._parse_muldiv())
            elif t and t == ("PUNCT", "-"):
                self.i += 1
                lhs = ("binop", "-", lhs, self._parse_muldiv())
            else:
                return lhs

    def _parse_muldiv(self) -> Any:
        lhs = self._parse_unary()
        while True:
            t = self._peek()
            if t and t == ("PUNCT", "*"):
                self.i += 1
                lhs = ("binop", "*", lhs, self._parse_unary())
            elif t and t == ("PUNCT", "/"):
                self.i += 1
                lhs = ("binop", "/", lhs, self._parse_unary())
            else:
                return lhs

    def _parse_unary(self) -> Any:
        t = self._peek()
        if t == ("PUNCT", "-"):
            self.i += 1
            return ("unary", "-", self._parse_unary())
        return self._parse_atom()

    def _parse_atom(self) -> Any:
        t = self._peek()
        if t is None:
            raise _ParseError("unexpected EOF in expression")
        if t[0] == "NUM":
            self.i += 1
            txt = t[1]
            if "." in txt or "e" in txt or "E" in txt:
                v: Any = float(txt)
            else:
                v = int(txt)
            return ("num", v)
        if t[0] == "PUNCT" and t[1] == "(":
            self.i += 1
            e = self.parse_expr()
            self._eat("PUNCT", ")")
            return e
        if t[0] == "ID":
            self.i += 1
            # call?
            nxt = self._peek()
            if nxt == ("PUNCT", "("):
                self.i += 1
                args: List[Any] = []
                if self._peek() != ("PUNCT", ")"):
                    args.append(self.parse_expr())
                    while self._peek() == ("PUNCT", ","):
                        self.i += 1
                        args.append(self.parse_expr())
                self._eat("PUNCT", ")")
                return ("call", t[1], args)
            return ("name", t[1])
        raise _ParseError(f"unexpected token {t!r}")

    # ─── statements & body ─────────────────────────────────────────
    def parse_body(self) -> List[Any]:
        """Parse a {-delimited block of statements; returns list of stmts."""
        self._eat("PUNCT", "{")
        stmts: List[Any] = []
        while self._peek() != ("PUNCT", "}"):
            stmts.append(self._parse_stmt())
        self._eat("PUNCT", "}")
        return stmts

    def _parse_stmt(self) -> Any:
        t = self._peek()
        if t == ("ID", "let"):
            self.i += 1
            name_tok = self._eat("ID")
            self._eat("PUNCT", "=")
            expr = self.parse_expr()
            return ("let", name_tok[1], expr)
        if t == ("ID", "return"):
            self.i += 1
            expr = self.parse_expr()
            return ("return", expr)
        raise _ParseError(f"unexpected stmt token {t!r}")


# Map of hexa intrinsic call → Python lowering. Each value is
# (n_args, fmt_string) where fmt_string uses {0}, {1}, ... as args.
_INTRINSIC_LOWERINGS = {
    "mean":   (2, "{0}.mean({1}, keepdim=True)"),
    "pow":    (2, "{0}.pow({1})"),
    "rsqrt":  (1, "{0}.rsqrt()"),
    "silu":   (1, "F.silu({0})"),
    "linear": (2, "F.linear({0}, {1})"),
}


def _lower_expr(node: Any) -> str:
    kind = node[0]
    if kind == "num":
        return repr(node[1])
    if kind == "name":
        return node[1]
    if kind == "unary":
        return f"(-{_lower_expr(node[2])})"
    if kind == "binop":
        op, lhs, rhs = node[1], node[2], node[3]
        return f"({_lower_expr(lhs)} {op} {_lower_expr(rhs)})"
    if kind == "call":
        name, args = node[1], node[2]
        spec = _INTRINSIC_LOWERINGS.get(name)
        lowered = [_lower_expr(a) for a in args]
        if spec is None:
            # Fallback: emit as plain Python call (may fail at runtime if
            # the name isn't defined — caller is responsible for vocabulary).
            return f"{name}({', '.join(lowered)})"
        n_args, fmt = spec
        if len(lowered) != n_args:
            raise _ParseError(f"intrinsic {name} expects {n_args} args, got {len(lowered)}")
        return fmt.format(*lowered)
    raise _ParseError(f"unknown expr kind {kind!r}")


def _lower_stmts(stmts: List[Any], indent: str = "        ") -> str:
    """Lower a parsed body to Python statements (already indented)."""
    out: List[str] = []
    for s in stmts:
        if s[0] == "let":
            out.append(f"{indent}{s[1]} = {_lower_expr(s[2])}")
        elif s[0] == "return":
            out.append(f"{indent}return {_lower_expr(s[1])}")
        else:
            raise _ParseError(f"unknown stmt {s[0]!r}")
    return "\n".join(out)


def parse_fn_bodies(hexa_text: str) -> Dict[str, str]:
    """Find `fn NAME(args) { body }` blocks; parse + lower bodies.

    Returns {fn_name: lowered_python_body_string}. Parameters are not
    lowered here — the caller supplies them when emitting forward().
    Functions that fail to parse are silently skipped (caller falls back).
    """
    out: Dict[str, str] = {}
    # Match `fn NAME(...) {`; capture everything up to the matching `}`.
    # Hexa fn bodies in this module are flat (no nested braces other than
    # struct/dict, which the ATP vocabulary doesn't use), so a brace
    # balancer is sufficient.
    for m in re.finditer(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^)]*\)\s*\{", hexa_text):
        name = m.group(1)
        start = m.end() - 1  # position of opening `{`
        depth = 0
        i = start
        while i < len(hexa_text):
            c = hexa_text[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if depth != 0:
            continue
        body_src = hexa_text[start:i + 1]
        try:
            toks = _tokenize(body_src)
            stmts = _Parser(toks).parse_body()
            out[name] = _lower_stmts(stmts)
        except _ParseError:
            # Bail — caller keeps hand-written template for this fn.
            continue
    return out


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
    """atp_rmsnorm — body lowered from hexa source."""
    def __init__(self, d: int, eps: float = 1e-6):
        super().__init__()
        self.weight = nn.Parameter(torch.ones(d))
        self.eps = eps

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # Bind hexa fn params to module attrs.
        weight = self.weight
        eps = self.eps
__RMSNORM_BODY__


class SwiGLUFFN(nn.Module):
    """atp_ffn_swiglu — body lowered from hexa source."""
    def __init__(self, d_model: int, ff: int):
        super().__init__()
        # Use raw weight tensors so the lowered body's `linear(x, w)` calls
        # land on F.linear directly (matching the hexa intrinsic).
        self.w_gate = nn.Parameter(torch.empty(ff, d_model))
        self.w_up   = nn.Parameter(torch.empty(ff, d_model))
        self.w_down = nn.Parameter(torch.empty(d_model, ff))
        nn.init.kaiming_uniform_(self.w_gate, a=math.sqrt(5))
        nn.init.kaiming_uniform_(self.w_up,   a=math.sqrt(5))
        nn.init.kaiming_uniform_(self.w_down, a=math.sqrt(5))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # Bind hexa fn params to module attrs.
        w_gate = self.w_gate
        w_up   = self.w_up
        w_down = self.w_down
__SWIGLU_BODY__


class Head(nn.Module):
    """atp_head — body lowered from hexa source."""
    def __init__(self, d_model: int, v_codebook: int):
        super().__init__()
        self.weight = nn.Parameter(torch.empty(v_codebook, d_model))
        nn.init.kaiming_uniform_(self.weight, a=math.sqrt(5))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # Bind hexa fn params to module attrs.
        w_head = self.weight
__HEAD_BODY__


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

        # Per-stage classifier heads (body lowered from hexa atp_head).
        self.heads = nn.ModuleList(
            [Head(cfg.d_model, cfg.v_codebook) for _ in range(cfg.rvq_stages)]
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


# Hand-written fallback bodies (used when hexa parse fails for a fn).
_FALLBACK_BODIES = {
    "atp_rmsnorm": (
        "        rms = x.pow(2).mean(-1, keepdim=True).add(eps).rsqrt()\n"
        "        return x * rms * weight"
    ),
    "atp_ffn_swiglu": (
        "        return F.linear(F.silu(F.linear(x, w_gate)) * F.linear(x, w_up), w_down)"
    ),
    "atp_head": (
        "        return F.linear(x, w_head)"
    ),
}


def render_pytorch(hexa_path: Path, out_path: Path, cfg: Dict[str, float],
                   features: Dict[str, bool],
                   fn_bodies: Optional[Dict[str, str]] = None) -> str:
    head = own_preamble(str(hexa_path), str(out_path), cfg, features)
    body = PY_BODY
    # Substitute config integer/float values (placeholders like {N_LAYERS}).
    for k in CONFIG_KEYS:
        body = body.replace("{" + k + "}", str(cfg[k]))

    # Substitute parsed-from-source fn body placeholders. Fall back to the
    # hand-written body when the hexa parser couldn't lower the source.
    fn_bodies = fn_bodies or {}
    body_subs = {
        "__RMSNORM_BODY__": ("atp_rmsnorm",   _FALLBACK_BODIES["atp_rmsnorm"]),
        "__SWIGLU_BODY__":  ("atp_ffn_swiglu", _FALLBACK_BODIES["atp_ffn_swiglu"]),
        "__HEAD_BODY__":    ("atp_head",      _FALLBACK_BODIES["atp_head"]),
    }
    for marker, (fn_name, fallback) in body_subs.items():
        chosen = fn_bodies.get(fn_name, fallback)
        body = body.replace(marker, chosen)
    return head + body


def write_audit(audit_dir: Path, hexa_path: Path, out_path: Path,
                cfg: Dict[str, float], features: Dict[str, bool],
                fn_bodies: Optional[Dict[str, str]] = None) -> None:
    audit_dir.mkdir(parents=True, exist_ok=True)
    manifest = audit_dir / "manifest.txt"
    fn_bodies = fn_bodies or {}
    body_lines = ""
    if fn_bodies:
        body_lines = "  parsed fn bodies (lowered from hexa source):\n"
        for name in sorted(fn_bodies.keys()):
            body_lines += f"    - {name}\n"
    manifest.write_text(
        "atp_transpile audit\n"
        f"  source:    {hexa_path}\n"
        f"  generated: {out_path}\n"
        f"  features:  " + ",".join(k for k, v in features.items() if v) + "\n"
        "  config:\n" +
        "".join(f"    {k} = {cfg[k]}\n" for k in CONFIG_KEYS) +
        body_lines
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
    fn_bodies = parse_fn_bodies(text)

    parsed = sorted(fn_bodies.keys())
    print(f"[atp_transpile] parsed {len(parsed)} fn bodies from source: "
          f"{parsed if parsed else '(none)'}")

    py = render_pytorch(hexa_path, out_path, cfg, feat, fn_bodies=fn_bodies)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(py)
    write_audit(audit_dir, hexa_path, out_path, cfg, feat, fn_bodies=fn_bodies)

    print(f"[atp_transpile] wrote {out_path} ({out_path.stat().st_size} bytes)")
    print(f"[atp_transpile] audit  {audit_dir}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
