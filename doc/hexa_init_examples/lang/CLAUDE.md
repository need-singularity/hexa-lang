# example-lang — AI 에이전트 가이드

hexa 기반 언어/도구 프로젝트용.

## 첫 30 초 체크

1. `Read project.hexa` — @doc_rules(spec/adr/breakthrough) 확인
2. `Read README.md` — 언어/도구 목적
3. `Read main.hexa` — 실행 진입점
4. `Glob self/attrs/*.hexa` — 프로젝트 고유 attr 목록

## 핵심 규칙 (hexa-lang HX 준수)

- **HX3**: `.hexa` 작성 전 `doc/grammar.jsonl` P1~P5 체크.
- **HX4**: 모든 코드 `.hexa`. Rust/Python/JS 도입 금지.
- **HX7**: 언어 기능 변경은 `self/` 경로 전용 (외부 디렉토리에 codegen 로직 금지).
- **HX11**: attr 규칙은 `project.hexa` 의 `@<attr>_rules` 블록으로.

## 금지

- 외부 라이선스 의존 (순수 hexa-lang 생태 유지).
- `src/` 디렉토리 Rust 재도입 (HX4 위반).
- README.md 섹션 순서 임의 변경 (`@readme_rules(scope=root)` 순서 고정).

## 추가 attr 작업 패턴

```bash
# 새 attr 추가
# 1. self/attrs/<name>.hexa 생성 (meta + check fn)
# 2. self/attrs/_registry.hexa 에 dispatch 엔트리 추가
# 3. hexa self/attr_cli.hexa show <name> 으로 카탈로그 확인
```

## 참조

- hexa-lang SSOT: `$HEXA_LANG/CLAUDE.md`
- 문법: `$NEXUS/doc/grammar.jsonl`
- ai-native 벡터: `$HEXA_LANG/docs/ai-native.md`
