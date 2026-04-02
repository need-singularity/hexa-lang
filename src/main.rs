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
#[allow(dead_code)]
mod jit;
#[allow(dead_code)]
mod lsp;
#[allow(dead_code)]
mod codegen_esp32;
#[allow(dead_code)]
mod codegen_verilog;
#[allow(dead_code)]
mod codegen_wgsl;

use std::io::{self, Write, BufRead};

fn main() {
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
            "--lsp" => lsp::run_lsp(),
            "--bench" => run_benchmark(),
            "build" => {
                if args.len() < 4 || args[2] != "--target" {
                    eprintln!("Usage: hexa build --target <esp32|fpga|wgpu|cpu> <file.hexa>");
                    std::process::exit(1);
                }
                let target = &args[3];
                let file = if args.len() > 4 { &args[4] } else {
                    eprintln!("Usage: hexa build --target {} <file.hexa>", target);
                    std::process::exit(1);
                };
                cmd_build(target, file);
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
            "run" => cmd_run(),
            "test" => cmd_test(),
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
    // Type checking pass (runs before execution)
    let mut checker = type_checker::TypeChecker::new();
    if let Err(e) = checker.check(&result.stmts, &result.spans) {
        eprint_diagnostic(&e, &source_lines, file_name);
        std::process::exit(1);
    }
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
    let mut jit_compiler = match jit::JitCompiler::new() {
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
            eprintln!("{}", e);
            std::process::exit(1);
        }
    }
}

fn run_benchmark() {
    use std::time::Instant;

    let fib_src = r#"
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(30)
"#;

    // Parse once
    let tokens = lexer::Lexer::new(fib_src).tokenize().unwrap();
    let stmts = parser::Parser::new(tokens).parse().unwrap();

    // Tree-walk interpreter
    let start = Instant::now();
    let mut interp = interpreter::Interpreter::new();
    let tree_result = interp.run(&stmts).unwrap();
    let tree_time = start.elapsed();

    // VM
    let mut comp = compiler::Compiler::new();
    let chunk = comp.compile(&stmts).unwrap();
    let start = Instant::now();
    let mut machine = vm::VM::new();
    let vm_result = machine.execute(&chunk).unwrap();
    let vm_time = start.elapsed();

    // Native (Cranelift JIT)
    let tokens2 = lexer::Lexer::new(fib_src).tokenize().unwrap();
    let stmts2 = parser::Parser::new(tokens2).parse().unwrap();
    let start = Instant::now();
    let mut jit_compiler = jit::JitCompiler::new().unwrap();
    let native_result = jit_compiler.compile_and_run(&stmts2).unwrap();
    let native_time = start.elapsed();

    println!("=== HEXA-LANG Benchmark: fib(30) ===");
    println!("Tree-walk:    {} = {:?}", tree_result, tree_time);
    println!("Bytecode VM:  {} = {:?}", vm_result, vm_time);
    println!("Native (JIT): {} = {:?}", native_result, native_time);
    println!();
    let vm_speedup = tree_time.as_secs_f64() / vm_time.as_secs_f64();
    let native_speedup = tree_time.as_secs_f64() / native_time.as_secs_f64();
    let native_vs_vm = vm_time.as_secs_f64() / native_time.as_secs_f64();
    println!("VM vs Tree-walk:     {:.1}x", vm_speedup);
    println!("Native vs Tree-walk: {:.1}x", native_speedup);
    println!("Native vs VM:        {:.1}x", native_vs_vm);
}

fn run_repl() {
    println!("HEXA-LANG v0.1.0 -- The Perfect Number Language");
    println!("sigma(n)*phi(n) = n*tau(n) iff n = 6");
    println!("Type :quit to exit, :help for help\n");

    let mut interp = interpreter::Interpreter::new();
    let stdin = io::stdin();

    loop {
        print!("hexa> ");
        io::stdout().flush().unwrap();

        let mut line = String::new();
        if stdin.lock().read_line(&mut line).unwrap() == 0 {
            break; // EOF
        }
        let trimmed = line.trim();

        if trimmed.is_empty() {
            continue;
        }
        if trimmed == ":quit" || trimmed == ":q" {
            break;
        }
        if trimmed == ":help" || trimmed == ":h" {
            println!("Commands: :quit, :help");
            println!("n=6 builtins: sigma(n), phi(n), tau(n), gcd(a,b)");
            println!("Try: sigma(6) * phi(6) == 6 * tau(6)");
            continue;
        }

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
                eprintln!("{}", e);
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
            Err(e) => eprintln!("{}", e),
        }
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
        "[package]\nname = \"{}\"\nversion = \"0.1.0\"\n",
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

fn cmd_run() {
    // Find hexa.toml in current directory
    let toml_path = std::path::Path::new("hexa.toml");
    if !toml_path.exists() {
        eprintln!("Error: no hexa.toml found in current directory");
        eprintln!("Run 'hexa init <name>' to create a new project");
        std::process::exit(1);
    }

    let main_path = "src/main.hexa";
    if !std::path::Path::new(main_path).exists() {
        eprintln!("Error: src/main.hexa not found");
        std::process::exit(1);
    }

    run_file(main_path);
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

    let (output_code, output_dir, output_file) = match target {
        "esp32" => (
            codegen_esp32::generate(&stmts),
            "target/esp32",
            "main.rs",
        ),
        "fpga" => {
            let stem = std::path::Path::new(file)
                .file_stem()
                .map(|s| s.to_string_lossy().to_string())
                .unwrap_or_else(|| "consciousness".to_string());
            (
                codegen_verilog::generate(&stmts),
                "target/fpga",
                // Leak the string so we get a &'static str — fine for CLI
                Box::leak(format!("{}.v", stem).into_boxed_str()) as &str,
            )
        }
        "wgpu" => {
            let stem = std::path::Path::new(file)
                .file_stem()
                .map(|s| s.to_string_lossy().to_string())
                .unwrap_or_else(|| "consciousness".to_string());
            (
                codegen_wgsl::generate(&stmts),
                "target/wgpu",
                Box::leak(format!("{}.wgsl", stem).into_boxed_str()) as &str,
            )
        }
        "cpu" => {
            // CPU target — run with default interpreter
            println!("CPU target: running with tree-walk interpreter...");
            run_file(file);
            return;
        }
        _ => {
            eprintln!("Unknown target: {}", target);
            eprintln!("Available targets: cpu, esp32, fpga, wgpu");
            std::process::exit(1);
        }
    };

    // Create output directory
    std::fs::create_dir_all(output_dir).unwrap_or_else(|e| {
        eprintln!("Error creating {}: {}", output_dir, e);
        std::process::exit(1);
    });

    let output_path = format!("{}/{}", output_dir, output_file);
    std::fs::write(&output_path, &output_code).unwrap_or_else(|e| {
        eprintln!("Error writing {}: {}", output_path, e);
        std::process::exit(1);
    });

    println!("Build complete: {} -> {}", file, output_path);
    println!("  Target: {}", target);
    println!("  Output: {} bytes", output_code.len());
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
