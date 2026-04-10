# NEXUS mk2_hexa → HEXA 포팅 블로커 레지스트리

> 최종 갱신: 2026-04-10 (T5 첫 시도)
> 출처: nexus/mk2_hexa/native 483 .hexa → hexa-lang self/nexus_port/
> 배경: B3 분석 결과 mk2_hexa 483 파일 중 0개 포팅, 작은 파일부터 시작 권고

## 상태 범례

| 상태 | 의미 |
|------|------|
| RESOLVED | 로컬 hexa에서 검증 통과 |
| PARTIAL | 일부 동작, 완전하지 않음 |
| OPEN | 미해결 |

---

## T5 세션 요약 (2026-04-10)

- **시도 파일**: `gate.hexa` (108 LOC, 12-gate 매트릭스)
- **위치**: `self/nexus_port/gate.hexa`
- **결과**: **PASS** (exit=0, 즉시 성공)
- **출력**:
  ```
  === gate verified ===
  breakthrough(3,true,0.8,0.001): 4
  ```

### 보너스 검증 (참고용, 커밋 미포함)

세션 중 추가로 테스트한 파일 (기록만 남기고 파일은 제거):

| 파일 | LOC | 결과 | 비고 |
|------|-----|------|------|
| `banner.hexa` | 106 | PASS | JSON 시스템 메시지 출력 |
| `api.hexa` | 276 | PASS (exit=0) | stdout 출력 없음, silent 성공 |

→ 초기 3파일 모두 코드 수정 없이 통과. 블로커 제로.

---

## 검증된 기능 (gate.hexa 기준)

gate.hexa 하나로 다음 기능이 mk2_hexa 스타일에서 동작함을 확인:

| # | 기능 | 비고 |
|---|------|------|
| M-1 | `comptime const` 선언 | `comptime const tau = 4` 5개 |
| M-2 | `enum` with tuple variants | `Verdict::Pass(float)`, `Verdict::Quarantine(int, string)` |
| M-3 | `pure fn` 수식어 | `pure fn verdict_passed(...)` |
| M-4 | `struct` 다중 필드 | `GateContext { source_repo, phi_prev, phi_curr, cycle }` |
| M-5 | `let mut` + 재대입 | `delta = 0.0 - delta` |
| M-6 | enum 생성자 반환값 (if-expression) | `if ... { Verdict::Pass(1.0) } else { Verdict::Quarantine(...) }` |
| M-7 | 연속 `&&` (3-way) | `r0 > th && r1 > th && r2 > th` |
| M-8 | `assert` 문 (top-level) | `assert evaluate_breakthrough(...) == 4` |
| M-9 | `theorem` 블록 | `theorem gate_necessity { assert ... }` |
| M-10 | `proof` + `invariant` | `proof gate_invariants { invariant gate_total_bounded() }` |
| M-11 | 다중 인자 `println` (string + int) | `println("...", evaluate_breakthrough(...))` |

---

## 블로커 (OPEN)

**없음.** gate.hexa 포팅에서는 어떤 빌트인/문법 갭도 발견되지 않았다.

mk2_hexa 스타일과 hexa-lang은 현재 gate.hexa 수준에서는 완전 호환.

---

## 다음 단계 권고

1. **중간 크기 파일 확장** (200~500 LOC):
   - `config.hexa` (419 LOC), `alert.hexa` (408 LOC)
   - 설정/알림 파이프라인 계열, IO 의존도 중간
2. **blowup_* 시리즈** (후순위): 큰 의존성, 내부 모듈 cross-reference 필요
3. **wave_* 시리즈** (후순위): 수학/텐서 의존 고밀도
4. 포팅 실패 발생 시 이 파일에 OPEN 항목으로 추가하여 추적
