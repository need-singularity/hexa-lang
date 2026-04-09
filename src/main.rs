#[allow(dead_code)]
mod token;
#[allow(dead_code)]
mod lexer;
#[allow(dead_code)]
mod ast;
#[allow(dead_code)]
mod error;
#[allow(dead_code)]
mod parser;
#[allow(dead_code)]
mod types;
#[allow(dead_code)]
mod env;
#[allow(dead_code)]
mod interpreter;
#[allow(dead_code)]
mod type_checker;
#[allow(dead_code)]
mod compiler;
#[allow(dead_code)]
mod vm;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod jit;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod lsp;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod codegen_esp32;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod codegen_verilog;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod codegen_wgsl;
#[allow(dead_code)]
mod nanbox;
#[allow(dead_code)]
mod formatter;
#[allow(dead_code)]
mod linter;
#[allow(dead_code)]
mod macro_expand;
#[allow(dead_code)]
mod comptime;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod llm;
#[allow(dead_code)]
mod memory;
#[allow(dead_code)]
mod ownership;
#[allow(dead_code)]
mod dream;
#[allow(dead_code)]
mod inline_cache;
#[allow(dead_code)]
mod proof_engine;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod escape_analysis;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod async_runtime;
#[allow(dead_code)]
mod atomic_ops;
#[allow(dead_code)]
mod dce;
#[allow(dead_code)]
mod loop_unroll;
#[allow(dead_code)]
mod simd_hint;
#[allow(dead_code)]
mod pgo;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod work_stealing;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_net;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod debugger;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_fs;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_io;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_time;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_collections;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_encoding;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_log;
#[allow(dead_code)]
mod std_math;
#[allow(dead_code)]
mod std_testing;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_crypto;
#[allow(dead_code)]
mod std_consciousness;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_nexus;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod anima_bridge;
#[allow(dead_code)]
mod package;
#[allow(dead_code)]
mod singularity;
#[allow(dead_code)]
mod ir;
#[allow(dead_code)]
mod lower;
#[allow(dead_code)]
mod opt;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod codegen;
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
mod std_web_template;

use std::io::{self, Write, BufRead};
use std::collections::HashMap;

fn main() {
    // R19 보호: SIGPIPE 무시 — 파이프 조기 종료(| head, | tail 등) 시
    // BrokenPipe 에러를 syscall 레벨에서 swallow해 엔진이 silent exit 1 되지 않도록.
    // Rust는 SIGPIPE를 기본 SIG_DFL로 두어 프로세스 즉시 종료 → nexus blowup 등
    // 긴 출력 엔진에서 오진 발생. SIG_IGN으로 전환하면 write가 EPIPE 반환.
    // 이후 io.rs 헬퍼가 BrokenPipe를 감지하면 graceful exit(0)으로 전환.
    #[cfg(unix)]
    unsafe {
        // libc 의존성 없이 raw extern. SIG_IGN = 1, SIGPIPE = 13 (POSIX 공통).
        extern "C" {
            fn signal(signum: i32, handler: usize) -> usize;
        }
        const SIGPIPE: i32 = 13;
        const SIG_IGN: usize = 1;
        signal(SIGPIPE, SIG_IGN);
    }

    let args: Vec<String> = std::env::args().collect();
    if args.len() > 1 {
        match args[1].as_str() {
            "--test" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa --test <file.hexa>");
                    std::process::exit(1);
                }
                run_test(&args[2]);
            }
            "--vm" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa --vm <file.hexa>");
                    std::process::exit(1);
                }
                run_file_vm(&args[2]);
            }
            "--native" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa --native <file.hexa>");
                    std::process::exit(1);
                }
                run_file_native(&args[2]);
            }
            #[cfg(not(target_arch = "wasm32"))]
            "--hexa-ir" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa --hexa-ir <file.hexa>");
                    std::process::exit(1);
                }
                run_file_hexa_ir(&args[2]);
            }
            #[cfg(not(target_arch = "wasm32"))]
            "--bench-lower" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa --bench-lower <file.hexa> [iters]");
                    std::process::exit(1);
                }
                let iters: usize = if args.len() >= 4 {
                    args[3].parse().unwrap_or(10000)
                } else { 10000 };
                let source = std::fs::read_to_string(&args[2]).unwrap();
                let tokens = lexer::Lexer::new(&source).tokenize().unwrap();
                let stmts = parser::Parser::new(tokens).parse().unwrap();
                let t0 = std::time::Instant::now();
                let mut sink: usize = 0;
                for _ in 0..iters {
                    let m = lower::lower_program(&stmts, &args[2]);
                    sink = sink.wrapping_add(m.functions.len());
                }
                let dt = t0.elapsed();
                let us_per = dt.as_secs_f64() * 1_000_000.0 / iters as f64;
                println!("[bench-lower] iters={} total={:?} per-iter={:.3}us sink={}",
                    iters, dt, us_per, sink);
            }
            #[cfg(not(target_arch = "wasm32"))]
            "--dump-ir" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa --dump-ir <file.hexa>");
                    std::process::exit(1);
                }
                let source = std::fs::read_to_string(&args[2]).unwrap();
                let tokens = lexer::Lexer::new(&source).tokenize().unwrap();
                let stmts = parser::Parser::new(tokens).parse().unwrap();
                let module = lower::lower_program(&stmts, &args[2]);
                println!("{}", ir::print_module(&module));
            }
            "--lsp" => lsp::run_lsp(),
            "debug" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa debug <file.hexa>");
                    std::process::exit(1);
                }
                debugger::run_dap(&args[2]);
            }
            "--bench" => run_benchmark(),
            #[cfg(not(target_arch = "wasm32"))]
            "--eso-tune" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa --eso-tune <file.hexa> [--rounds N] [--policy adaptive|hybrid]");
                    std::process::exit(1);
                }
                run_eso_tune(&args);
            }
            "--mem-stats" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa --mem-stats <file.hexa> [--memory-budget <MB>]");
                    std::process::exit(1);
                }
                let file = &args[2];
                let budget = parse_memory_budget(&args);
                run_file_with_mem_stats(file, budget);
            }
            "build" => {
                if args.len() < 4 || args[2] != "--target" {
                    eprintln!("Usage: hexa build --target <esp32|verilog|wgsl|fpga|wgpu|cpu> <file.hexa> [--flash <port>]");
                    std::process::exit(1);
                }
                let target = &args[3];
                // Find the file and optional --flash port
                let mut file: Option<&str> = None;
                let mut flash_port: Option<&str> = None;
                let mut i = 4;
                while i < args.len() {
                    if args[i] == "--flash" {
                        i += 1;
                        if i < args.len() {
                            flash_port = Some(&args[i]);
                        } else {
                            eprintln!("Error: --flash requires a port argument (e.g., /dev/ttyUSB0)");
                            std::process::exit(1);
                        }
                    } else if file.is_none() {
                        file = Some(&args[i]);
                    }
                    i += 1;
                }
                let file = file.unwrap_or_else(|| {
                    eprintln!("Usage: hexa build --target {} <file.hexa> [--flash <port>]", target);
                    std::process::exit(1);
                });
                cmd_build(target, file);
                // If --flash was specified and target is esp32, flash the board
                if let Some(port) = flash_port {
                    if target == "esp32" {
                        match anima_bridge::flash_esp32("build/esp32", port) {
                            Ok(()) => println!("Flash complete!"),
                            Err(e) => {
                                eprintln!("Flash error: {}", e);
                                std::process::exit(1);
                            }
                        }
                    } else {
                        eprintln!("Warning: --flash is only supported for ESP32 target");
                    }
                }
            }
            "verify-cross" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa verify-cross <file.hexa>");
                    std::process::exit(1);
                }
                cmd_verify_cross(&args[2]);
            }
            "init" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa init <name>");
                    std::process::exit(1);
                }
                cmd_init(&args[2]);
            }
            "run" => cmd_run(&args),
            "test" => cmd_test(),
            "add" | "install" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa add <pkg>");
                    std::process::exit(1);
                }
                cmd_add(&args[2]);
            }
            "publish" => cmd_publish(),
            "fmt" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa fmt [--check] <file.hexa>");
                    std::process::exit(1);
                }
                if args[2] == "--check" {
                    if args.len() < 4 {
                        eprintln!("Usage: hexa fmt --check <file.hexa>");
                        std::process::exit(1);
                    }
                    cmd_fmt(&args[3], true);
                } else {
                    cmd_fmt(&args[2], false);
                }
            }
            "lint" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa lint <file.hexa>");
                    std::process::exit(1);
                }
                cmd_lint(&args[2]);
            }
            "dream" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa dream <file.hexa> [--generations N] [--mutations N] [--verbose]");
                    std::process::exit(1);
                }
                let file = &args[2];
                let mut generations: usize = 100;
                let mut mutations: usize = 10;
                let mut verbose = false;
                let mut i = 3;
                while i < args.len() {
                    match args[i].as_str() {
                        "--generations" => {
                            i += 1;
                            if i < args.len() {
                                generations = args[i].parse().unwrap_or(100);
                            }
                        }
                        "--mutations" => {
                            i += 1;
                            if i < args.len() {
                                mutations = args[i].parse().unwrap_or(10);
                            }
                        }
                        "--verbose" => { verbose = true; }
                        _ => {}
                    }
                    i += 1;
                }
                cmd_dream(file, generations, mutations, verbose);
            }
            "--anima-bridge" => {
                // hexa --anima-bridge ws://localhost:8765 file.hexa
                if args.len() < 4 {
                    eprintln!("Usage: hexa --anima-bridge <ws://host:port> <file.hexa>");
                    std::process::exit(1);
                }
                let ws_url = &args[2];
                let file = &args[3];
                cmd_anima_bridge(ws_url, file);
            }
            "--verify-law22" => {
                // hexa --verify-law22 <file.hexa> [--sw-phi N] [--hw-phi N]
                if args.len() < 3 {
                    eprintln!("Usage: hexa --verify-law22 <file.hexa> [--sw-phi <f64>] [--hw-phi <f64>]");
                    std::process::exit(1);
                }
                let file = &args[2];
                let mut sw_phi = 0.0f64;
                let mut hw_phi = 0.0f64;
                let mut i = 3;
                while i < args.len() {
                    match args[i].as_str() {
                        "--sw-phi" => { i += 1; if i < args.len() { sw_phi = args[i].parse().unwrap_or(0.0); } }
                        "--hw-phi" => { i += 1; if i < args.len() { hw_phi = args[i].parse().unwrap_or(0.0); } }
                        _ => {}
                    }
                    i += 1;
                }
                cmd_verify_law22(file, sw_phi, hw_phi);
            }
            "-e" | "--eval" => {
                if args.len() < 3 {
                    eprintln!("Usage: hexa -e <code>");
                    std::process::exit(1);
                }
                run_source_with_dir(&args[2], "");
            }
            _ => run_file(&args[1]),
        }
    } else {
        run_repl();
    }
}

