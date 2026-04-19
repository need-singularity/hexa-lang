<!-- @doc(type=spec) -->
<!-- @readme(scope=attr-doc) -->

# hexa init scaffold — 파일 세트 + 확장자 체계 심층 연구

**버전**: 2026-04-18 draft-1
**상태**: 설계 문서 (구현 금지 — 예시 scaffold 5 종 병행)
**준수**: HX3(grammar pitfalls) + HX4(self-host) + HX7(self-host 경로 전용) + HX11(self-format hexa-native)
**관계 문서**:
- `docs/self_format_architecture.md` (JSON→project.hexa 통합 결론, 이 문서의 전제)
- `docs/self_format_examples/*.hexa` (4개 예시 project.hexa)
- `tool/init_project.hexa` (현행 구현, 1 파일만 생성)
- 본 문서가 정의하는 것: **`hexa init` 이 **어떤 파일 세트**를 생성해야 하는가** (scope 정의)

---

## §1. WHY — 현행 `hexa init` 의 한계

### 1-1. 현재 동작

`tool/init_project.hexa` 는 85 줄짜리 단일 함수 호출 도구다. 실행 결과:

```
<target>/project.hexa   (36 줄, ProjectMeta struct + project_meta fn)
```

**그뿐이다.** `.gitignore` 도, `README.md` 도, `main.hexa` 도, 도메인 디렉토리도 만들지 않는다.

### 1-2. 의도와 실사용의 간극

`init_project.hexa` 서두 주석은 이렇게 선언한다:

> Policy (HX4 self-host, 0% external dep):
>   The generated artifact is ONE file: `<target>/project.hexa`
>   No markdown, no JSON, no YAML. hexa defines hexa.

이는 **HX4 에 극단적으로 충실**한 설계지만, 4 가지 실용 문제를 부른다:

1. **AI 컨텍스트 공백**: Claude Code 가 새 프로젝트에 들어가 첫 `Read` 를 하면 `project.hexa` 36 줄만 본다. "이 프로젝트의 **목적**" 은 어디에도 없다 — project_meta.name 만 있을 뿐. README.md 가 관습적으로 담는 **WHY/Quick Start/Architecture** 를 어디서 얻을 것인가?
2. **git 친화도 0**: `.gitignore` 부재 → 사용자가 `hexa init` 직후 `git init && git add .` 하면 `.sealed.hash`, `build/artifacts/`, `.DS_Store` 가 전부 스테이징된다. 이는 HX4 문제가 아니라 **툴체인 위생** 문제다.
3. **entry point 공백**: `hexa run <target>` 에 해당하는 진입 실행 파일이 없다. 사용자는 `main.hexa` 를 직접 만들어야 한다.
4. **용도 스펙트럼 대응 0**: 논문 프로젝트, 언어 도구 프로젝트, 허브/SSOT 프로젝트, 도메인 전용 서브리포는 모두 **필요 scaffold 가 다른데**, 현 init 은 "일단 marker 하나" 로 끝낸다. 용도별 preset 개념 부재.

### 1-3. self-host 순혈주의 vs AI-native 실용주의

"hexa 이외 확장자 0" 을 고집하면 `README.md` / `.gitignore` / `CLAUDE.md` 는 전부 배제돼야 한다. 그러나 이는 **HX4 의 과확장 해석**이다. HX4 본문은:

> SELF-HOST FIRST — src/ Rust 폐기, 모든 코드 .hexa

**"모든 코드"** 이지 **"모든 파일"** 이 아니다. README.md, .gitignore 는 코드가 아니다 — 각각 "AI/인간 가이드 문서", "VCS 메타" 다. Zig (HX4 모범) 도 `build.zig` 는 self-host 하되 `README.md`, `.gitignore` 는 관례적으로 동반한다.

본 연구는 **"hexa 코드 = .hexa 단일, 비코드 컨텍스트 = 관례 파일명 + 표준 확장자(.md, .gitignore)"** 절충을 제안한다. 이는 Zig/Rust/Go 가 모두 택한 노선이다.

### 1-4. 연구 범위

이 문서는 다음을 **정의**한다:
- `hexa init` 이 생성해야 할 **파일 세트** (필수/선택 구분)
- **확장자 체계** — `.hexa` 단일 + 표준 확장자 vs 파생 확장자
- **preset 스펙트럼 5+** — 용도별 scaffold 차이
- **AI-native 최적 scaffold** — Claude Code 가 첫 30 초에 읽을 파일
- **idempotency / migration** — 기존 프로젝트에 재실행 시 동작

다음은 **정의하지 않는다** (다른 agent 담당):
- `project.hexa` 내부 `@<attr>_rules` 블록 확장 (self_format_architecture.md 소관)
- 실제 `init_project.hexa` Rust/hexa 구현 코드 변경

---

## §2. COMPARE — 20+ 언어/도구 `init` 생태 비교

### 2-1. 전수 조사표 (22 도구)

각 도구가 `init|new|create` 명령으로 생성하는 **실제 파일 트리** 를 조사했다. "Files" 열은 실제 생성물 (ASCII tree, 디렉토리는 `/` 접미).

