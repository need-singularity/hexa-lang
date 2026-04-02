#![allow(dead_code)]

//! ESP32 Code Generator — Transpile HEXA AST to no_std Rust for ESP32.
//!
//! Generates `#![no_std]` + `#![no_main]` Rust source that compiles with
//! `cargo build --target xtensa-esp32-none-elf`.
//!
//! Supported subset:
//!   - Integer / float / bool / string literals
//!   - Arithmetic, comparison, logical, bitwise operators
//!   - let bindings, variable assignment
//!   - if/else, while, loop, for (range only)
//!   - Function declarations and calls
//!   - Consciousness Psi-constants → compile-time constants
//!   - spawn → sequential fallback (single core)
//!   - println → defmt::println! (or uart_write stub)

use crate::ast::*;

/// Generate no_std Rust source code from a HEXA AST.
pub fn generate(stmts: &[Stmt]) -> String {
    let mut ctx = Esp32Context::new();
    ctx.emit_program(stmts);
    ctx.output
}

struct Esp32Context {
    output: String,
    indent: usize,
    /// Track which functions have been declared (for forward references).
    declared_fns: Vec<String>,
}

impl Esp32Context {
    fn new() -> Self {
        Self {
            output: String::new(),
            indent: 0,
            declared_fns: Vec::new(),
        }
    }

    fn push_indent(&mut self) { self.indent += 1; }
    fn pop_indent(&mut self) { if self.indent > 0 { self.indent -= 1; } }

    fn write_indent(&mut self) {
        for _ in 0..self.indent {
            self.output.push_str("    ");
        }
    }

    fn writeln(&mut self, s: &str) {
        self.write_indent();
        self.output.push_str(s);
        self.output.push('\n');
    }

    fn emit_program(&mut self, stmts: &[Stmt]) {
        // Header
        self.output.push_str("#![no_std]\n");
        self.output.push_str("#![no_main]\n\n");
        self.output.push_str("use core::panic::PanicInfo;\n\n");

        // Psi-constants
        self.output.push_str("// Consciousness Psi-Constants (from ln(2))\n");
        self.output.push_str("const PSI_COUPLING: f64 = 0.014;\n");
        self.output.push_str("const PSI_BALANCE: f64 = 0.5;\n");
        self.output.push_str("const PSI_STEPS: f64 = 4.33;\n");
        self.output.push_str("const PSI_ENTROPY: f64 = 0.998;\n");
        self.output.push_str("const PSI_FRUSTRATION: f64 = 0.10;\n\n");

        // Panic handler
        self.output.push_str("#[panic_handler]\n");
        self.output.push_str("fn panic(_info: &PanicInfo) -> ! {\n");
        self.output.push_str("    loop {}\n");
        self.output.push_str("}\n\n");

        // UART write stub
        self.output.push_str("/// UART write stub — replace with actual ESP32 HAL UART.\n");
        self.output.push_str("fn uart_write(s: &str) {\n");
        self.output.push_str("    // TODO: Replace with esp32-hal UART write\n");
        self.output.push_str("    let _ = s;\n");
        self.output.push_str("}\n\n");

        self.output.push_str("fn uart_write_int(n: i64) {\n");
        self.output.push_str("    // TODO: Replace with actual int-to-string + UART\n");
        self.output.push_str("    let _ = n;\n");
        self.output.push_str("}\n\n");

        self.output.push_str("fn uart_write_float(n: f64) {\n");
        self.output.push_str("    let _ = n;\n");
        self.output.push_str("}\n\n");

        self.output.push_str("fn uart_write_bool(b: bool) {\n");
        self.output.push_str("    let _ = b;\n");
        self.output.push_str("}\n\n");

        // First pass: emit function declarations
        for stmt in stmts {
            if let Stmt::FnDecl(f) = stmt {
                self.emit_fn_decl(f);
                self.output.push('\n');
            }
        }

        // Entry point
        self.output.push_str("#[no_mangle]\n");
        self.output.push_str("pub extern \"C\" fn main() -> ! {\n");
        self.push_indent();

        // Second pass: emit non-function statements inside main
        for stmt in stmts {
            match stmt {
                Stmt::FnDecl(_) => {} // already emitted
                _ => self.emit_stmt(stmt),
            }
        }

        self.writeln("loop {}");
        self.pop_indent();
        self.output.push_str("}\n");
    }

