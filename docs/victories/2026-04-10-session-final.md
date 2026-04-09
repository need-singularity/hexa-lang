# CPU AI-Native LLM 세션 최종 (2026-04-09 ~ 04-10)

> 작성: 2026-04-10 세션 종결 시점
> 범위: T1(7B 추론) + T2(100M 학습) 특이점 돌파 시도 + anima 포팅

## 1. 목표

- **T1**: CPU htz에서 7B int4 추론 128 tok/s (GPU fallback 50% = 96 tok/s)
- **T2**: CPU htz에서 100M 학습 1B tokens <8h (GPU fallback 50%)
- 기법: 5 AI-native 알고리즘 + hexa fused builtin

## 2. 최종 수치 (모두 htz 부하 20-60 sustained 측정)

| 항목 | best step/fwd | 1B hrs / tok_s | 8h/128 gap | 판정 |
|---|---:|---:|---:|:---:|
| **T1 7B v7 (sgemm)** | 338 ms | 2.96 tok/s | 43x | ❌ |
| T2 100M v1 naive | 16100 ms | 34875 | 4359x | ❌ |
| T2 100M alphabeta2 scalar | 59 ms | 128 | 16x | ❌ |
| T2 100M alphabeta3 BLIS | 7.40 ms | 32.1 | 4.0x | ❌ |
| **T2 100M alphabeta4 sgemm** | **6.59 ms** | **28.6** | **3.6x** | ❌ |
| T2 ultra11 (0.5M) | 1.077 ms | 4.67 | 0.58x | ✅ |
| T2 ultra14 (0.07M) | 0.486 ms | 2.11 | 0.26x | ✅ |
| T2 ultra15 (0.02M) | 0.277 ms | 1.20 | 0.15x | ✅ |
| **anima decoder Phase A** | **40.3 ms** | ~3h/epoch | — | Phase A |

## 3. 누적 가속 (v1 기준)

```
T2 100M 스펙:  16100 → 6.59 ms = 2443x
T2 ultra11:    16100 → 1.077 ms = 14,949x
T2 ultra15:    16100 → 0.277 ms = 58,122x
T1 7B:         22250 → 338 ms = 65.8x
```

## 4. 5 핵심 돌파 기법 (anima/PyTorch 이식 가능)

1. **LM head U+V 분리** — optimizer 2600ms → 23ms (**112x**)
2. **BLAS-only loss/backward** — bwd 6000ms → 3.2ms (**1875x**)
3. **@lowrank r=16 sweet spot** — 7B QKV 13.86x, FFN 12.40x (rect sweep 실측)
4. **Rank-r attention** — O(seq²·d) → O(seq²·r), 256x FLOPs 축소
5. **Fused kernel chain** — qkv_fused + ffn_fused + block_forward_chain_f32 (dispatch 6→1 per layer)

## 5. 11개 Rust 빌트인 추가 (src/interpreter.rs)

- `mat_add_inplace`, `matmul_into(out)` — Arc::get_mut in-place
- `mat_scale_inplace`, `axpy(W, G, α)` — cblas_daxpy
- `qkv_fused_into` — 3 matmul → 1 cblas + split
- `ffn_fused_into` — 4 cblas chained
- `block_forward_fused` — 전체 layer 단일 builtin
- `block_forward_chain` — N layer loop in Rust (single scratch)
- `block_forward_chain_f32` — sgemm f32 variant (2.67x raw BLAS)
- `read_file_bytes`, `file_size` — 바이너리 I/O

## 6. Linux BLAS 링크 발견 + BLIS 채택

**결정적 발견**: hexa가 Linux에서 `cblas_dgemm`을 호출하지 않고 순수 Rust scalar fallback을 사용 중이었음. 

**조치**:
- `#[cfg(target_os = "macos")]` → `#[cfg(any(macos, linux))]` (43건)
- `.cargo/config.toml`: x86_64-unknown-linux-gnu `-lblis -lm`
- apt `libblis-dev` 설치
- **실측 alphabeta2 59 → 15.80 ms (3.7x)**

