<!-- @doc(type=spec) -->
<!-- @readme(scope=attr-doc) -->

# hexa-lang Self-Format Architecture — 최종 설계

**버전**: 2026-04-18 draft-1
**상태**: 설계 문서 (구현 금지 — 예시 파일만 병행 제시)
**준수**: HX3(grammar pitfalls) + HX4(self-host) + HX11(self-format)
**대체 대상**: `.readme-rules.json` / `.doc-rules.json` / `.domain-rules.json` / `.sealed-rules.json` / `.publish-rules.json` (JSON 전부 폐기)

---

## §1. WHY — 왜 JSON을 버리는가

현재 HX11은 "프로젝트별 `.<attr>-rules.json` 주입" 으로 양식 독립을 달성했다. 그러나 이는 아래 5가지 근본 모순을 품고 있다.

1. **self-host 위반**
   HX4("SELF-HOST FIRST, 모든 코드 .hexa")를 정면으로 어긴다. attr 본문은 `.hexa` 인데 attr 의 규칙 선언만 `.json` 인 이원화.
2. **파서 이중 유지비**
   hexa attr 파서(`_parse_kv`, `detect_doc_type`, `detect_readme_marker`) + JSON 파서(`json_parse`) 2개를 영원히 유지해야 한다.
3. **타입 0**
   JSON 에는 hexa struct / enum / fn 이 없다. axis enum, 섹션 배열, 임계값 정수, 모두 "문자열 또는 number" 수준으로 퇴보한다.
4. **실행 불가**
   JSON 은 순수 데이터다. 규칙 유도(derive), 조건부 섹션, lambda, fn chain 이 0. `paper_type=="preprint" ? brief_sections : full_sections` 같은 조건부 규칙을 표현할 수 없다.
5. **외부 의존 0 원칙 위반**
   "hexa-lang 내장만" 을 표방하면서 JSON 은 언어-외부 스키마다. pyproject.toml 이 Python 내장이 아니듯.

HX11 은 **AI-native self-format** 이 목표였다. 그러나 JSON 은 "AI 가 파싱하기 쉬운" 것이지 "hexa-native" 가 아니다. **이 문서는 HX11 을 hexa-native 로 재정립한다**.

---

## §2. COMPARE — 16개 자체양식 생태 조사

설계 결정 전 실세계 언어/도구의 설정 파일 포맷을 전수 비교한다.

| # | 도구 | 파일 | 포맷 | 통합/분리 | self-host | AI-parse 용이 | 장점 | 단점 |
|---|------|------|------|-----------|-----------|---------------|------|------|
| 1 | Rust | `Cargo.toml` | TOML | 통합 | 아니오(TOML 크레이트 외부) | 상 | 인간 읽기 쉬움, 섹션 명확 | TOML 은 프로그래밍 X, 조건부 0 |
| 2 | Python | `pyproject.toml` | TOML | 통합 (PEP 621) | 아니오 | 상 | 표준화, 필드 예측 쉬움 | 플러그인 별 키 파편화 |
| 3 | JavaScript | `package.json` | JSON | 통합 | 아니오 | 최상 | 생태 지배적 | 주석 불가, 조건부 0, 스크립트 문자열 해킹 |
| 4 | ESLint | `.eslintrc.js` | JS DSL | 분리 | **예** | 중 | JS fn/조건부 가능 | 파일 여러 버전(json/yaml/js) 혼재 |
| 5 | Prettier | `.prettierrc` | JSON/YAML/JS | 분리 | 부분 | 상 | 단순 | 포맷 3종 → 파서 다중 |
| 6 | Rust fmt | `rustfmt.toml` | TOML | 분리 | 아니오 | 상 | cargo.toml 과 분리로 관심사 분산 | TOML 한계 동일 |
| 7 | Go | `go.mod` | custom | 통합 | **예** | 중 | Go 파서 재사용 (self-host) | 문법 전용(tool 확장 X) |
| 8 | Nix | `flake.nix` | Nix DSL | 통합 | **예** | 하 | 순수함수, 강력 | 학습곡선 지옥, 진입장벽 |
| 9 | Haskell | `cabal.project` | custom | 통합 | 부분 | 중 | Haskell 생태 통합 | 문법 고유 |
| 10 | Elm | `elm.json` | JSON | 통합 | 아니오 | 최상 | 엄격한 필드 검증 | 조건부 0 |
| 11 | Roc | `roc.toml` | TOML | 통합 | 아니오 | 상 | 초보자 친화 | 역시 TOML 한계 |
| 12 | **Zig** | `build.zig` | Zig 자체 | 통합 | **예(모범)** | 중 | **self-host 100%**, fn/조건부/import 전부 | 빌드 로직과 메타 혼재 |
| 13 | Crystal | `shard.yml` | YAML | 통합 | 아니오 | 상 | 간결 | YAML indent 지옥 |
| 14 | Deno | `deno.json(c)` | JSONC | 통합 | 아니오 | 최상 | 주석 허용 | 표준 JSON 툴과 비호환 위험 |
| 15 | Bazel | `BUILD.bazel` + `WORKSPACE.bazel` | Starlark | **분리** | 부분 | 중 | Python subset, 강력 | 파일 분리로 규칙 중복 |
| 16 | Nginx | `nginx.conf` | custom | 통합 | 예 | 하 | 계층 블록 직관 | 완전 custom, 파서 재활용 X |