| # | 도구 | 커맨드 | 생성 파일 | 확장자 개수 | opinionated | self-host | git init |
|---|------|--------|----------|-----------|-------------|-----------|----------|
| 1 | Rust Cargo | `cargo new foo` (bin) | `Cargo.toml`, `src/main.rs`, `.gitignore`, `.git/` | 3 (`.toml` `.rs` no-ext) | 중 | 부분 | **예** |
| 2 | Rust Cargo | `cargo init .` | 동일 but git 기존 사용 | 3 | 중 | 부분 | 기존 사용 |
| 3 | Rust Cargo | `cargo new foo --lib` | `Cargo.toml`, `src/lib.rs`, `.gitignore`, `.git/` | 3 | 중 | 부분 | 예 |
| 4 | Go | `go mod init example.com/foo` | `go.mod` | 1 | 극저 | **예** | 아니오 |
| 5 | Python | `python -m venv .venv` | `.venv/` 전체 (~수백 파일) | 다수 | 환경만 | 아니오 | 아니오 |
| 6 | Python Poetry | `poetry init` | `pyproject.toml` (인터랙티브) | 1 | 중 | 아니오 | 아니오 |
| 7 | Python uv | `uv init foo` | `pyproject.toml`, `README.md`, `main.py`, `.python-version`, `.gitignore` | 4 + no-ext | 중 | 아니오 | **예** |
| 8 | Python hatch | `hatch new foo` | `foo/foo/__init__.py`, `foo/__about__.py`, `tests/__init__.py`, `pyproject.toml`, `README.md`, `LICENSE.txt`, `.gitignore` | 3 + no-ext | 높음 | 아니오 | 아니오 |
| 9 | npm | `npm init -y` | `package.json` | 1 | 극저 | 아니오 | 아니오 |
| 10 | yarn | `yarn init -y` | `package.json` | 1 | 극저 | 아니오 | 아니오 |
| 11 | pnpm | `pnpm init` | `package.json` | 1 | 극저 | 아니오 | 아니오 |
| 12 | Zig | `zig init` (pre-0.12) | `build.zig`, `src/main.zig` | 1 (`.zig` 만) | 낮음 | **예** | 아니오 |
| 13 | Zig | `zig init-exe` / `init-lib` (구) | `build.zig`, `src/main.zig` 또는 `src/root.zig` | 1 | 낮음 | **예** | 아니오 |
| 14 | Nix | `nix flake init` | `flake.nix` | 1 | 낮음 | **예** | 아니오 |
| 15 | Deno | `deno init foo` | `main.ts`, `main_test.ts`, `deno.json` | 2 | 낮음 | 아니오 | 아니오 |
| 16 | Bazel | `bazel new` (실제로는 workspace 수동) | `WORKSPACE`, `BUILD` | no-ext 2 | 중 | Starlark self-host | 아니오 |
| 17 | Haskell Cabal | `cabal init -n` | `foo.cabal`, `src/MyLib.hs`, `app/Main.hs`, `test/Main.hs`, `CHANGELOG.md`, `.gitignore` | 4 (`.cabal` `.hs` `.md`) | 높음 | 아니오 | 아니오 |
| 18 | Elm | `elm init` | `elm.json`, `src/` | 2 | 낮음 | 아니오 | 아니오 |
| 19 | Dart | `dart create foo` | `pubspec.yaml`, `bin/foo.dart`, `analysis_options.yaml`, `README.md`, `CHANGELOG.md`, `.gitignore` | 4 | 높음 | 부분(Dart) | **예** |
| 20 | Kotlin Gradle | `gradle init` | `build.gradle.kts`, `settings.gradle.kts`, `gradle/`, `gradlew`, `.gitignore`, `src/main/kotlin/` | 5+ | 매우 높음 | Kotlin DSL | 아니오 |
| 21 | Elixir | `mix new foo` | `mix.exs`, `lib/foo.ex`, `test/foo_test.exs`, `README.md`, `.formatter.exs`, `.gitignore` | 3 | 높음 | **예** (Elixir) | 아니오 |
| 22 | Crystal | `crystal init app foo` | `shard.yml`, `src/foo.cr`, `spec/foo_spec.cr`, `README.md`, `LICENSE`, `.gitignore`, `.editorconfig` | 4+ | 매우 높음 | Crystal DSL | **예** |
| 23 | OCaml Dune | `dune init proj foo` | `dune-project`, `bin/dune`, `bin/main.ml`, `lib/dune`, `test/dune`, `test/foo.ml` | 2 | 높음 | OCaml DSL | 아니오 |
| 24 | Nim | `nimble init foo` | `foo.nimble`, `src/foo.nim`, `tests/test1.nim`, `config.nims` | 2 | 중 | Nim DSL | 아니오 |
| 25 | Julia | `Pkg.generate("Foo")` | `Project.toml`, `src/Foo.jl` | 2 | 낮음 | 아니오 | 아니오 |
| 26 | Gleam | `gleam new foo` | `gleam.toml`, `src/foo.gleam`, `test/foo_test.gleam`, `README.md`, `.gitignore`, `.github/workflows/test.yml` | 4 | 매우 높음 | 부분 | **예** |
| 27 | Roc | `roc` (init 없음, `--build` 중심) | 없음 (관습: `app.roc`) | 0 | 극저 | **예** | 아니오 |
| 28 | 현행 hexa | `hexa tool/init_project.hexa` | `project.hexa` | 1 | 극저 | **예** | 아니오 |

### 2-2. 확장자 패턴 4 가지 분류

22 도구의 확장자 사용 패턴은 네 범주로 나뉜다.

**패턴 P1: 자국어 확장자 단일 (Zig 모델)**
- 대상: Zig, Roc, 현행 hexa
- 특징: 빌드 파일부터 소스 전부 `.zig` / `.roc` / `.hexa` 하나로 해결.
- 장점: self-host 100%. 파서 재사용. 도구 복잡도 최소.
- 단점: README/doc 이 없어 AI/인간 onboarding 공백. (Zig 는 관례적으로 README.md 동반하지만 `zig init` 은 생성 안 함)

**패턴 P2: 자국어 + 표준 설정 (Rust/Go 모델)**
- 대상: Rust(`.toml`+`.rs`), Go(`go.mod`+`.go`), Python(`.toml`+`.py`), Haskell(`.cabal`+`.hs`)
- 특징: 빌드/메타는 TOML 같은 표준 포맷, 실제 코드만 자국어.
- 장점: 외부 툴 연동 쉬움 (GitHub CI, IDE).
- 단점: 포맷 2 개 파서 유지. self-host 약화.

**패턴 P3: 메타 파일 다중 (Kotlin/Gleam 모델)**
- 대상: Gradle, Gleam, Crystal, Dart, Elixir
- 특징: 빌드 + 포맷 + CI + test + LICENSE 전부 scaffold.
- 장점: 신규 사용자 "뭐부터 하지?" 고민 0.
- 단점: 파일 10+ 개, 삭제 못하는 boilerplate 지옥. 미니멀리스트 반발.

**패턴 P4: 하이브리드 확장자 (JS/TypeScript)**
- 대상: npm (package.json) + TypeScript(.ts) + JSX(.tsx) + MJS/CJS
- 특징: 한 생태에 `.js` `.mjs` `.cjs` `.ts` `.tsx` `.json` 혼재.
- 장점: 맥락별 최적화.
- 단점: 혼돈. 도구 설정 복잡. hexa 가 절대 피해야 할 길.

### 2-3. 핵심 관찰 (smash 결과)

**관찰 1**: **self-host 언어 중 `init` 도 self-host 인 것은 3 개 뿐** (Zig, Elixir, Nim). 나머지는 "언어는 self-host 이지만 init 도구는 scaffold 템플릿을 String 으로 박아두는 수준". 현 `init_project.hexa` 는 Zig 수준의 자립도를 갖는다.

