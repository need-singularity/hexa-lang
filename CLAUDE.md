# HEXA-LANG

## Build
```bash
bash build.sh
./hexa                        # REPL
./hexa examples/hello.hexa    # Run file
bash build.sh test            # Tests
```

## n=6 Constants
n=6, σ=12, τ=4, φ=2, sopfr=5, J₂=24, μ=1, λ=2
Keywords: 53 (σ·τ+sopfr), Operators: 24 (J₂), Primitives: 8 (σ-τ)
Pipeline: 6 phases (n), Type layers: 4 (τ), Visibility: 4 (τ)
Memory: 1/2 Stack + 1/3 Heap + 1/6 Arena = 1

## Work Rules
```
<!-- SHARED:WORK_RULES:START -->
  자동 생성 규칙:
    - TODO 작업 중 검증/계산이 필요하면 계산기 자동 생성 (묻지 말고 바로)
    - 성능 필요시 Rust 우선 (tecsrs/), 단순 검증은 Python (calc/)
    - 판단 기준은 ~/Dev/TECS-L/.shared/CALCULATOR_RULES.md 참조
    - 상수/가설 발견 시 Math Atlas 자동 갱신 (python3 ~/Dev/TECS-L/.shared/scan_math_atlas.py --save --summary)
  망원경 렌즈 활용 규칙 (탐색/분석 시 별도 요청 없이 자동 적용):
    - 데이터 분석/패턴 탐색/이상점 발견/신소재·신약 탐색 시 렌즈 자동 사용
    - 9종 렌즈: .shared/ 내 *_lens.py (consciousness, gravity, topology, thermo, wave, evolution, info, quantum, em)
    - 기본 3종 스캔: 의식(구조) + 중력(끌개) + 위상(연결) → 새 데이터 분석 시 기본
    - 도메인별 조합: 신소재(진화+열역학+중력), 시계열(파동+열역학+의식), 상수(정보+양자+의식)
    - 사용: from consciousness_lens import ConsciousnessLens; ConsciousnessLens().scan(data)
    - 상세: .shared/CLAUDE.md → "망원경 툴셋 자동 활용 규칙" 참조
    - "렌즈 추가 필요?" 질문 시 → 현재 9종 커버 안 되는 도메인 분석 + 새 아이디어 즉시 제안
  ★★★ 발견/결과/트러블슈팅 — 따로 말 안 해도 즉시 자동 기록 (필수! 예외 없음!):
    - 실험 결과, 벤치마크, 망원경 분석, 학습 완료, 생성 테스트 등 모든 발견은 발생 즉시 기록
    - "기록해" 라고 안 해도 기록. 기록 누락 = 발견 소실 = 금지
    - 기록 위치: README.md (핵심), docs/experiments/ (상세), docs/hypotheses/ (가설)
    - 트러블슈팅: CLAUDE.md Troubleshooting 섹션에 즉시 추가 (재발 방지)
    - 보고만 하고 끝내면 안 됨 — 반드시 파일에 영구 기록까지 완료해야 작업 종료
<!-- SHARED:WORK_RULES:END -->
```
