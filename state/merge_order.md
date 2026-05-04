# Merge Order — 18 Worktree-Agent PRs (#28-#45)

Generated: 2026-05-04. Repo: `need-singularity/hexa-lang`. Base: `main` @ `ea736c1d`.

This doc is the recommended landing order for the 18 PRs opened against
`main` after the parallel-worktree cycle. PRs are grouped into **waves**;
within a wave the PRs are independent (any order works). Across waves there
is a hard ordering driven by file-overlap, semantic dependency, or
"land-last to minimize churn" pragmatism.

---

## Quick PR index

| PR | Branch tail | Track | Title |
|----|------------|-------|-------|
| [#28](https://github.com/need-singularity/hexa-lang/pull/28) | `abd57c2b3b22c19df` | A1 | atp: hexa->PyTorch transpiler (F-VLM-TRANSPILE-1) |
| [#29](https://github.com/need-singularity/hexa-lang/pull/29) | `aa0d8fc7699b8901c` | A2 | transpile: hexa->Python codegen + 55 fixtures (F-TRANSPILE-2) |
| [#30](https://github.com/need-singularity/hexa-lang/pull/30) | `aa1915a82e4720367` | B3 | tensor: stdlib/tensor with FFI dispatch (F-STDLIB-T1) |
| [#31](https://github.com/need-singularity/hexa-lang/pull/31) | `aa98fd8b1488456ac` | B4 | autograd: tape-based reverse-mode (F-AUTOGRAD-T2) |
| [#32](https://github.com/need-singularity/hexa-lang/pull/32) | `a08aa1e1c72bd9894` | B5 | safetensors: read+write (F-SAFETENSORS-1) **DUPLICATE — discard** |
| [#33](https://github.com/need-singularity/hexa-lang/pull/33) | `a54d4a1efb177d2a4` | B7 | sqlite: CLI binding + ORM-lite (F-SQLITE) |
| [#34](https://github.com/need-singularity/hexa-lang/pull/34) | `a6bb89d9e434dc1c2` | C8 | multiarch: 4-arch dispatch (F-MULTIARCH-1) |
| [#35](https://github.com/need-singularity/hexa-lang/pull/35) | `a2f7fb3fd6735db92` | C9 | lora: custom CUDA kernels (F-CUDA-LORA-1) |
| [#36](https://github.com/need-singularity/hexa-lang/pull/36) | `ae14c5a00d4575683` | C10 | jit: @jit fn -> IR -> C -> dylib (F-JIT) |
| [#37](https://github.com/need-singularity/hexa-lang/pull/37) | `a81f63b9c097fd22e` | D11 | python_ffi: embedded CPython + zero-copy (F-FFI-1) |
| [#38](https://github.com/need-singularity/hexa-lang/pull/38) | `a48d44074dfbace8b` | D12 | docs_gen: 58-module doc tree (F-DOCS-1) |
| [#39](https://github.com/need-singularity/hexa-lang/pull/39) | `a87d4dd7595005b57` | D13 | repl: interactive REPL |
| [#40](https://github.com/need-singularity/hexa-lang/pull/40) | `abfa46f95e9f28b3b` | E14 | stdlib: @version + @capabilities + lint (F-VERSION-1) |
| [#41](https://github.com/need-singularity/hexa-lang/pull/41) | `aaf67bec5f2156579` | FIX-1 | parser: G-AMBIG `Foo<Bar` vs `L < n` |
| [#42](https://github.com/need-singularity/hexa-lang/pull/42) | `a231861dc50c580e7` | FIX-2 | runtime: IEEE-754 reinterpret builtins |
| [#43](https://github.com/need-singularity/hexa-lang/pull/43) | `a310e7a7f6c12dfe4` | FIX-4 | lint: private_fn_collision_lint |
| [#44](https://github.com/need-singularity/hexa-lang/pull/44) | `ad16c0b0762154690` | FIX-5 | resolver: @resolver-bypass mechanism |
| [#45](https://github.com/need-singularity/hexa-lang/pull/45) | `ace8e5f603275089b` | FIX-6 | bytes: chunked I/O + fn-param fix |

---

## Wave 0 — DISCARD

- **#32 (B5 safetensors retry)** — duplicate of `947944ed` already on main. **Action: close without merge.** Keep main; no rebase needed.

## Wave 1 — Foundational fixes (parser/runtime). Land first.

These have **no dependencies on other PRs** but **do** unblock or
de-risk later waves. Also lowest churn (small, focused diffs).

- **#41 FIX-1 (parser G-AMBIG)** — fixes the `while L < n_layers` bug
  C8 worked around. Touches `self/parser.hexa`, `self/hexa_full.hexa`,
  `self/native/hexa_cc.c`. Land first so FIX-6 can rebase against the
  parser changes cleanly.
- **#42 FIX-2 (IEEE-754 reinterpret builtins)** — adds 4 runtime
  builtins. Selftest unverified at PR time; **reviewer must run
  `test_bytes_float.hexa` end-to-end before merge.** No file overlap
  with FIX-1.
- **#43 FIX-4 (private_fn_collision_lint)** — pure tool addition,
  no stdlib runtime change. Documents 3 existing collisions for
  follow-up tickets but does not block them. Should land before B3
  to lock in the regression-prevention story.
- **#44 FIX-5 (resolver-bypass mechanism)** — promotes a doc + 1
  stdlib module + selftest. No file overlap with other fixes. Land
  before C10 (JIT depends on the Mach-O bypass classification).

## Wave 2 — Stdlib feature tracks. Order within wave is free.

These are independent stdlib additions. **B3 -> B4** is a soft order
(B4's autograd has a TODO to swap to `stdlib/tensor` from B3 once landed),
but they don't share files today.

- **#30 B3 (stdlib/tensor)** — depends on FIX-4 conceptually (used
  the workaround that motivated FIX-4); no file conflict.
- **#31 B4 (autograd)** — independent of B3 at the file level (B4's
  Tensor is a stub); a follow-up will couple them.
- **#33 B7 (sqlite)** — fully independent. Uses chunked walk
  workaround that FIX-6 will replace systemically.
- **#37 D11 (python_ffi)** — fully independent stdlib module +
  C shim (`lib/hxpyembed/`).
- **#45 FIX-6 (chunked I/O + fn-param)** — depends on FIX-1
  landing first (parser). Touches `stdlib/bytes.hexa` plus
  `tool/docs/interp_memory.md`.

## Wave 3 — Tooling / runtime extensions.

- **#34 C8 (multiarch)** — depends on FIX-1 (worked around
  `while L < n_layers`); rebase to drop the workaround if desired.
  Touches `state/multiarch_matrix.md` (negated in `.gitignore`).
- **#35 C9 (CUDA LoRA)** — fully independent. C/CUDA only.
- **#36 C10 (JIT)** — depends on FIX-5 (uses `@resolver-bypass`
  classification on Mach-O). Touches `tool/jit/`.
- **#39 D13 (REPL)** — fully independent tool addition.

## Wave 4 — Transpilers + documentation.

- **#28 A1 (atp transpiler)** — fully independent.
- **#29 A2 (hexa->Python transpiler)** — fully independent. 55
  fixtures under `fixtures/transpile/`.
- **#38 D12 (docs_gen)** — **conflicts with E14**. Both regenerate
  per-module artifacts. D12 writes `tool/docs/<slug>.md`; E14 mutates
  the headers of those modules' source. Land D12 *before* E14, then
  re-run `python3 tool/docs_gen.py` post-E14 to refresh anchors.

## Wave 5 — Cross-cutting governance. Land last.

- **#40 E14 (@version + @capabilities)** — touches **58 stdlib
  files**. Will conflict with **every** stdlib-bearing PR above
  (B3, B4, B7, D11, FIX-5, FIX-6) unless landed last. Strategy:
    1. Merge all stdlib-bearing PRs first.
    2. Rebase E14 onto the merged main; the lint will flag any
       module added after the original E14 batch (B3 tensor/, D11
       python_ffi, FIX-5 resolver, FIX-6 bytes chunked surface) — add
       headers manually before merge.
    3. Re-run `tool/version_lint.sh` post-merge; `OK 58 module(s)`
       becomes `OK NN module(s)` where NN reflects the new modules.

---

## Conflict-likely pairs (explicit)

| Pair | Conflict surface | Mitigation |
|------|------------------|------------|
| **E14 (#40) vs everything stdlib** | `@version` / `@capabilities` headers prepended to every stdlib file. B3, B4, B7, D11, FIX-5, FIX-6 all add new stdlib files. | Land E14 last. Add headers to new files during rebase. |
| **D12 (#38) vs E14 (#40)** | D12 generates `tool/docs/*.md` from module headers; E14 changes those headers. | Land D12 first, then E14, then re-run docs_gen and commit refreshed `tool/docs/` either as part of E14 merge or a follow-up. |
| **FIX-1 (#41) vs FIX-6 (#45)** | Both touch parser-adjacent surfaces. FIX-1 is `self/parser.hexa` + `self/hexa_full.hexa`; FIX-6 touches `stdlib/bytes.hexa` (renames `fn`->`cb`). Low file overlap, but FIX-6's silent-failure was a parser-keyword consequence. | Land FIX-1 first. Rebase FIX-6. |
| **B3 (#30) vs B4 (#31)** | Tensor concept overlap. B4's Tensor is an inline stub; B3 is the canonical module. No current file conflict. | Land B3 first; B4 follow-up to swap stub. |
| **C8 (#34) vs FIX-1 (#41)** | C8 worked around the `L < n_layers` bug by renaming the loop variable. The workaround is benign on top of FIX-1. | Either order; optionally rebase C8 to drop the rename after FIX-1 lands. |
| **C9 (#35) build manifest** | `tool/build_hxqwen14b_linux.hexa` may collide with future C8 NVIDIA build wiring. | C9 is `linux+NVIDIA` only; the C8 PR's build paths are arch-probing. No current overlap. |

---

## Recommended landing sequence (linear)

```
1.  #41 FIX-1   parser ambiguity
2.  #42 FIX-2   IEEE-754 builtins   [verify selftest first]
3.  #43 FIX-4   collision lint
4.  #44 FIX-5   resolver-bypass
5.  #45 FIX-6   bytes chunked I/O   [rebase on FIX-1]
6.  #30 B3      tensor stdlib
7.  #31 B4      autograd
8.  #33 B7      sqlite
9.  #37 D11     python_ffi
10. #28 A1      atp transpiler
11. #29 A2      hexa->Python transpiler
12. #34 C8      multiarch              [optional rebase to drop workaround]
13. #35 C9      CUDA LoRA
14. #36 C10     JIT                    [verifies FIX-5]
15. #39 D13     REPL
16. #38 D12     docs_gen
17. #40 E14     versioning             [LAST — rebases through all stdlib PRs]
18. #32 B5      DISCARD — close without merge
```

---

## Verification gates (post-merge)

After all PRs land:

- `tool/version_lint.sh` exits 0 on the new main.
- `tool/private_fn_collision_lint.hexa --strict` reports the 3 known
  follow-ups (`_shell_escape`, `_last_index_of`, `_s`) but no new
  collisions from the merged surface.
- `python3 tool/docs_gen.py` writes 60+ module pages (58 original + B3
  tensor/* + D11 python_ffi + FIX-5 resolver + FIX-6 chunked-I/O surface).
- All selftests under `stdlib/test/` pass on agent host.

---

*Generated by parallel-PR opener on worktree branch
`worktree-agent-acbde1373ebe4c665`. Source-of-truth for any claim in this
doc is the linked PR body and the per-branch commit message.*
