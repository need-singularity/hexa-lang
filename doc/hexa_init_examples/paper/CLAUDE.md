# example-paper-project — AI 에이전트 가이드

이 파일은 Claude Code / Cursor / Aider 가 이 프로젝트에서 작업할 때 참조하는 가이드.

## 첫 30 초 체크

1. `Read project.hexa` — 규칙 SSOT 확인
2. `Read README.md` — 프로젝트 의도 확인
3. `Glob papers/*.md`, `Glob domains/**/*.md` — 문서 구조 인지

## 핵심 규칙

- **HX11 준수**: 모든 문서는 첫 줄에 `<!-- @doc(type=...) -->` 또는 `<!-- @readme(scope=...) -->` 마커.
- **@doc(type=paper) §1-21 섹션**: WHY/COMPARE/REQUIRES/STRUCT/FLOW/EVOLVE/VERIFY + EXEC SUMMARY ~ IMPACT 순서.
- **@domain 필수 필드**: id, axis. id 는 lowercase-kebab, 40 자 이내.
- **@sealed**: 주요 .md 는 봉인. 수정 시 `.sealed.hash` 재생성 필요.
- **@publish**: main 브랜치만. 한글 커밋 메시지.

## 금지

- hexa 외 언어로 코드 추가 (HX4). Rust/Python/JS 소스 금지.
- `@doc_rules` 하드코딩된 섹션을 우회한 논문 작성.
- `domains/` 외부에 도메인 seed 파일 생성.

## 추가 도메인 작업 패턴

```bash
hexa init . --preset domain_only --axis <axis> --id <id>
# 생성된 domains/<axis>/<id>/<id>.md 에 @domain 헤더 + §1-7 작성
hexa self/attr_cli.hexa lint domains/<axis>/<id>/<id>.md
```

## 참조

- 글로벌 규칙: `$NEXUS/shared/rules/common.json` (R0~R27)
- hexa 규칙: `$HEXA_LANG/CLAUDE.md` (HX3/HX4/HX7/HX11)
