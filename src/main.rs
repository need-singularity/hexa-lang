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

use std::io::{self, Write, BufRead};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() > 1 {
        if args[1] == "--test" {
            if args.len() < 3 {
                eprintln!("Usage: hexa --test <file.hexa>");
                std::process::exit(1);
            }
            run_test(&args[2]);
        } else if args[1] == "--vm" {
            if args.len() < 3 {
                eprintln!("Usage: hexa --vm <file.hexa>");
                std::process::exit(1);
            }
            run_file_vm(&args[2]);
        } else if args[1] == "--bench" {
            run_benchmark();
        } else {
            run_file(&args[1]);
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
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    let stmts = result.stmts;
    let spans = result.spans;

    // Type checking pass (runs before execution)
    let mut checker = type_checker::TypeChecker::new();
    if let Err(e) = checker.check(&stmts, &spans) {
        eprintln!("{}", e);
        std::process::exit(1);
    }

    // First pass: run non-proof statements to set up functions/variables
    let mut interp = interpreter::Interpreter::new();
    interp.test_mode = true;

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
                    eprintln!("Error: {}", e);
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
            eprintln!("{}", e);
            std::process::exit(1);
        }
    };
    // Type checking pass (runs before execution)
    let mut checker = type_checker::TypeChecker::new();
    if let Err(e) = checker.check(&result.stmts, &result.spans) {
        eprintln!("{}", e);
        std::process::exit(1);
    }
    let mut interp = interpreter::Interpreter::new();
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
            eprintln!("{}", e);
            std::process::exit(1);
        }
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

    println!("=== HEXA-LANG Benchmark: fib(30) ===");
    println!("Tree-walk: {} = {:?}", tree_result, tree_time);
    println!("Bytecode VM: {} = {:?}", vm_result, vm_time);
    let speedup = tree_time.as_secs_f64() / vm_time.as_secs_f64();
    println!("Speedup: {:.1}x", speedup);
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
