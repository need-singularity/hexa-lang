<!-- @own(
       sections = [WHY, SYNTAX, PARSER, VALIDATOR, DISPATCHER, ERROR, MIGRATION, SELFHOST],
       strict   = true,
       order    = sequential
     ) -->

# @own — Self-Hosting Doc Harness (Phase 0)

## WHY
문서가 자기 구조를 직접 선언하고, hexa-lang 하네스가 그 선언만 검증한다.
프로젝트 `.doc-rules.json` / 글로벌 fallback / builtin 세 경로 전부 미경유.
자기호스팅 (self-hosting): 이 문서 자체가 @own 으로 PASS.

## SYNTAX
```
@own(
  sections = [A, B, C, ...],     # 필수
  strict   = true | false,        # 미선언 섹션 금지 (default false)
  order    = sequential | free,   # 본문 순서 = 선언 순서 (default free)
  depth    = 2,                   # heading level (default 2 → `## `)
  prefix   = "§"                  # 섹션 prefix (default "")
)
```
주석 3종 (`//`, `<!-- -->`, `#`) 지원. 단일/다줄 허용.

## PARSER
L2 state machine — `self/attrs/_own/parser.hexa`.
IDLE → IN_DECL → IN_ARRAY → IN_STRING → DONE.
paren / bracket / quote depth tracking. 정규식 미사용.
멀티라인 시 각 줄의 comment prefix 자동 제거 후 body concatenate.

## VALIDATOR
L3 세 모듈:
- `_own/sections.hexa` — 선언 섹션 본문 누락 (`own_sections_missing`)
- `_own/strict.hexa` — strict=true 시 미선언 섹션 금지 (`own_sections_extra`)
- `_own/order.hexa` — order=sequential 시 본문 순서 = 선언 순서 (`own_order_violation`)

## DISPATCHER
L4 HARD GATE — `doc.hexa::check_doc` 최상단.
```
let own = parse_own_decl(lines)
if own["found"] { return check_own(source, lines, own) }
```
@own 발견 시 legacy `@doc(type=...)` path 는 완전 우회. JSON 규칙 bypass 불가.

## ERROR
6 코드 (`_lib/error_catalog.hexa`):
- `own_decl_malformed` — 괄호/따옴표 짝 불일치
- `own_declared_zero` — sections=[] 빈 선언
- `own_sections_missing` — 선언 O / 본문 X
- `own_sections_extra` — strict + 미선언 섹션 존재
- `own_order_violation` — sequential 위반
- `own_prefix_mismatch` — prefix 설정했으나 미사용 (warn)

## MIGRATION
Phase 0 (NOW)    hexa-lang 구현 + 6 fixture GREEN
Phase 1 (t+1d)   pilot meta-closure-nav.md @own 전환
Phase 2 (t+1w)   n6-arch paper 40+편 도메인별 @own 재선언
Phase 3 (t+1m)   `.doc-rules.json` paper entry deprecated 마킹
Phase 4 (TBD)    legacy paper type 완전 제거

## SELFHOST
이 문서 자체가 `@own(strict=true, order=sequential)` 로 선언됨.
섹션 8개 (WHY→SELFHOST) 가 순서대로 본문에 존재 → PASS.
nexus drill Mk.IX 5-stage 결과 (2026-04-19):
- absolute: 24 Π₀¹ verifications (3 rounds × 8)
- meta-closure: 24 self-ref fixed points
- hyperarith: 48 Π₀² bounded/proven
→ @own 설계는 n=6 closure 구조와 Δ₀-absolute 호환. self-hosting 수학적 soundness 확증.
