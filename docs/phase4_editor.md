# Phase 4 — hexa 자체 에디터 / LSP (편집 진입점 법)

> edict 로드맵 Phase 4 — 편집 **진입점** 자체를 hexa-native 로 대체.
> Claude `Write` / IDE save / vim :w 모두 → `hexa edit` 경유 강제.

## 목적

Phase 3 (FUSE) 이 **파일시스템 레벨** 차단이라면, Phase 4 는 **에디터 프로토콜 레벨** 차단:

- 편집 행위 자체가 law 를 통과한 hexa API 로만 가능
- 어떤 tool 을 쓰든 최종 write 는 `hexa edit` 인스턴스를 거침
- LSP (Language Server Protocol) 로 IDE 통합 → 표준 워크플로 유지

## 설계

### 핵심: `hexa edit` 명령

```
$ hexa edit <path>
  ─ open file in LSP session
  ─ all edits flow through law gate
  ─ invalid edits get rejected with position-specific error
  ─ save = atomic rename via law-approved write
```

### LSP 프로토콜 (표준)

```
IDE ◄─────► hexa-lsp (자체 구현)
            │
            ├─ textDocument/didChange  → law.check_delta()
            ├─ textDocument/willSave   → law.check_full(path, content)
            ├─ textDocument/didSave    → law.pass? write : reject
            └─ diagnostics             → publish edict violations
```

### Claude Write tool 통합

- 단기: 사용자 wrapper — `alias claude-write='hexa edit --stdin'`
- 중기: MCP server — Claude Code 가 MCP 통해 hexa-lsp 호출
- 장기: Claude Code native "hexa edit mode" PR (upstream)

## 아키텍처

```
┌────────────────────────────────────────────┐
│  클라이언트 (IDE / Claude / CLI)            │
│    ├ VS Code              (LSP client)     │
│    ├ nvim coc             (LSP client)     │
│    ├ Claude Code MCP      (MCP client)     │
│    └ hexa edit stdin      (pipe)           │
└────────────────┬───────────────────────────┘
                 │  JSON-RPC (LSP) or MCP
                 ▼
┌────────────────────────────────────────────┐
│  hexa-lsp server                           │
│    ├ parse tree builder                    │
│    ├ edict law engine                      │
│    │   ├ rule 1 ai-native @attr            │
│    │   ├ rule 3 @attr naming               │
│    │   ├ rule 4 폴더명                     │
│    │   ├ rule 5 트리구조                   │
│    │   └ rule 6 파일명                     │
│    ├ pitfall detector (P1-P8)              │
│    └ atomic write gateway                  │
└────────────────┬───────────────────────────┘
                 │  safe write
                 ▼
              filesystem
```

## 단계별 구현

### Step 1 — `hexa edit` CLI 최소 버전

- stdin → file 쓰기 전 law check
- 위반 시 stderr 에 diagnostic + exit 1
- ~100 LOC

```bash
cat new_content | hexa edit foo.hexa
# → law gate pass → atomic write
# → fail → exit 1 with rule id + position
```

### Step 2 — LSP server 스켈레톤

- JSON-RPC 2.0 서버 (`self/lsp/server.hexa`)
- initialize / textDocument/didOpen / publishDiagnostics
- 최소: diagnostics 발행만, 편집 차단 안 함
- 500 LOC

### Step 3 — LSP 편집 차단

- textDocument/willSave 에서 law 검사 → 위반 시 `willSaveWaitUntil` 응답으로 차단
- hexa edit 과 동일 law engine 공유
- 800 LOC

### Step 4 — Claude Code MCP 통합

- `~/.claude/mcp.json` 에 hexa-lsp 추가
- Claude Write tool 호출 시 MCP 경유 law 검사
- hexa-lsp 이 `write_file` tool 제공
- Claude 가 직접 open(2) 호출 대신 MCP tool 사용
- Claude Code 측 feature gate 필요할 수 있음 (현재는 Write tool 우회 어려움)

### Step 5 — IDE 확장

- VS Code extension (자동 lsp 설정)
- `.vscode/extensions/hexa-lsp` 포함
- 사용자는 extension 설치만 → LSP 자동 동작

## 파일 구조

```
self/lsp/
  server.hexa         ← JSON-RPC 2.0 루프
  protocol.hexa       ← LSP 메시지 타입 / serde
  handlers.hexa       ← didOpen/didChange/willSave/…
  diagnostics.hexa    ← edict violation → LSP diagnostic 변환
self/edit/
  cli.hexa            ← `hexa edit` 진입점 (stdin/file 모드)
  law_gate.hexa       ← 공용 law 검사 (lsp + cli 공유)
scripts/
  hexa-lsp.hexa       ← LSP 서버 실행 스크립트
mcp/
  hexa-mcp.hexa       ← MCP server (Claude Code 통합)
```

## 장점

1. **편집 시점 즉시 피드백** — 저장 전에 diagnostic 표시
2. **IDE 자연 워크플로** — 사용자 경험 변경 없음, 법은 투명하게 강제
3. **MCP 경유 Claude** — Claude Code hook / settings.json 의존 완전 제거
4. **self-hosting 완성도** — hexa 로 작성된 LSP = hexa 자체 메타 개선

## 단점 / 위험

1. **LSP 구현 복잡도** — JSON-RPC 2.0 + LSP spec 준수 = 수 주
2. **MCP 통합 불확실** — Claude Code side 의 Write tool API 우회 방법 미확정
3. **CI 환경**: LSP 없음 → pre-commit 유지 필수
4. **현재 hexa 컴파일러 안정성** — LSP 는 빠른 incremental parse 필요 → hexa_stage0 성능 튜닝 선행

## 의존 최소화 판정

| 요소 | 의존 |
|------|------|
| LSP spec | OSS 표준 |
| JSON-RPC | OSS 표준 |
| hexa 바이너리 | ✅ self |
| VS Code extension | OS (선택) |
| MCP | Claude Code 표준 |

→ **Phase 4 완성 시 Claude Code hook/settings 의존 = 0**. Write 자체가 hexa 경유.

## 우선순위 / 순서

Phase 4 는 **가장 임팩트 높지만 가장 큰 작업**:

```
Phase 1 (done) ◄── 의존 大 (git + CLAUDE.md)
   ↓
Phase 2 (done) ◄── 의존 中 (hexa + git)
   ↓
Phase 3 ◄── 의존 小 (OS FUSE)
   ↓
Phase 4 ◄── 의존 ≈0 (hexa self)
```

Phase 3 대비 Phase 4 는 더 큰 UX 개선 + 더 큰 구현 비용. 두 경로 병행 가능.

## 다음 단계

1. `hexa edit` Step 1 (100 LOC stdin 모드) — 1일
2. LSP spec 읽기 + 스켈레톤 (Step 2) — 3-5일
3. MCP 실험 (Claude Code feature gate 조사) — 1일
4. 결정: Phase 4 풀 구현 vs Phase 3 완성 우선

## 대체 경로

Phase 4 도 복잡하면:
- **Phase 4-alt**: `hexa edit` CLI 만 (LSP 없이). 사용자가 `$EDITOR=hexa-edit` 설정 → vim 대체
- **Phase 4-alt-2**: git pre-push 에 law check 강제 (commit 통과했어도 push 불가)

---

**결론**: Phase 4 는 "hexa self-host 완성" 의 상징적 마일스톤. 실제 law 커버리지는 Phase 2+3 로 충분 (~95%). Phase 4 는 선택적, 장기 로드맵.
