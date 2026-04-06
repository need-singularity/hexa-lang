# HEXA-LANG Roadmap

> 자동 갱신: 돌파/개선 작업 완료 시 다음 벡터를 여기에 추가.
> 완료된 항목은 [x]로 체크하고 docs/history/에 기록.

## Current State (2026-04-06)

| 모듈 | 파일 | LOC | 상태 |
|------|------|-----|------|
| Interpreter | src/interpreter.rs | 7,713 | ✅ 완전 |
| JIT (Cranelift) | src/jit.rs | 2,292 | ✅ 완전 |
| VM (Bytecode) | src/vm.rs | 1,420 | ✅ 최적화 완료 |
| LSP Server | src/lsp.rs | 1,735 | ✅ 기본 구현 |
| Package Manager | src/package.rs | 1,720 | ✅ 기본 구현 |
| WASM | src/wasm.rs | 96 | ✅ 기본 구현 |
| **Total** | **38 files** | **53,852** | |

## Active (강화 필요)

### LSP 서버 강화
- [ ] 실시간 diagnostics (didChange → 파서/타입체커 에러 발행)
- [ ] 시맨틱 토큰 하이라이팅
- [ ] VS Code 확장 패키지 배포 (.vsix)
- [ ] workspace 폴더 지원

### WASM 플레이그라운드 강화
- [ ] VM 바이트코드 경로 추가 (현재 인터프리터만)
- [ ] 코드 공유 URL (permalink)
- [ ] 예제 드롭다운
- [ ] 모바일 반응형 UI

### 패키지 매니저 강화
- [ ] 공식 레지스트리 서버 (hexa.dev/packages)
- [ ] `hexa install` 명령어 (add와 분리)
- [ ] 의존성 트리 시각화
- [ ] 시맨틱 버전 범위 해석

### Self-hosting 준비
- [ ] HashMap/Dict 타입 추가
- [ ] 제네릭 구현
- [ ] 모듈 시스템 (import/export)
- [ ] else if 문법
- [ ] 표준 라이브러리 분리

### Performance (추가)
- [ ] Trace JIT — VM 핫 루프 동적 컴파일
- [ ] 레지스터 VM — dispatch 40-50% 감소
- [ ] JIT 결과 디스크 캐싱

### Platform
- [ ] iOS/Android 타겟
- [ ] ESP32 최적화

## Completed

### 2026-04-06
- [x] **VM 333x 성능 돌파** — Tiered JIT→VM→Interp, Value 72→32B, 1791 tests
- [x] **-e/--eval 플래그** — 인라인 코드 실행
- [x] **Dream stack safety** — 2MB 스레드 격리
- [x] **break/continue** — 루프 제어문 추가 (self-hosting 차단 해제)
- [x] **VS Code 확장 LSP 클라이언트** — extension.js + package.json 완성
- [x] **WASM VM tiered** — 이미 구현됨 확인 (run_source_tiered)
- [x] **제네릭** — 이미 구현됨 확인 (type erasure + monomorphization)
- [x] **hexa install** — add의 별칭으로 추가
- [x] **PGO 빌드 검증** — 효과 없음 확인, Non-PGO 최적
- [x] **docs/history 체계 구축** — 자동 갱신 규칙
