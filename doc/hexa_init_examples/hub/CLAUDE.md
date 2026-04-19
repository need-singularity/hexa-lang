# example-hub — AI 에이전트 가이드

hexa 허브/SSOT 리포.

## 첫 30 초 체크

1. `Read project.hexa` — @readme_rules(scope=module) + @sealed 엄격
2. `Read README.md` — 허브 목적 + 참조 프로젝트
3. `Glob shared/config/*.json`, `Glob shared/rules/*.json` — 공유 자원 구조

## 핵심 규칙

- **봉인 엄수**: `shared/` 하위 파일은 `.sealed.hash` 로 봉인. 수정 전 `hexa sealed unseal <file>` 필요.
- **main push 금지**: @publish_rules 가 develop 만 허용.
- **프로젝트간 의존 관리**: 허브 변경이 N 개 프로젝트에 영향 → 변경 전 전수 검증.

## 금지

- 특정 프로젝트 전용 코드를 `shared/` 에 주입.
- `main` 브랜치로 직접 push.
- `shared/rules/common.json` 수동 편집 (loop/l0_guard 경유).

## 참조

- 프로젝트 목록: `shared/config/projects.json`
- 공통 규칙: `shared/rules/common.json`
- harness: `shared/harness/`
