<!-- @readme(scope=root) -->

# example-paper-project

논문 + 도메인 중심 hexa 프로젝트 (paper preset).

## Overview

이 프로젝트는 hexa-lang 기반 논문·도메인 관리 리포다. `@doc(type=paper)` 21 섹션 템플릿으로 논문을 작성하고, `domains/<axis>/<id>/` 구조로 도메인을 관리한다.

- 논문: `papers/` 하위 `.md` 파일, `@doc(type=paper)` attr.
- 도메인: `domains/<axis>/<id>/<id>.md`, `@domain(id=..., axis=...)` attr.
- 리포트: `reports/` 하위 시점별 리포트.

## Quick Start

```bash
# 규칙 검증
hexa policy

# 문서 lint
hexa self/attr_cli.hexa lint papers/my-paper.md
hexa self/attr_cli.hexa lint domains/life/dog-robot/dog-robot.md

# 도메인 추가
hexa init . --preset domain_only --axis life --id dog-robot
```

## Architecture

- `project.hexa` — 프로젝트 SSOT (@doc_rules, @domain_rules, @sealed_rules, @publish_rules)
- `papers/` — 논문 `.md` 파일 집합
- `reports/` — 시점 리포트
- `domains/<axis>/<id>/` — 도메인 seed + CLAUDE.md + .sealed.hash

## Domains

10 축: compute, life, energy, material, space, number, signal, bio, chem, sf-ufo.

도메인 목록은 `domains/` 디렉토리 스캔 또는 `hexa scan` 으로 확인.

## Contributing

PR 전 `hexa policy` + `hexa doc lint` 위반 0 확인 필수.

## License

프로젝트별 설정.
