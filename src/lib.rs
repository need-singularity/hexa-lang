//! HEXA-LANG Library
//!
//! Re-exports the interpreter, formatter, and linter for library use.
//! Used by the WASM playground and as an embeddable Hexa runtime.

#[allow(dead_code)]
pub mod token;
#[allow(dead_code)]
pub mod lexer;
#[allow(dead_code)]
pub mod ast;
#[allow(dead_code)]
pub mod error;
#[allow(dead_code)]
pub mod parser;
#[allow(dead_code)]
pub mod types;
#[allow(dead_code)]
pub mod env;
#[allow(dead_code)]
pub mod interpreter;
#[allow(dead_code)]
pub mod type_checker;
#[allow(dead_code)]
pub mod compiler;
#[allow(dead_code)]
pub mod vm;
#[allow(dead_code)]
pub mod nanbox;
#[allow(dead_code)]
pub mod formatter;
#[allow(dead_code)]
pub mod linter;
#[allow(dead_code)]
pub mod macro_expand;
#[allow(dead_code)]
pub mod comptime;
#[allow(dead_code)]
pub mod memory;
#[allow(dead_code)]
pub mod ownership;
#[allow(dead_code)]
pub mod dream;
#[allow(dead_code)]
pub mod inline_cache;
#[allow(dead_code)]
pub mod proof_engine;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod escape_analysis;

#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod async_runtime;

// Phase 10+11 optimization and async modules
#[allow(dead_code)]
pub mod dce;
#[allow(dead_code)]
pub mod loop_unroll;
#[allow(dead_code)]
pub mod simd_hint;
#[allow(dead_code)]
pub mod pgo;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod work_stealing;

// These modules use platform-specific features (filesystem, network, threads)
// and are only available on non-WASM targets.
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod jit;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod lsp;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod debugger;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod codegen_esp32;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod codegen_verilog;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod codegen_wgsl;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod llm;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_net;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_fs;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_io;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_time;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_collections;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_encoding;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_log;
#[allow(dead_code)]
pub mod std_math;
#[allow(dead_code)]
pub mod std_testing;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_crypto;
#[allow(dead_code)]
pub mod std_consciousness;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_nexus6;
#[allow(dead_code)]
pub mod atomic_ops;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod anima_bridge;
#[allow(dead_code)]
pub mod package;

// HEXA-IR: Self-designed IR backend (no LLVM, no Cranelift)
#[allow(dead_code)]
pub mod ir;
#[allow(dead_code)]
pub mod lower;
#[allow(dead_code)]
pub mod opt;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod codegen;
#[allow(dead_code)]
pub mod alloc;

// WASM entry points
#[cfg(target_arch = "wasm32")]
pub mod wasm;

/// Run Hexa source code and return (stdout_output, error_or_empty).
///
/// Uses the tree-walk interpreter with a reduced memory budget suitable
/// for embedded/WASM environments (8 MB).
pub fn run_source(source: &str) -> (String, String) {
    run_source_with_budget(source, 8 * 1024 * 1024)
}

/// Run Hexa source code with a specific memory budget (in bytes).
pub fn run_source_with_budget(source: &str, budget: usize) -> (String, String) {
    // Capture stdout by temporarily replacing the print implementation
    let output = std::sync::Arc::new(std::sync::Mutex::new(String::new()));

    let source_lines: Vec<String> = source.lines().map(String::from).collect();

    // Lex
    let tokens = match lexer::Lexer::new(source).tokenize() {
        Ok(t) => t,
        Err(e) => return (String::new(), format!("Lexer error: {}", e)),
    };

    // Parse
    let result = match parser::Parser::new(tokens).parse_with_spans() {
        Ok(r) => r,
        Err(e) => {
            let msg = format_diagnostic(&e, &source_lines, "<playground>");
            return (String::new(), msg);
        }
    };

    // Type check
    let mut checker = type_checker::TypeChecker::new();
    if let Err(e) = checker.check(&result.stmts, &result.spans) {
        let msg = format_diagnostic(&e, &source_lines, "<playground>");
        return (String::new(), msg);
    }

    // Ownership analysis (compile-time)
    let ownership_errors = ownership::analyze_ownership(&result.stmts, &result.spans);
    if !ownership_errors.is_empty() {
        let first = ownership_errors[0].clone().into_hexa_error();
        let msg = format_diagnostic(&first, &source_lines, "<playground>");
        return (String::new(), msg);
    }

    // Interpret with output capture
    let mut interp = interpreter::Interpreter::new_with_memory_budget(budget);
    interp.source_lines = source_lines;
    interp.file_name = "<playground>".to_string();

    // Set the output capture buffer
    let out_clone = output.clone();
    interp.set_output_capture(Some(out_clone));

    match interp.run_with_spans(&result.stmts, &result.spans) {
        Ok(_) => {
            let captured = output.lock().unwrap().clone();
            (captured, String::new())
        }
        Err(e) => {
            let captured = output.lock().unwrap().clone();
            let err_msg = format!("Runtime error: {}", e.message);
            (captured, err_msg)
        }
    }
}

/// Format a diagnostic error message (without ANSI colors for non-terminal use).
fn format_diagnostic(err: &error::HexaError, source_lines: &[String], file_name: &str) -> String {
    let mut msg = String::new();
    msg.push_str(&format!("error[{:?}]: {}\n", err.class, err.message));
    if err.line > 0 && err.line <= source_lines.len() {
        msg.push_str(&format!("  --> {}:{}:{}\n", file_name, err.line, err.col));
        msg.push_str(&format!("   |\n"));
        msg.push_str(&format!("{:>3} | {}\n", err.line, source_lines[err.line - 1]));
        msg.push_str(&format!("   |"));
        if err.col > 0 {
            msg.push_str(&format!(" {:>width$}^", "", width = err.col - 1));
        }
        msg.push('\n');
    }
    if let Some(hint) = &err.hint {
        msg.push_str(&format!("hint: {}\n", hint));
    }
    msg
}