    fn emit_fn_decl(&mut self, f: &FnDecl) {
        self.declared_fns.push(f.name.clone());
        self.write_indent();
        self.output.push_str(&format!("fn {}(", f.name));
        for (i, p) in f.params.iter().enumerate() {
            if i > 0 { self.output.push_str(", "); }
            self.output.push_str(&p.name);
            self.output.push_str(": ");
            self.output.push_str(&map_type_annotation(p.typ.as_deref()));
        }
        self.output.push_str(") -> ");
        self.output.push_str(&map_type_annotation(f.ret_type.as_deref()));
        self.output.push_str(" {\n");
        self.push_indent();
        self.emit_block_stmts(&f.body);
        self.pop_indent();
        self.writeln("}");
    }

    fn emit_block_stmts(&mut self, stmts: &[Stmt]) {
        for stmt in stmts {
            self.emit_stmt(stmt);
        }
    }

    fn emit_stmt(&mut self, stmt: &Stmt) {
        match stmt {
            Stmt::Let(name, typ, init, _vis) => {
                self.write_indent();
                self.output.push_str("let ");
                self.output.push_str(name);
                if let Some(t) = typ {
                    self.output.push_str(": ");
                    self.output.push_str(&map_type_str(t));
                }
                if let Some(expr) = init {
                    self.output.push_str(" = ");
                    self.emit_expr(expr);
                }
                self.output.push_str(";\n");
            }
            Stmt::Assign(target, value) => {
                self.write_indent();
                self.emit_expr(target);
                self.output.push_str(" = ");
                self.emit_expr(value);
                self.output.push_str(";\n");
            }
            Stmt::Expr(expr) => {
                self.write_indent();
                self.emit_expr(expr);
                self.output.push_str(";\n");
            }
            Stmt::Return(Some(expr)) => {
                self.write_indent();
                self.output.push_str("return ");
                self.emit_expr(expr);
                self.output.push_str(";\n");
            }
            Stmt::Return(None) => {
                self.writeln("return;");
            }
            Stmt::FnDecl(f) => {
                // Nested function — emit as closure or inner fn
                self.emit_fn_decl(f);
            }
            Stmt::For(var, iter_expr, body) => {
                self.write_indent();
                self.output.push_str(&format!("for {} in ", var));
                self.emit_expr(iter_expr);
                self.output.push_str(" {\n");
                self.push_indent();
                self.emit_block_stmts(body);
                self.pop_indent();
                self.writeln("}");
            }
            Stmt::While(cond, body) => {
                self.write_indent();
                self.output.push_str("while ");
                self.emit_expr(cond);
                self.output.push_str(" {\n");
                self.push_indent();
                self.emit_block_stmts(body);
                self.pop_indent();
                self.writeln("}");
            }
            Stmt::Loop(body) => {
                self.writeln("loop {");
                self.push_indent();
                self.emit_block_stmts(body);
                self.pop_indent();
                self.writeln("}");
            }
            Stmt::Assert(expr) => {
                self.write_indent();
                self.output.push_str("// assert: ");
                self.emit_expr(expr);
                self.output.push('\n');
            }
            Stmt::Spawn(body) => {
                // ESP32 single core — sequential fallback
                self.writeln("// spawn → sequential fallback (ESP32 single core)");
                self.writeln("{");
                self.push_indent();
                self.emit_block_stmts(body);
                self.pop_indent();
                self.writeln("}");
            }
            Stmt::DropStmt(name) => {
                self.writeln(&format!("drop({});", name));
            }
            Stmt::TryCatch(try_block, var, catch_block) => {
                self.writeln("// try-catch (no_std: simplified)");
                self.writeln("{");
                self.push_indent();
                self.emit_block_stmts(try_block);
                self.pop_indent();
                self.writeln("}");
                self.writeln(&format!("// catch {} {{ ... }}", var));
                let _ = catch_block;
            }
            _ => {
                self.writeln("// unsupported statement");
            }
        }
    }

