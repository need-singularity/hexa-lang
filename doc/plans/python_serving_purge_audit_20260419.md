// T8c — Python Serving/Inference Purge Audit (ROI #161 follow-on)
// 작성일: 2026-04-19
// 근거: ROI #161 (audit+3 purges 이미 done, build-time 경로 한정). 이 문서는
//       serving/inference runtime 경로에 한정한 심화 감사 + 박멸 plan.

## 1. ROI #161 현황 (doc/roi.json:2890-2909)

- status: done @ 2026-04-19
- 완료 범위: **build-time / grammar count 경로 4건 purge**
  - P1/P2: bin/build.hexa realpath (python3 -c os.path.realpath → realpath_posix)
  - P3/P4: loop-rules.json keywords/operators (python3 -c json.loads → tool/count_grammar_tokens.hexa)
- 보류: doc/emergence_patterns.json:168 (n6-architecture 외부 의존 — 본 repo 밖)
- **미감사 영역: serving / inference runtime 경로** — 본 문서에서 처음 감사.

## 2. Serving/Inference Surface (hexa-lang 내)

| Path | LOC | 역할 | Python 의존 상태 |
|------|----:|------|------------------|
| self/serve/serve_alm.hexa | 1302 | ALM 14B HTTP inference server | **제로** (주석만 "replaces serve_alm_14b.py") |
| self/serve/serve.hexa     | 1089 | 일반 GGUF HTTP inference | **제로** |
| self/serve/http_server.hexa | ~400 | HTTP/1.1 router (net_* builtins) | **제로** |
| self/ml/qwen14b.hexa      |  160 | hxqwen14b FFI skeleton | **제로** (C stub) |
| self/ml/gpu_inference.hexa|  440 | GPU 추론 | **제로** |
| self/ml/generate.hexa     |  216 | sampling loop | **제로** |
| self/ml/lora_serve.hexa   |  317 | LoRA hot-swap | **제로** |
| self/ml/mac_serve.hexa    |  481 | Mac Metal serve | **제로** |
| self/native/hxqwen14b.c   | 1000+ | CUDA/CPU FFI shim | C-only, python 런타임 호출 0 |

**결론: hexa-lang repo 의 serving/inference 실 실행 경로에 `python3` 호출 0.**
`anima/serving/serve_alm_14b.py` 는 이미 `self/serve/serve_alm.hexa` 로 1:1 포팅 완료.

## 3. 주변부 (비-실행) 잔재 — 유지/모니터 대상

### 3a. 주석/문서 참조 (실행 경로 아님)
- serve_alm.hexa:4,24 — "replaces anima/serving/serve_alm_14b.py", "Route parity with Python reference"
- qwen14b.hexa:5-8 — 마이그레이션 컨텍스트 설명
- 수십개 train_*.hexa / tokenizer_bpe.hexa — "Replaces Python X" 주석
- **조치: 유지** (역사적 맥락, 회귀 판단 불필요)

### 3b. RunPod pod 이미지 태그
- tool/build_hxblas_linux.hexa / build_hxlayer_linux.hexa / build_hxccl_linux.hexa / build_hxflash_linux.hexa / build_hxlmhead_linux.hexa / build_hxqwen14b_linux.hexa / deploy_h100.hexa:18
  - `runpod/pytorch:2.4.0` 이미지 참조. 이미지 자체가 pytorch 기반이지만, **hexa 빌드는 apt 의존만 사용** (gcc, libopenblas-dev, libgomp1). pytorch 런타임 호출 경로 0.
  - **조치: 유지** (호스트 OS 이미지 선정, hexa는 무관)

### 3c. hexa_only_lint.hexa:24 — `.py` 확장자 커밋 차단 가드
- **조치: 유지** (신규 .py 유입 차단 = 본 purge 효력 보존)

### 3d. doc/ml_pipeline.json — pytorch_comparison 섹션 + `"python": false`
- 의도적 벤치마크 비교표 (hexa vs pytorch 성능 우위 증명). `zero_dependencies.python=false` = hexa 자체 의존 없음 선언.
- **조치: 유지**

