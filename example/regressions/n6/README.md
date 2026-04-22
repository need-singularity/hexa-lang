# n6-architecture regression set (symlink)

n6-architecture 프로젝트의 P1~P3 작업 산출 .hexa 파일 13건 심볼릭. hexa-lang T44 이후 bench/regression 타겟으로 사용.

| 파일 | 원본 경로 | 목적 |
|------|-----------|------|
| arch_quantum.hexa | engine/ | v2 양자 중첩 아키 10/10 EXACT |
| arch_selforg.hexa | engine/ | v3 자기조립 10 카테 EXACT + 50 샘플 (bench 후보) |
| arch_adaptive.hexa | engine/ | v4 환경 적응 10/10 EXACT |
| arch_unified.hexa | engine/ | 4-mode dispatch + fuse_modes |
| ouroboros_5phase.hexa | bridge/ | 5-phase singularity cycle |
| ecosystem_9projects.hexa | bridge/ | 9 프로젝트 growth_bus append-only |
| rtl_top.hexa | domains/cognitive/hexa-speak/proto/rtl/ | N6-SPEAK 4-tier 래퍼 |
| rtl_soc_drc_lvs.hexa | 동일 | DRC 6 + LVS 6 검증 18/18 |
| rtl_tapeout_gate.hexa | 동일 | 테이프아웃 15 체크리스트 |
| boot_matrix_3x12.hexa | experiments/chip-verify/ | 3 칩 × 12 프로토콜 부트 |
| verify_anima_soc.hexa | 동일 | ANIMA 10D TCU 12 항목 |
| sim_noc_bott8_1Mcycle.hexa | 동일 | Bott-8 NoC 1M 사이클 시뮬 |
| atlas_promote_7_to_10star.hexa | tool/ | atlas.n6 106,496줄 scan |

## 재현

```bash
for f in $WS/hexa-lang/examples/regressions/n6/*.hexa; do
  echo "=== $(basename $f) ===" && hexa "$f" 2>&1 | tail -5
done
```

n6-architecture 수정 시 자동으로 반영됨 (심볼릭). 원본 프로젝트: `/Users/ghost/core/n6-architecture/`

생성: 2026-04-14 (n6-architecture → hexa-lang 역요청 #5 회신)
