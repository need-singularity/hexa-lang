<!-- @readme(scope=root) -->

# example-hub

여러 hexa 프로젝트가 공유하는 허브/SSOT 리포 (hub preset).

## Overview

이 리포는 N 개 프로젝트 (hexa-lang, n6-arch, anima, ...) 가 공유하는 SSOT. 개별 프로젝트 고유 코드는 여기 없고, **공통 규칙/config/harness** 만 있다.

## Architecture

- `shared/config/` — 프로젝트 간 공유 설정 (projects.json 등)
- `shared/rules/` — R0~R27 공통 규칙 + 프로젝트별 규칙 (hexa-lang.json, n6-architecture.json, ...)
- `shared/harness/` — loop / l0_guard 공통 harness hexa 스크립트
- `docs/` — 허브 수준 문서 (RFC, ADR)

## Projects

하위 참조 프로젝트 목록:

- `../hexa-lang/` — HEXA 프로그래밍 언어
- `../n6-architecture/` — n=6 현실지도 아키텍처
- `../anima/` — 의식(consciousness) 프로젝트
- (기타 리포는 shared/config/projects.json 에 등록)

## Shared Rules

- R0~R27: 전 프로젝트 공통 (`shared/rules/common.json`)
- HX1~HX11: hexa-lang 전용 (`shared/rules/hexa-lang.json`)
- N61~N65: n6-architecture 전용 (`shared/rules/n6-architecture.json`)

## Contributing

허브 변경은 develop 브랜치만. main 은 승인 후 수동 머지.

`shared/` 하위 파일은 `.sealed.hash` 로 봉인 — 수정 전 팀 승인 필요.

## License

MIT (허브 정책)
