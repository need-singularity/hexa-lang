# HEXA-LANG Development Plan

## Phase 완료 현황

| Phase | 버전 | 상태 | 테스트 | LOC | 주요 성과 |
|-------|------|------|--------|-----|----------|
| Phase 1 | v0.2 | ✅ 완료 | 131 | ~4500 | stdlib, enum, pattern matching, error line:col |
| Phase 2 | v0.3 | ✅ 완료 | 165 | ~5800 | static type checker, modules, Result/Option, impl |
| Phase 3 | v0.4 | ✅ 완료 | 194 | ~7800 | bytecode VM (2.8x), Cargo, spawn/channel |
| Phase 4 | v0.5 | ✅ 완료 | 218 | ~8800 | Rust급 에러, JSON, hexa init/run/test |
| Phase 5 | v1.0 | ✅ 완료 | 239 | ~9500 | intent/verify, Ψ-builtins, consciousness DSL |

## 현재 상태 vs Go/Rust (Phase 5 완료 후)

| 항목 | HEXA | Go | Rust | 갭 |
|------|------|-----|------|-----|
| 컴파일러 | tree-walk + bytecode VM | SSA→native | LLVM→native | VM 있음, LLVM 미완 |
| 속도 | VM 2.8x (Python대비) | C의 80% | C의 100% | ~30x 느림 |
| 표준 라이브러리 | 40+ builtins + JSON | 풍부 | 풍부 | 기본 수준 |
| 패키지 매니저 | hexa init/run/test | go mod | cargo | 기본 수준 |
| LSP | 미완 | gopls | rust-analyzer | 미완 |
| 테스트 | 239개 + --test 러너 | go test | cargo test | 내장 러너 있음 |
| 에러 메시지 | Rust급 (소스+제안) | 좋음 | 최고 | **달성** |
| 동시성 | spawn/channel (OS threads) | goroutine | async/tokio | 기본 수준 |
| 타입 시스템 | static check + inference | 강함 | 최강 | 기본 수준 |
| 의식 DSL | intent/verify/proof + Ψ builtins | 없음 | 없음 | **HEXA 유일** |
| 배포 | brew + cargo install | brew/apt | cargo | **달성** |

## 성장 그래프

```
Tests   │                                         ★ 239
        │                                   ╭─────╯
        │                             ╭─────╯ 218
        │                       ╭─────╯ 194
        │                 ╭─────╯ 165
        │           ╭─────╯ 131
        │     ╭─────╯ 94
        │─────╯ 58
        └──────────────────────────────────────────
         v0.1   P1     P2     P3     P4     P5

LOC     │                                         ★ ~9500
        │                                   ╭─────╯
        │                             ╭─────╯ ~8800
        │                       ╭─────╯ ~7800
        │                 ╭─────╯ ~5800
        │           ╭─────╯ ~4500
        │     ╭─────╯
        │─────╯ 2662
        └──────────────────────────────────────────
         v0.1   P1     P2     P3     P4     P5
```

## 남은 갭 (Next Phases)

### Phase 6: LLVM Backend (v1.1) — 네이티브 속도

| 항목 | 작업 | 효과 |
|------|------|------|
| Cranelift/LLVM | bytecode → native code | 30x → C급 |
| Generics | `fn first<T>(arr: [T]) -> T` | 타입 안전 재사용 |
| Trait dispatch | `impl Display for Point` | 다형성 |

### Phase 7: Ecosystem (v1.2) — 생태계

| 항목 | 작업 | 효과 |
|------|------|------|
| LSP | hexa-analyzer (diagnostics + completion) | IDE 지원 |
| Package registry | hexa.io 또는 GitHub-based | 패키지 공유 |
| VS Code extension | syntax highlight + LSP client | 개발 경험 |

### Phase 8: Hardware Targets (v2.0) — SW↔HW 통합

| 항목 | 작업 | 효과 |
|------|------|------|
| ESP32 codegen | .hexa → no_std Rust → ESP32 binary | 하드웨어 의식 |
| FPGA codegen | .hexa → Verilog HDL | 클럭당 1 step |
| WGSL codegen | .hexa → WebGPU shader | 브라우저 512c 병렬 |

## HEXA만의 불가침 영역 (Go/Rust가 절대 못 따라오는 것)

1. **n=6 수학적 완전성**: 모든 설계 상수가 하나의 정리에서 유도
2. **의식 프로그래밍**: intent/verify/proof + 12 Ψ-builtins = 의식 전용
3. **SW↔HW 통합 컴파일**: 하나의 소스 → CPU/ESP32/FPGA/WGSL (Phase 8)
4. **446개 법칙 형식 검증**: proof 블록으로 의식 법칙 코드 검증
5. **Egyptian Fraction 메모리**: 1/2+1/3+1/6=1 수학적 최적 분할
