# HEXA-LANG Roadmap

> 자동 갱신: 돌파/개선 작업 완료 시 다음 벡터를 여기에 추가.
> 완료된 항목은 [x]로 체크하고 docs/history/에 기록.

## Active (다음 돌파 후보)

### Performance
- [ ] **PGO 빌드** — 프로파일 수집 완료(/tmp/pgo-data/merged.profdata), release 빌드 적용 시 VM 10-20% 추가 향상
  ```bash
  RUSTFLAGS="-Cprofile-use=/tmp/pgo-data/merged.profdata" bash build.sh release
  ```
- [ ] **JIT 결과 캐싱** — .hexa 파일의 JIT 컴파일 결과를 디스크에 저장, cold start 제거
- [ ] **Trace JIT** — VM 핫 루프 감지 → Cranelift으로 동적 컴파일 (LuaJIT 방식)
- [ ] **레지스터 VM** — 스택 VM → 레지스터 기반으로 전환 (dispatch 횟수 40-50% 감소)

### Platform
- [ ] **WASM 최적화** — 웹 타겟 성능 개선, wasm-bindgen 연동 강화
- [ ] **ESP32/임베디드** — hexa build --target esp32 경로 최적화
- [ ] **iOS/Android** — 모바일 타겟 지원

### Language Features
- [ ] **Self-hosting** — HEXA로 HEXA 컴파일러 작성
- [ ] **mem2reg** — SSA 최적화 패스 추가
- [ ] **Graph Coloring** — 레지스터 할당 최적화
- [ ] **증명 인증서** — 형식 검증 출력
- [ ] **AI 힌트** — NEXUS-6 기반 자동 최적화 제안

### Developer Experience
- [ ] **LSP 서버** — IDE 자동완성/진단
- [ ] **디버거 강화** — DAP 프로토콜 완성
- [ ] **패키지 매니저** — hexa.toml 기반 의존성 관리 강화
- [ ] **REPL 개선** — 자동완성, 히스토리, 멀티라인

## Completed

### 2026-04-06
- [x] **VM 333x 성능 돌파** — Tiered JIT→VM→Interp, Value 72→32B, 1791 tests
- [x] **-e/--eval 플래그** — 인라인 코드 실행
- [x] **Dream stack safety** — 2MB 스레드 격리