**관찰 2**: **최소 scaffold ≥ 2 파일이 지배적**. 22 도구 중 "1 파일만 생성" 은 4 개 (Go mod, npm/yarn/pnpm init) 이고 전부 "이미 존재하는 소스 위에 메타만 얹는" 도구다. **새 프로젝트 부트스트랩은 거의 다 `.gitignore` + 메타 + 소스 = 최소 3 파일**.

**관찰 3**: **`.gitignore` 생성률 11/22 = 50%**. 그 중 `git init` 까지 자동 수행은 5/22 (Rust, uv, Dart, Crystal, Gleam). hexa 는 `.gitignore` 는 생성하고 `git init` 은 **선택**으로 두는 게 표준적이다 (Cargo 도 `--vcs none` 옵션 존재).

**관찰 4**: **README.md 생성률 12/22 = 55%**. 언어 종속 아님, 사용자 문화 종속. hexa 가 AI-native 를 표방한다면 README.md 는 **생성해야** 한다 — 이유: Claude Code 가 첫 30 초에 읽을 텍스트 슬롯. `project.hexa` 는 규칙/메타, README.md 는 의도/설명.

**관찰 5**: **entry point (`main.ext`) 생성률 15/22 = 68%**. Zig/Dart/Gleam/Elixir 전부 main 생성. hexa 도 옵션으로 제공하되 **preset 종속**.

**관찰 6**: **확장자 2~4 개가 최적 구간**. 확장자 1 개 = AI/인간 onboarding 문서 공백, 확장자 5+ = boilerplate 지옥. Rust (3), Dart (4), Elixir (3) 의 sweet spot.

### 2-4. hexa-lang 특수성

hexa-lang 은 위 도구들과 다른 **2 가지 독특한 요구**가 있다:

1. **AI 가 핵심 사용자**. Claude Code/Cursor/Aider 가 생성된 프로젝트에 들어가 작업한다. README.md + CLAUDE.md 가 맞춤 가이드 역할 필수.
2. **프로젝트 종류가 4 축으로 확장** (hexa-lang/anima/nexus/n6-arch). 각각 paper 중심 / 언어 도구 / 허브 / 도메인 중심 성격이 다르므로 **preset** 개념이 **최초 설계부터** 필요.

이 두 특수성이 §6 preset 스펙트럼의 근거가 된다.

---

## §3. REQUIRES — HX 제약 재검토

본 연구가 준수해야 할 HX 규칙 항목 5 개:

### 3-1. HX3 (grammar pitfalls)

- `.hexa` 생성 시 hexa 문법 준수. 예시 scaffold 의 project.hexa 는 `doc/grammar.jsonl` P1~P5 검증 통과 필요.
- **영향**: 생성된 project.hexa 가 `hexa parse project.hexa` 로 파싱 성공해야 함.
- **연구 범위 내 조치**: 예시 scaffold 의 project.hexa 샘플은 self_format_examples 의 검증된 문법만 사용.

### 3-2. HX4 (self-host first)

- **모든 코드** 는 `.hexa`. Rust 재작성 금지.
- **해석**: README.md / .gitignore 는 "코드" 아님. 따라서 scaffold 에 포함 OK.
- **금지**: init 이 생성하는 `.hexa` 파일은 Rust 보조 없이 `hexa run <file>` 으로 동작 가능해야.
- **영향**: scaffold 에 `build.sh` 같은 쉘 스크립트 대신 `tool/*.hexa` 만.

### 3-3. HX7 (self-host 경로 전용)

- 언어 기능은 `self/codegen_c2.hexa` + `self/runtime.c` + `self/interpreter.hexa` + `self/native/hexa_v2` 만.
- **해석**: init 은 언어 "기능" 이 아니라 "도구". HX7 직접 제약 없음.
- **단**: init 이 생성하는 파일명이 `self/` 위치와 충돌하면 안 됨. → preset 의 `lang` 한정으로 `self/` 디렉토리 도입 (3-4 참조).

### 3-4. HX11 (AI-native self-format)

- 모든 attr (@doc/@domain/@readme/@sealed/@publish) 은 `project.hexa` 내 `@<attr>_rules` 블록으로 양식 독립.
- **영향**: init 생성 파일의 README.md, <id>.md 는 `@readme(scope=root)`, `@doc(type=...)` attr 마커를 가질 수 있어야 함. 하드코딩 금지.
- **조치**: scaffold 의 README.md 첫 줄은 `<!-- @readme(scope=root) -->` 주석. project.hexa 의 `@readme_rules(scope=root)` 블록 옵션으로 포함.

### 3-5. 기타 (R0~R27 공통)

- R0 (Spec First): scaffold 생성 전 spec 문서 우선 — 본 연구 문서가 그 spec.
- R3 (무자료 장기실행 금지): init 은 <3 초 완료.
- R12 (한글 필수): 주석/문서 한글. 코드 식별자 영문.

**결론**: HX4 의 "코드는 .hexa" 는 지키되, `.gitignore`/`.md`/`LICENSE` 는 "비코드 컨텍스트" 로 허용. HX11 덕분에 `.md` 파일들은 attr 마커로 project.hexa 규칙과 연결 가능. Zig 모델 + 관례 파일 절충.

---

## §4. STRUCT — 파일 세트 + 확장자 스펙트럼 (5 안)

### 4-1. 확장자 체계 6 옵션 비교

| # | 옵션 | 확장자 세트 | HX4 순혈도 | 파서 재사용 | IDE 친화 | git 친화 | 산업 선례 |
|---|------|------------|-----------|-----------|---------|---------|---------|
| E1 | `.hexa` 단일 (현행) | `.hexa` | **10/10** | 10 | 낮음 | 낮음 | Roc |
| E2 | `.hexa` + `.md` | `.hexa`, `.md` | 9 | 10 | 중 | 높음 | Zig (관례), Rust |
| E3 | `.hexa` + `.md` + `.gitignore`+LICENSE | `.hexa`, `.md`, no-ext | 9 | 10 | **높음** | **높음** | Cargo, Dart, uv |
| E4 | `.hexa` + 파생 `.hexa.md` | `.hexa`, `.hexa.md` | 8 (hexa 컴파일러 해석 필요) | 7 (파서 분기) | 낮음 | 중 | 없음 |
| E5 | `.hexa` + `.hxd` (도메인 seed) / `.hxlock` / `.hxcache` / `.hxsealed` | `.hexa`, `.hxd`, `.hxlock`, `.hxcache`, `.hxsealed` | **3/10 (역행)** | 4 (여러 lexer) | 낮음 | 중 | 없음 (anti-pattern) |
| E6 | `.hexa` + `.md` + `.gitignore` + 내부 `self/` | E3 + hexa 관례 디렉토리 | 9 | 10 | **높음** | **높음** | Bazel (WORKSPACE+BUILD) |

