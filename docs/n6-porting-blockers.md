# N6 → HEXA 포팅 블로커 레지스트리

> 최종 갱신: 2026-04-08 (로컬 hexa 실제 테스트 반영)
> 출처: nexus/n6-architecture 38개 .rs → .hexa 대규모 포팅
> 원본: n6-architecture/docs/superpowers/specs/2026-04-08-hexa-lang-blockers-for-porting.md

## 상태 범례

| 상태 | 의미 |
|------|------|
| RESOLVED | 로컬 hexa에서 검증 통과 |
| PARTIAL | 일부 동작, 완전하지 않음 |
| OPEN | 미해결 |

---

## 긴급 조치 필요 2건

### 1. 원격 gate 바이너리 구버전

원격 Ubuntu gate (`/tmp/hexa-build/hexa-lang/hexa`)가 로컬보다 뒤처짐.
로컬에서 통과하는 B-30(mut reassign), B-34(destructure), B-17(+=) 등이 원격에서 실패.
→ **gate 바이너리 재빌드 필요** (2026-04-11 이후: src/ 삭제, cargo 제거됨.
   `ssh ubu "cd /tmp/hexa-build/hexa-lang && ./hexa build_hexa.hexa"` 또는
   precompiled 바이너리 rsync 배포.)

### 2. ~~codegen_c2 math 함수 누락~~ ✅ RESOLVED (ready 226e337)

`bootstrap_compiler.c` (775~777) 의 `ln/log10/exp`을 codegen_c2.hexa의
gen2_expr() 분기에 포팅 완료. 보너스로 sqrt/floor/ceil/abs/round/sin/cos/pow,
CompoundAssign(+= -= *= /= %=), BreakStmt/ContinueStmt, StructDecl 도 추가.

---

## RESOLVED (25건)

로컬 `GATE_LOCAL=1 hexa` 로 직접 실행 검증 완료.

| # | 내용 | 해결 커밋 | 검증 방법 |
|---|------|-----------|-----------|
| B-1 | 다중 인수 println | string concat 우회 | 포팅 19개 전부 사용 |
| B-2 | CLI argv (args()) | 동작 확인 | `args()` 테스트 |
| B-3a | format("{} {}", a, b) 다중 치환 | `b051665` | 직접 실행 |
| B-5 | 유니코드 리터럴 (BMP) | 동작 확인 | `═ ✓ ✗ → τ σ` 출력 |
| B-6 | for-in 루프 | 동작 확인 | `for item in ["a","b","c"]` 통과 |
| B-12 | 배열 리터럴 codegen | `4acae83` | 다중 원소 배열 |
| B-13 | while 본문 외부 스코프 | 동작 확인 | 루프 내 외부 변수 접근 |
| B-14 | 변수 임베드 format | `a2b0afe` | format + string concat |
| B-15 | 튜플 리터럴 | `97da478` | ("name", 42) 생성 |
| B-16 | if-as-expression (중첩 포함) | `4fdd236` | 중첩 if-expr 값 반환 |
| B-17 | mut + 복합 대입 (+=) | `a2b0afe` | `x += 1` (로컬 OK, gate 실패) |
| B-19 | float 나눗셈 → 정수 | `62e4020` | `12.0 / 4.0 = 3.0` |
| B-20 | abs() 태그 불일치 | `62e4020` | `abs(-3.5) = 3.5` |
| B-21 | String.repeat(n) | `62e4020` | 동작 확인 |
| B-22 | Bool to_string → true/false | `62e4020` | 동작 확인 |
| B-23 | 과학 표기 리터럴 (1.38e-23) | `e98579e` | 렉서 인식 |
| B-26 | array.contains() | `f4f9943` | 동작 확인 |
| B-28 | fn에서 top-level let 접근 | `34e2be6` | fn 안에서 global 상수 참조 |
| B-29 | format_float scientific notation | `060ff9c` | sci 포맷 |
| B-30 | while 조건에서 글로벌 변수 | 로컬 OK | `while x <= SIGMA` 통과 |
| B-31 | fn에서 글로벌 배열 접근 | 로컬 OK | fn 안에서 names[0] 통과 |
| B-32 | 대문자 변수명 파서 버그 | 로컬 OK | `let TOP = 20; if TOP > 10` 통과 |
| B-34 | 튜플 destructure | 로컬 OK | `let (a, b) = t` 통과 |
| B-36 | argv 변수명 C 충돌 | 로컬 OK | `let argv = args()` 통과 |
| B-37 | global mut를 fn에서 R/W | 로컬 OK | fn에서 global mut 변경 통과 |

## PARTIAL (4건)

### B-3b/c: 폭/정밀도 포맷 지정자
- **동작**: pad_right/pad_left 문자열 패딩 (`7bbe7da`), format_float(f, prec)
- **미지원**: `{:<8}` `{:>2}` Rust 등가, 정수 폭 지정
- **영향**: L2 계산기 표 출력 G3 byte-perfect 자동화

