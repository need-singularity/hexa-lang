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

use std::io::{self, Write, BufRead};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() > 1 {
        run_file(&args[1]);
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
    run_source(&source);
}

fn run_source(source: &str) {
    let tokens = match lexer::Lexer::new(source).tokenize() {
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
    let mut interp = interpreter::Interpreter::new();
    match interp.run(&stmts) {
        Ok(_) => {}
        Err(e) => {
            eprintln!("{}", e);
            std::process::exit(1);
        }
    }
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
        let stmts = match parser::Parser::new(tokens).parse() {
            Ok(s) => s,
            Err(e) => {
                eprintln!("{}", e);
                continue;
            }
        };
        match interp.run(&stmts) {
            Ok(val) => match &val {
                env::Value::Void => {}
                _ => println!("{}", val),
            },
            Err(e) => eprintln!("{}", e),
        }
    }
}