### 3e. tool/config/commands.json:90-99 "lm" 명령
- `"runtime": "Python + PyTorch"` (alm 트랙 설명)
- 단, `scripts: ["self/serve/serve_alm.hexa"]` + `serving: ["self/serve/serve_alm.hexa"]` 로 실제 경로는 이미 hexa
- **불일치 버그**: runtime 필드가 실제 scripts 와 모순. **P6 수정 대상** (아래).

## 4. 박멸 Plan — Phase T8c

### P6 (단일 file edit, ≤5 LOC)
`tool/config/commands.json:90-99` lm 커맨드 runtime 필드 수정:
- `"runtime": "Python + PyTorch"` → `"runtime": "hexa native (/usr/local/bin/hexa) + hxqwen14b CUDA FFI"`
- description `"CLM(hexa, CPU) + ALM(Python, GPU)"` → `"CLM(hexa, CPU) + ALM(hexa+CUDA FFI, GPU)"`
- 근거: scripts 필드가 이미 .hexa 만 가리킴. runtime 설명만 stale.
- 검증: `./hexa tool/count_grammar_tokens.hexa` 류 벤치 영향 없음 (description 필드).

### P7 (보류 — hxqwen14b CUDA v5 의존)
self/native/hxqwen14b.c: `hxqwen14b_generate` 및 LoRA 3 entries 가 `RC_ERR_CUDA_TODO (-5)` 반환.
- 현재 stub → CUDA 5 kernels 착륙 시 serving/training "실구동" 가능.
- ROI #161 scope 밖 (Day-1 CUDA 대작업). 별도 ROI로 승계.
- 상태 추적: self/ml/qwen14b.hexa 주석에 "CUDA v5 pending" 명시.

### P8 (외부 — hexa-lang repo 밖)
- anima/serving/serve_alm_14b.py — hexa-lang 에는 포팅 완료 파일만 존재. anima repo 측에서 기존 .py 파일 삭제 + systemd unit의 `python serve_alm_14b.py` → `hexa self/serve/serve_alm.hexa` 교체 필요.
- hexa-lang 단독으로는 영향 0 (self-host surface 이미 깨끗).

## 5. 완료 기준

- hexa-lang serving/inference 실행 경로 python3 호출 수 = **0** (이미 충족)
- hexa-lang serving/inference import/FFI 경로 torch/transformers/peft/vllm 의존 = **0** (이미 충족)
- 주변부 stale 메타데이터 1건 수정 (P6) = **이번 T8c 유닛**
- P7/P8 은 별도 ROI 승계 (hexa-lang 외 의존)

## 6. Metrics (audit snapshot 2026-04-19)

- serving .hexa 파일 수: 3 (self/serve/)
- inference .hexa 파일 수: ~15 (generate, gpu_*, mac_*, lora_serve, batch_inference, streaming, continuous_batching, paged_attention, kv_cache, prefix_cache, flash_*, ring_*, speculative_*, medusa, eagle_*)
- python3 실행 호출: **0**
- torch/transformers/peft import: **0**
- CUDA FFI stub 반환 -5 (pending kernels): 4 entries (generate, fwd_with_lora, bwd_lora_only, apply_lora_delta)
- Runpod pytorch image tag 참조 (빌드 환경, 실행 경로 아님): 7 files
- 주석상 python 참조 (역사/마이그레이션 메모): ~40 lines

## 7. 결론

ROI #161 의 공식 완료 범위(build+grammar)는 이미 2026-04-19 에 done.
**serving/inference 런타임 경로도 _이미_ python 의존 0.** T8c 는 audit 로 이 상태를 확인하고,
딱 1건의 stale 메타데이터(commands.json lm.runtime 필드)를 P6 로 정리한다.
P7(hxqwen14b CUDA) / P8(anima 측 .py 삭제)은 hexa-lang 단독 범위 밖 → 별도 ROI 승계.