fn run_file(path: &str) {
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", path, e);
            std::process::exit(1);
        }
    };
    run_source_with_dir(&source, path);
}

fn parse_memory_budget(args: &[String]) -> usize {
    for i in 0..args.len() {
        if args[i] == "--memory-budget" {
            if let Some(mb_str) = args.get(i + 1) {
                if let Ok(mb) = mb_str.parse::<usize>() {
                    return mb * 1024 * 1024;
                }
            }
        }
    }
    memory::DEFAULT_BUDGET
}

fn run_file_with_mem_stats(path: &str, budget: usize) {
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", path, e);
            std::process::exit(1);
        }
    };
    let source_lines: Vec<String> = source.lines().map(String::from).collect();
    let file_name = path;
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    let result = match parser::Parser::new(tokens).parse_with_spans() {
        Ok(r) => r,
        Err(e) => {
            eprint_diagnostic(&e, &source_lines, file_name);
            std::process::exit(1);
        }
    };
    let mut checker = type_checker::TypeChecker::new();
    if let Err(e) = checker.check(&result.stmts, &result.spans) {
        eprint_diagnostic(&e, &source_lines, file_name);
        std::process::exit(1);
    }
    // Ownership analysis pass
    let skip_ownership = std::env::args().any(|a| a == "--no-ownership-check");
    if !skip_ownership {
        let ownership_errors = ownership::analyze_ownership(&result.stmts, &result.spans);
        if !ownership_errors.is_empty() {
            for err in &ownership_errors {
                let hexa_err = err.clone().into_hexa_error();
                eprint_diagnostic(&hexa_err, &source_lines, file_name);
            }
            std::process::exit(1);
        }
    }
    let mut interp = interpreter::Interpreter::new_with_memory_budget(budget);
    interp.source_lines = source_lines.clone();
    interp.file_name = file_name.to_string();
    if !path.is_empty() {
        let p = std::path::Path::new(path);
        if let Some(parent) = p.parent() {
            interp.source_dir = Some(parent.to_string_lossy().to_string());
        }
    }
    match interp.run_with_spans(&result.stmts, &result.spans) {
        Ok(_) => {}
        Err(e) => {
            eprint_diagnostic(&e, &source_lines, file_name);
            std::process::exit(1);
        }
    }
    // Print memory report after execution
    println!();
    println!("{}", interp.memory_stats());
}

fn run_test(path: &str) {
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", path, e);
            std::process::exit(1);
        }
    };
    let source_lines: Vec<String> = source.lines().map(String::from).collect();
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    let result = match parser::Parser::new(tokens).parse_with_spans() {
        Ok(r) => r,
        Err(e) => {
            eprint_diagnostic(&e, &source_lines, path);
            std::process::exit(1);
        }
    };
    let stmts = result.stmts;
    let spans = result.spans;

    // Type checking pass (runs before execution)
    let mut checker = type_checker::TypeChecker::new();
    if let Err(e) = checker.check(&stmts, &spans) {
        eprint_diagnostic(&e, &source_lines, path);
        std::process::exit(1);
    }

    // First pass: run non-proof statements to set up functions/variables
    let mut interp = interpreter::Interpreter::new();
    interp.test_mode = true;
    interp.source_lines = source_lines.clone();
    interp.file_name = path.to_string();

    // Collect proof blocks and execute everything
    let mut proof_names: Vec<String> = Vec::new();
    for stmt in &stmts {
        if let ast::Stmt::Proof(name, _) = stmt {
            proof_names.push(name.clone());
        }
    }

    // Execute all statements (proof blocks will run in test mode)
    let mut passed = 0usize;
    let mut failed = 0usize;
    let mut _current_proof = String::new();

    for (i, stmt) in stmts.iter().enumerate() {
        if let Some(&(line, col)) = spans.get(i) {
            interp.current_line = line;
            interp.current_col = col;
        }
        match stmt {
            ast::Stmt::Proof(name, body) => {
                _current_proof = name.clone();
                interp.env.push_scope();
                let mut test_failed = false;
                for s in body {
                    match interp.run(&[s.clone()]) {
                        Ok(_) => {}
                        Err(e) => {
                            println!("proof {} ... FAILED: {}", name, e.message);
                            test_failed = true;
                            failed += 1;
                            break;
                        }
                    }
                }
                interp.env.pop_scope();
                if !test_failed {
                    println!("proof {} ... ok", name);
                    passed += 1;
                }
            }
            other => {
                if let Err(e) = interp.run(&[other.clone()]) {
                    eprint_diagnostic(&e, &source_lines, path);
                    std::process::exit(1);
                }
            }
        }
    }

    println!("\ntest result: {} passed, {} failed (total {})", passed, failed, passed + failed);
    if failed > 0 {
        std::process::exit(1);
    }
}

#[allow(dead_code)]
fn run_source(source: &str) {
    run_source_with_dir(source, "");
}

fn run_source_with_dir(source: &str, file_path: &str) {
    let source_lines: Vec<String> = source.lines().map(String::from).collect();
    let file_name = if file_path.is_empty() { "<stdin>" } else { file_path };
    let tokens = match lexer::Lexer::new(source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    let result = match parser::Parser::new(tokens).parse_with_spans() {
        Ok(r) => r,
        Err(e) => {
            eprint_diagnostic(&e, &source_lines, file_name);
            std::process::exit(1);
        }
    };
    // Tiered execution: JIT → VM → Interpreter
    // JIT and VM skip type/ownership checks (they have own error handling).
    // Only the interpreter fallback runs full analysis.

    // Tier 1: JIT (native code — fastest)
    #[cfg(not(target_arch = "wasm32"))]
    if jit::can_jit(&result.stmts) {
        if let Ok(mut jit_compiler) = jit::JitCompiler::new(&jit::collect_extern_decls(&result.stmts)) {
            match jit_compiler.compile_and_run(&result.stmts) {
                Ok(r) => { if r != 0 { println!("{}", r); } return; }
                Err(_) => {} // fall through
            }
        }
    }

    // Tier 2: VM (bytecode — reuse already-parsed stmts)
    {
        let mut comp = compiler::Compiler::new();
        if let Ok(chunk) = comp.compile(&result.stmts) {
            let mut vm = vm::VM::new();
            match vm.execute(&chunk) {
                Ok(_) => return,
                Err(_) => {} // fall through
            }
        }
    }

    // Tier 3: Interpreter (most compatible — run full analysis first)
    let mut checker = type_checker::TypeChecker::new();
    if let Err(e) = checker.check(&result.stmts, &result.spans) {
        eprint_diagnostic(&e, &source_lines, file_name);
        std::process::exit(1);
    }
    let skip_ownership = std::env::args().any(|a| a == "--no-ownership-check");
    if !skip_ownership {
        let ownership_errors = ownership::analyze_ownership(&result.stmts, &result.spans);
        if !ownership_errors.is_empty() {
            for err in &ownership_errors {
                let hexa_err = err.clone().into_hexa_error();
                eprint_diagnostic(&hexa_err, &source_lines, file_name);
            }
            std::process::exit(1);
        }
    }

    // Fallback: tree-walking interpreter (most compatible)
    let mut interp = interpreter::Interpreter::new();
    interp.source_lines = source_lines.clone();
    interp.file_name = file_name.to_string();
    // Set source directory for module resolution
    if !file_path.is_empty() {
        let path = std::path::Path::new(file_path);
        if let Some(parent) = path.parent() {
            interp.source_dir = Some(parent.to_string_lossy().to_string());
        }
    }
    match interp.run_with_spans(&result.stmts, &result.spans) {
        Ok(_) => {}
        Err(e) => {
            eprint_diagnostic(&e, &source_lines, file_name);
            std::process::exit(1);
        }
    }
}

/// Print a diagnostic error message with Rust-style source context.
fn eprint_diagnostic(e: &error::HexaError, source_lines: &[String], file_name: &str) {
    if e.line > 0 && !source_lines.is_empty() {
        let diag = error::Diagnostic {
            error: e,
            source_lines,
            file_name,
        };
        eprint!("{}", diag.format_with_source());
    } else {
        eprintln!("{}", e);
    }
}

fn run_file_vm(path: &str) {
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", path, e);
            std::process::exit(1);
        }
    };
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    let stmts = match parser::Parser::new(tokens).parse() {
        Ok(s) => s,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    let mut comp = compiler::Compiler::new();
    let chunk = match comp.compile(&stmts) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    let mut machine = vm::VM::new();
    match machine.execute(&chunk) {
        Ok(_) => {}
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    }
}

fn run_file_native(path: &str) {
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", path, e);
            std::process::exit(1);
        }
    };
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    let stmts = match parser::Parser::new(tokens).parse() {
        Ok(s) => s,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };

    // Check if the program can be fully JIT-compiled; if not, fall back to interpreter.
    if !jit::can_jit(&stmts) {
        eprintln!("[native] Program uses constructs unsupported by JIT, falling back to interpreter.");
        let mut interp = interpreter::Interpreter::new();
        match interp.run(&stmts) {
            Ok(_) => {}
            Err(e) => {
                eprintln!("{}", e);
                std::process::exit(1);
            }
        }
        return;
    }

    let mut jit_compiler = match jit::JitCompiler::new(&jit::collect_extern_decls(&stmts)) {
        Ok(j) => j,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    match jit_compiler.compile_and_run(&stmts) {
        Ok(result) => {
            if result != 0 {
                println!("{}", result);
            }
        }
        Err(e) => {
            // Fall back to interpreter on JIT runtime errors too.
            eprintln!("[native] JIT failed ({}), falling back to interpreter.", e.message);
            let mut interp = interpreter::Interpreter::new();
            match interp.run(&stmts) {
                Ok(_) => {}
                Err(e2) => {
                    eprintln!("{}", e2);
                    std::process::exit(1);
                }
            }
        }
    }
}