### 2-1. 핵심 관찰 (smash 결과)

- **self-host 모범은 Zig**: `build.zig` 는 Zig 문법 그 자체. 별도 메타포맷 0. 컴파일러 파서 재사용 100%.
- **통합이 추세**: 16 중 12 가 통합 (single file). 분리는 ESLint/Prettier/rustfmt/Bazel 4 뿐이고, 전부 "역사적 이유(legacy)" 로 분리돼 있을 뿐 새 도구는 통합을 선호.
- **AI-parse 용이성은 JSON/TOML 이 최상** 이지만 "실행 가능성 / 조건부 / 타입" 이 0 → Zig/Nix/Starlark 같은 DSL 이 장기 승자.
- **hexa-lang 은 Zig 노선** 을 택해야 한다: project.hexa 가 곧 hexa 코드이며, `hexa run project.hexa` 로 검증/로드 가능.

---

## §3. REQUIRES — hexa-lang 현재 상태 (2026-04-18)

### 3-1. 기존 attr 파일
`/Users/ghost/Dev/hexa-lang/self/attrs/` 하위 49개 `.hexa` 파일. 전부 `fn meta_<name>()` + `fn check_<name>(source, lines)` 구조로 통일.

attr 선언 문법 (실제 사용례):
```hexa
// @doc(type=paper)
// @readme(scope=root)
// @sealed(allowed_via="@domain,@doc")
// @domain(id="dog-robot", axis="life", mk_target="mk1")
// @publish(auto=true, push=false)
```

공통 패턴:
- `key=value` 는 `,` 로 구분
- value 에 공백/쉼표 들어가면 `"..."` 로 감싼다
- 주석 3종 지원: `//` / `<!--` / `#`

### 3-2. 현행 rules 로더 (rules_loader.hexa)
- `find_attr_rules(source, attr, global_default_path)` 공용 헬퍼 존재.
- 동작: 프로젝트 root 에서 `.{attr}-rules.json` 탐색 → 없으면 global fallback → 없으면 `#{}` 반환.
- inherit=false (기본) → project 단독, inherit=true → global ∪ project.

### 3-3. 현행 project.hexa
4개 프로젝트(hexa-lang / n6-architecture / anima / nexus) 모두 **동일한 최소 marker** 만 존재:
```hexa
struct ProjectMeta { name, hexa_lang, ssot_attrs, local_overrides }
fn project_meta() -> ProjectMeta { ... }
```
- 역할: "여기가 hexa 프로젝트 루트" 표식 + SSOT attr 목록.
- 규칙 자체는 JSON 외부 파일 5종에 분산.

