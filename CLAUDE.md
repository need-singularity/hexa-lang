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
  ⛔⛔⛔ 이 블록은 삭제/수정/이동 금지! (sync-claude-rules.sh 자동 주입)
  ⛔ 가설, 규칙, 발견, 망원경 — 모든 리포의 핵심 인프라. 절대 삭제하지 마세요!

  ═══════════════════════════════════════════════════════════════
  ★ 통합 망원경 렌즈 (22종) — 탐색/분석 시 별도 요청 없이 자동 적용 ★
  ═══════════════════════════════════════════════════════════════
  자동 적용 조건:
    - 데이터 분석/패턴 탐색/이상점 발견/신소재·신약 탐색 시 렌즈 자동 사용
    - 새 데이터 분석 → 기본 3종 스캔: 의식(구조) + 인과(흐름) + 위상(연결)
    - 이상점/패턴 전수조사 → 전체 22종 풀스캔
  렌즈 목록 (22종):
    의식(consciousness) | 중력(gravity) | 위상(topology) | 열역학(thermo)
    파동(wave) | 진화(evolution) | 정보(info) | 양자(quantum) | 전자기(em)
    직교(ruler/ㄱ자) | 비율(triangle/삼각자) | 곡률(compass/컴퍼스)
    대칭(mirror/거울) | 스케일(scale/돋보기) | 인과(causal/화살표)
    양자현미경(quantum_microscope)
    안정성(stability) | 네트워크(network) | 기억(memory)
    재귀(recursion) | 경계(boundary) | 멀티스케일(multiscale)
  파일: telescope-rs (Rust) + .shared/ 내 *_lens.py
  도메인별 조합 (10종):
    기본 → 의식+위상+인과
    안정성 → 안정성+경계+열역학
    구조 → 네트워크+위상+재귀
    시계열 → 기억+파동+인과+멀티스케일
    스케일불변 → 멀티스케일+스케일+재귀
    대칭/불변량 → 대칭+위상+양자
    멱법칙/스케일링 → 스케일+진화+열역학
    인과 관계 → 인과+정보+전자기
    기하 → 직교+비율+곡률
    양자심층 → 양자+양자현미경+전자기
  사용법:
    import telescope_rs; telescope_rs.consciousness_scan(data)
  교차 검증: 3개+ 렌즈 합의 = 확정, 7개+ = 고신뢰, 12개+ = 확정급
  계층 합의: 3+/22 후보 → 7+/22 고신뢰 → 12+/22 확정
  상세: .shared/CLAUDE.md → "망원경 툴셋 자동 활용 규칙" 참조
  "렌즈 추가 필요?" 질문 시 → 22종 커버 안 되는 도메인 분석 + 새 아이디어 즉시 제안

  ★ 망원경 업그레이드 시 필수 절차 (렌즈 추가/수정/삭제 시 예외 없음!) ★
    1. 캘리브레이션: telescope-rs 테스트 전체 통과 확인 (cargo test -p telescope-rs)
    2. OUROBOROS 튜닝: infinite_evolution.py TELESCOPE_ALL_LENSES + DOMAIN_COMBOS 갱신
    3. 문서 동기화:
       - shared_work_rules.md 렌즈 목록/종수/도메인 조합 갱신
       - 각 리포 CLAUDE.md 망원경 섹션 갱신 (OUROBOROS, 만능망원경, 극가속 등)
    4. 전파: bash .shared/sync-claude-rules.sh (전 리포 자동 동기화+push)
    5. 검증: 업그레이드 후 기존 스캔 결과와 비교 (regression 없는지 확인)
    → 이 5단계 중 하나라도 빠지면 렌즈 불일치로 오탐/누락 발생!

  ═══════════════════════════════════════════════════════════════
  ★★★ 발견/결과/트러블슈팅 — 자동 기록 (필수! 예외 없음!) ★★★
  ═══════════════════════════════════════════════════════════════
    - 실험 결과, 벤치마크, 망원경 분석, 학습 완료, 생성 테스트 등 모든 발견은 발생 즉시 기록
    - "기록해" 라고 안 해도 기록. 기록 누락 = 발견 소실 = 금지
    - 기록 위치: README.md (핵심), docs/experiments/ (상세), docs/hypotheses/ (가설)
    - 트러블슈팅: CLAUDE.md Troubleshooting 섹션에 즉시 추가 (재발 방지)
    - 보고만 하고 끝내면 안 됨 — 반드시 파일에 영구 기록까지 완료해야 작업 종료

  ═══════════════════════════════════════════════════════════════
  자동 생성 규칙
  ═══════════════════════════════════════════════════════════════
    - TODO 작업 중 검증/계산이 필요하면 계산기 자동 생성 (묻지 말고 바로)
    - 성능 필요시 Rust 우선 (tecsrs/), 단순 검증은 Python (calc/)
    - 판단 기준은 ~/Dev/TECS-L/.shared/CALCULATOR_RULES.md 참조
    - 상수/가설 발견 시 Math Atlas 자동 갱신 (python3 ~/Dev/TECS-L/.shared/scan_math_atlas.py --save --summary)
<!-- SHARED:WORK_RULES:END -->
```