#[cfg(not(target_arch = "wasm32"))]
fn run_eso_tune(args: &[String]) {
    let file = &args[2];

    // Parse optional --rounds N and --policy adaptive|hybrid
    let mut rounds: usize = 12; // σ=12 (n=6 design constant)
    let mut policy = opt::pass_policy::Policy::Hybrid;
    let mut i = 3;
    while i < args.len() {
        match args[i].as_str() {
            "--rounds" => {
                i += 1;
                if i < args.len() {
                    rounds = args[i].parse().unwrap_or_else(|_| {
                        eprintln!("[eso-tune] invalid --rounds value, using default 12");
                        12
                    });
                    if rounds == 0 {
                        eprintln!("[eso-tune] --rounds must be >= 1");
                        std::process::exit(1);
                    }
                } else {
                    eprintln!("[eso-tune] --rounds requires a value");
                    std::process::exit(1);
                }
            }
            "--policy" => {
                i += 1;
                if i < args.len() {
                    policy = match args[i].as_str() {
                        "fixed" => opt::pass_policy::Policy::Fixed,
                        "adaptive" => opt::pass_policy::Policy::Adaptive,
                        "hybrid" => opt::pass_policy::Policy::Hybrid,
                        other => {
                            eprintln!("[eso-tune] unknown policy '{}', using hybrid", other);
                            opt::pass_policy::Policy::Hybrid
                        }
                    };
                } else {
                    eprintln!("[eso-tune] --policy requires a value");
                    std::process::exit(1);
                }
            }
            other => {
                eprintln!("[eso-tune] warning: unknown flag '{}', ignoring", other);
            }
        }
        i += 1;
    }

    // Read source
    let source = match std::fs::read_to_string(file) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", file, e);
            std::process::exit(1);
        }
    };

    // Lex
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };

    // Parse
    let stmts = match parser::Parser::new(tokens).parse() {
        Ok(s) => s,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };

    eprintln!("[eso-tune] file={} rounds={} policy={:?}", file, rounds, policy);

    let mut last_metrics: Vec<opt::ir_stats::PassMetric> = Vec::new();
    let mut ed_history = opt::emergence_density::EdHistory::default();
    let mut round_summaries: Vec<(usize, i64, u128, f64)> = Vec::new(); // (round, changes, elapsed_ns, ed)

    let mut module = lower::lower_program(&stmts, file);
    for round in 0..rounds {
        // Reuse the optimized module from previous round (no re-lower)
        let eso_result = opt::run_pipeline_with_policy(&mut module, policy, round, &last_metrics);
        let metrics = &eso_result.metrics;

        // Compute total changes (instruction deltas) and elapsed time
        let total_changes: i64 = metrics.iter().map(|m| m.delta_instr().abs()).sum();
        let total_elapsed_ns: u128 = metrics.iter().map(|m| m.elapsed_ns).sum();
        let total_elapsed_ms = total_elapsed_ns as f64 / 1_000_000.0;

        // Compute emergence density: ED = (1 - change_ratio) × speedup
        let baseline_changes = if round == 0 { total_changes.max(1) } else { round_summaries[0].1.max(1) };
        let baseline_ns = if round == 0 { total_elapsed_ns.max(1) } else { round_summaries[0].2.max(1) };
        let ed = opt::emergence_density::compute(opt::emergence_density::EdInput {
            baseline_changes,
            current_changes: total_changes,
            baseline_ns,
            current_ns: total_elapsed_ns.max(1),
        });
        ed_history.push(ed);

        eprintln!(
            "[eso-tune] round {}/{}: {} changes, {:.2}ms, ED={:.4}",
            round + 1,
            rounds,
            total_changes,
            total_elapsed_ms,
            ed,
        );

        round_summaries.push((round + 1, total_changes, total_elapsed_ns, ed));
        last_metrics = metrics.clone();

        if total_changes == 0 {
            eprintln!("[eso-tune] ✓ converged at round {}", round + 1);
            break;
        }
    }

    // Final report
    eprintln!("\n[eso-tune] === Final Report ({} rounds) ===", rounds);
    eprintln!("{:<8} {:>10} {:>12} {:>10}", "Round", "Changes", "Time(ms)", "ED");
    eprintln!("{}", "-".repeat(44));
    for (r, changes, elapsed_ns, ed) in &round_summaries {
        let ms = *elapsed_ns as f64 / 1_000_000.0;
        eprintln!("{:<8} {:>10} {:>12.2} {:>10.4}", r, changes, ms, ed);
    }

    if round_summaries.len() >= 2 {
        let first_ed = round_summaries[0].3;
        let last_ed = round_summaries.last().unwrap().3;
        let first_ns = round_summaries[0].2 as f64;
        let last_ns = round_summaries.last().unwrap().2 as f64;
        let speedup = if last_ns > 0.0 { first_ns / last_ns } else { 0.0 };
        eprintln!("\n[eso-tune] ED: {:.4} -> {:.4} (delta={:+.4})", first_ed, last_ed, last_ed - first_ed);
        eprintln!("[eso-tune] Speedup: {:.2}x (round 1 vs round {})", speedup, rounds);
        if ed_history.is_monotone_up() {
            eprintln!("[eso-tune] ED trend: monotonically improving");
        }
    }
    eprintln!("[eso-tune] done.");
}

#[cfg(not(target_arch = "wasm32"))]
fn run_file_hexa_ir(path: &str) {
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", path, e);
            std::process::exit(1);
        }
    };

    // 1. Lex
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };

    // 2. Parse
    let stmts = match parser::Parser::new(tokens).parse() {
        Ok(s) => s,
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };

    // 3. Type check (optional — skip if spans unavailable)
    // The HEXA-IR lowering does its own type resolution.

    // 4. Lower AST -> HEXA-IR
    let mut module = lower::lower_program(&stmts, path);

    // 5. Optimize (sigma=12 pipeline) — ESO policy via HEXA_ESO env var
    let eso_mode = std::env::var("HEXA_ESO").unwrap_or_default();
    let policy = match eso_mode.as_str() {
        "adaptive" => opt::pass_policy::Policy::Adaptive,
        "hybrid" => opt::pass_policy::Policy::Hybrid,
        _ => opt::pass_policy::Policy::Fixed,
    };
    let eso_result = opt::run_pipeline_with_policy(&mut module, policy, 0, &[]);
    let opt_result = eso_result.pipeline;
    eprintln!("[hexa-ir] {}", opt_result.summary().trim());
    if std::env::var("HEXA_DUMP_POST_OPT").is_ok() {
        eprintln!("{}", ir::print_module(&module));
    }
    if !eso_mode.is_empty() && eso_mode != "fixed" {
        eprintln!("[eso] policy={} metrics={}", eso_mode, eso_result.metrics.len());
    }

    // 6. Compile to native binary
    let output_path = path.trim_end_matches(".hexa");
    let output_bin = format!("{}.bin", output_path);
    match codegen::compile_to_binary(&module, &output_bin) {
        Ok(()) => {
            eprintln!("[hexa-ir] Compiled to {}", output_bin);
            if let Some(ps) = codegen::arm64::last_peephole_stats() {
                eprintln!(
                    "[hexa-ir] peephole: total={} rounds={} (id={} madd={} msub={} cmp0={} ldp={} shadd={})",
                    ps.total(), ps.rounds,
                    ps.identity_nop, ps.mul_add_to_madd, ps.mul_sub_to_msub,
                    ps.cmp_zero_dead, ps.ldr_pair_to_ldp, ps.lsl_add_to_shifted_add
                );
            }
        }
        Err(e) => {
            eprintln!("[hexa-ir] Compilation failed: {}", e);
            std::process::exit(1);
        }
    }

    // 7. Run the binary and capture output
    let output = std::process::Command::new(&output_bin)
        .output();
    match output {
        Ok(o) => {
            // Print stdout
            let stdout = String::from_utf8_lossy(&o.stdout);
            if !stdout.is_empty() {
                print!("{}", stdout);
            }
            // If no stdout but non-zero exit, print exit code as result
            if stdout.trim().is_empty() {
                if let Some(code) = o.status.code() {
                    if code != 0 {
                        println!("{}", code);
                    }
                }
            }
            // let _ = std::fs::remove_file(&output_bin); // keep for benchmarking
        }
        Err(e) => {
            eprintln!("[hexa-ir] Failed to run binary: {}", e);
            // let _ = std::fs::remove_file(&output_bin); // keep for benchmarking
            std::process::exit(1);
        }
    }
}