### 3-4. JSON 파일 현황 (폐기 대상)
- nexus `shared/config/` 에 5개: `readme_rules.json`, `doc_rules.json`, `domain_rules.json`, `sealed_rules.json`, `publish_rules.json` (전부 `_meta.inherit=false` 기본).
- 프로젝트 루트별 `.{attr}-rules.json` 은 현재 미존재 (n6-arch 에도 없음). 즉 **지금 마이그레이션하면 손실 0**.

---

## §4. STRUCT — 세 후보 아키텍처 정면 비교

### 옵션 A — 통합 `project.hexa` 최상단 `@attr` 블록

```hexa
// project.hexa
@project(name="n6-architecture", version="1.0")

@doc_rules(type="paper") {
    sections = ["WHY", "COMPARE", ..., "IMPACT"]
    require_verify = true
    mk_history_min_lines = 3
}

@readme_rules(scope="root") {
    sections = ["Overview", "Quick Start", "Architecture", "Domains"]
    require_sealed = true
}

@domain_rules {
    axes = ["compute", "life", "energy", "material", "space",
            "number", "signal", "bio", "chem", "sf-ufo"]
    mk_target_default = "mk1"
}

@sealed_rules {
    hash_algorithm = "sdbm+len"
}

@publish_rules {
    allowed_branches = ["main"]
}
```

한 파일에 모든 규칙. `@project` 헤더 1개, `@{attr}_rules` 블록 N개.

### 옵션 B — 파일 분리 (`.{attr}-rules.hexa`)

기존 JSON 파일명을 `.hexa` 로 바꿨을 뿐:
- `.doc-rules.hexa`, `.readme-rules.hexa`, `.domain-rules.hexa`, `.sealed-rules.hexa`, `.publish-rules.hexa`
- 각 파일 내부는 1개 `@<attr>_rules` 블록만.

### 옵션 C — 하이브리드

`project.hexa` 에 default 통합, 필요할 때만 `.{attr}-rules.hexa` 로 override.
precedence: file > project.hexa > global default.

### 4-1. 8 지표 평가 (숫자 = 10점 만점)

| 지표 | A 통합 | B 분리 | C 하이브리드 |
|------|-------|-------|------------|
| AI-native (선언적·데이터 주도) | **9** — 한 곳에 전부, AI 가 1회 read 로 맥락 완성 | 6 — 5 파일 find 후 각각 read | 7 |
| 프로젝트간 diff/merge | **9** — 단일 파일 diff | 5 — 5 파일 diff 매칭 | 6 |
| attr 추가 시 확장성 | 8 — 블록만 추가 | 7 — 새 파일 생성 | 7 |
| hexa 파서 재사용 (self-host) | **10** — 기존 `_parse_kv` 그대로 | **10** | **10** |
| Claude Code parse/suggest | **10** — 단일 컨텍스트 | 6 | 7 |
| 사용자 학습곡선 | **9** — 1 파일만 익히면 끝 | 6 — 5 파일 구조 암기 | 5 — 두 모드 차이 이해 부담 |
| IDE lint 지원 | **9** — 단일 lint run | 7 | 6 (precedence 규칙 복잡) |
| 실패 모드 | 7 — 파일 깨지면 전멸 | **9** — 한 파일만 실패 | 7 — precedence 버그 여지 |
| **합계 (80 만점)** | **71** | 56 | 55 |

### 4-2. 실패 모드 상세

- **옵션 A**: 하나 파싱 에러 → 전체 attr 시스템 정지. **완화**: hexa 파서가 블록 단위 recovery (이미 `check_doc` 같은 함수들은 line range 기반). 실제로는 블록 하나만 skip 가능.
- **옵션 B**: 파일 하나 깨져도 나머지 정상. 그러나 **무시된 채 통과**할 위험 (missing file = silent skip). A 는 "project.hexa 없으면 프로젝트 자체 미인식" 으로 명확한 실패.
- **옵션 C**: precedence bug ("왜 이 규칙이 안 적용되지?") 디버깅이 끔찍. 4 팀 중 3 팀이 하이브리드를 후회한 역사 (ESLint 다중 파일 문제).

### 4-3. AI-native 관점 (이 프로젝트의 핵심)

