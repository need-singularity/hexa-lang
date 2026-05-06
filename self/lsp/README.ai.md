# hexa-lsp — capability matrix

> Status: **Phase 4 Step 2 skeleton** · LSP spec target: **3.17**
> Roadmap: `.roadmap.lsp` (LP1 close 2026-05-06)

This file is the SSOT for what hexa-lsp does and does not implement, so
IDE consumers can plan integration without reading the source.

## JSON-RPC envelope
| Direction | Format | Implemented |
|---|---|---|
| Request | `{jsonrpc, id, method, params}` | ✅ |
| Response | `{jsonrpc, id, result\|error}` | ✅ |
| Notification | `{jsonrpc, method, params}` | ✅ |

## Server capabilities (from `init_capabilities` in `protocol.hexa`)
| Capability | Value | Notes |
|---|---|---|
| `textDocumentSync.openClose` | `true` | didOpen / didClose tracked |
| `textDocumentSync.change` | `1` (full) | full sync per change; incremental deferred |
| `textDocumentSync.willSave` | `true` | willSave notification accepted |
| `textDocumentSync.willSaveWaitUntil` | `true` | request handled (returns empty TextEdit) |
| `textDocumentSync.save.includeText` | `true` | didSave carries post-save bytes |
| `diagnosticProvider.interFileDependencies` | `false` | per-file only |
| `diagnosticProvider.workspaceDiagnostics` | `false` | push model only |

## Method matrix
| Method | Kind | Status | Handler | Notes |
|---|---|---|---|---|
| `initialize` | request | ✅ | `handle_initialize` | Returns capabilities + serverInfo |
| `initialized` | notif | ✅ | (no-op) | |
| `textDocument/didOpen` | notif | ✅ | `handle_did_open` | Triggers law_check, emits diagnostics |
| `textDocument/didChange` | notif | ✅ | `handle_did_change` | Re-runs law_check (full sync) |
| `textDocument/willSave` | notif | ✅ | `handle_will_save` | No-op (notification spec) |
| `textDocument/willSaveWaitUntil` | request | ✅ (stub) | `handle_will_save_wait_until` | Always returns `[]` — TextEdit auto-fix is Step 3+ |
| `textDocument/didSave` | notif | ✅ | `handle_did_save` | Legacy law gate + per-rule (raw#9/#11/#12) granular diagnostics |
| `shutdown` | request | ✅ | `handle_shutdown` | Returns null |
| `exit` | notif | ✅ | (process exit 0) | |
| `textDocument/completion` | request | ❌ Step 3+ | — | not advertised in capabilities |
| `textDocument/hover` | request | ❌ Step 3+ | — | not advertised in capabilities |
| `textDocument/definition` | request | ❌ Step 3+ | — | not advertised in capabilities |
| `textDocument/references` | request | ❌ Step 3+ | — | not advertised in capabilities |
| `textDocument/rename` | request | ❌ Step 3+ | — | not advertised in capabilities |
| `textDocument/formatting` | request | ❌ Step 3+ | — | not advertised in capabilities |
| `workspace/diagnostic` | request | ❌ Step 3+ | — | pull-model deferred |

## Diagnostic surface
Granular per-rule diagnostics emitted by `raw_diagnostics.hexa`:

| Rule | Enforcer | Severity | Notes |
|---|---|---|---|
| raw#9 (hexa-only) | `tool/hexa_only_lint.hexa` | Error (1) | Banned-extension on staged paths |
| raw#11 (ai-native-enforce) | `tool/ai_native_lint.hexa` | Error (1) | `path:line:col: [tag L# ...] msg` parsing |
| raw#12 (silent-error) | `tool/error_propagation_lint.hexa` | Error (1) | `level[@rule] locus: msg` parsing |

Skipped on per-keystroke save (cost > benefit):
raw#10 proof-carrying · raw#15 no-hardcode · raw#18 self-host-fixpoint.

## IDE compatibility notes
- **VS Code**: Use `vscode-languageclient` with stdio transport. The skeleton
  reads stdin once per invocation; an editor extension should keep the
  process alive (LSP standard) — this is a Step 3 lift.
- **Neovim built-in LSP**: same caveat — needs persistent stdin.
- **Helix / Zed**: untested; should work given LSP 3.17 conformance.

## Known gaps (not yet roadmapped)
- `read_message` reads entire stdin at once instead of doing
  Content-Length framed loop. See `server.hexa` line 50 comment.
- JSON parsing is substring-based regex-lite; no proper escape handling
  beyond the basic four (`\"`, `\\`, `\n`, `\t`). UTF-8 multi-byte intact.
- Document store (`doc_store`) is single-process and never garbage-
  collected — Step 3 should add a uri-keyed map with TTL/explicit close.