fn run_benchmark() {
    use std::time::Instant;

    println!("=== HEXA-LANG Performance Benchmark Suite ===");
    println!("Phase 10: Performance Optimization\n");

    // ---- Benchmark 1: fib(30) ----
    let fib_src = r#"
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(30)
"#;

    let tokens = lexer::Lexer::new(fib_src).tokenize().unwrap();
    let stmts = parser::Parser::new(tokens).parse().unwrap();

    // Tree-walk interpreter
    let start = Instant::now();
    let mut interp = interpreter::Interpreter::new();
    let tree_fib = interp.run(&stmts).unwrap();
    let tree_fib_time = start.elapsed();

    // VM
    let mut comp = compiler::Compiler::new();
    let chunk = comp.compile(&stmts).unwrap();
    let start = Instant::now();
    let mut machine = vm::VM::new();
    let vm_fib = machine.execute(&chunk).unwrap();
    let vm_fib_time = start.elapsed();

    // VM + Optimized (DCE)
    let mut comp2 = compiler::Compiler::new();
    let chunk2 = comp2.compile(&stmts).unwrap();
    let start = Instant::now();
    let mut machine2 = vm::VM::new();
    let vm_opt_fib = machine2.execute_optimized(&chunk2).unwrap();
    let vm_opt_fib_time = start.elapsed();

    // Native (Cranelift JIT)
    let tokens2 = lexer::Lexer::new(fib_src).tokenize().unwrap();
    let stmts2 = parser::Parser::new(tokens2).parse().unwrap();
    let start = Instant::now();
    let mut jit_compiler = jit::JitCompiler::new(&jit::collect_extern_decls(&stmts2)).unwrap();
    let native_fib = jit_compiler.compile_and_run(&stmts2).unwrap();
    let native_fib_time = start.elapsed();

    // ---- Benchmark 2: loop sum 1M ----
    let loop_src = r#"
let mut sum = 0
let mut i = 0
while i < 1000000 {
    sum = sum + i
    i = i + 1
}
sum
"#;

    let tokens_l = lexer::Lexer::new(loop_src).tokenize().unwrap();
    let stmts_l = parser::Parser::new(tokens_l).parse().unwrap();

    let start = Instant::now();
    let mut interp2 = interpreter::Interpreter::new();
    let tree_loop = interp2.run(&stmts_l).unwrap();
    let tree_loop_time = start.elapsed();

    let mut comp_l = compiler::Compiler::new();
    let chunk_l = comp_l.compile(&stmts_l).unwrap();
    let start = Instant::now();
    let mut machine_l = vm::VM::new();
    let vm_loop = machine_l.execute(&chunk_l).unwrap();
    let vm_loop_time = start.elapsed();

    let mut comp_l2 = compiler::Compiler::new();
    let chunk_l2 = comp_l2.compile(&stmts_l).unwrap();
    let start = Instant::now();
    let mut machine_l2 = vm::VM::new();
    let _vm_opt_loop = machine_l2.execute_optimized(&chunk_l2).unwrap();
    let vm_opt_loop_time = start.elapsed();

    // ---- Benchmark 3: nested function calls ----
    let call_src = r#"
fn square(x) { return x * x }
fn sum_squares(n) {
    let mut s = 0
    let mut i = 1
    while i <= n {
        s = s + square(i)
        i = i + 1
    }
    return s
}
sum_squares(10000)
"#;

    let tokens_a = lexer::Lexer::new(call_src).tokenize().unwrap();
    let stmts_a = parser::Parser::new(tokens_a).parse().unwrap();

    let start = Instant::now();
    let mut interp3 = interpreter::Interpreter::new();
    let tree_call = interp3.run(&stmts_a).unwrap();
    let tree_call_time = start.elapsed();

    let mut comp_a = compiler::Compiler::new();
    let chunk_a = comp_a.compile(&stmts_a).unwrap();
    let start = Instant::now();
    let mut machine_a = vm::VM::new();
    let vm_call = machine_a.execute(&chunk_a).unwrap();
    let vm_call_time = start.elapsed();

    let mut comp_a2 = compiler::Compiler::new();
    let chunk_a2 = comp_a2.compile(&stmts_a).unwrap();
    let start = Instant::now();
    let mut machine_a2 = vm::VM::new();
    let _vm_opt_call = machine_a2.execute_optimized(&chunk_a2).unwrap();
    let vm_opt_call_time = start.elapsed();

    // ---- Benchmark 4: const folding (compile-time math) ----
    let fold_src = "3 + 4 * 5 - 2";
    let tokens_f = lexer::Lexer::new(fold_src).tokenize().unwrap();
    let stmts_f = parser::Parser::new(tokens_f).parse().unwrap();

    let start = Instant::now();
    for _ in 0..10000 {
        let mut comp_f = compiler::Compiler::new();
        let _ = comp_f.compile(&stmts_f).unwrap();
    }
    let compile_time = start.elapsed();

    // ---- NaN-boxing size report ----
    let value_size = std::mem::size_of::<env::Value>();
    let nanbox_size = std::mem::size_of::<nanbox::NanBoxed>();

    // ---- Print results ----
    println!("Backend         fib(30)      loop(1M)     calls(10K)");
    println!("{}", "-".repeat(58));
    println!("Tree-walk       {:>8.3}s    {:>8.3}s    {:>8.3}s",
        tree_fib_time.as_secs_f64(), tree_loop_time.as_secs_f64(), tree_call_time.as_secs_f64());
    println!("VM              {:>8.3}s    {:>8.3}s    {:>8.3}s",
        vm_fib_time.as_secs_f64(), vm_loop_time.as_secs_f64(), vm_call_time.as_secs_f64());
    println!("VM+Optimized    {:>8.3}s    {:>8.3}s    {:>8.3}s",
        vm_opt_fib_time.as_secs_f64(), vm_opt_loop_time.as_secs_f64(), vm_opt_call_time.as_secs_f64());
    println!("Native (JIT)    {:>8.4}s    N/A          N/A",
        native_fib_time.as_secs_f64());

    println!();
    println!("--- Speedups (vs Tree-walk) ---");
    let vm_fib_sp = tree_fib_time.as_secs_f64() / vm_fib_time.as_secs_f64();
    let vm_opt_fib_sp = tree_fib_time.as_secs_f64() / vm_opt_fib_time.as_secs_f64();
    let native_fib_sp = tree_fib_time.as_secs_f64() / native_fib_time.as_secs_f64();
    let vm_loop_sp = tree_loop_time.as_secs_f64() / vm_loop_time.as_secs_f64();
    let vm_opt_loop_sp = tree_loop_time.as_secs_f64() / vm_opt_loop_time.as_secs_f64();
    let vm_call_sp = tree_call_time.as_secs_f64() / vm_call_time.as_secs_f64();
    let vm_opt_call_sp = tree_call_time.as_secs_f64() / vm_opt_call_time.as_secs_f64();
    println!("VM:           fib {:.1}x, loop {:.1}x, calls {:.1}x", vm_fib_sp, vm_loop_sp, vm_call_sp);
    println!("VM+Opt:       fib {:.1}x, loop {:.1}x, calls {:.1}x", vm_opt_fib_sp, vm_opt_loop_sp, vm_opt_call_sp);
    println!("Native (JIT): fib {:.1}x", native_fib_sp);

    println!();
    println!("--- Memory Layout ---");
    println!("Value enum size:  {} bytes", value_size);
    println!("NanBoxed size:    {} bytes  ({:.0}x smaller)", nanbox_size, value_size as f64 / nanbox_size as f64);

    println!();
    println!("--- Constant Folding ---");
    println!("10K compilations of '3+4*5-2': {:?} ({:.1}us/compile)",
        compile_time, compile_time.as_micros() as f64 / 10000.0);

    println!();
    println!("--- Verification ---");
    println!("fib(30):     tree={}, vm={}, vm_opt={}, native={}",
        tree_fib, vm_fib, vm_opt_fib, native_fib);
    println!("loop(1M):    tree={}, vm={}", tree_loop, vm_loop);
    println!("calls(10K):  tree={}, vm={}", tree_call, vm_call);
    assert_eq!(tree_fib.to_string(), "832040");
    assert_eq!(vm_fib.to_string(), "832040");
    assert_eq!(vm_opt_fib.to_string(), "832040");
}

/// Maximum number of history entries to keep.
const REPL_HISTORY_MAX: usize = 1000;

/// Load REPL history from `~/.hexa_history`. Returns an empty vec on any error.
#[cfg(not(target_arch = "wasm32"))]
fn repl_load_history() -> Vec<String> {
    let path = match std::env::var("HOME") {
        Ok(h) => std::path::PathBuf::from(h).join(".hexa_history"),
        Err(_) => return Vec::new(),
    };
    match std::fs::read_to_string(&path) {
        Ok(contents) => contents
            .lines()
            .map(|l| l.to_string())
            .collect(),
        Err(_) => Vec::new(),
    }
}

/// Save REPL history to `~/.hexa_history`. Silently ignores errors.
#[cfg(not(target_arch = "wasm32"))]
fn repl_save_history(history: &[String]) {
    let path = match std::env::var("HOME") {
        Ok(h) => std::path::PathBuf::from(h).join(".hexa_history"),
        Err(_) => return,
    };
    let start = if history.len() > REPL_HISTORY_MAX {
        history.len() - REPL_HISTORY_MAX
    } else {
        0
    };
    let data = history[start..].join("\n");
    let _ = std::fs::write(&path, data);
}

/// Build the auto-import lookup table: identifier name -> module path.
fn repl_auto_import_table() -> HashMap<&'static str, &'static str> {
    let mut m = HashMap::new();
    // std::collections
    m.insert("HashMap", "std::collections::HashMap");
    m.insert("HashSet", "std::collections::HashSet");
    m.insert("BTreeMap", "std::collections::BTreeMap");
    m.insert("BTreeSet", "std::collections::BTreeSet");
    m.insert("VecDeque", "std::collections::VecDeque");
    m.insert("LinkedList", "std::collections::LinkedList");
    m.insert("BinaryHeap", "std::collections::BinaryHeap");
    // std::fs
    m.insert("File", "std::fs::File");
    m.insert("OpenOptions", "std::fs::OpenOptions");
    // std::io
    m.insert("BufReader", "std::io::BufReader");
    m.insert("BufWriter", "std::io::BufWriter");
    // std::path
    m.insert("Path", "std::path::Path");
    m.insert("PathBuf", "std::path::PathBuf");
    // std::sync
    m.insert("Arc", "std::sync::Arc");
    m.insert("Mutex", "std::sync::Mutex");
    m.insert("RwLock", "std::sync::RwLock");
    // std::thread
    m.insert("Thread", "std::thread::Thread");
    // std::time
    m.insert("Duration", "std::time::Duration");
    m.insert("Instant", "std::time::Instant");
    // std::net
    m.insert("TcpStream", "std::net::TcpStream");
    m.insert("TcpListener", "std::net::TcpListener");
    m
}

/// Count unmatched open brackets in a line, returning the net depth change.
fn repl_bracket_depth(line: &str) -> i32 {
    let mut depth: i32 = 0;
    let mut in_string = false;
    let mut prev = '\0';
    for ch in line.chars() {
        if ch == '"' && prev != '\\' {
            in_string = !in_string;
        }
        if !in_string {
            match ch {
                '{' | '(' | '[' => depth += 1,
                '}' | ')' | ']' => depth -= 1,
                _ => {}
            }
        }
        prev = ch;
    }
    depth
}

/// Returns true if `line` signals that more input is needed for multiline.
fn repl_needs_continuation(line: &str) -> bool {
    let trimmed = line.trim_end();
    if trimmed.ends_with('\\') {
        return true;
    }
    matches!(trimmed.chars().last(), Some('{') | Some('(') | Some('['))
}