### ~~B-4: 부동소수 수학 함수~~ ✅ RESOLVED (ready 226e337)
- **동작 (12종)**: sin, cos, tan, abs, sqrt, pow, floor, ceil, round, **ln/log/log10/exp**
- **해결**: codegen_c2.hexa gen2_expr() 분기 추가 (B-4 관련 OPEN 항목과 통합 해소)
- **영향**: Boltzmann/Shannon/Landauer 등 물리 계산 가능

### B-7: 외부 명령 호출
- **동작**: `exec("echo hello")` → stdout 캡처
- **미확인**: 종료코드 반환, stderr 캡처, 환경변수, pipe
- **영향**: L1 .sh 포팅 (복잡한 파이프라인)

### B-9: JSON 파싱
- **동작**: `json_parse('{"key":"val"}')` 기본 동작
- **미확인**: 중첩 객체, 배열, 숫자 타입 보존, 에러 처리
- **영향**: nexus/shared/*.json 읽기

## OPEN (8건)

### B-8: 파일시스템 디렉토리 순회
- **동작**: read_file, write_file
- **미지원**: `read_dir`, glob, exists, mkdir_p
- **영향**: L1 .sh 포팅 (find + glob 패턴)

### B-10: ML 수치 스택
- numpy/torch/scipy/matplotlib 상응 0
- **영향**: L3 techniques/*.py 23개, L4 experiments/*.py 전부
- **결정 필요**: HEXA ML 스택 / Python FFI / 영구 보류

### B-11: 빌드 산출물 cleanup
- .c 중간 파일이 소스 옆에 남음
- -I 플래그 상대경로 하드코딩 → cwd 의존
- **영향**: 비-hexa-lang cwd에서 빌드 시 오염

### B-15-ext: 튜플 for-in 구조분해
- 튜플 리터럴/destructure 동작 (B-15, B-34 해소)
- **미확인**: `for (name, val) in tuples { ... }` 구조분해 for-in
- **영향**: Rust calc의 표 출력 패턴 (parallel array 우회 중)

### B-18: 메서드 호출 문법
- len(arr) 함수형 동작
- **미확인**: arr.len(), str.contains() 등 dot-method
- **영향**: Rust 스타일 코드 직역

### B-33: 대규모 배열 정렬 성능
- 3000+ 요소 정렬 → 인터프리터 극단적 느림
- **우회**: 삽입정렬 top-N 유지
- **영향**: DSE 전수탐색 (수천 조합)

### B-35: Option/nullable 타입 부재
- **우회**: sentinel 값 (0.0, "", -1)
- **영향**: 조건부 검증 패턴

### ~~codegen_c2 math 함수 누락 (B-4 관련)~~ ✅ RESOLVED (ready 226e337)
- B-4와 통합 해소.

---

## 포팅 우회 패턴 (설계 참고)

포팅 중 반복 사용한 우회 패턴. 언어 개선 시 이런 패턴이 불필요해지면 좋음:

| 우회 | 대상 블로커 | 설명 |
|------|-------------|------|
| parallel array | B-15-ext | 튜플 배열 대신 name[], val[], expr[] 병렬 배열 |
| 평탄화 | B-28 (해소) | fn 없이 top-level 코드로 펼침 |
| sentinel 값 | B-35 | Option 대신 0.0 / "" / -1 |
| 수동 mantissa 분해 | B-29 (해소) | sci notation 대신 val * 1e21 후 표시 |
| 삽입정렬 top-N | B-33 | 전체 정렬 대신 상위 N개만 추적 |
| fmt_num 헬퍼 | B-3b | float/int 자동 분기 포맷 함수 |

## 포팅 재개 조건

| 단계 | 필요 조치 | 현재 상태 |
|------|-----------|-----------|
| L2 나머지 19개 커밋 | gate 바이너리 재빌드 | 코드 생성 완료, 커밋 대기 |
| L2 G3 byte-perfect | B-3b 정수 폭 (math 해소됨, ready 226e337) | 부분 해소 |
| L1 .sh sweep | B-8 read_dir/glob | OPEN |
| L3/L4 .py sweep | B-10 ML 스택 | 별도 결정 필요 |

## 포팅 통계

- **대상**: n6-architecture tools/ 38개 .rs (nexus/dashboard 제외)
- **커밋 완료**: 19개 (.hexa + .rs 삭제)
- **미커밋**: 17개 (.hexa 생성됨, porting worktree에 대기)
- **미생성**: 1개 (chip-perf-calc)
- **우회 주석**: 모든 .hexa 파일에 `// WORKAROUND B-XX` 표기
- **검증**: G1(구문) + G2(실행) 전체 통과, G3(byte-perfect) 3개 확인
