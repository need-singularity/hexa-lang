# hexa test — 네이티브 테스트 러너

**요약**: `@test` attr 이 붙은 fn 을 자동 수집·실행하는 built-in 러너.
`hexa test <file>` 한 줄로 PASS/FAIL/SKIP 리포트를 받는다.

**도입**: 2026-04-18 (anima 15+ `_test.hexa` 의 ad-hoc `--selftest` harness 대체)
**SSOT**:
- `self/attrs/test.hexa` — `@test` attr meta + `collect_test_fns` helper
- `self/test_runner.hexa` — 수집·spawn·집계 로직
- `self/main.hexa` — `sub == "test"` 디스패처

---

## 빠른 시작

```hexa
// my_suite.hexa
@test
fn test_add() {
    assert(1 + 1 == 2)
}

@test
fn t_string_prefix() {
    assert("hello".starts_with("hel"))
}

@test @skip
fn t_pending() {
    assert(false)  // 구현 전 — 러너가 skip 으로 리포트
}
```

```bash
$ hexa test my_suite.hexa
── hexa test: my_suite.hexa (3 tests) ──
  PASS  test_add
  PASS  t_string_prefix
  SKIP  t_pending

── summary ──
  PASS : 2
  FAIL : 0
  SKIP : 1
  total: 3   in 0.120s
```

**exit code**: `0` = 모두 PASS, `1` = 1개 이상 FAIL, `2` = 사용자 입력 오류.

---

## `@test` 컨벤션

| 항목 | 규칙 |
| --- | --- |
| attr | `@test` |
| fn 이름 | `test_<what>` 또는 `t_<what>` (권장, 경고만) |
| 시그니처 | `fn name()` — 인자 없음, 반환 값 없음 (있어도 무시) |
| PASS 조건 | fn 이 예외 없이 리턴 |
| FAIL 조건 | `assert(false)` / `exit(N>0)` / runtime error |
| SKIP | `@test @skip fn ...` 조합 |

`attr_cli` 로 확인:
```bash
hexa self/attr_cli.hexa show test
hexa self/attr_cli.hexa lint my_suite.hexa   # 이름 경고 체크
```

---

## 옵션

| 옵션 | 의미 |
| --- | --- |
| `--filter NAME` | 이름에 substring 포함되는 테스트만 실행 |
| `--verbose`, `-v` | 각 테스트 stdout (tail 10 줄) 함께 출력 |
| `--selftest-only` | `@test` 무시하고 legacy `--selftest` 경로만 실행 |

예시:
```bash
hexa test my_suite.hexa --filter string   # t_string_* 만 실행
hexa test my_suite.hexa --verbose         # 실패 시 stderr tail 표시
```

---

## 설계 메모

### 왜 프로세스 분리인가

stage0 interpreter 는 `assert(cond)` 실패 시:
- `stderr` 에 `error: assertion failed` 출력
- 현재 런타임 루프를 종료
- **exit code 0** (bug-class, 2026-04-18 관찰)

단일 프로세스에서 N 개 테스트를 순차 실행하면 첫 실패 시점에 전체가 중단된다.
따라서 각 테스트를 **독립 stage0 서브프로세스**로 spawn 한다:

1. 원본 source 뒤에 `<fn>()` + `println("__HEXA_TEST_OK__")` + `exit(0)` 삽입
2. 임시 .hexa 파일로 기록 → `hexa run` 자식 호출
3. `exit_code == 0 && output.contains(sentinel)` = PASS
4. 임시파일 정리

`exec_with_status` (2026-04-17 확정, HX9) 로 exit code 신뢰 가능.

### anima `--selftest` 관행과의 공존

anima `serving/*.hexa`, `training/*.hexa` 등 15+ 파일이 다음 패턴 사용:
```hexa
fn run_selftest() -> int { ... }

if args().contains("--selftest") { exit(run_selftest()) }
```

러너는 대상 파일에 `@test fn` 이 **하나도 없을 때** 자동으로:
```bash
hexa run <file> --selftest
```
로 fallback 한다. 기존 파일은 무수정 동작. 새 파일은 `@test` 를 쓰는 것이 권장.

점진 migration:
1. 기존 `run_selftest()` 내부 blocks 를 `@test fn test_<what>()` 로 분리
2. `--selftest` main 가드는 당분간 유지 (CI backward compat)
3. 모든 블록이 `@test` 로 이관되면 `run_selftest` + main 가드 제거

### 현재 한계

- **timing 해상도**: stage0 `clock()` 은 int 초. 네이티브 빌드 바이너리에서는 ms 해상도 (runtime.c:4282 float). 러너는 둘 다 지원.
- **병렬 실행**: 현재 순차. 다수 테스트 (50+) 시 느림. 추후 fan-out 옵션 검토.
- **stdout 캡처**: tail 20 줄 고정. 전체 로그는 `--verbose` 로만.
- **fixtures**: 없음. 각 테스트는 자기 상태를 스스로 준비/정리해야 함.

---

## 새 테스트 파일 만들기

```bash
# 1. 디렉토리 생성
mkdir -p tests/my_feature

# 2. 테스트 작성
cat > tests/my_feature/foo_test.hexa <<'EOF'
@test
fn test_foo_basic() {
    // ...
}
EOF

# 3. 실행
hexa test tests/my_feature/foo_test.hexa
```

참조 예제: `tests/runner/sample_test.hexa`.