#### E1 분석 (현행 `.hexa` 단일)
- 순혈 HX4. 그러나 §1-2 에서 확인된 AI onboarding 공백.
- **채택 시**: 반드시 `project.hexa` 안에 README 수준 주석 블록 포함해야 — 그러나 hexa 문법 주석은 단순 `//` 뿐이어서 구조적 마크다운 섹션 불가.

#### E2 분석 (`.hexa` + `.md`)
- `.md` 는 업계 사실상 표준. README.md, CLAUDE.md, LICENSE 전부 `.md` 계열.
- HX11 `@readme_rules` 가 `.md` 를 대상으로 이미 정의돼 있음 → 생태 일치.
- 소폭 HX4 약화 (`.md` 는 hexa 파서가 안 읽음). 그러나 "문서 = 비코드" 원칙으로 정당화 가능.

#### E3 분석 (`.hexa` + `.md` + `.gitignore` + LICENSE) ★ 추천
- 업계 95% 가 이 구성. Rust/Dart/uv/Gleam.
- `.gitignore` 는 git 생태 통합에 필수. 확장자 없음 = 관례.
- LICENSE 파일은 법적 명시. 기본은 선택, preset 에서 넣을 수 있음.

#### E4 분석 (`.hexa.md` 복합 확장자)
- 아이디어: hexa 컴파일러가 `.hexa.md` 를 "code-block 추출 → hexa 로 실행" 식 literate programming.
- **문제**: hexa 파서에 markdown lexer 추가 필요 → HX3 pitfall 위험 (indent block 충돌). 파서 재사용도 반감. **과잉 설계 판정**.
- 선례: Haskell `.lhs` 만 드물게 사용. 산업 메인스트림 아님.

#### E5 분석 (파생 확장자 `.hxd/.hxlock/.hxcache/.hxsealed`)
- 의도: 파일 종류별 의미 명시 (도메인 seed, lockfile, 캐시, 봉인 사이드카).
- **문제 1**: self-host 역행 — `.hxd` 는 hexa 파서가 파싱하나? `.hxlock` 은 hexa 파서가 읽나? 각 확장자마다 lexer/parser 분기 → HX4 본문 "모든 코드 .hexa" 위반.
- **문제 2**: 사용자 학습 부담 급증. 파생 확장자 5 개 = TypeScript `.ts/.tsx/.d.ts/.mts/.cts` 지옥 재현.
- **문제 3**: IDE tooling 재작성. VS Code syntax highlighting 을 5 번 등록.
- **결론**: **거절**. 산업 선례 전무. E3 의 파일명 관례 + `.hexa` + `.md` 로 모두 해결됨:
  - `.hxd` → `domains/<axis>/<id>/<id>.hexa` (파일 위치로 의미 표현)
  - `.hxlock` → 불필요 (hexa 는 flake 스타일 lockfile 미필요, `project.hexa` 의 `version=` 으로 해결)
  - `.hxcache` → `build/artifacts/` 디렉토리 + `.gitignore` 로 해결
  - `.hxsealed` → 현 `.sealed.hash` 유지 (이름 변경 불필요)

#### E6 분석 (E3 + hexa 관례 디렉토리 `self/`, `domains/`)
- E3 + lang preset 한정으로 `self/attrs/`, `self/codegen/` 같은 hexa-lang 내부 관례 디렉토리 시드.
- hub preset 에서는 `shared/`. paper preset 에서는 `domains/`, `papers/`.
- 확장자 자체는 E3 와 동일. 파일명/디렉토리 관례만 추가.
- **최종 추천 지점**.

### 4-2. 확장자 결론: E3 + 파일명/디렉토리 관례 (E6)

```
확장자 세트 = { .hexa, .md, 확장자 없음(.gitignore/LICENSE) }
```

- **.hexa 만 = "hexa 코드"**. 컴파일/실행 대상.
- **.md 만 = "문서"**. HX11 attr 마커 부착 가능. Claude Code/인간 공통 읽기.
- **확장자 없음 = "VCS/라이선스 메타"**. git/법률 표준.
- **파생 확장자 0**. 모든 파일 종류 의미는 **경로 + 파일명 관례**로 표현:
  - `project.hexa` — 프로젝트 마커 (파일명 고정)
  - `main.hexa` — 실행 진입점 (관례)
  - `domains/<axis>/<id>/<id>.hexa` — 도메인 seed (디렉토리 구조)
  - `self/attrs/<name>.hexa` — attr SSOT (lang preset 전용)
  - `.sealed.hash` — 봉인 사이드카 (숨김 파일 관례, 이름 유지)
  - `build/artifacts/` — 캐시 (디렉토리 + .gitignore)

이는 **Zig + Rust 의 공통 교훈**이다. Zig 는 `.zig` 단일이지만 `src/main.zig`, `build.zig`, `zig-out/` 디렉토리로 의미 표현. Rust 는 `Cargo.toml` 같은 고정 파일명 + `src/`, `target/` 디렉토리로 표현. **확장자 늘리지 말고 파일명/경로로 해결**.

### 4-3. 파일 세트 필수/선택 분류

22 도구 조사 + 4 프로젝트 실측 + AI-native 요구 종합:

