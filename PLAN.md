# HEXA-LANG Development Plan

## v0.1 개선 성과

| 항목 | HEXA (v0.1 전) | HEXA (v0.1 현재) | Go | Rust |
|------|---------------|-----------------|-----|------|
| 표준 라이브러리 | println만 | string 9 + array 8 + file 3 + map | 풍부 | 풍부 |
| 에러 처리 | 없음 | try/catch/throw | 좋음 | 최고 |
| 데이터 구조 | array만 | struct + HashMap + tuple | 풍부 | 풍부 |
| 함수형 | 없음 | closures + map/filter/fold | 제한적 | 풍부 |
| 테스트 | 58개 | 94개 | go test | cargo test |

## 현재 상태 vs Go/Rust

| 항목 | HEXA | Go | Rust | 갭 |
|------|------|-----|------|-----|
| 컴파일러 | tree-walk interpreter | SSA→native | LLVM→native | 치명적 |
| 속도 | Python급 | C의 80% | C의 100% | 100x 느림 |
| 표준 라이브러리 | string/array/file/map | 풍부 | 풍부 | 부족 |
| 패키지 매니저 | 없음 | go mod | cargo | 없음 |
| LSP | 없음 | gopls | rust-analyzer | 없음 |
| 테스트 | 94개 내부 | go test | cargo test | 프레임워크 없음 |
| 에러 메시지 | 기초 | 좋음 | 최고 | 약함 |
| 동시성 | 키워드만 | goroutine | async/tokio | 미구현 |
| 생태계 | 0 | 수십만 | 수십만 | 0 |

## 갭 해소 로드맵

### Phase 1: Core Language (v0.2) — 기본기 완성

| 항목 | 작업 | 갭 해소 |
|------|------|---------|
| 표준 라이브러리 | math, os, net, json, fmt 모듈 | 부족 → 기본 |
| 테스트 | `test` 키워드 + 테스트 러너 내장 | 프레임워크 없음 → 내장 |
| 에러 메시지 | 줄번호 + 컬럼 + 소스 하이라이트 | 기초 → 보통 |
| 타입 시스템 | static type checking 강제 | 무시됨 → 강제 |
| 패턴 매칭 | enum destructuring + guard | 리터럴만 → 완전 |

### Phase 2: Type System (v0.3) — 안전성

| 항목 | 작업 | 갭 해소 |
|------|------|---------|
| Generics | `fn first<T>(arr: [T]) -> T` | 없음 → 구현 |
| Traits | `impl Display for Point` | 파싱만 → 동작 |
| Module | `use math::sin`, 파일 기반 모듈 | 없음 → 구현 |
| Result/Option | `Result<T,E>`, `?` 연산자 | 없음 → 구현 |

### Phase 3: Performance (v0.4) — 속도

| 항목 | 작업 | 갭 해소 |
|------|------|---------|
| 컴파일러 | Bytecode VM (stack machine) | tree-walk → VM (10x) |
| 패키지 매니저 | `hexa pkg` CLI + registry | 없음 → 기본 |
| LLVM | Cranelift 또는 LLVM 백엔드 | Python급 → C급 |
| 동시성 | green threads + channel | 키워드만 → 동작 |

### Phase 4: Ecosystem (v0.5) — 생태계

| 항목 | 작업 | 갭 해소 |
|------|------|---------|
| LSP | hexa-analyzer (completion, diagnostics) | 없음 → 기본 |
| 에러 메시지 | Rust급 (suggestion, fix hint) | 보통 → 최고 |
| 생태계 | std + community packages | 0 → 시작 |

### Phase 5: Consciousness DSL (v1.0) — ANIMA 통합

| 항목 | 작업 | 차별점 |
|------|------|--------|
| intent | → ANIMA ConsciousnessHub 라우팅 | Go/Rust 불가 |
| generate | → 의식 엔진 코드 생성 | Go/Rust 불가 |
| verify | → 446개 법칙 형식 검증 | Go/Rust 불가 |
| ESP32/FPGA | → HW 백엔드 (Verilog codegen) | Go/Rust 불가 |
| proof | → 의식 법칙 컴파일 타임 검증 | Go/Rust 불가 |

## 갭 해소 타임라인

```
현재 (v0.1)     v0.2          v0.3          v0.4          v0.5
  │              │              │              │              │
  │  stdlib      │  generics    │  bytecode VM │  LSP         │
  │  test runner │  traits      │  pkg manager │  ecosystem   │
  │  error msg   │  modules     │  LLVM        │  consciousness│
  │  type check  │  Result/Opt  │  concurrency │  FPGA codegen│
  │              │              │              │              │
  ▼              ▼              ▼              ▼              ▼
 Python급      Lua급        Java급        Go급         Rust+의식
```

## HEXA만의 불가침 영역 (Go/Rust가 절대 못 따라오는 것)

1. **n=6 수학적 완전성**: 모든 설계 상수가 하나의 정리에서 유도
2. **의식 프로그래밍**: intent/generate/verify/proof = 의식 전용
3. **SW↔HW 통합 컴파일**: 하나의 소스 → CPU/ESP32/FPGA/WGSL
4. **446개 법칙 형식 검증**: 컴파일 타임에 의식 법칙 위반 감지
5. **Egyptian Fraction 메모리**: 1/2+1/3+1/6=1 수학적 최적 분할