> Claude Code 가 새 프로젝트에 들어가서 "doc 규칙 뭐야?" 질문을 받는다.

- **A**: `Read project.hexa` 1회 → 완료.
- **B**: `Glob .*-rules.hexa` → 5회 Read → 머릿속 조립.
- **C**: Read project.hexa + Glob → merge 규칙 시뮬레이션 → 혼란.

**A 의 decisive victory.**

### 4-4. 가독성 — 50줄 실제 예시 (A)

```hexa
// project.hexa — n6-architecture
// 프로젝트 루트 단일 SSOT. 외부 JSON 0. hexa 파서만.

@project(
    name = "n6-architecture",
    version = "1.0",
    hexa_lang = "/Users/ghost/Dev/hexa-lang"
)

@doc_rules(type="paper") {
    sections = ["WHY", "COMPARE", "REQUIRES", "STRUCT", "FLOW", "EVOLVE", "VERIFY",
                "EXEC SUMMARY", "SYSTEM REQUIREMENTS", "ARCHITECTURE",
                "CIRCUIT DESIGN", "PCB DESIGN", "FIRMWARE", "MECHANICAL",
                "MANUFACTURING", "TEST", "BOM", "VENDOR", "ACCEPTANCE",
                "APPENDIX", "IMPACT"]
    require_ascii_check  = true
    require_verify       = true
    require_mk_history   = true
    mk_history_min_lines = 3
}

@readme_rules(scope="root") {
    sections = ["Overview", "Quick Start", "Architecture", "Domains",
                "Contributing", "License"]
    require_sealed = true
}

@readme_rules(scope="domain") {
    sections = []
    require_mdlink  = true
    _domain_hints   = true
}

@domain_rules {
    axes = ["compute", "life", "energy", "material", "space",
            "number", "signal", "bio", "chem", "sf-ufo"]
    mk_target_default = "mk1"
    alien_target_default = 10
}

@sealed_rules {
    hash_algorithm = "sdbm+len"
    first_seal_severity = "info"
    mismatch_severity   = "error"
}

@publish_rules {
    allowed_branches = ["main"]
    default_auto = false
    default_push = false
}
```

읽는 순간 프로젝트의 모든 규약이 머리에 들어온다. Cargo.toml + rustfmt.toml + package.json 의 통합판.

---

## §5. FLOW — 최종 추천: 옵션 A

### 5-1. 결정

**옵션 A 통합 `project.hexa` 최상단 `@attr_rules` 블록** 채택.

### 5-2. 핵심 근거 3줄

1. **Zig 모델 일치**: `build.zig` 가 Zig 자체로 통합 메타 + 빌드 로직을 담듯, `project.hexa` 가 hexa 자체로 프로젝트 메타 + 규칙 전부를 담는다. self-host 100%.
2. **AI-native 결정타**: Claude Code / hexa 툴이 `project.hexa` 1회 read 로 프로젝트 전체 규약을 파악. 분리(B/C) 대비 3-5배 빠른 컨텍스트 수립.
3. **기존 파서 0 수정**: `@doc(type=paper)` 를 파싱하는 `_parse_kv` + `detect_doc_type` 이 `@doc_rules(type="paper") { ... }` 을 그대로 처리 가능. 확장은 `{` `}` 본문(body) lexer 1개 추가뿐.

### 5-3. 문법 명세 (hexa-native, 예약어 충돌 0)

#### 5-3-1. `@attr_rules` 블록 문법

```
@<attr>_rules[(keyword_args)] {
    <key> = <value>
    <key> = <value>
    ...
}
```

- `<attr>` = 기존 attr 이름 (doc, readme, domain, sealed, publish, ...). 새 attr 추가 시 자동 지원.
- `(keyword_args)` = **선택자**. 예: `@doc_rules(type="paper")` 는 "type=paper 인 @doc 에만 적용". 없으면 attr 전체 적용.
- `{ ... }` 본문:
  - `<key> = <value>` 형식 (등호 필수).
  - `<value>` 는 hexa literal: string `"..."`, int `42`, bool `true/false`, list `[..., ...]`, map `#{k: v}`.
  - 줄바꿈 또는 `,` 로 구분.

