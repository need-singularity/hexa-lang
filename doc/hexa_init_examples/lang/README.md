<!-- @readme(scope=root) -->

# example-lang

hexa-lang 기반 언어/도구 프로젝트 (lang preset).

## Overview

이 프로젝트는 hexa-lang 의 self-host 파이프라인을 따라 작성된 언어/도구다. `main.hexa` 진입, `self/attrs/` 에 프로젝트 고유 attr 추가.

## Install

```bash
# hexa 도구 필요 (https://github.com/.../hexa-lang 참조)
hexa --version
```

## Quick Start

```bash
# 실행
hexa run main.hexa

# 테스트
hexa tests/hello_test.hexa

# attr lint
hexa self/attr_cli.hexa lint README.md
```

## Language Tour

```hexa
// main.hexa 샘플
fn main() {
    println("hello hexa")
}
```

## Breakthroughs

돌파 문서는 `docs/breakthroughs/` 에 `<!-- @doc(type=breakthrough) -->` 헤더로 작성.

## Contributing

- HX3 (grammar pitfalls): `.hexa` 작성 전 `shared/hexa-lang/grammar.jsonl` 참조.
- HX4 (self-host): Rust/Python 소스 금지. `.hexa` 만.
- HX7 (self-host 경로): 언어 기능 변경은 `self/` 하위에만.

## License

MIT