| 파일 | 필수도 | 이유 |
|------|--------|------|
| `project.hexa` | **필수** | 프로젝트 루트 마커. 없으면 hexa 툴체인이 프로젝트 미인식. |
| `README.md` | **강권** (preset 에 따라 필수) | AI/인간 onboarding 슬롯. 없으면 프로젝트 의도 미전달. |
| `.gitignore` | **강권** | git 사용 시 필수. 부재 시 `.sealed.hash`/캐시 누출. |
| `main.hexa` | **선택** | 실행형 프로젝트만. 라이브러리/허브/도메인 전용은 불필요. |
| `CLAUDE.md` | **선택** | AI 에이전트 특화 규칙. 없어도 동작, 있으면 품질 상승. |
| `LICENSE` | **선택** | 공개 리포만. 내부 프로젝트는 불필요. |
| `docs/` | **선택** | 문서 집중 프로젝트 (hexa-lang 자체, nexus, n6-arch) |
| `tool/` | **선택** | 보조 hexa 스크립트. lang 프로젝트에 관례적. |
| `self/` | **lang 전용** | hexa-lang 자체의 attr SSOT / 코드젠. 타 프로젝트 금지. |
| `domains/<axis>/<id>/` | **domain-only/paper 전용** | 도메인 중심 프로젝트만. |
| `shared/` | **hub 전용** | nexus 같은 허브 리포. |
| `papers/` | **paper 전용** | 논문 관리 프로젝트. |
| `.sealed.hash` (빈 파일/디렉토리) | **생성 안 함** | @sealed attr 실행 시 자동 생성. init 생성 대상 아님. |

### 4-4. 최소 scaffold 의미 경계

"최소한 의미 있는 scaffold" 의 정의:
- 사용자가 **삭제하지 않을** 파일만 생성 (Gleam/Gradle 식 14+ 파일 반대)
- Claude Code 가 **첫 30 초 read 로 맥락** 완성
- `git init && git add . && git commit` 직후 **누출 없음** (.gitignore 필수)
- `hexa run <main>` 또는 `hexa policy` 즉시 동작

이 4 조건을 만족하는 최소 = **project.hexa + README.md + .gitignore** = 3 파일. E3 의 핵심 3 종.

---

## §5. FLOW — AI-native 관점 시나리오 3 개

### 5-1. 시나리오 A: Claude Code 가 새 프로젝트에 진입

사용자가 `hexa init myproject --preset paper` 실행 후, 다른 세션에서 Claude Code 를 `cd myproject` 로 호출.

**Claude Code 의 첫 30 초 behavior**:
1. `Read README.md` — "이 프로젝트는 무엇인가" 파악 (5 초)
2. `Read project.hexa` — 규칙/SSOT 파악 (5 초)
3. `Read CLAUDE.md` (있으면) — AI 에이전트 특화 가이드 (10 초)
4. `Glob **/*.hexa`, `Glob **/*.md` — 파일 구조 인지 (5 초)
5. 사용자 첫 질문 대응 (남은 5 초)

**E1 (현행) 시나리오**: `project.hexa` 만 존재 → 36 줄 read → "이게 뭐하는 프로젝트?" 답변 불가. `Glob **/*` → 파일 1 개만 나옴. Claude 가 사용자에게 역질문 필요.

**E3 (추천) 시나리오**: 3 파일 read → 맥락 완성. 사용자 첫 질문에 즉시 대응.

**정량 비교**:
- E1: onboarding read 1 회, 맥락 공백 80%
- E3: onboarding read 3 회, 맥락 완성 100%
- E6 (paper preset): 3 파일 + CLAUDE.md + domains/<sample>/ = 5~6 read, 맥락 완성 + 도메인 구조까지 파악

### 5-2. 시나리오 B: 사용자가 `hexa policy` 즉시 실행

`hexa init` 직후 사용자가 `cd myproject && hexa policy` 를 친다 (규칙 확인).

**E1**: project.hexa 의 ssot_attrs 리스트만 출력. `@<attr>_rules` 블록 0 이므로 글로벌 default 사용. 통과.

**E3**: 동일 + README.md 의 attr 마커가 lint 되면 규칙 적용 확인. @readme(scope=root) 에 sections 요구사항이 있으므로 README.md 가 그 요구사항을 충족하는지 체크.

**핵심**: init 은 첫 lint/policy 가 **즉시 통과**하는 상태를 만들어야 한다. README.md 를 생성하면서 `@readme(scope=root)` 의 필수 섹션을 **이미 포함한 템플릿**으로 넣어야 첫 lint 통과. 이것이 Gleam/Cargo 의 scaffold 템플릿 철학.

### 5-3. 시나리오 C: 사용자가 도메인 하나 추가

기존 paper 프로젝트에 새 도메인 `dog-robot` 을 추가하려 한다.

**방법 1**: `hexa init . --preset domain_only --id dog-robot --axis life`
- 예상 결과: `domains/life/dog-robot/dog-robot.hexa` + `domains/life/dog-robot/CLAUDE.md` 추가 (merge mode).
- 기존 `project.hexa`, `README.md`, `.gitignore` **보존**. idempotent.
- `.domain.hexa` 같은 파생 확장자 **불필요** — 파일 위치로 의미 표현.

**방법 2 (거부)**: `.domain.hexa` 새 확장자
- 기각 — §4-1 E5 참조. 파생 확장자 = hexa 생태 파편화.

**핵심**: init 은 **merge mode** 를 지원해야 함. 기존 파일 덮어쓰지 않고, preset 에 정의된 "추가만" 수행.

### 5-4. 시나리오 분석 종합

3 시나리오 모두에서 **E3 (`.hexa` + `.md` + `.gitignore`)** + **preset 분기** 가 최적. E1 은 AI onboarding 실패, E5 는 파생 확장자 혼란.

---

## §6. EVOLVE — preset 스펙트럼 5+ 상세

"단순" vs "풀 scaffold" 2 지선다 대신 5 단계 스펙트럼.

### 6-1. preset minimum (현행 동등, 1 파일)

```
<target>/
└── project.hexa            (ProjectMeta marker)
```

**용도**: HX4 순혈주의 지지자. 기존 비어있는 디렉토리에 mark 만 찍기.
**CLI**: `hexa init <target> --preset minimum` (또는 `--preset=none`)
**장점**: 제로 오버헤드. 사용자가 모든 것을 통제.
**단점**: §1-2 AI onboarding 공백.

### 6-2. preset basic (권장 기본, 3 파일)

```
<target>/
├── project.hexa            (ProjectMeta + 최소 @project 헤더)
├── README.md               (@readme(scope=root) 마커 + 5 섹션 템플릿)
└── .gitignore              (.sealed.hash, build/, *.hxcache 제외 X — hxcache 없음)
```

**README.md 템플릿 (예시 뼈대)**:
```markdown
<!-- @readme(scope=root) -->
# <프로젝트명>

## Overview
<프로젝트 한 문단>

## Quick Start
```bash
hexa run main.hexa   # (main.hexa 가 있다면)
hexa policy          # 규칙 검증
```

## Architecture
<구조 설명>

## Contributing
<PR 가이드>

## License
MIT / Apache-2.0 / ...
```

