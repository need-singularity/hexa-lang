# CPU AI-Native 물리한계 돌파 설계

> 2026-04-09 — 박민우 / hexa-lang
> 전략: CPU-only + AI-native @attr 조합으로 LLM 학습/추론/서빙의 GPU 의존 최소화.
> 종료 조건: **T1(7B 추론) + T2(100M 학습) 2개 달성 시 루프 종료**.
> 중간 브레이크포인트: CPU 불가 증명 시 "현재 기준 GPU 수준 도달" fallback 목표.

---

## 1. 목적

"같은 알고리즘을 빠른 명령어로" 가 아니라 **"알고리즘 자체를 AI-native attr로 교체"** 해서
CPU 물리한계 위에서도 GPU 동급 LLM 워크로드를 달성. T3(7B 학습)은 본 설계의 out-of-scope
— GPU 필수 영역이므로 "CPU 특이점"의 본질과 결이 다르며 스트레치 목표로만 남김.

## 2. 기준 하드웨어 (shared/infrastructure.json)

| 역할 | 호스트 | 스펙 | 피크 | 대역폭 |
|---|---|---|---|---|
| CPU-only | htz | Ryzen 9 7950X3D, 32T, 124GB DDR5 | ~2.5 TF FP32 / ~10 TOPS INT8 | ~83 GB/s |
| GPU local | ubu | RTX 5070, 12GB GDDR7 | ~30 TF FP32 / ~240 TOPS INT8 | ~672 GB/s |
| GPU cloud | vast (34448146, running) | 4× RTX 4090, 96GB | ~330 TF FP32 / ~1320 TOPS INT8 | ~4 TB/s |

## 3. 현재 hexa-lang 출발점 (Agent C/E 발견)

**이미 준비된 것**:
- Tensor 타입 완성 (test_tensor 129/129 PASS, mmap_weights, Arc<TensorData> zero-copy)
- matmul: `src/interpreter.rs:4351` cblas_dgemm + rayon 병렬화 (≥64×64)
- @memoize JIT 네이티브 (1126x), @optimize, @fuse 부분, @autograd 부분
- **추론 파이프라인 기 가속 20,000x** (forward 160s → 8ms, 현재 **2 ms/token**)
- mmap weight loader 253ms (목표 <50ms)

**미구현 / 블로커**:
- gguf/safetensors 실 로더 (현재 fake-weight/난수만)
- @sparse / @lowrank / @speculative_decode / @moe_active — 전무
- @memoize_grad, @matmul_fused 본격 구현 없음
- KV-cache 구조화 안 됨
- @simd/@parallel는 파서만, 실제 intrinsic/rayon 연결 부분만

**L0 수정 권한 부여됨** — interpreter.rs/parser.rs/lexer.rs/ir/ 직접 수정 가능.

## 4. 워크로드별 물리한계 계산

### A. 7B int4 추론 (batch 1, 128 tok/s 목표)
- 모델 3.5 GB × token당 1회 읽기
- htz 순수 CPU: `3.5 / 83 = 42 ms/tok` → **물리한계 24 tok/s**
- ubu 5070: `3.5 / 672 = 5.2 ms/tok` → **192 tok/s** (목표 초과)
- vast 4×4090: `3.5 / 4000 = 0.9 ms/tok` → **1100+ tok/s** (오버킬)

**현재 hexa 위치**: 2 ms/token 달성 중 (단, fake weight 기준 + 소형 모델). 실제 7B 실측 필요.

### B. 100M 학습, 1B tokens
- 총 FLOPs = 6 × 1e8 × 1e9 = 6e17
- htz 실효 0.75 TF: `6e17 / 7.5e11 = 8×10⁵ s = 222 h (9.3일)`
- vast 4×4090 실효 132 TF: `6e17 / 1.32e14 = 4545 s ≈ 1.3 h`