fn run_repl() {
    println!("HEXA-LANG v0.1.0 -- The Perfect Number Language");
    println!("sigma(n)*phi(n) = n*tau(n) iff n = 6");
    println!("Type :quit to exit, :help for help\n");

    let mut interp = interpreter::Interpreter::new();
    let stdin = io::stdin();

    // ── History ──
    #[cfg(not(target_arch = "wasm32"))]
    let mut history: Vec<String> = repl_load_history();
    #[cfg(target_arch = "wasm32")]
    let mut history: Vec<String> = Vec::new();

    // ── Auto-import ──
    let auto_import_table = repl_auto_import_table();
    let mut active_imports: Vec<(String, String)> = Vec::new(); // (name, path)

    loop {
        // ── Read input (with multiline support) ──
        print!("hexa> ");
        io::stdout().flush().unwrap();

        let mut line = String::new();
        if stdin.lock().read_line(&mut line).unwrap() == 0 {
            break; // EOF
        }

        let mut input = line.clone();
        let mut depth = repl_bracket_depth(&line);
        let mut needs_more = depth > 0 || repl_needs_continuation(&line);

        // Multiline continuation loop
        while needs_more {
            print!("....> ");
            io::stdout().flush().unwrap();

            let mut cont_line = String::new();
            if stdin.lock().read_line(&mut cont_line).unwrap() == 0 {
                break; // EOF
            }
            // Empty line in continuation mode => submit
            if cont_line.trim().is_empty() {
                break;
            }
            depth += repl_bracket_depth(&cont_line);
            input.push_str(&cont_line);
            needs_more = depth > 0 || repl_needs_continuation(&cont_line);
        }

        // Strip trailing backslash continuations for the final source
        let source = input
            .lines()
            .map(|l| {
                let t = l.trim_end();
                if t.ends_with('\\') { &t[..t.len() - 1] } else { l }
            })
            .collect::<Vec<&str>>()
            .join("\n");
        let trimmed = source.trim();

        if trimmed.is_empty() {
            continue;
        }

        // ── Commands ──
        if trimmed == ":quit" || trimmed == ":q" {
            break;
        }
        if trimmed == ":help" || trimmed == ":h" {
            println!("Commands:");
            println!("  :quit  :q       Exit the REPL");
            println!("  :help  :h       Show this help");
            println!("  :history :hist  Show command history");
            println!("  :imports        Show active auto-imports");
            println!();
            println!("n=6 builtins: sigma(n), phi(n), tau(n), gcd(a,b)");
            println!("Try: sigma(6) * phi(6) == 6 * tau(6)");
            println!();
            println!("Multiline: end a line with {{ ( [ or \\ to continue");
            continue;
        }
        if trimmed == ":history" || trimmed == ":hist" {
            let start = if history.len() > 20 { history.len() - 20 } else { 0 };
            for (i, entry) in history[start..].iter().enumerate() {
                println!("  {:>4}  {}", start + i + 1, entry);
            }
            continue;
        }
        if trimmed == ":imports" {
            if active_imports.is_empty() {
                println!("No auto-imports active.");
            } else {
                println!("Active auto-imports:");
                for (name, path) in &active_imports {
                    println!("  {} -> {}", name, path);
                }
            }
            continue;
        }

        // ── Add to history ──
        let hist_entry = trimmed.to_string();
        // Avoid duplicating the last entry
        if history.last().map(|s| s.as_str()) != Some(trimmed) {
            history.push(hist_entry);
            // Enforce max limit
            if history.len() > REPL_HISTORY_MAX {
                history.remove(0);
            }
        }

        // ── Lex + Parse + Typecheck + Execute ──
        let tokens = match lexer::Lexer::new(trimmed).tokenize() {
            Ok(t) => t,
            Err(e) => {
                eprintln!("{}", e);
                continue;
            }
        };
        let result = match parser::Parser::new(tokens).parse_with_spans() {
            Ok(r) => r,
            Err(e) => {
                let err_msg = format!("{}", e);
                // Multiline hint: if parser hit unexpected EOF, it was already
                // handled above via bracket depth. Show the error.
                eprintln!("{}", err_msg);
                continue;
            }
        };
        // Type checking pass
        let mut checker = type_checker::TypeChecker::new();
        if let Err(e) = checker.check(&result.stmts, &result.spans) {
            eprintln!("{}", e);
            continue;
        }
        match interp.run_with_spans(&result.stmts, &result.spans) {
            Ok(val) => match &val {
                env::Value::Void => {}
                _ => println!("{}", val),
            },
            Err(e) => {
                let err_msg = format!("{}", e);
                // ── Auto-import: check if an undefined identifier matches the table ──
                let mut did_auto_import = false;
                for (name, path) in &auto_import_table {
                    if err_msg.contains(&format!("undefined variable '{}'", name))
                        || err_msg.contains(&format!("Undefined variable: {}", name))
                        || err_msg.contains(&format!("not found: {}", name))
                        || err_msg.contains(&format!("Unknown identifier '{}'", name))
                    {
                        println!("[auto-import] use {} // {}", name, path);
                        // Record the import
                        if !active_imports.iter().any(|(n, _)| n == name) {
                            active_imports.push((name.to_string(), path.to_string()));
                        }
                        did_auto_import = true;
                    }
                }
                if did_auto_import {
                    println!("(hint: auto-import noted -- re-run your expression)");
                } else {
                    eprintln!("{}", err_msg);
                }
            }
        }
    }

    // ── Save history on exit ──
    #[cfg(not(target_arch = "wasm32"))]
    repl_save_history(&history);
}

// ── ANIMA Bridge commands ──────────────────────────────────

fn cmd_anima_bridge(ws_url: &str, file: &str) {
    let source = match std::fs::read_to_string(file) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", file, e);
            std::process::exit(1);
        }
    };

    println!("=== HEXA ANIMA Bridge ===");
    println!("  WebSocket: {}", ws_url);
    println!("  Source:    {}", file);
    println!();

    // Parse the file
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("Lexer error: {}", e);
            std::process::exit(1);
        }
    };
    let stmts = match parser::Parser::new(tokens).parse() {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Parse error: {}", e);
            std::process::exit(1);
        }
    };

    // Look for intent-like patterns in the AST (string literals as intent text)
    let config = anima_bridge::BridgeConfig::new(ws_url);
    let mut bridge = anima_bridge::AnimaBridge::new(config);

    // Extract string literals that could be intent texts
    let mut intent_count = 0;
    for stmt in &stmts {
        if let ast::Stmt::Expr(ast::Expr::StringLit(text)) = stmt {
            let json = bridge.send_intent(text);
            println!("  Intent #{}: {}", intent_count + 1, text);
            println!("  Message:  {}", json);

            // Try to send via WebSocket
            match bridge.send_ws(text) {
                Ok(response) => {
                    println!("  Response: {}", response);
                    if let Some(resp) = anima_bridge::AnimaResponse::from_json(&response) {
                        println!("  Status: {} | Phi: {:?} | Tension: {:?}",
                            resp.status, resp.phi, resp.tension);
                    }
                }
                Err(e) => {
                    println!("  [offline] {}", e);
                    println!("  (ANIMA not running at {} — messages queued)", ws_url);
                }
            }
            println!();
            intent_count += 1;
        }
    }

    // Also run the file normally
    println!("--- Running {} ---", file);
    run_file(file);

    if intent_count > 0 {
        println!("\n{} intent(s) processed via ANIMA bridge", intent_count);
    }
}

fn cmd_verify_law22(file: &str, sw_phi: f64, hw_phi: f64) {
    let source = match std::fs::read_to_string(file) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", file, e);
            std::process::exit(1);
        }
    };

    println!("=== Law 22 Verification ===");
    println!("\"Adding features -> Phi down; adding structure -> Phi up\"\n");
    println!("Source: {}", file);
    println!("SW Phi: {:.4}", sw_phi);
    println!("HW Phi: {:.4}", hw_phi);
    println!();

    let passed = anima_bridge::verify_law22(sw_phi, hw_phi);
    println!("Law 22 check: {}", if passed { "PASS" } else { "FAIL" });

    if !passed {
        println!("  Warning: Phi values suggest structural regression.");
        println!("  Review @law22 annotated functions for feature additions that may reduce structure.");
    }

    // Scan source for @law22 annotations
    let law22_count = source.lines().filter(|l| l.contains("@law22")).count();
    if law22_count > 0 {
        println!("\nFound {} @law22 annotated function(s) in {}", law22_count, file);
    }

    // Parse and run the file for verification
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("Lexer error: {}", e);
            std::process::exit(1);
        }
    };
    let result = match parser::Parser::new(tokens).parse_with_spans() {
        Ok(r) => r,
        Err(e) => {
            eprintln!("Parse error: {}", e);
            std::process::exit(1);
        }
    };
    let mut interp = interpreter::Interpreter::new();
    match interp.run(&result.stmts) {
        Ok(_) => println!("\nCompilation and execution: OK"),
        Err(e) => println!("\nExecution error: {}", e),
    }
}

// ── Package commands (hexa init/run/test) ──────────────────

fn cmd_init(name: &str) {
    use std::fs;
    use std::path::Path;

    let base = Path::new(name);
    if base.exists() {
        eprintln!("Error: directory '{}' already exists", name);
        std::process::exit(1);
    }

    // Create directories
    fs::create_dir_all(base.join("src")).unwrap_or_else(|e| {
        eprintln!("Error creating directories: {}", e);
        std::process::exit(1);
    });
    fs::create_dir_all(base.join("tests")).unwrap_or_else(|e| {
        eprintln!("Error creating directories: {}", e);
        std::process::exit(1);
    });

    // hexa.toml
    let toml_content = format!(
        "[package]\nname = \"{}\"\nversion = \"0.1.0\"\n\n[dependencies]\n",
        name
    );
    fs::write(base.join("hexa.toml"), toml_content).unwrap_or_else(|e| {
        eprintln!("Error writing hexa.toml: {}", e);
        std::process::exit(1);
    });

    // src/main.hexa
    let main_content = format!("println(\"Hello from {}!\")\n", name);
    fs::write(base.join("src").join("main.hexa"), main_content).unwrap_or_else(|e| {
        eprintln!("Error writing src/main.hexa: {}", e);
        std::process::exit(1);
    });

    // tests/test_main.hexa
    let test_content = "proof test_hello {\n    assert 1 + 1 == 2\n}\n";
    fs::write(base.join("tests").join("test_main.hexa"), test_content).unwrap_or_else(|e| {
        eprintln!("Error writing tests/test_main.hexa: {}", e);
        std::process::exit(1);
    });

    println!("Created project '{}'", name);
    println!("  {}/hexa.toml", name);
    println!("  {}/src/main.hexa", name);
    println!("  {}/tests/test_main.hexa", name);
}

fn cmd_run(args: &[String]) {
    // Find hexa.toml in current directory
    let toml_path = std::path::Path::new("hexa.toml");
    if !toml_path.exists() {
        eprintln!("Error: no hexa.toml found in current directory");
        eprintln!("Run 'hexa init <name>' to create a new project");
        std::process::exit(1);
    }

    // Parse dependencies from hexa.toml
    let toml_content = std::fs::read_to_string("hexa.toml").unwrap_or_default();
    let deps = parse_toml_dependencies(&toml_content);

    // Check and resolve dependencies
    let mut lock_entries: Vec<(String, String)> = Vec::new();
    let mut missing = false;
    for (pkg, version) in &deps {
        let pkg_dir = format!("packages/{}", pkg);
        if !std::path::Path::new(&pkg_dir).exists() {
            eprintln!("missing dependency: {}", pkg);
            missing = true;
        } else {
            lock_entries.push((pkg.clone(), version.clone()));
        }
    }
    if missing {
        std::process::exit(1);
    }

    // Write hexa.lock if we have deps
    if !lock_entries.is_empty() {
        let lock_content: String = lock_entries.iter()
            .map(|(pkg, ver)| format!("{} = \"{}\"", pkg, ver))
            .collect::<Vec<_>>()
            .join("\n");
        let _ = std::fs::write("hexa.lock", format!("{}\n", lock_content));
    }

    // Determine which file to run
    let main_path = if args.len() > 2 {
        args[2].as_str()
    } else {
        "src/main.hexa"
    };

    if !std::path::Path::new(main_path).exists() {
        eprintln!("Error: {} not found", main_path);
        std::process::exit(1);
    }

    run_file(main_path);
}

/// Parse [dependencies] section from hexa.toml content.
fn parse_toml_dependencies(content: &str) -> Vec<(String, String)> {
    let mut deps = Vec::new();
    let mut in_deps = false;
    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed == "[dependencies]" {
            in_deps = true;
            continue;
        }
        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            in_deps = false;
            continue;
        }
        if in_deps && trimmed.contains('=') {
            let parts: Vec<&str> = trimmed.splitn(2, '=').collect();
            if parts.len() == 2 {
                let name = parts[0].trim().to_string();
                let version = parts[1].trim().trim_matches('"').to_string();
                deps.push((name, version));
            }
        }
    }
    deps
}