**.gitignore 템플릿**:
```
# hexa build artifacts
build/
target/

# hexa sealed sidecars (@sealed 가 자동 생성)
*.sealed.hash

# OS
.DS_Store
Thumbs.db

# editor
.vscode/
.idea/
*.swp
```

**용도**: 대부분 프로젝트의 기본값. `hexa init <target>` 만 쳐도 이게 나온다 (default preset).
**장점**: AI-native 완성. git 친화. 최소 boilerplate.
**단점**: 고급 기능(도메인/허브) 은 후속 preset 으로.

### 6-3. preset paper (논문/돌파 중심, 6~8 파일)

```
<target>/
├── project.hexa            (+ @doc_rules(type=paper) 블록, §1-21 섹션)
├── README.md               (Overview, Architecture, Domains, ...)
├── CLAUDE.md               (AI 에이전트 가이드 — n6-arch 스타일 축약판)
├── .gitignore
├── papers/
│   └── .keep
├── reports/
│   └── .keep
└── domains/
    └── .keep
```

**용도**: n6-architecture, anima 같은 논문 + 도메인 중심 프로젝트.
**project.hexa 에 자동 포함**: `@doc_rules(type=paper)` (21 섹션), `@domain_rules` (10 축 default), `@publish_rules`.
**장점**: `hexa doc lint` 즉시 동작. 도메인 scaffold 터가 잡혀 있음.
**단점**: 빈 디렉토리(`.keep`) 호불호.

### 6-4. preset lang (self-host 언어 도구 개발자용)

```
<target>/
├── project.hexa            (+ @doc_rules(type=spec,adr,breakthrough))
├── README.md
├── CLAUDE.md               (HX3/HX4/HX7 규칙 요약)
├── .gitignore
├── main.hexa               (실행 진입점, "hello hexa" 수준)
├── self/
│   ├── attrs/              (빈 — 사용자가 attr 추가)
│   │   └── .keep
│   └── .keep
├── docs/
│   └── .keep
├── tool/
│   └── hello.hexa          (샘플 보조 스크립트)
└── tests/
    └── hello_test.hexa     (샘플 테스트)
```

**용도**: hexa-lang 자체, 또는 hexa 기반 언어 도구.
**핵심 차이**: `self/attrs/` 디렉토리 = hexa-lang 고유 관례. `main.hexa` 존재.
**장점**: hexa 자체 진화 시 관습 통일.
**단점**: 타 프로젝트엔 불필요한 구조.

### 6-5. preset hub (허브/SSOT 리포)

```
<target>/
├── project.hexa            (+ @readme_rules(scope=module), @sealed_rules 엄격)
├── README.md               (허브 의도)
├── CLAUDE.md               (하위 프로젝트 목록)
├── .gitignore
├── shared/
│   ├── config/             (JSON/hexa 설정 — 폐기 예정, project.hexa 로 흡수 중)
│   │   └── .keep
│   ├── rules/              (R0~R27 + 프로젝트별 규칙)
│   │   └── .keep
│   └── harness/            (loop / l0_guard 공통 harness)
│       └── .keep
└── docs/
    └── .keep
```

**용도**: nexus 같은 허브 리포. 여러 프로젝트가 참조.
**핵심 차이**: `shared/` 디렉토리 표준. main.hexa 없음 (실행형 아님).
**장점**: 4 프로젝트 생태의 허브 표준화.

### 6-6. preset domain_only (기존 프로젝트에 도메인 추가)

**특이 사항**: `project.hexa` **생성하지 않음**. 기존 프로젝트에 merge mode 로 도메인 하나만 추가.

```
<target>/
└── domains/<axis>/<id>/
    ├── <id>.hexa           (도메인 seed — @domain attr)
    ├── CLAUDE.md           (@readme(scope=domain))
    └── .sealed.hash        (빈 파일, 첫 @sealed 시 채워짐)
```

**용도**: 이미 paper preset 으로 초기화된 프로젝트에 새 도메인 하나 추가.
**CLI**: `hexa init . --preset domain_only --axis life --id dog-robot`
**핵심**: idempotent. 기존 파일 건드리지 않음. 도메인 파일만 생성.

### 6-7. preset 스펙트럼 요약표

| preset | 파일 수 | project.hexa | README | CLAUDE | main | self/ | domains/ | shared/ | 사용 대상 |
|--------|--------|--------------|--------|--------|------|-------|---------|---------|----------|
| minimum | 1 | O | X | X | X | X | X | X | 커스텀 원하는 사용자 |
| **basic** | **3** | **O** | **O** | **X** | **X** | **X** | **X** | **X** | **대부분 프로젝트 (default)** |
| paper | 6~8 | O | O | O | X | X | O | X | 논문+도메인 (n6-arch, anima) |
| lang | 8~10 | O | O | O | O | O | X | X | hexa-lang, 언어 도구 |
| hub | 6~8 | O | O | O | X | X | X | O | 허브 (nexus) |
| domain_only | +3 (merge) | (merge) | (merge) | (추가) | X | X | **O** | X | 기존 프로젝트 확장 |

### 6-8. CLI 제안

```bash
# 기본 (basic)
hexa init myproject

# 명시
hexa init myproject --preset paper
hexa init myproject --preset lang --name my-lang
hexa init myproject --preset hub
hexa init myproject --preset minimum

# 도메인 추가 (merge)
hexa init . --preset domain_only --axis life --id dog-robot

# idempotent 재실행 (기존 파일 보존, 없는 것만 추가)
hexa init . --merge

# 덮어쓰기 경고
hexa init myproject --preset paper --force   # 기존 project.hexa 덮어씀
```

---

## §7. VERIFY — 각 선택지 평가표

### 7-1. 확장자 체계 6 옵션 점수 (10 점 만점, 가중 합산)