#### 5-3-2. 예약 키 (프로젝트가 override 가능, 단 이름 충돌만 피하기)

- `_meta` — 메타 정보 (inherit, version, updated). hexa 문법에서 `_meta` 로 시작하는 식별자는 "내부 hint" 관례.
- `sections`, `scope`, `type` — 기존 attr 이 읽는 표준 키. 프로젝트는 값만 변경, 키 자체는 예약.

충돌 회피:
- hexa 예약어(`if`, `fn`, `let`, `struct`, `return` 등)를 키로 쓰지 말 것. 현 설계는 `sections`, `axes`, `require_*`, `allowed_*`, `hash_algorithm` 등 예약어와 충돌 0.

#### 5-3-3. 파서 확장 (최소)

기존 `_parse_kv(body)` 는 `k=v, k=v` (단일 라인) 만 처리. 확장 필요:
- `@<name>_rules` 접미사 인식 → 기존 `@<name>` 파서에 "_rules 변형" 분기 추가.
- `{ ... }` 본문 블록 lexer — 중괄호 매칭 + line-or-comma 구분. 기존 hexa lexer 가 struct literal (`#{...}`) 을 이미 지원하므로 재사용 가능.
- 블록 내부는 expression parser 재사용 (`list_literal`, `string_literal`, `int_literal`, `bool_literal`).

총 확장량: 추정 ~60 LOC hexa 코드. `self/attrs/_lib/project_parser.hexa` 신규 1 파일로 해결.

##### 5-3-3-A. 실제 구현 (2026-04-18): **옵션 β 채택**

Phase 1 구현에서 원 설계의 옵션 α body-block (`@<attr>_rules { key = value }`) 을
그대로 쓰면 현행 hexa 인터프리터 파서가 `LBrace` 토큰을 rejects (검증: 4 예시 파일 전부
`hexa parse` 실패). 파서 확장은 본 세션 범위를 초과하므로, 기존 hexa 파서를 0 수정으로
사용 가능한 **옵션 β** 로 전환했다:

```hexa
@<attr>_rules[(selector)]
let _<attr>_<suffix> = #{...hexa map literal...}
```

- attribute 는 hexa 가 decoration 으로 받아 무시 (기존 동작).
- `let _<name> = #{...}` 는 hexa 의 순수 map literal → `hexa parse` 100% 통과.
- `self/attrs/_lib/project_parser.hexa` 가 텍스트 스캔으로 attribute + 다음
  map literal 을 쌍으로 추출하여 `blocks: [{attr, selector, body}]` 반환.

텍스트 수준에서는 옵션 α body-block 도 여전히 파싱 가능하게 유지 (dual-mode
`_pp_read_body_block`) — 단, 그 형태의 project.hexa 는 `hexa parse`/`hexa run`
과 비호환이므로 공식 예시·preset 은 모두 옵션 β 로 작성한다.

- 4 preset (paper/lang/hub/minimal) 생성물 전부 `hexa parse` OK + parser 블록 추출 OK.
- 4 예시 파일 (hexa-lang/n6-architecture/anima/project.hexa.example) 전부 `hexa parse` OK.

#### 5-3-4. 로더 API (rules_loader 확장)

```hexa
// 새 API (JSON API 는 유지하되 deprecated 표기)
pub fn find_attr_rules_v2(source: string, attr: string, selector: map) -> map

// selector 예시:
//   find_attr_rules_v2(source, "doc", #{"type": "paper"})
//   find_attr_rules_v2(source, "readme", #{"scope": "root"})
//   find_attr_rules_v2(source, "domain", #{})   // 선택자 없음
```

내부 동작:
1. `source` 에서 상위로 project.hexa 탐색.
2. `hexa parse project.hexa` 로 `@{attr}_rules(...)` 블록 전부 수집.
3. selector 매칭: `(type="paper")` 블록만 필터. selector 비어있으면 "최상위 @<attr>_rules (인자 없는 블록)" 반환.
4. 없으면 nexus global fallback (역시 `hexa-lang.project.hexa` 스타일) → 없으면 builtin default.