### C. 7B 학습 1B tokens (out-of-scope, 참고만)
- 총 FLOPs = 6 × 7e9 × 1e9 = 4.2e19
- htz 실효 0.75 TF: **15,555 h ≈ 1.8년** (현실 불가)
- vast 4×4090 실효 132 TF: `~88 h, $97`

## 5. AI-native 돌파 경로 (attr 조합)

### T1: 7B int4 추론 on htz
| 단계 | attr | 예상 가속 | 누적 tok/s (CPU) |
|---|---|---|---|
| baseline (bandwidth-bound) | - | 1.0× | 24 |
| sparsity 50% | `@sparse(0.5)` | 2.0× | 48 |
| low-rank proj | `@lowrank(r=64)` | 1.5× | 72 |
| speculative decode (draft 모델) | `@speculative_decode` | 2.5× | 180 |
| MoE active 4/8 | `@moe_active(4,8)` | 2.0× | 360 |
| matmul+activation fusion | `@matmul_fused` | 1.3× | 470 |

**T1 목표**: 128 tok/s. **CPU 경로로 이론상 3~4x 여유**.

### T2: 100M 학습 on htz (1B tokens)
| 단계 | attr | 누적 가속 | 누적 시간 |
|---|---|---|---|
| baseline | - | 1× | 222 h |
| @lowrank(32) | LoRA-like | 8× | 28 h |
| @sparse(0.3) | 활성 gradient sparsity | 3× | 9.3 h |
| @memoize_grad | 반복 minibatch 캐시 | 2× | 4.6 h |
| @matmul_fused | backward fusion | 1.5× | 3.1 h |

**T2 목표**: <8h. **이론상 달성 + 여유**.

### T3 (참고): CPU 불가, vast 4×4090 FSDP+bf16 ~40-50h 예상.

## 6. 경계선 지도

```
              ┌─ CPU-only 우세 ─┐─ 혼전 ─┐─ GPU 필수 ─
실시간 루프    │ htz (@memoize)                       │
≤100M 추론    │ htz (@sparse)                         │
7B 추론       │ htz+AI-native 360 tok/s ┃ ubu 192    │
13B 추론      │                         ┃ ubu 한계   │
≤32B 추론     │                                      │ vast 2×4090
100M 학습     │ htz+AI-native 3.1h ★   ┃ vast 1.3h │
7B 학습       │ CPU 불가                              │ vast 88h ★
```

## 7. 검증 루프 (이중)

### Loop A: CPU-only 검증 (htz)
```
baseline.hexa → @attr 단계 적용 → bench → shared/bench/zeta_compare.jsonl
  ↓ 목표 미달
  다음 attr or 신규 primitive 설계
  ↓ 목표 달성
  shared/growth_bus.jsonl 돌파 기록 → nexus blowup 트리거
```

### Loop B: GPU 기준선 (ubu / vast)
```
PyTorch+transformers baseline → hexa @link(FFI) or @fuse → 벤치
  ↓
  Loop A 결과와 비교 → shared/physical_limits.json 경계선 업데이트
```

### 중간 브레이크포인트 (핵심 추가)
```
T1/T2 각 단계 종료 시:
  ├─ CPU 목표 달성 → T ✓, 다음 단계
  ├─ CPU 불가 증명 (bandwidth/FLOPs 상한 수학적 확인)
  │   ├─ GPU 기준선 대비 CPU 현 수준 ≥ 50% → "부분 달성" 기록, 다음 단계
  │   └─ GPU 기준선 대비 CPU 현 수준 < 50% → T 실패, 알고리즘 재설계 루프
```

### 종료 조건
- **T1 ✓ AND T2 ✓** → 특이점 돌파 기록 후 루프 종료
- **T1 부분 + T2 부분** (둘 다 GPU≥50%) → "CPU-GPU 동급" 기록 후 종료
- **어느 하나 실패** (GPU<50%) → 재설계 루프 재진입 (최대 3회)

