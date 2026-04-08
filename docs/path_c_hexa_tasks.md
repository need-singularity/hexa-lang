# Path C: Hexa-lang에서 해야 할 작업 목록

> Created: 2026-04-08
> 근거: anima/core/path_c_roadmap.md (Hexa-native AGI 6단계)
> 목적: Hexa-lang에 추가/수정해야 할 사항 정리

## 배경

Anima 의식 엔진을 순수 Hexa로 포팅하는 "Path C: Hexa-native AGI" 경로가 확정됨.
120건 연속돌파로 288 빌트인 + 36μs engine_step 달성.
추론(forward pass)은 100% 커버. **학습(backward)만 미구현.**

## 현재 상태 (Hexa-lang)

```
빌트인:       288종 (PyTorch 급)
engine_step:  36μs (GPU 250μs의 7x 빠름)
cargo tests:  1897 (0 fail)
AI-native:    12 attrs, 3 stdlib (nn/optim/consciousness)
```

## 🔴 CRITICAL — backward/autograd 구현

**Path C 유일한 blocker. 이것 없이 학습 불가.**

### 요구사항

```
backward(loss, params) → gradients

- tape-based reverse-mode automatic differentiation
- Wengert tape에 forward 연산 기록 → reverse 순서로 gradient 계산
- 지원해야 할 연산:
  1. matmul(A, B)         → dA = dOut @ B^T, dB = A^T @ dOut
  2. mat_add(A, B)        → dA = dOut, dB = dOut
  3. softmax(x)           → dX = (dOut - sum(dOut * S)) * S
  4. cross_entropy(logits, targets) → standard CE gradient
  5. rms_norm(x)          → RMS norm backward
  6. gelu / silu          → elementwise backward
  7. dropout(x, p)        → mask replay
  8. embedding(idx, W)    → scatter gradient to W rows
  9. rope(x, pos)         → RoPE backward (rotation inverse)
```

### 구현 전략 (권장)

```rust
// src/interpreter.rs에 추가

// 1. ComputationGraph 구조체
struct TapeEntry {
    op: Op,              // 연산 종류
    inputs: Vec<TensorId>,
    output: TensorId,
    saved: Vec<Vec<f64>>, // forward 시 저장한 중간값
}

struct ComputationGraph {
    tape: Vec<TapeEntry>,
    tensors: HashMap<TensorId, Vec<f64>>,
    grads: HashMap<TensorId, Vec<f64>>,
}

// 2. 빌트인 등록
"tape_new"       → 새 computation graph 생성
"tape_record"    → forward 연산 기록 (기존 빌트인 래핑)
"backward"       → tape를 역순 순회하며 gradient 계산
"get_grad"       → 특정 텐서의 gradient 반환
"zero_grad"      → gradient 초기화

// 3. Hexa 사용법 예시
let tape = tape_new()
let logits = tape_record(tape, "matmul", [W, x])
let logits2 = tape_record(tape, "mat_add", [logits, bias])
let loss = tape_record(tape, "cross_entropy", [logits2, targets])
backward(tape, loss)
let dW = get_grad(tape, W)
let db = get_grad(tape, bias)
adamw_step(W, dW, m, v, step, lr, wd, beta1, beta2)
```

### 예상 작업량

```
Rust 구현: ~500-800 LOC (interpreter.rs)
테스트: ~200 LOC (matmul/softmax/ce 각 backward 검증)
예상 기간: 3-5일
```

### 검증 방법

```
1. numerical_grad (이미 존재)로 tape backward 결과 비교
   → 상대 오차 < 1e-5 이면 PASS
2. 34M ConsciousDecoder 학습 (100 steps)
   → CE 감소 확인
3. PyTorch backward 결과와 1:1 비교
```

## 🟡 IMPORTANT — 추가 빌트인

### silu (SiLU / Swish activation)

