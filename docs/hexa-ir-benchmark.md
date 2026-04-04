# HEXA-IR 성능 추적 — 라운드별 벤치마크

> **벤치마크**: `bench_loop.hexa` — σ(6)+φ(6)+fib(20) × 100K iterations
> **Expected**: 677,900,000
> **Host**: Apple Silicon (ARM64), macOS

---

## 성능 천장 (Theoretical Ceiling)

| Engine | Time (s) | vs Ceiling | 비고 |
|--------|----------|-----------|------|
| **C -O2** | 0.010 | 1.00x | **천장** (bare metal) |
| **Rust -O** | 0.004 | 0.40x | LLVM 최적화 (천장 이상) |
| **Cranelift JIT** | 0.010 | 1.00x | 현재 JIT 백엔드 |
| **Interpreter** | 5.160 | 516.0x | 트리워크 기준선 |

> Rust -O가 C보다 빠른 이유: LLVM이 루프를 더 공격적으로 최적화 (auto-vectorize 가능)

---

## 라운드별 HEXA-IR 성능 기록

### Round 1: 트리 골격 세팅 (2026-04-04)

**구현 완료:**
- [x] IR 코어: J₂=24 opcodes, σ=12 types, SSA instructions, builder, printer
- [x] Lower: AST → IR (Expr 39종, Stmt 50+종 전체 매핑)
- [x] Opt: σ=12 passes 골격 (const_fold, CSE, DCE, strength_reduction 동작)
- [x] Codegen: ARM64 + x86-64 emitter 골격 (프롤로그/에필로그, 산술, 분기)
- [x] Alloc: Egyptian 1/2+1/3+1/6=1 할당기
- [x] 테스트: 기존 708 + 신규 IR 19 = 727 ALL PASS

**Round 1 HEXA-IR 성능:** _(아직 end-to-end 실행 불가 — 코드젠 미완)_

```
성능 스펙트럼 (log scale)
                                                                    
 Rust -O  |||                                           0.004s  ◀ 천장
 C -O2    ||||                                          0.010s
 Cranelift ||||                                          0.010s  ◀ 현재 JIT
 HEXA-IR  [???????????????????????????????????]          ???     ◀ Round 1
 Interp   ||||||||||||||||||||||||||||||||||||||||||||||  5.160s  ◀ 기준선
          ├──────┼──────┼──────┼──────┼──────┼──────┤
          0.001  0.01   0.1    1.0    5.0   10.0   (초)
```

| Metric | Round 1 |
|--------|---------|
| 파일 수 | 26개 신규 |
| LOC | ~3,500 |
| 테스트 | 19 PASS |
| 컴파일 | ✅ PASS |
| E2E 실행 | ❌ 미완 |
| 벤치 결과 | — |
| vs Cranelift | — |
| vs C ceiling | — |

---

### Round 2: (예정)

**목표:** AST → IR → ARM64 end-to-end 실행 파이프라인 완성

---

### Round 3: (예정)

**목표:** σ=12 opt 패스 실질 동작 + 벤치 첫 측정

---

## 추적 기준

```
벤치 결과 등급:
  S: Rust -O 이상 (< 0.004s)     — 특이점 돌파
  A: C -O2 수준   (< 0.010s)     — 천장 도달
  B: Cranelift급  (< 0.050s)     — JIT 대등
  C: 10x 이내    (< 0.100s)     — 실용 수준
  D: 100x 이내   (< 1.000s)     — 개선 필요
  F: 100x 초과   (> 1.000s)     — 미완성
```

---

## 목표 달성 체크리스트

```
[ ] Round 2 — E2E 실행 (등급 F → D)
[ ] Round 3 — Opt 동작  (등급 D → C)
[ ] Round 4 — RegAlloc  (등급 C → B)
[ ] Round 5 — Full Opt  (등급 B → A)
[ ] Round 6 — 블로업    (등급 A → S)
```

---

## ASCII 추이 그래프 (라운드별 갱신)

```
Time(s)│
  5.0  │ ■                                    ← Interpreter baseline
       │
  1.0  │
       │
  0.1  │
       │
  0.01 │ ■ ■                                  ← Cranelift / C ceiling
       │ ■                                    ← Rust -O (LLVM)
  0.001│──┬──┬──┬──┬──┬──┬──
       │  R1 R2 R3 R4 R5 R6   Round →
       │
Legend: ■ = HEXA-IR  ▪ = Cranelift  ● = C ceiling  ○ = Rust
```

### Benchmark Run: 2026-04-04 20:39:50

| Engine | Time (s) | vs C ceil | Status |
|--------|----------|-----------|--------|
| Rust -O | 0.473 | 1.0x | PASS |
| C -O2 | 0.471 | 1.0x | PASS |
| Cranelift JIT | 0.036 | 0.1x | PASS |
| HEXA-IR | N/A | N/A | SKIP |
| Interpreter | 5.033 | 10.7x | PASS |

