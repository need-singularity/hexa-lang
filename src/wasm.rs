//! WASM entry points for the HEXA-LANG Playground.
//!
//! Provides `run_hexa`, `format_hexa`, and `lint_hexa` functions
//! callable from JavaScript via wasm-bindgen.

use wasm_bindgen::prelude::*;

/// WASM memory budget: 8 MB (reduced from the default 64 MB).
const WASM_MEMORY_BUDGET: usize = 8 * 1024 * 1024;

/// Run a Hexa source program and return its output.
///
/// Uses tiered execution:
///   Tier 1 — VM (bytecode): compile → execute on the stack-based VM.
///             Faster for numeric/loop-heavy code. JIT is skipped in WASM.
///   Tier 2 — Interpreter (tree-walk): fallback if VM compilation fails.
///
/// Returns a JSON string: `{"output": "...", "error": "...", "tier": "vm"|"interpreter"}`.
/// If execution succeeds, `error` is empty. If it fails, `output` contains
/// any partial output produced before the error.
#[wasm_bindgen]
pub fn run_hexa(source: &str) -> String {
    let (output, error, tier) = crate::run_source_tiered(source, WASM_MEMORY_BUDGET);
    let result = serde_json::json!({
        "output": output,
        "error": error,
        "tier": tier,
    });
    result.to_string()
}

/// Format Hexa source code and return the formatted version.
///
/// Returns a JSON string: `{"formatted": "...", "error": "..."}`.
#[wasm_bindgen]
pub fn format_hexa(source: &str) -> String {
    match crate::formatter::format_source(source) {
        Ok(formatted) => {
            let result = serde_json::json!({
                "formatted": formatted,
                "error": "",
            });
            result.to_string()
        }
        Err(e) => {
            let result = serde_json::json!({
                "formatted": "",
                "error": e,
            });
            result.to_string()
        }
    }
}

/// Lint Hexa source code and return warnings as JSON.
///
/// Returns a JSON string: `{"warnings": [...], "error": "..."}`.
/// Each warning is `{"level": "warn"|"info", "message": "...", "line": N}`.
#[wasm_bindgen]
pub fn lint_hexa(source: &str) -> String {
    match crate::linter::lint_source(source) {
        Ok(messages) => {
            let warnings: Vec<serde_json::Value> = messages
                .iter()
                .map(|m| {
                    serde_json::json!({
                        "level": match m.level {
                            crate::linter::LintLevel::Warn => "warn",
                            crate::linter::LintLevel::Info => "info",
                        },
                        "message": m.message,
                        "line": m.line,
                    })
                })
                .collect();
            let result = serde_json::json!({
                "warnings": warnings,
                "error": "",
            });
            result.to_string()
        }
        Err(e) => {
            let result = serde_json::json!({
                "warnings": [],
                "error": e,
            });
            result.to_string()
        }
    }
}

/// Return the Hexa language version string.
#[wasm_bindgen]
pub fn hexa_version() -> String {
    format!("HEXA-LANG v{}", env!("CARGO_PKG_VERSION"))
}