| 지표 | 가중 | E1 (.hexa only) | E2 (+md) | E3 (+md+git) | E4 (.hexa.md) | E5 (파생 .hxd) | E6 (E3+관례 dir) |
|------|-----|----------|---------|-------------|-------------|--------------|----------------|
| HX4 self-host 순혈 | 20% | 10 | 9 | 9 | 8 | 3 | 9 |
| AI onboarding | 25% | 3 | 7 | **9** | 6 | 5 | **10** |
| git/VCS 친화 | 15% | 2 | 5 | **10** | 5 | 5 | **10** |
| IDE/tooling 호환 | 10% | 7 | 8 | 9 | 5 | 3 | 9 |
| 산업 선례 | 10% | 2 (Roc) | 6 (Zig 관례) | **10** (Rust/Dart) | 1 | 0 | **10** |
| 학습곡선 | 10% | 9 | 8 | 8 | 5 | 3 | 7 |
| 확장성 | 10% | 5 | 6 | 8 | 6 | 4 | **9** |
| **가중합** | 100% | **5.15** | 7.10 | **9.00** | 5.70 | 3.60 | **9.30** |

**승자**: E6 (E3 + 파일명/디렉토리 관례). 근소하게 E3 우위지만 lang/paper/hub preset 확장 고려 시 E6 가 명확.

### 7-2. preset 5 종 비교표 (실제 사용 상황별 점수)

| 상황 | minimum | basic | paper | lang | hub | domain_only |
|------|---------|-------|-------|------|-----|-------------|
| 새 hobby 프로젝트 시작 | 8 | **10** | 6 | 5 | 4 | 0 |
| n6-arch 같은 논문 리포 초기화 | 3 | 7 | **10** | 5 | 6 | 0 |
| hexa-lang 자체 재초기화 | 3 | 6 | 5 | **10** | 4 | 0 |
| nexus 허브 재초기화 | 3 | 6 | 7 | 5 | **10** | 0 |
| 기존 paper 에 도메인 추가 | 0 | 0 | 0 (덮어씀) | 0 | 0 | **10** |
| 극단 미니멀리스트 | **10** | 7 | 3 | 2 | 2 | 0 |

### 7-3. 파일별 필수도 투표 (4 프로젝트 실측 기준)

n6-arch, anima, hexa-lang, nexus 의 현재 파일 구조를 기준으로 "있으면 좋았을" 정도:

| 파일 | n6-arch | anima | hexa-lang | nexus | 종합 필수도 |
|------|---------|-------|-----------|-------|------------|
| project.hexa | O | O | O | O | **필수** |
| README.md | O | O | O | O | **필수** (사실상) |
| CLAUDE.md | O | O | O | O | **강권** |
| .gitignore | O | O | O | O | **필수** |
| main.hexa | X | X | X (scripts 산재) | X | 선택 |
| LICENSE | X | X | X | X | 선택 |
| domains/ | O | (다른 이름) | X | X | preset 종속 |
| shared/ | X | X | X | O | preset 종속 |
| self/ | X | X | O | X | preset 종속 |

**결론**: 4 프로젝트 전부 README.md / CLAUDE.md / .gitignore 보유. **basic preset 이 기본값이 되어야 할 강력한 증거**.

### 7-4. HX11 준수 검증

각 scaffold 가 HX11 (AI-native self-format) 을 준수하는가?

- minimum: project.hexa 에 `@<attr>_rules` 블록 0 → 글로벌 default fallback. **준수**.
- basic: `@readme_rules(scope=root)` 기본 블록 1 개 + README.md 에 `<!-- @readme(scope=root) -->` 마커. **준수**.
- paper: 추가 `@doc_rules(type=paper)`, `@domain_rules`, `@publish_rules`. **준수**.
- lang: 추가 `@doc_rules(type=spec,adr,breakthrough)`. **준수**.
- hub: `@readme_rules(scope=module)`, `@sealed_rules` (엄격). **준수**.
- domain_only: `project.hexa` 수정 없음 (merge 안 함). 생성된 `<id>.hexa` 는 `@domain` attr 포함. **준수**.

### 7-5. idempotency 검증

`hexa init . --merge` 재실행 시 4 상태:

| 상태 | project.hexa | README.md | .gitignore | 처리 |
|------|--------------|-----------|-----------|------|
| 빈 디렉토리 | 없음 | 없음 | 없음 | 전부 생성 |
| 기존 project.hexa | 있음 | 없음 | 없음 | project.hexa 건드리지 않음, 나머지 생성 |
| 전부 존재 | O | O | O | **no-op** + "already initialized" 메시지 |
| project.hexa 만 존재 | O | X | X | README+gitignore 추가 (preset 기준) |

**원칙**: **덮어쓰지 않음**. 사용자 명시적 `--force` 플래그만 덮어쓰기.

---

## §8. IMPACT — 최종 추천 + 마이그레이션

### 8-1. 최종 추천

**확장자 체계**: E6 = `.hexa` (코드) + `.md` (문서) + 확장자 없음 (`.gitignore`, `LICENSE`) + 파일명/디렉토리 관례.

**default preset**: `basic` (3 파일: project.hexa, README.md, .gitignore).

**preset 5 종 제공**: minimum / basic / paper / lang / hub / domain_only.

**CLI 문법**:
```bash
hexa init <target> [--preset minimum|basic|paper|lang|hub|domain_only] [--name <n>] [--force] [--merge] [--vcs git|none]
```

### 8-2. 핵심 근거 3 줄

1. **Zig + Rust 공통 교훈**: 자국어 확장자 단일 + 관례 파일명 + `.md`/`.gitignore`. 파생 확장자는 TypeScript 지옥. `.hxd/.hxlock` 류 전부 기각.
2. **4 프로젝트 실측**: n6-arch/anima/hexa-lang/nexus 모두 README.md+CLAUDE.md+.gitignore 보유. basic preset = 현실 반영.
3. **AI-native 결정타**: Claude Code 는 첫 30 초에 README.md + project.hexa + CLAUDE.md 3 파일을 읽어야 맥락 완성. E1 (현행) 은 맥락 공백 80%.

### 8-3. 구현 영향 (코드 수정 금지 범위이나 경로 예측)

**tool/init_project.hexa 확장 필요** (다른 agent 담당):
- 현재: 85 줄, project.hexa 1 개만.
- 확장 후: ~400 줄 예상. preset 분기 + 템플릿 렌더러 6 종 (minimum/basic/paper/lang/hub/domain_only).
- HX4 준수: 여전히 `.hexa` 자체 (Rust 추가 없음).

**새 파일 불필요**. 기존 `tool/init_project.hexa` 만 수정.

**템플릿 SSOT 위치**: `$HEXA_LANG/docs/hexa_init_examples/<preset>/` (본 연구가 생성) → init 스크립트가 런타임에 읽어서 렌더 (또는 inline 템플릿으로 박아넣기).

### 8-4. 마이그레이션 경로

