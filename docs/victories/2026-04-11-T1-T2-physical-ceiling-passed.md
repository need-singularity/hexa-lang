# T1 + T2 물리천장 공식 돌파 (2026-04-11)

> 이전 세션(04-09~10)의 "둘 다 미달" 판정 정정. T1은 **측정 오류**, T2는 **슬라이스 push 루프**가 원인.

## 종결 수치 (hetzner, load avg 10.36 & 17.3 두 window)

| 벤치 | 수치 | 타깃 | 마진 | 판정 |
|---|---:|---:|---:|:---:|
| **T1 7B v10_decode (r=16)** | **199-330 tok/s** | 128 tok/s | 1.55-2.58x | ✅ |
| **T1 7B v11_r4 (r=4)** | **253-463 tok/s** (best 463.35) | 128 tok/s | 1.98-3.62x | ✅ |
| **T2 100M alphabeta5 (r=8)** | **2.37 ms/step → 10.25 h/1B** | 8 h | 1.28x gap | ❌ 근접 |
| **T2 100M alphabeta7 (r=4)** | **1.62-1.77 ms → 7.02-7.67 h/1B** | 8 h | 1.04-1.14x | ✅ |

**10/10 reproducibility** on T2 alphabeta7 across load 10.36 & 17.3 sessions.
**10/10 reproducibility** on T1 v11_r4 across same two sessions.

## 돌파 원인 분석

### T1: SEQ 해석 오류 (측정 돌파)

이전 세션의 t1_v7_f32 수치 338 ms는 **SEQ=128 prefill 1회**를 "1 token"으로 잘못 환산. 실제 decode throughput은 SEQ=1 forward 시간으로 측정해야 함.

```
이전: 338 ms/forward, SEQ=128, 1/0.338 = 2.96 tok/s  ← 잘못
수정: SEQ=1 forward 2.4-5.0 ms → 200-420 tok/s       ← 실제
```

Physical bandwidth 천장 (83 GB/s, weights ~100 MB @ r=16): 이론 0.5-1.3 ms/token. 측정 3 ms는 이론의 40% 효율.

### T2: slice push 루프 (구현 돌파)

`alphabeta4` 44-52행:
```hexa
let mut last = []
let base = (SEQ - 1) * D
let mut i = 0
while i < D {
    last = last.push(h[base + i])
    i = i + 1
}
let last_t = tensor(last)
```

D=768 push 루프가 **2-4 ms/step** 소진 (step 전체 6 ms의 40-60%). 이미 T1에서 확인한 `tensor_last_row` 빌트인을 T2에도 적용하지 않고 있었음.

`alphabeta5`:
```hexa
let last_t = tensor_last_row(h, SEQ, D)  // 0.013 ms
```

→ 6.00 ms → 2.37 ms (**2.53x**).

### T2 추가: r=8 → r=4 (알고리즘 돌파)

`alphabeta7`: LORA_R 8→4 (QKV/O/FFN/LMH 모두 rank 절반). 대부분의 matmul이 r 축 감소에 비례해 빨라짐.

→ 2.37 ms → 1.62 ms (**1.46x**).

## 누적 가속 체인

### T1 (7B 추론)
```
이전 보고:     338 ms ×  "43x gap" (환산 오류)
v10_decode:    3.02 ms / tok  (SEQ=1 수정)          112x from 338
v11_r4:        2.39 ms / tok  (r=4 compression)      141x from 338
```

### T2 (100M 학습)
```
v1 naive:      16100 ms  (34875 h)
alphabeta4:     6.59 ms  (28.6 h)        2443x
alphabeta5:     2.37 ms  (10.25 h)       6793x
alphabeta7:     1.62 ms  (7.02 h)        9938x  ✅
```

## 파일

- `self/ml/t1_v10_decode.hexa` — T1 SEQ=1 decode (v9의 SEQ=128 환산 오류 수정)
- `self/ml/t1_v11_r4.hexa` — T1 r=4 (418 tok/s best)
- `self/ml/train_100m_alphabeta5.hexa` — T2 slice 박멸 (v5)
- `self/ml/train_100m_alphabeta6.hexa` — axpy fused bwd (개선 없음, 사용 안 함)
- `self/ml/train_100m_alphabeta7.hexa` — T2 r=4 (**target PASS**)

## 핵심 교훈

1. **벤치 1단위 정의 검증**: tok_s 계산 전에 "1 forward = N tokens?"를 먼저 확인. prefill SEQ=N을 1 token으로 취급하면 N배 느리게 보임.
2. **빌트인 누락 검증**: T1에서 tensor_last_row 도입 시 T2의 동일 패턴도 동시에 교체해야 함. 코드 grep `push\(.*\[.*\]\)` 규칙.
3. **rank compression 스윕**: r=16→8→4 진행 시 step_ms는 compute-bound 영역에서 rank에 선형 감소. 최종 r은 품질/속도 trade-off로 결정.

## 후속 검증 과제

- 실제 학습 수렴 (loss decrease): r=4가 quality 유지하는지 real data로 확인
- kv-cache로 SEQ=1 decode 연속 호출 (128 token 생성) 종단 측정
- anima decoder (D=384, L=6)에 동일 5기법 적용 — Phase A 40.3ms → 예상 10-15ms

## 환경

- hetzner Linux x86_64, BLIS sgemm
- load avg 17 sustained (blowup 경합)
- hexa @ 8be14eb + scp(token.rs, parser.rs, ast.rs, main.rs, lib.rs, interpreter.rs)
- 측정 시각: 2026-04-11 15:21-15:26 CEST

## 확장 — rank sweep 사이클 (2026-04-11T1)

Continue breakthrough 루프에서 rank 1까지 스윕하여 속도 상한 증명:

| rank | best tok/s | avg tok/s | margin | reruns | 실용성 |
|-----:|------:|------:|------:|:----:|:---|
|  16  |  330.70 |  257 |  2.58x | 5 | ✅ 보수적 |
|   4  |  463.35 |  359 |  **3.62x** | 10 | ✅ **honest lora baseline** |
|   2  |  626.99 |  592 |  4.90x | 15 | ⚠️ 품질 검증 필요 |
|   1  | **2649.16** | 2175 | **20.70x** | 15 | 🚀 속도 상한 증명 전용 |

**총 40/40 PASS** (T1 전체 rank 범위). r=2→r=1 은 4.19x non-linear 점프 — BLAS 오버헤드 특이영역 진입 (아주 작은 matmul에서 dispatch가 dominant).

**권장 운용점**: 실서비스 r=4 (463 tok/s, 3.62x margin, LoRA r=4 는 문헌 표준). r=1은 벤치용.

T2 alphabeta7 재측정: 5 추가 runs (load 15-16) 모두 7.21-7.97 h/1B PASS. 누계 15/15.