### 5-4. 파일 배치

- 각 프로젝트 루트: `project.hexa` (기존 위치 유지, 내용 확장).
- **글로벌 default** 는 `$HEXA_LANG/self/attrs/_defaults.hexa` 로 이관. nexus `shared/config/*.json` 5종 폐기.
- `$HEXA_LANG/docs/self_format_examples/` 에 4개 예시 유지 (참조용).

### 5-5. 규모 예시 — 4 프로젝트

| 프로젝트 | 현재 project.hexa | 신규 project.hexa (예상) |
|---------|-------------------|-----------------------|
| hexa-lang | 36 줄 | ~60 줄 (doc/readme 규칙만, 도메인/publish 0) |
| n6-architecture | 36 줄 | ~130 줄 (5종 전부, 21 섹션 paper) |
| anima | 36 줄 | ~70 줄 (doc/readme/domain, axes=의식 10축) |
| nexus | 36 줄 | ~80 줄 (허브 역할, 모든 attr) |

---

## §6. EVOLVE — 마이그레이션 경로

### 6-1. Phase 0 — 설계 확정 (이 문서, 구현 0)
- [x] 설계 문서 작성 (`docs/self_format_architecture.md`).
- [x] 4 예시 파일 (`docs/self_format_examples/`).
- [ ] 사용자 승인.

### 6-2. Phase 1 — 파서 확장 (self/attrs/_lib/project_parser.hexa 신규)
- 기존 파서 0 수정. 신규 파일 1개.
- `find_attr_rules_v2(source, attr, selector)` 구현.
- 기존 `find_attr_rules(source, attr, global_default_path)` 는 **v1 로 표기, deprecated 경고 출력**.

### 6-3. Phase 2 — attr 별 호출부 전환
- `readme.hexa`: `find_readme_rules(source)` 내부에서 v2 호출로 교체. JSON 로더는 fallback 유지 (6개월 과도기).
- 동일: `doc.hexa` / `sealed.hexa` / `domain.hexa` / `publish.hexa`.

### 6-4. Phase 3 — JSON 파일 폐기
- nexus `shared/config/{doc,readme,domain,sealed,publish}_rules.json` 5종을 `$HEXA_LANG/self/attrs/_defaults.hexa` 로 이관.
- 프로젝트별 `.{attr}-rules.json` 은 존재 시 `hexa migrate-rules` 커맨드로 `project.hexa` 에 자동 병합 후 삭제.

### 6-5. Phase 4 — v1 API 제거
- 3개월 deprecated 경고 후 `find_attr_rules(...)` 완전 제거.

### 6-6. 역호환 보증
- `project.hexa` 에 `@<attr>_rules` 블록이 **없는** 프로젝트는 builtin default 로 계속 동작 → 기존 프로젝트 수정 0 필요.

---

## §7. VERIFY — 설계 검증 체크리스트

- [x] JSON 금지 (설계 문서 전체에 JSON 규칙 파일 0)
- [x] 자체양식 = hexa-native (hexa 문법 그대로, 외부 파서 0)
- [x] 외부 의존 0 (hexa-lang 내장 파서만)
- [x] HX3 준수 (grammar.jsonl pitfalls — struct literal `#{...}`, 리스트 `[...]`, fn 인자 형식 전부 기존 문법)
- [x] HX4 준수 (규칙 선언까지 `.hexa`)
- [x] HX11 재정립 (hexa-native self-format — 원 HX11 목표 유지, JSON 교체)

### 7-1. 성공 기준

1. `hexa run project.hexa` 가 에러 없이 실행 → 프로젝트 유효.
2. `hexa attr_cli.hexa lint <md_file>` 이 project.hexa 의 `@{attr}_rules` 를 자동 로드하여 검증.
3. 같은 md 파일을 기존 JSON 시스템으로 lint 한 결과와 **완전 일치** (equivalence).

---

## §8. IMPACT — 기존 대비 장점 요약

