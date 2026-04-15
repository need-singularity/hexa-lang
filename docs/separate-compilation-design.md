# Separate Compilation 설계 메모 — A milestone

**Author**: hexa-lang maintainers
**Date**: 2026-04-15
**Status**: Design proposal (implementation NOT authorized)
**Triggers**: void (170KB 번들, build 30-45분, hexa_v2 jetsam SIGKILL)

## 1. 문제

- void_main.hexa 2997 LOC + `use` 2개 → flatten 후 **4569 LOC, 170KB 단일 번들**
- `hexa build` 한 번 = **40분+ transpile + 5초 clang** (B0 O(N²) fix 후 측정 기준 2026-04-13 536462e)
- hexa_v2 (916KB AOT 트랜스파일러 바이너리) 가 번들 처리 중 수 GB 할당 → macOS jetsam SIGKILL
- 한 줄 편집에 전체 재-transpile — edit→binary 사이클이 생산성 영살

**근본**: 모든 `use "..."` 가 한 translation unit 으로 flatten 되어 hexa_v2 가 매번 4569 LOC 를 처음부터 lex/parse/typecheck/codegen. 모듈 경계 없음.

## 2. 업계 선례 (간략)

| 언어 | Sidecar | 공개 범위 | Invalidation | Bootstrap 비용 |
|---|---|---|---|---|
| Rust | `.rmeta` (binary) | 타입 + generic + inline body | 버전 + content | 매우 높음 |
| Go | `__.PKGDEF` | 시그니처만 | import graph + content | 중간 |
| Swift | `.swiftmodule` + `.swiftinterface` | public + `@inlinable` | binary: 버전, text: stable | 매우 높음 |
| **OCaml** | **`.cmi` + `.cmx`** | **시그니처 only** | **CRC hash** | **낮음** |
| GHC | `.hi` | per-decl MD5 | fingerprint 비교 | 높음 |
| Zig | `.zir` + `.o` | 전체 lazy | SHA-256 content | 중간-높음 |

**hexa-lang 선택: OCaml `.cmi` 모델**
- hexa-lang 은 C 로 transpile → clang 이 LTO 담당. cross-unit inline 직렬화 불필요 (`-opaque` 효과 공짜).
- self-hosted transpiler 부담 최소 — 시그니처 직렬화 스키마만 구현하면 됨.
- Rust/Swift/GHC 의 generic / inline-body / per-decl fingerprint 인프라는 현재 hexa_v2 규모에 과중.

## 3. Target 아키텍처

```
현재 (monolithic):
  foo.hexa + bar.hexa + baz.hexa
    → flatten_imports → 단일 .hexa
      → hexa_v2 → 단일 foo.c (모든 fn)
        → clang → binary

제안 (separate):
  foo.hexa  ─ hexa_v2 -c ─→  foo.c  +  foo.hxi
  bar.hexa  ─ hexa_v2 -c ─→  bar.c  +  bar.hxi   (depends: foo.hxi)
  baz.hexa  ─ hexa_v2 -c ─→  baz.c  +  baz.hxi   (depends: foo.hxi, bar.hxi)
                                ↓
                          clang foo.c bar.c baz.c -o binary
```

**`.hxi` 컨텐츠 (v0)**:
- `pub fn`  의 signature: 이름, 파라미터 (이름 + 타입), 반환 타입
- `pub struct`: 이름, 필드 (이름 + 타입)
- `pub let` / `pub let mut`: 이름, 타입 (추론 필요)
- `extern fn`: 이름, 시그니처, `@symbol`/`@link` attrs
- Content hash (SHA-256 of source `.hexa`) — invalidation 판단용

**의도적 비포함**: fn body, generic bodies (hexa-lang 은 현재 generic 제한적), `struct methods` body, `@inlinable` — clang LTO 가 최종 optimization.

## 4. 구현 로드맵 (단계별)

| 단계 | 요지 | 소요 추정 | 이득 |
|---|---|---|---|
| **M0** | `.hxi` 스키마 정의 + emit only | 2일 | 파일 포맷 anchor, 변경 없음 |
| **M1** | `hexa build -c file.hexa -o file.c` (compile-only, link 없음) | 2-3일 | 수동 분리 실험 가능 |
| **M2** | `hexa link a.c b.c -o final` (clang 래퍼 + .hxi cross-check) | 1일 | sign-off 가능한 E2E |
| **M3** | `flatten_imports` 가 .hxi 있으면 해당 모듈 제외 | 2-3일 | 실제 speedup 첫 발견 |
| **M4** | cmd_build 자동 재귀: `hexa build foo.hexa` 가 내부적으로 의존 그래프 walk | 3-4일 | 유저 API 복원 + 증분 |
| **M5** | stdlib 사전 빌드 (`.hxi` + `.o` 배포) | 2일 | 첫-빌드 cold start 단축 |

**First milestone (작은 것부터) = M0 + M1 조합 (4-5일)**:
1. codegen_c2.hexa 에 `emit_hxi(ast, path)` 추가 — `pub fn`/`struct` 순회해 text-serialized signature 덤프 (JSON or 커스텀 — 추천: 커스텀 줄-단위 포맷으로 grep 가능)
2. main.hexa `cmd_build` 에 `-c` 플래그 추가 — flatten 건너뛰고 단일 파일만 transpile, .hxi 동시 emit
3. 수동 테스트: void_main.hexa 를 3개로 쪼개서 (`ui.hexa`, `state.hexa`, `main.hexa`) `-c` 로 각각 transpile + clang 으로 link → void 바이너리 재현

**M2 이후부터 측정 시작**.