## 8. 구현 순서 (T1 → T2 순차)

### Phase 1: T1 준비 (gguf + 실측 인프라)
- P1-1. gguf 로더 구현 (self/ml/gguf_loader.hexa) — 7B int4 파일 파싱
- P1-2. KV-cache 구조 정의 + @memoize 연결
- P1-3. htz baseline 측정 (fake weight → 실 7B) → Loop B와 동기 비교
- P1-4. bench harness: shared/bench/zeta_compare.jsonl 자동 기록

### Phase 2: T1 AI-native 적용
- P2-1. `@sparse(ratio)` — matmul 입구점 훅 (interpreter.rs:4351), CSR 경로
- P2-2. `@lowrank(r)` — src/opt/p13_lowrank.rs 신규 pass
- P2-3. `@matmul_fused` — matmul+activation 커널 융합
- P2-4. `@speculative_decode` — draft 모델 이중 실행 + 검증 분기
- P2-5. `@moe_active(k,n)` — 라우터 + 활성 전문가 k 선택
- P2-6. 각 단계 bench → 브레이크포인트 평가 → 루프 판단

### Phase 3: T2 100M 학습
- P3-1. Training loop (forward/backward/optimizer) in self/
- P3-2. `@memoize_grad` — 반복 minibatch gradient 캐시
- P3-3. `@lowrank(r=32)` LoRA-style 파라미터 분해
- P3-4. `@sparse(0.3)` gradient sparsity
- P3-5. 1B token 실 학습 + Loop B vast 1.3h과 비교
- P3-6. 브레이크포인트 평가

### Phase 4: 종료 판정
- T1/T2 각각 ✓/부분/실패 판정
- 성공 시 shared/growth_bus.jsonl 특이점 기록 + nexus blowup 트리거
- 실패 시 재설계 루프 (최대 3회)

## 9. 성공 기준

- **T1 완전**: htz에서 7B int4 실 weight **≥128 tok/s** (batch 1, 품질 손실 <5%)
- **T1 부분**: ≥ubu 5070의 50% (≥96 tok/s)
- **T2 완전**: htz에서 100M 실 학습 1B tokens **<8h**, 수렴 loss 정상
- **T2 부분**: ≥vast 4×4090의 50% 속도 환산 (즉 ≤2.6h 대비 ≤13h)
- **루프 종료**: 두 T 모두 ✓ 또는 부분 달성

## 10. 리스크 & YAGNI

- **L0 수정 허용됨** — interpreter.rs 직접 훅 OK (이전 lockdown 해제)
- **실 7B weight 트래픽**: htz 1Gbps, 3.5GB 다운로드 ~30초 OK
- **vast 비용**: running $1.098/hr = $26/day. 꼭 필요할 때만 사용
- **@speculative_decode 복잡도**: 6개 primitive 중 최난이도 — 뒤로 미룰 것
- **fake weight로 선검증** → 실 weight는 각 phase 마지막에 합류

## 11. Out of Scope

- **T3 7B 학습** (CPU 불가 영역, vast 전용 스트레치 목표)
- 70B+ 모델
- Diffusion / Vision / Audio 모델
- 분산 학습 (단일 호스트만)
- WASM / 플레이그라운드 강화 (T1/T2 이후)
- C codegen 경로 (현재 인터프리터 + Cranelift JIT로 충분)

## 12. 참고 — 기존 진행 시너지 (Agent E)

- **Phase 2 self-hosting 완료**: 파서/타입/인터프리터 포팅 완료 → 복잡 루프 작성 가능
- **bigint Value::BigInt** 완성 → 수치 안정
- **@memoize main merge** 준비 완료 → 학습 반복 캐시 즉시 사용
- **추론 20,000x 가속 이미 달성** → T1 출발점은 baseline 24 tok/s가 아니라 **훨씬 위**일 가능성 있음 (실측 필수)