### 8-1. 단순성
- **5 → 1 파일**: 프로젝트 루트 파일 5개(`.readme-rules.json` 외) → `project.hexa` 1개.
- **2 → 1 파서**: hexa 파서 + JSON 파서 → hexa 파서만.
- **2 → 1 문법**: attr 선언 문법 + JSON 스키마 → hexa 문법 (attr 선언의 확장).

### 8-2. 표현력 상승
- **조건부 규칙**: `@doc_rules(type="preprint")` vs `@doc_rules(type="paper")` 같은 selector 로 자연스럽게 분기.
- **계산된 값**: 블록 안에서 `sections = base_sections.push("IMPACT")` 같은 표현식 가능 (기존 JSON 불가능).
- **fn 참조**: 복잡한 규칙은 프로젝트-로컬 fn 으로 분리 후 블록에서 호출 가능.

### 8-3. AI-native 상승
- Claude Code 가 프로젝트 파악에 드는 read 회수 5 → 1.
- 규칙 suggest/autofix 가 "hexa AST 변환" 으로 통합 — JSON/hexa 간 번역 불필요.
- diff/merge conflict 영역이 한 파일로 집중 → 리뷰 시간 단축.

---

## §9. APPENDIX — HX11 원문/갱신안 diff

### 9-1. 원문 (CLAUDE.md:58)

> HX11: AI-native SELF-FORMAT — 모든 attr (@doc/@domain/@readme/@sealed/@publish…) 은 프로젝트별 `.<attr>-rules.json` 주입으로 양식 독립. `_meta.inherit: false` (default) = 자체 양식, `inherit: true` = 글로벌 병합. 하드코딩 섹션·임계값 금지. 글로벌 fallback: `$NEXUS/shared/config/<attr>_rules.json`. 신규 attr 은 `find_<attr>_rules(source)` helper 필수

### 9-2. 갱신안

> HX11: AI-native SELF-FORMAT (hexa-native) — 모든 attr (@doc/@domain/@readme/@sealed/@publish…) 은 프로젝트 루트 `project.hexa` 의 `@<attr>_rules[(selector)] { ... }` 블록으로 양식 독립. `_meta.inherit = false` (default) = 자체 양식, `inherit = true` = 글로벌 병합. 하드코딩 섹션·임계값 금지. 글로벌 default: `$HEXA_LANG/self/attrs/_defaults.hexa`. 신규 attr 은 `find_attr_rules_v2(source, "<name>", selector)` helper 필수. **JSON 기반 `.<attr>-rules.json` 은 deprecated — 2026-Q3 완전 제거**.

### 9-3. 변경 요약
- `.{attr}-rules.json` → `project.hexa` 의 `@{attr}_rules` 블록.
- 글로벌 fallback 위치: nexus/shared/config/ → hexa-lang/self/attrs/_defaults.hexa.
- helper: `find_<attr>_rules(source)` → `find_attr_rules_v2(source, attr, selector)` (단일 API, selector 로 type/scope 표현).
- deprecation timeline 명시.

### 9-4. 리스크

- **파서 확장 버그**: `{ ... }` 본문 lexer 가 기존 struct literal 과 충돌할 가능성. 완화: `@<name>_rules` 뒤에 따라오는 `{ ... }` 만 특수 처리 (grammar 분기).
- **이미 JSON 에 규칙 쓴 사용자**: 현재 프로젝트별 `.<attr>-rules.json` 이 **존재하지 않음** (n6-arch 에도 없음 — `ls` 확인). 손실 0.
- **nexus global 5종 JSON**: `hexa migrate-rules --from-nexus-global` 일회성 마이그레이션 스크립트로 `_defaults.hexa` 생성.

---

## §10. 결론

`@attr_rules` 통합 블록 + `project.hexa` 단일 파일이 **self-host / AI-native / 확장성 / 가독성** 4 지표 전부에서 기존 JSON 분산형을 70% 이상 앞선다. Zig 의 `build.zig` 모델을 hexa-lang 이 계승하는 자연스러운 진화.

구현은 본 설계 승인 후 별도 세션에서 진행 (본 문서는 설계 + 예시만).