## 5. 측정 프레임워크 (before/after)

**측정 기준**: void 레포 4569 LOC flattened 번들 (또는 3-모듈 분해본)

| 지표 | 현재 (baseline) | M2 목표 | M4 목표 |
|---|---|---|---|
| cold build (full transpile) | 40분+ | 40분+ (-c 오버헤드 소량) | 15분 |
| incremental 1-line edit (no-op module) | 40분+ | 수동 per-module | 5분 |
| incremental 1-line edit (target module) | 40분+ | 수동 per-module | 8분 |
| hexa_v2 peak RSS | ~수 GB → jetsam | 모듈별 수 MB? | 수 MB |
| jetsam survival (3회 연속) | <50% | 100% (모듈 작을수록) | 100% |
| link time (clang) | ~5초 | ~5초 (변화 없음) | ~5초 |

**측정 스크립트**:
```bash
# Cold build time
/usr/bin/time -l hexa build src/void_main.hexa -o /tmp/void_cold 2>&1 | grep -E 'real|maximum'
# Peak RSS (macOS)
/usr/bin/time -l ... | grep "maximum resident set size"
# Jetsam survival loop (sustained build pressure)
for i in {1..5}; do /usr/bin/time -l hexa build ... ; done | grep -c "Killed"
```

측정 로그: `shared/hexa/build-speed-metrics.jsonl` (append-only, 각 실행마다 `{ts, variant, cold_or_incr, real_sec, peak_rss_mb, jetsam}`)

## 6. 리스크 & 완화

| 리스크 | 가능성 | 완화 |
|---|---|---|
| runtime.c 전역 (`__hexa_intern` 등) 중복 정의 | 높음 | 이미 `_Thread_local`/weak attr 사용. once-per-binary 검증 추가 |
| `main` 심볼 충돌 (ROI: u_main mangle 이미 있음) | 낮음 | 재확인 — M2 에 한 unit 만 main entry emit |
| `.hxi` 스키마 진화 | 중간 | v0 에 version 필드 헤더 + tolerant parser |
| flatten_imports.hexa 경로 의존 | 중간 | .hxi 가 content hash 포함 — 경로 해상은 별도 manifest |
| 회귀 — 11 pass/0 fail | 중간 | M0/M1 엔 `-c` opt-in, default flow 불변. M3-M4 에 flag gate |

## 7. B/C/D 재평가 (A 설계 후)

**B. AST/type 캐시 영속화** — ✅ A 와 **상호보완**. `.hxi` 는 공개 시그니처만; `~/.cache/hexa/ast/<sha>.astbin` 은 private-body 포함 AST. B 는 M4 이후 단일 모듈 변경 시 **동일 모듈 재-transpile 비용** 추가 절감 (lex/parse skip). 독립 착수 가능이지만 A M3 완료 후 효용 측정 권장. 추정 1주.

**C. bc_vm 을 `hexa build` 기본 경로로** — ⚠️ A 와 **독립**하되 **리스크 중간**. hexa build 는 AOT 경로이므로 bc_vm 은 자연 fit 아님 (bc_vm 은 tree-walk 대안). `hexa run` 의 성능 개선으로 방향 전환 제안 — `hexa run` 이 10-100× 빨라지면 build 불필요한 유즈케이스 (테스트/REPL) 가 즉시 이득. 11 pass/0 fail 회귀 가드는 C 에 특히 중요. A 후 재평가. 추정 1-2주.

**D. 증분 flatten** — ✅ **A M3 에 자동 흡수**. M3 가 ".hxi 있으면 모듈 스킵" 을 구현하는데, 이는 "unchanged 모듈 flatten skip" 의 일반화. 별도 항목 불필요. Drop 권장.

**수정 우선순위 (제안)**:
1. **A M0→M1** (4-5일, 기반 sidecar 확보)
2. **A M2→M3** (4-5일, E2E + 실 speedup 첫 측정)
3. **A M4** (3-4일, 유저 API 복원)
4. **B 착수** (A M4 이후 실측치로 효용 확인 후) — 1주
5. **A M5** (stdlib 배포) — 2일
6. **C 재평가** (번들 크기 줄어든 후 여전히 needs 있는지) — pending

**보류**: D (A 에 흡수)

## 8. 다음 행동 (구현 전)

1. 이 메모 리뷰 — 스키마 초안 / 로드맵 순서 검증
2. M0 스키마 (`.hxi` 라인 포맷) 에 한 번 더 정렬 — 추후 Rust/Swift LSP 경계 확장 대비 반드시 version 필드 확보
3. 측정 baseline 먼저 기록 (`shared/hexa/build-speed-metrics.jsonl` 첫 entry) — A 착수 전 현재 숫자 확정
4. void 레포에서 3-모듈 분해본 예제 제출 — 수동으로도 작동하는지 먼저 확인 (M1 target shape 결정)

**이 메모 승인 후 착수 권장**: M0 + baseline 기록.

## Sources
- [rustc-dev-guide: Libraries and metadata](https://rustc-dev-guide.rust-lang.org/backend/libs-and-metadata.html)
- [Jay Conrod: Export data — Go's fast builds](https://jayconrod.com/posts/112/export-data--the-secret-of-go-s-fast-builds)
- [Swift Forums: Module interface files](https://forums.swift.org/t/update-on-module-stability-and-module-interface-files/23337)
- [OCaml compiler toolchain](https://ocaml.org/docs/using-the-ocaml-compiler-toolchain)
- [GHC recompilation avoidance](https://gitlab.haskell.org/ghc/ghc/-/wikis/commentary/compiler/recompilation-avoidance)
