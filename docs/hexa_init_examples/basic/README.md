<!-- @readme(scope=root) -->

# example-project

한 줄 요약 — 이 프로젝트가 무엇을 하는지 한 문장으로.

## Overview

이 프로젝트는 hexa-lang 으로 작성된 <카테고리> 프로젝트다. <핵심 목표 1-2 문장>.

- 핵심 기능 1
- 핵심 기능 2
- 핵심 기능 3

## Quick Start

```bash
# 규칙 검증
hexa policy

# 실행 (main.hexa 가 있다면)
hexa run main.hexa

# attr lint
hexa self/attr_cli.hexa lint README.md
```

## Architecture

- `project.hexa` — 프로젝트 루트 마커 + @readme_rules 블록
- `README.md` — 이 파일 (프로젝트 소개)
- `.gitignore` — git 무시 목록

## Contributing

PR 환영. 수정 전 `hexa policy` 로 규칙 위반 0 확인 필수.

## License

MIT (또는 선택한 라이선스)