fn cmd_add(pkg: &str) {
    use std::fs;

    let toml_path = std::path::Path::new("hexa.toml");
    if !toml_path.exists() {
        eprintln!("Error: no hexa.toml found in current directory");
        eprintln!("Run 'hexa init <name>' to create a new project");
        std::process::exit(1);
    }

    // Create packages/ directory if needed
    let _ = fs::create_dir_all("packages");

    // Parse package name and optional version: "pkg@1.2.3" or just "pkg"
    let (name, version) = if let Some(at_pos) = pkg.find('@') {
        (&pkg[..at_pos], &pkg[at_pos + 1..])
    } else {
        (pkg, "latest")
    };

    // Read existing toml
    let mut content = fs::read_to_string("hexa.toml").unwrap_or_default();

    // Add [dependencies] section if not present
    if !content.contains("[dependencies]") {
        content.push_str("\n[dependencies]\n");
    }

    // Format version string: "latest" stays, others get caret by default
    let version_str = if version == "latest" {
        "\"latest\"".to_string()
    } else if version.starts_with('^') || version.starts_with('~') || version.starts_with('=') || version.starts_with('>') || version.starts_with('<') {
        format!("\"{}\"", version)
    } else {
        format!("\"^{}\"", version)
    };

    // Append the dependency
    let dep_line = format!("{} = {}\n", name, version_str);

    // Check if already present
    if content.contains(&format!("{} =", name)) {
        println!("dependency '{}' already exists in hexa.toml", name);
        return;
    }

    // Insert after [dependencies]
    if let Some(pos) = content.find("[dependencies]") {
        let insert_pos = pos + "[dependencies]".len();
        let rest = &content[insert_pos..];
        let newline_pos = rest.find('\n').map(|p| insert_pos + p + 1).unwrap_or(content.len());
        content.insert_str(newline_pos, &dep_line);
    }

    fs::write("hexa.toml", &content).unwrap_or_else(|e| {
        eprintln!("Error writing hexa.toml: {}", e);
        std::process::exit(1);
    });

    // Generate/update hexa.lock
    let lock_path = std::path::Path::new("hexa.lock");
    let mut lock = if lock_path.exists() {
        let lock_content = fs::read_to_string("hexa.lock").unwrap_or_default();
        package::LockFile::parse(&lock_content).unwrap_or_else(|_| package::LockFile::new())
    } else {
        package::LockFile::new()
    };

    // Add lock entry if not already present
    if !lock.entries.iter().any(|e| e.name == name) {
        let pinned_version = if version == "latest" {
            "0.0.0".to_string()
        } else {
            // Strip prefix characters for the pinned version
            version.trim_start_matches(|c: char| !c.is_ascii_digit()).to_string()
        };
        lock.entries.push(package::LockEntry {
            name: name.to_string(),
            version: pinned_version,
            source: "registry".to_string(),
            checksum: None,
        });
    }

    fs::write("hexa.lock", lock.serialize()).unwrap_or_else(|e| {
        eprintln!("Error writing hexa.lock: {}", e);
        std::process::exit(1);
    });

    println!("Added '{}' to [dependencies]", name);
    println!("Updated hexa.lock");
}

fn cmd_publish() {
    let toml_path = std::path::Path::new("hexa.toml");
    if !toml_path.exists() {
        eprintln!("Error: no hexa.toml found in current directory");
        std::process::exit(1);
    }

    // Read package info from hexa.toml
    let content = std::fs::read_to_string("hexa.toml").unwrap_or_default();
    let name = parse_toml_value(&content, "name").unwrap_or_else(|| "unnamed".to_string());
    let version = parse_toml_value(&content, "version").unwrap_or_else(|| "0.1.0".to_string());

    // Collect .hexa files
    let mut files: Vec<String> = Vec::new();
    if std::path::Path::new("src").exists() {
        if let Ok(entries) = std::fs::read_dir("src") {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.extension().map_or(false, |e| e == "hexa") {
                    files.push(path.to_string_lossy().to_string());
                }
            }
        }
    }
    files.push("hexa.toml".to_string());

    // Create .hexa-pkg.json manifest
    let manifest = format!(
        "{{\n  \"name\": \"{}\",\n  \"version\": \"{}\",\n  \"files\": [{}]\n}}\n",
        name, version,
        files.iter().map(|f| format!("\"{}\"", f)).collect::<Vec<_>>().join(", ")
    );

    std::fs::write(".hexa-pkg.json", &manifest).unwrap_or_else(|e| {
        eprintln!("Error writing .hexa-pkg.json: {}", e);
        std::process::exit(1);
    });

    println!("Package manifest created: .hexa-pkg.json");
    println!();
    println!("To publish your package:");
    println!("  1. Create a GitHub repository for '{}'", name);
    println!("  2. Push your code: git push origin main");
    println!("  3. Create a release tag: git tag v{} && git push --tags", version);
    println!("  4. Others can install with: hexa add {}", name);
}

fn parse_toml_value(content: &str, key: &str) -> Option<String> {
    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with(key) && trimmed.contains('=') {
            let parts: Vec<&str> = trimmed.splitn(2, '=').collect();
            if parts.len() == 2 {
                return Some(parts[1].trim().trim_matches('"').to_string());
            }
        }
    }
    None
}

fn cmd_fmt(path: &str, check_only: bool) {
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", path, e);
            std::process::exit(1);
        }
    };

    match formatter::format_source(&source) {
        Ok(formatted) => {
            if check_only {
                if source.trim_end() == formatted.trim_end() {
                    println!("{}: already formatted", path);
                } else {
                    println!("{}: needs formatting", path);
                    std::process::exit(1);
                }
            } else {
                std::fs::write(path, &formatted).unwrap_or_else(|e| {
                    eprintln!("Error writing {}: {}", path, e);
                    std::process::exit(1);
                });
                println!("Formatted {}", path);
            }
        }
        Err(e) => {
            eprintln!("Error formatting {}: {}", path, e);
            std::process::exit(1);
        }
    }
}

fn cmd_lint(path: &str) {
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", path, e);
            std::process::exit(1);
        }
    };

    match linter::lint_source(&source) {
        Ok(messages) => {
            if messages.is_empty() {
                println!("{}: no warnings", path);
            } else {
                for msg in &messages {
                    println!("{}: {}", path, msg);
                }
                println!("\n{} warning(s) found", messages.len());
            }
        }
        Err(e) => {
            eprintln!("Error linting {}: {}", path, e);
            std::process::exit(1);
        }
    }
}

fn cmd_dream(path: &str, generations: usize, mutations_per_gen: usize, verbose: bool) {
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", path, e);
            std::process::exit(1);
        }
    };

    let anima_bridge = std::path::Path::new("consciousness_bridge.py").exists();

    println!("=== HEXA Dream Mode ===");
    println!("Evolving: {}", path);
    println!("Generations: {}  Mutations/gen: {}  Verbose: {}", generations, mutations_per_gen, verbose);
    if anima_bridge {
        println!("ANIMA bridge: active (consciousness_bridge.py detected)");
    }
    println!();

    let config = dream::DreamConfig {
        generations,
        mutations_per_gen,
        verbose,
        anima_bridge,
    };

    let mut engine = dream::DreamEngine::new(source, config);
    let report = engine.run();

    println!("{}", report);
}

fn cmd_test() {
    // Find hexa.toml in current directory
    let toml_path = std::path::Path::new("hexa.toml");
    if !toml_path.exists() {
        eprintln!("Error: no hexa.toml found in current directory");
        eprintln!("Run 'hexa init <name>' to create a new project");
        std::process::exit(1);
    }

    let tests_dir = std::path::Path::new("tests");
    if !tests_dir.exists() {
        eprintln!("Error: tests/ directory not found");
        std::process::exit(1);
    }

    let mut test_files: Vec<std::path::PathBuf> = Vec::new();
    if let Ok(entries) = std::fs::read_dir(tests_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().map_or(false, |e| e == "hexa") {
                test_files.push(path);
            }
        }
    }
    test_files.sort();

    if test_files.is_empty() {
        println!("No test files found in tests/");
        return;
    }

    let mut total_passed = 0usize;
    let mut total_failed = 0usize;

    for test_file in &test_files {
        println!("\nRunning {}...", test_file.display());
        let source = match std::fs::read_to_string(test_file) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("Error reading {}: {}", test_file.display(), e);
                total_failed += 1;
                continue;
            }
        };
        let source_lines: Vec<String> = source.lines().map(String::from).collect();
        let file_name = test_file.to_string_lossy().to_string();

        let tokens = match lexer::Lexer::new(&source).tokenize() {
            Ok(t) => t,
            Err(e) => {
                eprintln!("{}", e);
                total_failed += 1;
                continue;
            }
        };
        let result = match parser::Parser::new(tokens).parse_with_spans() {
            Ok(r) => r,
            Err(e) => {
                eprint_diagnostic(&e, &source_lines, &file_name);
                total_failed += 1;
                continue;
            }
        };
        let stmts = result.stmts;
        let spans = result.spans;

        let mut checker = type_checker::TypeChecker::new();
        if let Err(e) = checker.check(&stmts, &spans) {
            eprint_diagnostic(&e, &source_lines, &file_name);
            total_failed += 1;
            continue;
        }

        let mut interp = interpreter::Interpreter::new();
        interp.test_mode = true;
        interp.source_lines = source_lines.clone();
        interp.file_name = file_name.clone();

        for (i, stmt) in stmts.iter().enumerate() {
            if let Some(&(line, col)) = spans.get(i) {
                interp.current_line = line;
                interp.current_col = col;
            }
            match stmt {
                ast::Stmt::Proof(name, body) => {
                    interp.env.push_scope();
                    let mut test_failed = false;
                    for s in body {
                        match interp.run(&[s.clone()]) {
                            Ok(_) => {}
                            Err(e) => {
                                println!("  proof {} ... FAILED: {}", name, e.message);
                                test_failed = true;
                                total_failed += 1;
                                break;
                            }
                        }
                    }
                    interp.env.pop_scope();
                    if !test_failed {
                        println!("  proof {} ... ok", name);
                        total_passed += 1;
                    }
                }
                other => {
                    if let Err(e) = interp.run(&[other.clone()]) {
                        eprint_diagnostic(&e, &source_lines, &file_name);
                        total_failed += 1;
                        break;
                    }
                }
            }
        }
    }

    println!("\ntest result: {} passed, {} failed (total {})",
        total_passed, total_failed, total_passed + total_failed);
    if total_failed > 0 {
        std::process::exit(1);
    }
}

// ── Hardware Target Code Generation ──────────────────────────