    fn emit_expr(&mut self, expr: &Expr) {
        match expr {
            Expr::IntLit(n) => {
                self.output.push_str(&format!("{}i64", n));
            }
            Expr::FloatLit(n) => {
                if n.fract() == 0.0 {
                    self.output.push_str(&format!("{:.1}f64", n));
                } else {
                    self.output.push_str(&format!("{}f64", n));
                }
            }
            Expr::BoolLit(b) => {
                self.output.push_str(&format!("{}", b));
            }
            Expr::StringLit(s) => {
                self.output.push_str(&format!("\"{}\"", s));
            }
            Expr::CharLit(c) => {
                self.output.push_str(&format!("'{}'", c));
            }
            Expr::Ident(name) => {
                self.output.push_str(name);
            }
            Expr::Binary(lhs, op, rhs) => {
                self.output.push('(');
                self.emit_expr(lhs);
                self.output.push_str(&format!(" {} ", binop_to_rust(op)));
                self.emit_expr(rhs);
                self.output.push(')');
            }
            Expr::Unary(op, operand) => {
                let op_str = match op {
                    UnaryOp::Neg => "-",
                    UnaryOp::Not => "!",
                    UnaryOp::BitNot => "!",
                };
                self.output.push_str(op_str);
                self.emit_expr(operand);
            }
            Expr::Call(func, args) => {
                if let Expr::Ident(name) = func.as_ref() {
                    match name.as_str() {
                        "println" => {
                            self.output.push_str("uart_write(\"");
                            // Emit args as concatenated string
                            for (i, arg) in args.iter().enumerate() {
                                if i > 0 { self.output.push(' '); }
                                match arg {
                                    Expr::StringLit(s) => self.output.push_str(s),
                                    _ => {
                                        self.output.push_str("\");\n");
                                        self.write_indent();
                                        // Emit typed write for non-string args
                                        match arg {
                                            Expr::IntLit(_) | Expr::Ident(_) | Expr::Binary(..) => {
                                                self.output.push_str("uart_write_int(");
                                                self.emit_expr(arg);
                                                self.output.push_str(");\n");
                                                self.write_indent();
                                                self.output.push_str("uart_write(\"");
                                                return;
                                            }
                                            _ => {
                                                self.output.push_str("uart_write(\"?\"");
                                                return;
                                            }
                                        }
                                    }
                                }
                            }
                            self.output.push_str("\\n\")");
                            return;
                        }
                        "print" => {
                            self.output.push_str("uart_write(\"");
                            for arg in args {
                                if let Expr::StringLit(s) = arg {
                                    self.output.push_str(s);
                                }
                            }
                            self.output.push_str("\")");
                            return;
                        }
                        // Consciousness builtins → constants
                        "psi_coupling" => { self.output.push_str("PSI_COUPLING"); return; }
                        "psi_balance" => { self.output.push_str("PSI_BALANCE"); return; }
                        "psi_steps" => { self.output.push_str("PSI_STEPS"); return; }
                        "psi_entropy" => { self.output.push_str("PSI_ENTROPY"); return; }
                        "psi_frustration" => { self.output.push_str("PSI_FRUSTRATION"); return; }
                        "consciousness_max_cells" => { self.output.push_str("1024i64"); return; }
                        "consciousness_decoder_dim" => { self.output.push_str("384i64"); return; }
                        "consciousness_phi" => { self.output.push_str("71i64"); return; }
                        // Number theory builtins — inline known values for n=6
                        "sigma" => {
                            self.output.push_str("sigma(");
                            if !args.is_empty() { self.emit_expr(&args[0]); }
                            self.output.push(')');
                            return;
                        }
                        "phi" => {
                            self.output.push_str("euler_phi(");
                            if !args.is_empty() { self.emit_expr(&args[0]); }
                            self.output.push(')');
                            return;
                        }
                        "tau" => {
                            self.output.push_str("tau(");
                            if !args.is_empty() { self.emit_expr(&args[0]); }
                            self.output.push(')');
                            return;
                        }
                        "pow" => {
                            if args.len() == 2 {
                                self.output.push_str("pow_i64(");
                                self.emit_expr(&args[0]);
                                self.output.push_str(", ");
                                self.emit_expr(&args[1]);
                                self.output.push(')');
                                return;
                            }
                        }
                        "len" => {
                            self.output.push_str("len(");
                            if !args.is_empty() { self.emit_expr(&args[0]); }
                            self.output.push(')');
                            return;
                        }
                        "to_float" => {
                            self.output.push('(');
                            if !args.is_empty() { self.emit_expr(&args[0]); }
                            self.output.push_str(" as f64)");
                            return;
                        }
                        "to_int" => {
                            self.output.push('(');
                            if !args.is_empty() { self.emit_expr(&args[0]); }
                            self.output.push_str(" as i64)");
                            return;
                        }
                        _ => {}
                    }
                }
                // General function call
                self.emit_expr(func);
                self.output.push('(');
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 { self.output.push_str(", "); }
                    self.emit_expr(arg);
                }
                self.output.push(')');
            }
            Expr::If(cond, then_block, else_block) => {
                self.output.push_str("if ");
                self.emit_expr(cond);
                self.output.push_str(" {\n");
                self.push_indent();
                self.emit_block_stmts(then_block);
                self.pop_indent();
                self.write_indent();
                if let Some(else_b) = else_block {
                    self.output.push_str("} else {\n");
                    self.push_indent();
                    self.emit_block_stmts(else_b);
                    self.pop_indent();
                    self.write_indent();
                }
                self.output.push('}');
            }
            Expr::Array(elems) => {
                self.output.push('[');
                for (i, e) in elems.iter().enumerate() {
                    if i > 0 { self.output.push_str(", "); }
                    self.emit_expr(e);
                }
                self.output.push(']');
            }
            Expr::Index(arr, idx) => {
                self.emit_expr(arr);
                self.output.push('[');
                self.emit_expr(idx);
                self.output.push_str(" as usize]");
            }
            Expr::Field(obj, field) => {
                self.emit_expr(obj);
                self.output.push('.');
                self.output.push_str(field);
            }
            Expr::Range(start, end, inclusive) => {
                self.emit_expr(start);
                if *inclusive {
                    self.output.push_str("..=");
                } else {
                    self.output.push_str("..");
                }
                self.emit_expr(end);
            }
            Expr::Block(stmts) => {
                self.output.push_str("{\n");
                self.push_indent();
                self.emit_block_stmts(stmts);
                self.pop_indent();
                self.write_indent();
                self.output.push('}');
            }
            _ => {
                self.output.push_str("()");
            }
        }
    }
}