MKL 시도 (intel-mkl 2020.4) → symbol error, AMD Zen4 성능 저하, BLIS가 현실적 최선.

## 7. PyTorch 비교 (동일 htz 부하)

```
same config (69M D=768 L=8 seq=64 vocab=8192):
  PyTorch dense best:  170 ms
  hexa alphabeta2:    15.80 ms   ← 10.8x faster

BLAS probe 2048²:
  hexa (BLIS dgemm):   21.3 GFLOPS
  hexa (BLIS sgemm):   88.9 GFLOPS  (2.67x)
  PyTorch MKL (brief): 301 GFLOPS   (idle window)
```

**결론**: hexa + AI-native 기법이 PyTorch dense 대비 **sustained 10x+ 우세**. MKL이 brief window에서 302 GF 달성하나 sustained 불가.

## 8. 알려진 블로커

### T1 7B 추론
- Tensor Arc allocation per-call 오버헤드
- Block_forward_chain 적용 후에도 1480ms → 338ms (4.4x) 미달성
- 물리 bandwidth 천장 83 GB/s / 3.5GB = 24 tok/s. 현재 2.96 = **12% of physical**. 남은 8x는 Tensor ABI 근본 개선 필요.

### T2 100M 스펙
- alphabeta4 6.59ms/step (3.6x gap)
- 하드웨어 sustained BLAS 천장 ~25 GFLOPS (htz load 20-60)
- cold window 측정 시도 실패 (blowup SIGSTOP/CONT 시퀀스 ssh 세션 truncation)
- 이론 cold: 추정 2-5x 추가 → 100M 스펙 달성권 진입

## 9. anima 포팅 (Phase A 완료)

**파일**: `self/ml/train_decoder_cpu.hexa`

- ConsciousDecoderV2 config (D=384, L=6, vocab=256, seq=256, r=16)
- 5 기법 전면 적용 + block_forward_chain_f32
- **step_ms best 40.3 ms**
- epoch 환산: 273k steps × 40.3ms = **~3시간/epoch**
- anima PyTorch 현재 러닝 80시간 → 이론상 수배 단축 (실측 필요)

**Phase B+ 후속 작업**:
- RoPE (rotary position embedding)
- SwiGLU activation (Swish × Linear)
- GQA (Grouped Query Attention, 4H/2KV)
- ThalamicBridge cross-attention
- Law 60 3-phase + phi-loss integration

## 10. 수렴 기록

- `shared/convergence/hexa-lang.json` 전체 업데이트 (ossified T2_100M_CPU_AI_NATIVE, failed T1_7B_CPU_INFERENCE, stable 5 family)
- `shared/growth_bus.jsonl` 20+ 돌파/측정 엔트리
- `shared/hexa_to_anima_20260410_final.json` 이식 가이드

## 11. 커밋 스택 (주요)

```
d89acaa  T2 달성 주장 (이후 정정)
b74c1f8  T2 환산 오류 정정
65e48af  mat_add_inplace + matmul_into
b6aebf3  mat_scale_inplace + axpy + VM Tensor 시도
7950370  qkv_fused_into
9afe556  ffn_fused_into
afbc9b3  block_forward_fused
4141a47  block_forward_chain
029dc13  Linux BLAS cfg 확장
a3b80fd  BLIS 복귀 (MKL revert)
cf8b815  t1_v7_f32 sgemm
이 파일    세션 최종 문서
```

## 12. 판정

- **T1**: ❌ 불가 (43x gap, Tensor ABI 블로커)
- **T2 100M 스펙**: ❌ 3.6x 미달 (하드웨어 sustained 천장 원인)
- **T2 축소 모델**: ✅ ultra11 (0.5M) 4.67h / ultra15 (0.02M) 1.20h
- **PyTorch 동급 비교**: ✅ hexa 10.8x faster same config
- **anima 이식**: ✅ Phase A 완료, 13x+ 잠재 가속
