# hexa-lang β main TOP 10 Design Consolidation (2026-04-22)

Source: `docs/loss_free_roi_brainstorm_20260422.md` TOP 10
.roadmap entries: 54–63 (HXA-#01 ~ #10)
Anima mirror: `$ANIMA/docs/upstream_notes/hexa_lang_beta_main_acceleration_20260422.md`
Dispatch: 10 parallel Plan agents (2026-04-22 세션) — 본 문서는 그 취합.

---

## 1. 요약 매트릭스

| # | roadmap | 항목 | 노력 | LOC (net) | 핵심 요약 |
|---|---|---|---|---|---|
| B1 | 54 | cert_gate stdlib | 2.5–3주 | −300 anima +500 stdlib | anima `tool/cert_gate.hexa` (980) → mechanism (stdlib 350) / policy (anima 200) 분리 |
| B20 | 55 | deterministic FP (IEEE 754 strict) | **3.6주** | ~3940 (vendored crlibm 포함) | 컴파일러 FMA-rewrite 2곳 guard + clang strict flags + crlibm vendoring + per-thread FPCR/MXCSR init |
| B7 | 56 | native http/1.1 server | 2주 | ~700 | `self/serve/http_server.hexa` (500 LOC) 이미 작동 → stdlib 승격이 본질; `net_read_n` builtin 추가 |
| B16 | 57 | matrix ops BLAS-lite | 4주 | ~1400 + 60 C | `hxblas_linux.c` 이미 sgemm/sdot/saxpy 존재 → `hxblas_snrm2`+`hxblas_sgemv` 30 LOC 추가 |
| B3 | 58 | eigen decomp primitive | 2주 | ~360 | **현재 anima eigenvec은 LCG synthetic** — 실제 decomp 없음. 16×16 cyclic Jacobi 충분 |
| B25 | 59 | `.meta2-cert/` 규약 표준화 | 3–4일 | ~600 | schema v2 + canonical emitter (숫자 토큰 원본 보존, float reformat drift 방지) |
| B15 | 60 | CPGD optimizer primitive | **2일** | ~400 stdlib / −450 anima | Dual API: concrete projector + callback `project: fn` (cert schema 확장 가능) |
| B5 | 61 | proof-carrying ckpt `.pcc` | 3주 | ~500 + xxh64 | HEXAW 확장; CERT inline + 4MiB chunk content-addressable + Merkle + Genesis-retroactive |
| B8+B10 | 62 | signal + flock stdlib | 2–3 세션 | ~850 | self-pipe async-signal-safe trampoline + O_CLOEXEC + setpgid |
| B12 | 63 | concurrent serve framework | 2일 | ~520 | `async_runtime` + `work_stealing` 이미 존재 → glue; SharedModelHandle read-only |

---

## 2. 의존 그래프

```
B20 (strict FP, 55) ──────┬──→ B16 (BLAS, 57) ──→ B3 (eigen, 58)
                          │                  └──→ B15 (CPGD, 60)
B1 (cert stdlib, 54) ─────┼──→ B25 (meta2, 59) ──→ B5 (ckpt, 61)
                          │
B8+B10 (sig+flock, 62) ───┼──→ B12 (concurrent serve, 63)
B7 (http, 56) ────────────┘
```

Critical path (병렬 실행 시): **B20 → B16 → B3 + B15 ≈ 9주**
단순 합 (1인): ~18–19주

Roadmap dep 재분류 필요:
- `roadmap 58 → 57`: **soft로** — Jacobi는 BLAS 없이 hand-roll 가능, hard dep는 55 (strict FP)
- `roadmap 60 → 55`: **soft dep 추가** — API는 55 없이 가능, byte-identical 수용만 55 필요

---

## 3. 설계 중 드러난 prereq (우선 landing 대상)

| # | 항목 | 위치 | LOC | Blocks |
|---|---|---|---|---|
| A1 | xxhash stdlib (xxh64 확장) | `self/runtime/xxhash_pure.hexa` → `stdlib/hash/xxhash.hexa` | ~50 | B5 |
| A2 | rng stdlib (lcg + Box-Muller) | `stdlib/math/rng.hexa` | ~40 | B15, B3 (selftest) |
| A3 | `net_read_n` builtin | `self/native/net.c` + codegen | ~20 | B7 |
| A4 | `hxblas_snrm2` + `hxblas_sgemv` | `self/native/hxblas_linux.c` | ~30 | B16 |
| A5 | `@strict_fp` attr | `self/attrs/strict_fp.hexa` | ~60 | B20 |

---

## 4. 파일 변경 scope 개요

### Stdlib (신규)
- `stdlib/cert/` — mod + verdict + json_scan + loader + factor + reward + selftest + writer + digest (B1)
- `stdlib/cert/meta2_schema.hexa` + `meta2_validator.hexa` (B25)
- `stdlib/ckpt/` — format + writer + reader + verifier + pcc (B5)
- `stdlib/hash/xxhash.hexa` (A1)
- `stdlib/linalg/` — blas1 + blas2 + blas3 + pack + ffi + reference (B16)
- `stdlib/math/eigen.hexa` (B3)
- `stdlib/math/rng.hexa` (A2)
- `stdlib/net/` — socket + http_request + http_response + http_server + concurrent_serve (B7 + B12)
- `stdlib/optim/cpgd.hexa` + `stdlib/optim/projector.hexa` (B15)
- `stdlib/os/signal.hexa` + `stdlib/os/flock.hexa` + `mod.hexa` (B8+B10)

### Compiler (self/)
- `self/codegen_c2.hexa` — FMA-rewrite guards (lines 1722, 2502); strict prologue emit (B20)
- `self/main.hexa` — clang flag assembly w/ HEXA_STRICT_FP (B20)
- `self/runtime.c` — `hexa_fp_init` + strict transcendental dispatch (B20)
- `self/native/net.c` — `net_read_n`, `net_set_timeout` (B7 prereq)
- `self/native/hxblas_linux.c` — snrm2 + sgemv (B16 prereq)
- `self/rt/syscall.hexa` — sigaction + flock + kqueue syscall nums (B8+B10)
- `self/attrs/strict_fp.hexa` (B20 prereq)
- `self/async_runtime.hexa` — signal pipe integration (B8+B10)

### Anima 소비자 리팩터
- `tool/cert_gate.hexa` — mechanism 제거, policy 유지 (−500 / +200)
- `tool/phi_extractor_cpu.hexa` + `phi_extractor_ffi_wire.hexa` — BLAS call-sites 3개 + eigen 도입
- `tool/serve_alm_persona.hexa` — native http + signal handler + flock
- `edu/lora/cpgd_wrapper.hexa` + `cpgd_minimal_proof.hexa` — CPGD 인라인 수학 제거
- `.meta2-cert/` — schema v2 migration + `archive/` 재배치
- `serving/concurrent_serve_3in1.hexa` (신규) — CP2 dest2 bench 대상
- `anima-tools/ckpt_rewriter.hexa` (신규) — `.pcc` 전환

---

## 5. 위험 플래그 (랜딩 시 주의)

1. **B20 libm vendor 의존성**: crlibm 벤더링은 non-negotiable. Apple libm ≠ glibc ≠ musl at ULP. 미도입 시 4-path Φ byte-identical 무의미.
2. **B20 FMA 비활성화 → training convergence drift**: substrate-portability > accuracy. 문서화.
3. **B20 per-thread FPCR/MXCSR**: 모든 thread entry에서 `hexa_fp_init()` 호출 필요 (pthread_create 훅).
4. **B25 numeric reformat drift**: `1.60329e-16` 같은 값 — parser/emitter 숫자 토큰 원본 문자열 보존.
5. **B25 evidence polymorphism**: stage0에 sum type 없음 — `extras: map` flat struct로 우회.
6. **B3 eigenvec sign canonicalization**: 첫 nonzero entry ≥ 0 강제 — byte-identical 필수.
7. **B3 FP32 1e-4 타이트**: 16×16 Jacobi 6 sweeps 누적 오차 ~1e-5 상대 — FP64 accumulator 옵션.
8. **B5 xxhash 비암호학**: dedup 키 전용. 인증은 cert chain (B1)의 merkle 서명이 담당. 문서 명시.
9. **B12 SharedModelHandle**: post-load read-only. KV/scratch는 per-request arena. model weights checksum post-bench 검증.
10. **B7 net_read single-recv**: 현재 builtin이 단일 recv — POST body 패킷 분할 시 truncate. `net_read_n` 추가가 단일 must-fix.

---

## 6. roadmap exit_criteria → 구현 체크리스트 (약식)

| roadmap | exit_criteria 압축 | 관측 |
|---|---|---|
| 54 | `stdlib/cert.hexa` + anima 마이그레이션 + 외부 verifier smoke | Phase 1 (stdlib 단독) + Phase 2 (anima refactor) + Phase 3 (external minimal_verifier example) |
| 55 | `HEXA_STRICT_FP=1` + 결정성 벡터 + cross-substrate byte-stable | crlibm + FMA guard + clang strict flags + CI 4-substrate hash 비교 |
| 56 | `stdlib/net/http_server.hexa` + anima native shim + smoke | `self/serve/http_server.hexa` 승격 + `net_read_n` 추가 |
| 57 | `stdlib/math/blas.hexa` + ≥numpy 0.3× + FP32 1e-5 | reference + ffi 이중 백엔드 + hxblas 30 LOC ext |
| 58 | `stdlib/math/eigen.hexa` + phi_extractor refactor + FP32 1e-4 | cyclic Jacobi + canonical sign + FP64 accumulator |
| 59 | `stdlib/cert/meta2_schema.hexa` + 100-cert round-trip | schema v2 + canonical emitter + archive/ 이동 |
| 60 | `stdlib/optim/cpgd.hexa` + anima 마이그레이션 + 70B PoC | dual API + 16×16 PoC + rng stdlib 선행 |
| 61 | `stdlib/ckpt/pcc.hexa` + 100-ckpt round-trip + tamper test | xxh64 ext + CERT inline + Merkle + 5 tamper 오류 분화 |
| 62 | `stdlib/os/signal.hexa` + `flock.hexa` + anima SIGTERM E2E | self-pipe + O_CLOEXEC + fork/kill E2E |
| 63 | `stdlib/net/concurrent_serve.hexa` + 3-endpoint bench ≥2× | work_stealing glue + flock cert trail |

---

## 7. 다음 단계 (2026-04-22 현재)

**Wave 1 (now)**: 본 문서 + anima mirror landing. Prereq A1-A5 병렬 implementation (worktree isolated). 소형 CP1: B25 + B7 + B8+B10 + B3 stdlib 단독 부분 병렬 착수.

**Wave 2 (after Wave 1 merge)**: B1 cert stdlib (A2 의존 없음, 단독 가능), B15 CPGD (A2 rng 의존), B12 (B7+B8+B10 의존).

**Wave 3 (largest)**: B5 proof-carrying ckpt (B1+B25+A1 의존), B16 BLAS-lite (A4+55 soft), B20 strict FP (가장 큰 compiler 변경).

**anima 측 대응 필요 (병렬)**:
- `docs/upstream_notes/hexa_lang_beta_main_acceleration_20260422.md` (본 문서 미러)
- roadmap 내 anima 엔트리 — hexa-lang 54–63 의존 선언 (anima@xxx depends-on hexa-lang@NN 형식)

---

## 8. 본 문서 유지보수

본 문서는 **설계 스냅샷** — 2026-04-22 조사 에이전트 10개의 산출을 요약. 실제 구현은 roadmap 54–63 `exit_criteria` 체크리스트와 개별 PR에서 진행. 구현 중 설계 변경 시 **본 문서 업데이트** + `.roadmap` 엔트리의 why/exit_criteria 동기화.

SSOT: `.roadmap` (54–63). 본 문서는 discussion doc / 합의된 기술 설계. 양자 불일치 시 `.roadmap` 우선.