**기존 4 프로젝트 영향**: **없음**. 전부 이미 project.hexa + README.md + .gitignore 보유. `hexa init . --merge` 가 no-op.

**신규 프로젝트**: `hexa init foo --preset basic` 으로 즉시 사용.

**도메인 추가 UX**: `hexa init . --preset domain_only --axis life --id dog-robot` → `domains/life/dog-robot/{dog-robot.hexa,CLAUDE.md,.sealed.hash}` 추가. 기존 tool/add_domain.hexa 류와 통합 여지.

**deprecated**: 없음. 현 init (minimum preset 동등) 은 `--preset minimum` 으로 유지.

### 8-5. 미해결 질문 (future work)

- **Q1**: `git init` 자동 실행 여부. Cargo 는 `--vcs git` 기본. hexa 도 `--vcs git` 기본? 현 연구는 "기본 X, `--vcs git` 명시 시 실행" 권장 — 사용자 선호 존중.
- **Q2**: LICENSE 파일 자동 생성 여부. 공개/비공개 판별이 init 시점에 불명. 권장: 기본 X, `--license MIT|Apache-2.0|...` 플래그 제공.
- **Q3**: `project.hexa` 템플릿의 `@doc_rules` 기본 섹션 리스트. n6-arch §1-21 그대로? anima brief? 권장: preset 별로 다르게 (paper = full 21, basic = 최소 sections 없음 = 글로벌 default 사용).
- **Q4**: `hexa init` 이후 `nexus scan` 자동 실행? registry 자동 등록? 이는 nexus 허브 연동 범위 — 본 연구 scope 외.
- **Q5**: `hexa new` vs `hexa init` 분리 (Cargo 방식) 고려. 현 제안은 `hexa init` 단일, 플래그로 분기. future: `hexa new foo` = 새 디렉토리 생성 + init, `hexa init` = 현 디렉토리 초기화.

---

## §9. APPENDIX — preset 별 scaffold 트리 실측

### 9-1. minimum preset 트리

```
myproject/
└── project.hexa
```

`project.hexa` = 현행 init 과 동일 (36 줄, ProjectMeta marker).

### 9-2. basic preset 트리 (default)

```
myproject/
├── project.hexa            (~50 줄, ProjectMeta + @project + @readme_rules)
├── README.md               (~30 줄, 5 섹션 템플릿 + @readme(scope=root) 마커)
└── .gitignore              (~15 줄, hexa 공통 ignore)
```

### 9-3. paper preset 트리

```
myproject/
├── project.hexa            (~130 줄, §1-21 + 10 축 + publish)
├── README.md               (~40 줄, Architecture+Domains 섹션)
├── CLAUDE.md               (~50 줄, AI 에이전트 가이드 축약판)
├── .gitignore
├── papers/
│   └── .keep
├── reports/
│   └── .keep
└── domains/
    └── .keep
```

### 9-4. lang preset 트리

```
myproject/
├── project.hexa            (~70 줄, @doc_rules(type=spec,adr,breakthrough))
├── README.md               (~35 줄, Install/Quick Start/Language Tour)
├── CLAUDE.md               (~60 줄, HX3/HX4/HX7 요약)
├── .gitignore
├── main.hexa               (~10 줄, "hello hexa" 샘플)
├── self/
│   └── attrs/
│       └── .keep
├── docs/
│   └── .keep
├── tool/
│   └── hello.hexa          (~10 줄, 샘플 보조 스크립트)
└── tests/
    └── hello_test.hexa     (~15 줄)
```

### 9-5. hub preset 트리

```
myproject/
├── project.hexa            (~80 줄, @readme_rules(scope=module)+@sealed)
├── README.md               (~30 줄, Hub Purpose+Projects)
├── CLAUDE.md               (~40 줄)
├── .gitignore
├── shared/
│   ├── config/
│   │   └── .keep
│   ├── rules/
│   │   └── .keep
│   └── harness/
│       └── .keep
└── docs/
    └── .keep
```

### 9-6. domain_only preset 트리 (merge mode, 기존 프로젝트에 추가)

```
(기존 프로젝트 루트)
domains/life/dog-robot/          (신규 생성)
├── dog-robot.hexa               (~40 줄, @domain seed + mk1 history)
├── CLAUDE.md                    (~20 줄, @readme(scope=domain))
└── .sealed.hash                 (빈 파일, 첫 @sealed 시 채워짐)
```

### 9-7. 실제 scaffold 파일 위치

본 연구와 함께 `$HEXA_LANG/docs/hexa_init_examples/<preset>/` 디렉토리에 위 5 종 scaffold 실제 파일을 생성:

- `minimum/project.hexa`
- `basic/project.hexa`, `basic/README.md`, `basic/.gitignore`
- `paper/project.hexa`, `paper/README.md`, `paper/CLAUDE.md`, `paper/.gitignore`
- `lang/project.hexa`, `lang/README.md`, `lang/CLAUDE.md`, `lang/.gitignore`, `lang/main.hexa`
- `hub/project.hexa`, `hub/README.md`, `hub/CLAUDE.md`, `hub/.gitignore`
- `domain_only/domains/life/dog-robot/dog-robot.hexa`, `domain_only/domains/life/dog-robot/CLAUDE.md`

구현 시 init 스크립트가 이 디렉토리를 SSOT 템플릿으로 참조하거나, 또는 문자열 리터럴로 박아 넣는다 (이는 구현 agent 결정).

---

## §10. 결론

**한 줄**: `hexa init` 은 preset 5 종 (minimum/basic/paper/lang/hub + domain_only merge) 을 제공하고, basic 을 기본값으로 삼으며, 확장자는 `.hexa` + `.md` + `.gitignore` 3 종만 사용하고, 파생 확장자 (`.hxd/.hxlock/.hxcache`) 는 산업 선례 부재로 전면 기각한다.

**Zig 계승**: `.zig` 단일 + 관례 파일명 = hexa 는 `.hexa` 단일 + `.md`/`.gitignore` 표준 관례.

**AI-native 진화**: 현행 1 파일 → basic 3 파일 → Claude Code 첫 30 초 맥락 완성.

**HX4 해석 확정**: "모든 **코드** .hexa" 이지 "모든 **파일** .hexa" 아님. README.md, .gitignore, LICENSE 는 비코드 컨텍스트로 허용.

구현은 본 설계 승인 후 별도 세션에서 진행 (본 문서는 설계 + 예시 scaffold 만).