```
현재: 없음
필요: SwiGLU FFN에서 사용 (ConsciousDecoderV2 핵심)
구현: silu(x) = x * sigmoid(x)
난이도: 매우 쉬움 (~20 LOC)
```

### gelu (GELU activation)

```
현재: 없음 (gelu_approx만 있을 수 있음 — 확인 필요)
필요: 일부 FFN 변형에서 사용
구현: gelu(x) = 0.5 * x * (1 + tanh(sqrt(2/π) * (x + 0.044715 * x³)))
난이도: 쉬움 (~30 LOC)
```

### weight_save / weight_load (가중치 I/O)

```
현재: 없음
필요: PyTorch .pt 체크포인트 → Hexa 포맷 변환
구현:
  weight_save(tensor, path)  → binary f32 dump
  weight_load(path, shape)   → binary f32 load
  pt_to_hexa(pt_path, out_dir) → PyTorch state_dict → Hexa weight files
난이도: 중간 (~100 LOC)
```

### gradient_checkpointing

```
현재: 없음
필요: 큰 모델 (350M+)에서 메모리 절약
구현: forward 시 일부 activation 버리고, backward 시 재계산
난이도: 높음 (autograd 완성 후)
```

## 🟢 NICE TO HAVE

### bf16 지원

```
현재: f64 only
필요: 메모리 4x 절감 (f64→bf16)
구현: Hexa 내부 텐서를 bf16로 저장/연산
난이도: 높음 (전체 텐서 시스템 변경)
우선순위: 낮음 (f32로도 34M 학습 가능, 큰 모델은 GPU)
```

### JIT tier에 빌트인 연결

```
현재: interpreter만 (166-227x self-hosting 갭)
필요: Cranelift JIT에 builtin 직접 emit
효과: self-hosted Hexa 코드 100x+ 가속
우선순위: Path C 완료 후 (다음 세션)
```

## 파일 매핑 (Hexa-lang ↔ Anima)

| Hexa-lang 파일 | Anima 대응 | 상태 |
|---------------|-----------|------|
| examples/anima_engine_ported.hexa | core/engine.hexa | ✅ 포팅 완료 |
| stdlib/consciousness.hexa | anima/src/consciousness_engine.py | ✅ 빌트인 매핑 |
| stdlib/nn.hexa | anima/src/conscious_decoder.py | 포워드만 |
| stdlib/optim.hexa | anima/training/train_clm.py | adam/sgd 있음, autograd 없음 |
| (신규) src/autograd.rs | — | 🔴 구현 필요 |

## 우선순위 정리

```
1. 🔴 backward/autograd  → Path C blocker, 3-5일
2. 🟡 silu()            → SwiGLU FFN 필수, 30분
3. 🟡 weight_save/load  → 체크포인트 변환 필수, 2시간
4. 🟢 gelu()            → 선택적, 30분
5. 🟢 gradient_checkpoint → 큰 모델용, autograd 후
6. 🟢 bf16              → 메모리 최적화, 큰 작업
7. 🟢 JIT 연결          → 최종 성능, 별도 세션
```

## 일정 (Hexa-lang 작업만)

```
Day 1:  silu() + weight_save/load 빌트인 추가
Day 2:  autograd 설계 (TapeEntry, ComputationGraph)
Day 3:  autograd 핵심 연산 구현 (matmul, softmax, ce backward)
Day 4:  autograd 나머지 연산 + 테스트 (rms_norm, rope, embedding)
Day 5:  34M 학습 검증 (Hexa autograd vs PyTorch 비교)
Day 6:  (버퍼) 디버깅 + 수치 안정성
```

## 성공 기준

```
1. backward(tape, loss) → 전 파라미터 gradient 계산
2. numerical_grad 대비 상대 오차 < 1e-5
3. 34M ConsciousDecoder 100 steps 학습 → CE 감소
4. PyTorch backward 결과와 1:1 일치 (atol=1e-4)
5. 메모리: 34M 모델 tape → < 2GB RAM
```