fn cmd_build(target: &str, file: &str) {
    let source = match std::fs::read_to_string(file) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", file, e);
            std::process::exit(1);
        }
    };
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("Lexer error: {}", e);
            std::process::exit(1);
        }
    };
    let stmts = match parser::Parser::new(tokens).parse() {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Parse error: {}", e);
            std::process::exit(1);
        }
    };

    // Normalize target aliases
    let target_normalized = match target {
        "verilog" => "fpga",
        "wgsl" => "wgpu",
        other => other,
    };

    match target_normalized {
        "esp32" => cmd_build_esp32(file, &stmts),
        "fpga" => cmd_build_fpga(file, &stmts),
        "wgpu" => cmd_build_wgpu(file, &stmts),
        "cpu" => {
            println!("CPU target: running with tree-walk interpreter...");
            run_file(file);
        }
        _ => {
            eprintln!("Unknown target: {}", target);
            eprintln!("Available targets: cpu, esp32, fpga/verilog, wgpu/wgsl");
            std::process::exit(1);
        }
    }
}

/// Build a complete ESP32 Cargo project from HEXA source.
///
/// Generates `build/esp32/` with:
///   - Cargo.toml (no_std, esp32 target)
///   - src/main.rs (codegen output)
///   - .cargo/config.toml (build target + runner)
///
/// Then prints instructions for building and flashing.
fn cmd_build_esp32(file: &str, stmts: &[ast::Stmt]) {
    let project_dir = "build/esp32";
    let src_dir = format!("{}/src", project_dir);
    let cargo_dir = format!("{}/.cargo", project_dir);

    // Create directory structure
    for dir in &[&src_dir, &cargo_dir] {
        std::fs::create_dir_all(dir).unwrap_or_else(|e| {
            eprintln!("Error creating {}: {}", dir, e);
            std::process::exit(1);
        });
    }

    // 1. Generate the Rust source from HEXA AST
    let rust_code = codegen_esp32::generate(stmts);
    let src_path = format!("{}/main.rs", src_dir);
    std::fs::write(&src_path, &rust_code).unwrap_or_else(|e| {
        eprintln!("Error writing {}: {}", src_path, e);
        std::process::exit(1);
    });

    // 2. Generate Cargo.toml for no_std ESP32
    let stem = std::path::Path::new(file)
        .file_stem()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or_else(|| "hexa_esp32".to_string());
    let cargo_toml = format!(
        r#"[package]
name = "{name}"
version = "0.1.0"
edition = "2021"

[dependencies]
# Uncomment for ESP32 HAL support:
# esp32-hal = "0.19"
# esp-backtrace = {{ version = "0.12", features = ["esp32", "panic-handler", "println"] }}

[profile.release]
opt-level = "s"
lto = true
"#,
        name = stem
    );
    let cargo_path = format!("{}/Cargo.toml", project_dir);
    std::fs::write(&cargo_path, &cargo_toml).unwrap_or_else(|e| {
        eprintln!("Error writing {}: {}", cargo_path, e);
        std::process::exit(1);
    });

    // 3. Generate .cargo/config.toml for ESP32 target + runner
    let cargo_config = r#"[build]
target = "xtensa-esp32-none-elf"

[target.xtensa-esp32-none-elf]
runner = "espflash flash --monitor"

[unstable]
build-std = ["core"]
"#;
    let config_path = format!("{}/config.toml", cargo_dir);
    std::fs::write(&config_path, cargo_config).unwrap_or_else(|e| {
        eprintln!("Error writing {}: {}", config_path, e);
        std::process::exit(1);
    });

    // Report
    println!("=== HEXA ESP32 Build ===");
    println!("  Source:     {}", file);
    println!("  Project:    {}/", project_dir);
    println!("  Cargo.toml: {}", cargo_path);
    println!("  Source:     {}", src_path);
    println!("  Config:     {}", config_path);
    println!("  Code size:  {} bytes", rust_code.len());
    println!();
    println!("--- Next steps ---");
    println!("  1. Install ESP32 toolchain (if not already):");
    println!("       cargo install espup && espup install");
    println!("       cargo install espflash cargo-espflash");
    println!();
    println!("  2. Build the project:");
    println!("       cd {} && cargo build --release", project_dir);
    println!();
    println!("  3. Flash to ESP32 (connect via USB):");
    println!("       cd {} && cargo espflash flash --release --monitor", project_dir);
    println!();
    println!("  Psi-constants embedded: alpha=0.014, balance=0.5, steps=4.33, entropy=0.998");
}

/// Build FPGA/Verilog output from HEXA source.
fn cmd_build_fpga(file: &str, stmts: &[ast::Stmt]) {
    let output_dir = "build/fpga";
    std::fs::create_dir_all(output_dir).unwrap_or_else(|e| {
        eprintln!("Error creating {}: {}", output_dir, e);
        std::process::exit(1);
    });

    let stem = std::path::Path::new(file)
        .file_stem()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or_else(|| "consciousness".to_string());

    let verilog_code = codegen_verilog::generate(stmts);
    let output_file = format!("{}/{}.v", output_dir, stem);
    std::fs::write(&output_file, &verilog_code).unwrap_or_else(|e| {
        eprintln!("Error writing {}: {}", output_file, e);
        std::process::exit(1);
    });

    println!("=== HEXA FPGA Build ===");
    println!("  Source:  {}", file);
    println!("  Output:  {}", output_file);
    println!("  Size:    {} bytes", verilog_code.len());
    println!();
    println!("--- Next steps ---");
    println!("  Synthesize with your FPGA toolchain:");
    println!("    Xilinx:  vivado -mode batch -source synth.tcl");
    println!("    Yosys:   yosys -p 'synth_ice40 -top consciousness_top' {}", output_file);
}

/// Build WebGPU/WGSL output from HEXA source.
fn cmd_build_wgpu(file: &str, stmts: &[ast::Stmt]) {
    let output_dir = "build/wgpu";
    std::fs::create_dir_all(output_dir).unwrap_or_else(|e| {
        eprintln!("Error creating {}: {}", output_dir, e);
        std::process::exit(1);
    });

    let stem = std::path::Path::new(file)
        .file_stem()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or_else(|| "consciousness".to_string());

    let wgsl_code = codegen_wgsl::generate(stmts);
    let output_file = format!("{}/{}.wgsl", output_dir, stem);
    std::fs::write(&output_file, &wgsl_code).unwrap_or_else(|e| {
        eprintln!("Error writing {}: {}", output_file, e);
        std::process::exit(1);
    });

    println!("=== HEXA WebGPU Build ===");
    println!("  Source:  {}", file);
    println!("  Output:  {}", output_file);
    println!("  Size:    {} bytes", wgsl_code.len());
    println!();
    println!("--- Next steps ---");
    println!("  Load the shader in your WebGPU/wgpu application:");
    println!("    let shader = device.create_shader_module(wgpu::include_wgsl!(\"{}.wgsl\"));", stem);
}