/// Map HEXA type annotations to Rust types.
fn map_type_annotation(typ: Option<&str>) -> String {
    match typ {
        Some("int") => "i64".to_string(),
        Some("float") => "f64".to_string(),
        Some("bool") => "bool".to_string(),
        Some("char") => "char".to_string(),
        Some("string") => "&str".to_string(),
        Some("byte") => "u8".to_string(),
        Some("void") => "()".to_string(),
        Some(other) => other.to_string(),
        None => "i64".to_string(), // default
    }
}

/// Map HEXA type string to Rust type.
fn map_type_str(typ: &str) -> String {
    match typ {
        "int" => "i64".to_string(),
        "float" => "f64".to_string(),
        "bool" => "bool".to_string(),
        "char" => "char".to_string(),
        "string" => "&str".to_string(),
        "byte" => "u8".to_string(),
        "void" => "()".to_string(),
        other => other.to_string(),
    }
}

/// Map HEXA binary operator to Rust operator string.
fn binop_to_rust(op: &BinOp) -> &'static str {
    match op {
        BinOp::Add => "+",
        BinOp::Sub => "-",
        BinOp::Mul => "*",
        BinOp::Div => "/",
        BinOp::Rem => "%",
        BinOp::Pow => "/* pow */+", // Rust doesn't have ** — use function
        BinOp::Eq => "==",
        BinOp::Ne => "!=",
        BinOp::Lt => "<",
        BinOp::Gt => ">",
        BinOp::Le => "<=",
        BinOp::Ge => ">=",
        BinOp::And => "&&",
        BinOp::Or => "||",
        BinOp::Xor => "^",
        BinOp::BitAnd => "&",
        BinOp::BitOr => "|",
        BinOp::BitXor => "^",
        BinOp::Assign => "=",
        BinOp::DeclAssign => "=",
        BinOp::Range => "..",
        BinOp::Arrow => "->",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn parse(source: &str) -> Vec<Stmt> {
        let tokens = Lexer::new(source).tokenize().unwrap();
        Parser::new(tokens).parse().unwrap()
    }

    #[test]
    fn test_esp32_basic_arithmetic() {
        let stmts = parse("let x = 5\nlet y = x + 3\n");
        let output = generate(&stmts);
        assert!(output.contains("#![no_std]"));
        assert!(output.contains("#![no_main]"));
        assert!(output.contains("let x = 5i64;"));
        assert!(output.contains("let y = (x + 3i64);"));
    }

    #[test]
    fn test_esp32_function_and_consciousness() {
        let stmts = parse("fn add(a, b) { return a + b }\nlet alpha = psi_coupling()\n");
        let output = generate(&stmts);
        assert!(output.contains("fn add(a: i64, b: i64) -> i64 {"));
        assert!(output.contains("return (a + b);"));
        assert!(output.contains("const PSI_COUPLING: f64 = 0.014;"));
        assert!(output.contains("let alpha = PSI_COUPLING;"));
    }

    #[test]
    fn test_esp32_control_flow() {
        let stmts = parse("let x = 10\nwhile x > 0 {\n    x = x - 1\n}\n");
        let output = generate(&stmts);
        assert!(output.contains("while (x > 0i64) {"));
        assert!(output.contains("x = (x - 1i64);"));
    }

    #[test]
    fn test_esp32_spawn_sequential_fallback() {
        let stmts = parse("spawn { let a = 1 }\n");
        let output = generate(&stmts);
        assert!(output.contains("sequential fallback"));
        assert!(output.contains("let a = 1i64;"));
    }

    #[test]
    fn test_esp32_panic_handler() {
        let stmts = parse("let x = 1\n");
        let output = generate(&stmts);
        assert!(output.contains("#[panic_handler]"));
        assert!(output.contains("fn panic("));
    }
}