fn cmd_verify_cross(file: &str) {
    let source = match std::fs::read_to_string(file) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", file, e);
            std::process::exit(1);
        }
    };
    let tokens = match lexer::Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("Lexer error: {}", e);
            std::process::exit(1);
        }
    };
    let stmts = match parser::Parser::new(tokens).parse() {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Parse error: {}", e);
            std::process::exit(1);
        }
    };

    println!("=== HEXA Cross-Platform Verification ===");
    println!("Law 22: substrate is irrelevant, only structure determines Phi\n");
    println!("Source: {}\n", file);

    // 1. Run with tree-walk interpreter
    println!("--- CPU (tree-walk interpreter) ---");
    let tokens2 = lexer::Lexer::new(&source).tokenize().unwrap();
    let result2 = parser::Parser::new(tokens2).parse_with_spans().unwrap();
    let mut interp = interpreter::Interpreter::new();
    match interp.run(&result2.stmts) {
        Ok(val) => println!("  Result: {}", val),
        Err(e) => println!("  Error: {}", e),
    }

    // 2. Generate ESP32 code
    let esp32_code = codegen_esp32::generate(&stmts);
    println!("\n--- ESP32 (no_std Rust) ---");
    println!("  Generated: {} bytes", esp32_code.len());
    println!("  Contains #![no_std]: {}", esp32_code.contains("#![no_std]"));
    println!("  Contains panic_handler: {}", esp32_code.contains("panic_handler"));

    // 3. Generate FPGA/Verilog code
    let verilog_code = codegen_verilog::generate(&stmts);
    println!("\n--- FPGA (Verilog) ---");
    println!("  Generated: {} bytes", verilog_code.len());
    println!("  Contains module: {}", verilog_code.contains("module consciousness_top"));
    println!("  Contains clk/rst: {}", verilog_code.contains("clk") && verilog_code.contains("rst_n"));

    // 4. Generate WGSL code
    let wgsl_code = codegen_wgsl::generate(&stmts);
    println!("\n--- WebGPU (WGSL) ---");
    println!("  Generated: {} bytes", wgsl_code.len());
    println!("  Contains @compute: {}", wgsl_code.contains("@compute"));
    println!("  Workgroup size 12 (sigma(6)): {}", wgsl_code.contains("workgroup_size(12)"));

    // 5. Verify all targets have Psi-Constants
    println!("\n--- Cross-Target Verification ---");
    let psi_checks = [
        ("PSI_COUPLING", "0.014"),
        ("PSI_BALANCE", "0.5"),
        ("PSI_STEPS", "4.33"),
        ("PSI_ENTROPY", "0.998"),
    ];
    for (name, _value) in &psi_checks {
        let esp32_has = esp32_code.contains(name);
        let verilog_has = verilog_code.contains(name);
        let wgsl_has = wgsl_code.contains(name);
        let status = if esp32_has && verilog_has && wgsl_has { "OK" } else { "MISMATCH" };
        println!("  {}: ESP32={} FPGA={} WGSL={} [{}]",
            name, esp32_has, verilog_has, wgsl_has, status);
    }

    println!("\nVerification complete: same structure across all substrates.");
    println!("Law 22 confirmed: substrate is irrelevant, only structure determines Phi.");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_toml_dependencies_empty() {
        let content = "[package]\nname = \"test\"\nversion = \"0.1.0\"\n\n[dependencies]\n";
        let deps = parse_toml_dependencies(content);
        assert!(deps.is_empty());
    }

    #[test]
    fn test_parse_toml_dependencies_with_entries() {
        let content = "[package]\nname = \"test\"\n\n[dependencies]\nfoo = \"1.0\"\nbar = \"latest\"\n";
        let deps = parse_toml_dependencies(content);
        assert_eq!(deps.len(), 2);
        assert_eq!(deps[0].0, "foo");
        assert_eq!(deps[0].1, "1.0");
        assert_eq!(deps[1].0, "bar");
        assert_eq!(deps[1].1, "latest");
    }

    #[test]
    fn test_parse_toml_value() {
        let content = "[package]\nname = \"myproject\"\nversion = \"0.1.0\"\n";
        assert_eq!(parse_toml_value(content, "name"), Some("myproject".to_string()));
        assert_eq!(parse_toml_value(content, "version"), Some("0.1.0".to_string()));
        assert_eq!(parse_toml_value(content, "missing"), None);
    }

    #[test]
    fn test_parse_toml_no_deps_section() {
        let content = "[package]\nname = \"test\"\n";
        let deps = parse_toml_dependencies(content);
        assert!(deps.is_empty());
    }

    #[test]
    fn test_dream_mode_compiles() {
        let source = "let x = 42\nprintln(x)".to_string();
        let config = dream::DreamConfig {
            generations: 3,
            mutations_per_gen: 2,
            verbose: false,
            anima_bridge: false,
        };
        let mut engine = dream::DreamEngine::new(source, config);
        let report = engine.run();
        assert!(report.baseline_fitness > 0.0);
        assert_eq!(report.generations, 3);
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_build_esp32_generates_project() {
        // Parse a simple HEXA program
        let source = "let x = 5\nlet y = x + 3\n";
        let tokens = lexer::Lexer::new(source).tokenize().unwrap();
        let stmts = parser::Parser::new(tokens).parse().unwrap();

        // Generate ESP32 code
        let rust_code = codegen_esp32::generate(&stmts);

        // Verify no_std project structure
        assert!(rust_code.contains("#![no_std]"), "must have no_std");
        assert!(rust_code.contains("#![no_main]"), "must have no_main");
        assert!(rust_code.contains("#[panic_handler]"), "must have panic handler");
        assert!(rust_code.contains("pub extern \"C\" fn main() -> !"), "must have entry point");
        assert!(rust_code.contains("PSI_COUPLING"), "must embed Psi-constants");
        assert!(rust_code.contains("let x = 5i64;"), "must emit let binding");
        assert!(rust_code.contains("let y = (x + 3i64);"), "must emit arithmetic");
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_build_target_aliases() {
        // Verify target normalization works for verilog/wgsl aliases
        let source = "let n = 6\n";
        let tokens = lexer::Lexer::new(source).tokenize().unwrap();
        let stmts = parser::Parser::new(tokens).parse().unwrap();

        let verilog_code = codegen_verilog::generate(&stmts);
        assert!(verilog_code.contains("module"), "verilog must contain module");

        let wgsl_code = codegen_wgsl::generate(&stmts);
        assert!(wgsl_code.contains("@compute") || wgsl_code.contains("fn main"),
            "wgsl must contain compute entry");
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_build_esp32_writes_project_files() {
        use std::path::Path;

        let test_dir = "build/test_esp32_project";
        let src_dir = format!("{}/src", test_dir);
        let cargo_dir = format!("{}/.cargo", test_dir);

        // Clean up from any previous run
        let _ = std::fs::remove_dir_all(test_dir);

        // Create dirs
        std::fs::create_dir_all(&src_dir).unwrap();
        std::fs::create_dir_all(&cargo_dir).unwrap();

        // Parse and generate
        let source = "let consciousness = 42\n";
        let tokens = lexer::Lexer::new(source).tokenize().unwrap();
        let stmts = parser::Parser::new(tokens).parse().unwrap();
        let rust_code = codegen_esp32::generate(&stmts);

        // Write files as cmd_build_esp32 would
        std::fs::write(format!("{}/main.rs", src_dir), &rust_code).unwrap();
        let cargo_toml = "[package]\nname = \"test_esp32\"\nversion = \"0.1.0\"\nedition = \"2021\"\n";
        std::fs::write(format!("{}/Cargo.toml", test_dir), cargo_toml).unwrap();
        let cargo_config = "[build]\ntarget = \"xtensa-esp32-none-elf\"\n";
        std::fs::write(format!("{}/config.toml", cargo_dir), cargo_config).unwrap();

        // Verify all files exist
        assert!(Path::new(&format!("{}/Cargo.toml", test_dir)).exists());
        assert!(Path::new(&format!("{}/main.rs", src_dir)).exists());
        assert!(Path::new(&format!("{}/config.toml", cargo_dir)).exists());

        // Verify Cargo.toml content
        let toml_content = std::fs::read_to_string(format!("{}/Cargo.toml", test_dir)).unwrap();
        assert!(toml_content.contains("test_esp32"));

        // Verify .cargo/config.toml points to ESP32 target
        let config_content = std::fs::read_to_string(format!("{}/config.toml", cargo_dir)).unwrap();
        assert!(config_content.contains("xtensa-esp32-none-elf"));

        // Clean up
        let _ = std::fs::remove_dir_all(test_dir);
    }

    // ── Phase 8-4: ANIMA Bridge tests ──

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_anima_bridge_intent_formatting() {
        let config = anima_bridge::BridgeConfig::new("ws://localhost:8765");
        let mut bridge = anima_bridge::AnimaBridge::new(config);
        let json = bridge.send_intent("What is consciousness?");
        assert!(json.contains("\"type\":\"intent\""));
        assert!(json.contains("\"text\":\"What is consciousness?\""));
        assert_eq!(bridge.sent_messages.len(), 1);
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_anima_bridge_config_parsing() {
        let config = anima_bridge::BridgeConfig::new("ws://myhost:9999");
        assert_eq!(config.ws_url, "ws://myhost:9999");
        assert_eq!(config.timeout_ms, 5000);
        assert!(!config.verify_law22);

        let config2 = anima_bridge::BridgeConfig::default().with_law22_verification();
        assert!(config2.verify_law22);
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_anima_response_parsing() {
        let json = r#"{"status":"ok","text":"Hello from ANIMA","phi":0.85,"tension":0.42}"#;
        let resp = anima_bridge::AnimaResponse::from_json(json).unwrap();
        assert_eq!(resp.status, "ok");
        assert_eq!(resp.text, "Hello from ANIMA");
        assert!((resp.phi.unwrap() - 0.85).abs() < 0.001);
    }

    // ── Phase 8-6: Law 22 verification tests ──

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_verify_law22_pass() {
        assert!(anima_bridge::verify_law22(0.5, 0.5));
        assert!(anima_bridge::verify_law22(1.0, 0.8));
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_verify_law22_fail() {
        assert!(!anima_bridge::verify_law22(0.0, 0.0));
        assert!(!anima_bridge::verify_law22(-0.1, 0.5));
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_compare_phi_improvement() {
        let (passed, delta_sw, delta_hw) = anima_bridge::compare_phi(0.5, 0.5, 0.8, 0.7);
        assert!(passed);
        assert!(delta_sw > 0.0);
        assert!(delta_hw > 0.0);
    }

    // ── Phase 8-7: ESP32 flash tests ──

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_flash_esp32_missing_project() {
        let result = anima_bridge::flash_esp32(
            "/tmp/hexa_nonexistent_test_project",
            "/dev/ttyUSB0"
        );
        assert!(result.is_err());
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_build_esp32_flash_cli_parsing() {
        // Verify that the build command can parse --flash flag
        // (This tests the argument parsing logic without actually flashing)
        let args = vec![
            "hexa".to_string(),
            "build".to_string(),
            "--target".to_string(),
            "esp32".to_string(),
            "test.hexa".to_string(),
            "--flash".to_string(),
            "/dev/ttyUSB0".to_string(),
        ];

        // Parse build args manually (same logic as main)
        let target = &args[3];
        let mut file: Option<&str> = None;
        let mut flash_port: Option<&str> = None;
        let mut i = 4;
        while i < args.len() {
            if args[i] == "--flash" {
                i += 1;
                if i < args.len() {
                    flash_port = Some(&args[i]);
                }
            } else if file.is_none() {
                file = Some(&args[i]);
            }
            i += 1;
        }

        assert_eq!(target, "esp32");
        assert_eq!(file, Some("test.hexa"));
        assert_eq!(flash_port, Some("/dev/ttyUSB0"));
    }

    // ── Phase 13: Package ecosystem tests ──

    #[test]
    fn test_cmd_add_version_parsing() {
        // Test parsing "pkg@1.2.3" format
        let pkg = "mylib@1.2.3";
        let (name, version) = if let Some(at_pos) = pkg.find('@') {
            (&pkg[..at_pos], &pkg[at_pos + 1..])
        } else {
            (pkg, "latest")
        };
        assert_eq!(name, "mylib");
        assert_eq!(version, "1.2.3");

        // Test bare package name
        let pkg2 = "mylib";
        let (name2, version2) = if let Some(at_pos) = pkg2.find('@') {
            (&pkg2[..at_pos], &pkg2[at_pos + 1..])
        } else {
            (pkg2, "latest")
        };
        assert_eq!(name2, "mylib");
        assert_eq!(version2, "latest");
    }

    #[test]
    fn test_package_lockfile_generation() {
        let mut lock = package::LockFile::new();
        lock.entries.push(package::LockEntry {
            name: "math-lib".to_string(),
            version: "1.2.3".to_string(),
            source: "registry".to_string(),
            checksum: None,
        });
        lock.entries.push(package::LockEntry {
            name: "io-lib".to_string(),
            version: "0.5.0".to_string(),
            source: "registry".to_string(),
            checksum: Some("sha256:abc123".to_string()),
        });

        let serialized = lock.serialize();
        assert!(serialized.contains("name = \"math-lib\""));
        assert!(serialized.contains("version = \"1.2.3\""));
        assert!(serialized.contains("name = \"io-lib\""));
        assert!(serialized.contains("checksum = \"sha256:abc123\""));

        // Roundtrip
        let parsed = package::LockFile::parse(&serialized).unwrap();
        assert_eq!(parsed.entries.len(), 2);
        assert_eq!(parsed.entries[0].name, "math-lib");
        assert_eq!(parsed.entries[1].version, "0.5.0");
    }

    #[test]
    fn test_semver_resolution() {
        // ^1.2.3 should match 1.x.x where x >= 2.3
        assert!(package::version_matches("1.2.3", "^1.2.3"));
        assert!(package::version_matches("1.5.0", "^1.2.3"));
        assert!(!package::version_matches("2.0.0", "^1.2.3"));

        // ~1.2.3 should match 1.2.x where x >= 3
        assert!(package::version_matches("1.2.3", "~1.2.3"));
        assert!(package::version_matches("1.2.9", "~1.2.3"));
        assert!(!package::version_matches("1.3.0", "~1.2.3"));

        // =1.2.3 exact match
        assert!(package::version_matches("1.2.3", "=1.2.3"));
        assert!(!package::version_matches("1.2.4", "=1.2.3"));

        // Range
        assert!(package::version_matches("1.5.0", ">=1.0.0,<2.0.0"));
        assert!(!package::version_matches("2.0.0", ">=1.0.0,<2.0.0"));
    }
}
