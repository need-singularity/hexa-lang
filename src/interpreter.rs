#![allow(dead_code)]

use crate::ast::*;
use crate::env::{Env, Value};
use crate::error::{HexaError, ErrorClass};

/// Sentinel error message used to propagate `return` across call frames.
const RETURN_SENTINEL: &str = "__hexa_return__";

pub struct Interpreter {
    pub env: Env,
    /// Holds the value carried by the most recent `return` statement.
    return_value: Option<Value>,
}

impl Interpreter {
    pub fn new() -> Self {
        Self {
            env: Env::new(),
            return_value: None,
        }
    }

    // ── Public entry point ──────────────────────────────────

    pub fn run(&mut self, stmts: &[Stmt]) -> Result<Value, HexaError> {
        let mut last = Value::Void;
        for stmt in stmts {
            last = self.exec_stmt(stmt)?;
            if self.return_value.is_some() {
                return Ok(self.return_value.take().unwrap());
            }
        }
        Ok(last)
    }

    // ── Statement execution ─────────────────────────────────

    fn exec_stmt(&mut self, stmt: &Stmt) -> Result<Value, HexaError> {
        match stmt {
            Stmt::Let(name, _typ, expr) => {
                let val = match expr {
                    Some(e) => self.eval_expr(e)?,
                    None => Value::Void,
                };
                self.env.define(name, val);
                Ok(Value::Void)
            }
            Stmt::Assign(lhs, rhs) => {
                let val = self.eval_expr(rhs)?;
                match lhs {
                    Expr::Ident(name) => {
                        if !self.env.set(name, val) {
                            return Err(self.runtime_err(format!("undefined variable: {}", name)));
                        }
                    }
                    Expr::Index(arr_expr, idx_expr) => {
                        let idx = self.eval_expr(idx_expr)?;
                        let idx = match idx {
                            Value::Int(i) => i as usize,
                            _ => return Err(self.type_err("index must be an integer".into())),
                        };
                        // Get the array name for simple cases
                        if let Expr::Ident(name) = arr_expr.as_ref() {
                            if let Some(Value::Array(mut arr)) = self.env.get(name) {
                                if idx >= arr.len() {
                                    return Err(self.runtime_err(format!(
                                        "index {} out of bounds (len {})", idx, arr.len()
                                    )));
                                }
                                arr[idx] = val;
                                self.env.set(name, Value::Array(arr));
                            } else {
                                return Err(self.type_err("index assignment requires array".into()));
                            }
                        } else {
                            return Err(self.runtime_err("complex index assignment not supported".into()));
                        }
                    }
                    _ => return Err(self.runtime_err("invalid assignment target".into())),
                }
                Ok(Value::Void)
            }
            Stmt::Expr(expr) => self.eval_expr(expr),
            Stmt::Return(expr) => {
                let val = match expr {
                    Some(e) => self.eval_expr(e)?,
                    None => Value::Void,
                };
                self.return_value = Some(val);
                Ok(Value::Void)
            }
            Stmt::FnDecl(decl) => {
                let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                self.env.define(
                    &decl.name,
                    Value::Fn(decl.name.clone(), param_names, decl.body.clone()),
                );
                Ok(Value::Void)
            }
            Stmt::For(var, iter_expr, body) => {
                let iter_val = self.eval_expr(iter_expr)?;
                let items = match iter_val {
                    Value::Array(a) => a,
                    _ => return Err(self.type_err("for loop requires iterable".into())),
                };
                for item in items {
                    self.env.push_scope();
                    self.env.define(var, item);
                    for s in body {
                        self.exec_stmt(s)?;
                        if self.return_value.is_some() {
                            self.env.pop_scope();
                            return Ok(Value::Void);
                        }
                    }
                    self.env.pop_scope();
                }
                Ok(Value::Void)
            }
            Stmt::While(cond, body) => {
                loop {
                    let c = self.eval_expr(cond)?;
                    if !Self::is_truthy(&c) {
                        break;
                    }
                    self.env.push_scope();
                    for s in body {
                        self.exec_stmt(s)?;
                        if self.return_value.is_some() {
                            self.env.pop_scope();
                            return Ok(Value::Void);
                        }
                    }
                    self.env.pop_scope();
                }
                Ok(Value::Void)
            }
            Stmt::Loop(body) => {
                loop {
                    self.env.push_scope();
                    for s in body {
                        self.exec_stmt(s)?;
                        if self.return_value.is_some() {
                            self.env.pop_scope();
                            return Ok(Value::Void);
                        }
                    }
                    self.env.pop_scope();
                }
            }
            Stmt::Proof(_name, body) => {
                // Execute proof block like a regular block
                self.env.push_scope();
                for s in body {
                    self.exec_stmt(s)?;
                }
                self.env.pop_scope();
                Ok(Value::Void)
            }
            Stmt::Assert(expr) => {
                let val = self.eval_expr(expr)?;
                if !Self::is_truthy(&val) {
                    return Err(HexaError {
                        class: ErrorClass::Logic,
                        message: "assertion failed".into(),
                        line: 0,
                        col: 0,
                    });
                }
                Ok(Value::Void)
            }
            // Declarations we don't fully interpret yet
            Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::TraitDecl(_) | Stmt::ImplBlock(_) => {
                Ok(Value::Void)
            }
            Stmt::Intent(_, _) | Stmt::Mod(_, _) | Stmt::Use(_) => Ok(Value::Void),
        }
    }

    // ── Expression evaluation ───────────────────────────────

    fn eval_expr(&mut self, expr: &Expr) -> Result<Value, HexaError> {
        match expr {
            Expr::IntLit(n) => Ok(Value::Int(*n)),
            Expr::FloatLit(n) => Ok(Value::Float(*n)),
            Expr::BoolLit(b) => Ok(Value::Bool(*b)),
            Expr::StringLit(s) => Ok(Value::Str(s.clone())),
            Expr::CharLit(c) => Ok(Value::Char(*c)),
            Expr::Ident(name) => {
                self.env.get(name).ok_or_else(|| HexaError {
                    class: ErrorClass::Name,
                    message: format!("undefined variable: {}", name),
                    line: 0,
                    col: 0,
                })
            }
            Expr::Binary(left, op, right) => {
                let l = self.eval_expr(left)?;
                // Short-circuit for logical operators
                match op {
                    BinOp::And => {
                        if !Self::is_truthy(&l) {
                            return Ok(Value::Bool(false));
                        }
                        let r = self.eval_expr(right)?;
                        return Ok(Value::Bool(Self::is_truthy(&r)));
                    }
                    BinOp::Or => {
                        if Self::is_truthy(&l) {
                            return Ok(Value::Bool(true));
                        }
                        let r = self.eval_expr(right)?;
                        return Ok(Value::Bool(Self::is_truthy(&r)));
                    }
                    _ => {}
                }
                let r = self.eval_expr(right)?;
                self.eval_binary(l, op, r)
            }
            Expr::Unary(op, operand) => {
                let val = self.eval_expr(operand)?;
                self.eval_unary(op, val)
            }
            Expr::Call(callee, args) => {
                let func = self.eval_expr(callee)?;
                let mut arg_vals = Vec::new();
                for a in args {
                    arg_vals.push(self.eval_expr(a)?);
                }
                self.call_function(func, arg_vals)
            }
            Expr::If(cond, then_block, else_block) => {
                let c = self.eval_expr(cond)?;
                if Self::is_truthy(&c) {
                    self.exec_block(then_block)
                } else if let Some(eb) = else_block {
                    self.exec_block(eb)
                } else {
                    Ok(Value::Void)
                }
            }
            Expr::Array(items) => {
                let mut vals = Vec::new();
                for item in items {
                    vals.push(self.eval_expr(item)?);
                }
                Ok(Value::Array(vals))
            }
            Expr::Tuple(items) => {
                let mut vals = Vec::new();
                for item in items {
                    vals.push(self.eval_expr(item)?);
                }
                Ok(Value::Tuple(vals))
            }
            Expr::Index(arr_expr, idx_expr) => {
                let arr = self.eval_expr(arr_expr)?;
                let idx = self.eval_expr(idx_expr)?;
                match (&arr, &idx) {
                    (Value::Array(a), Value::Int(i)) => {
                        let i = *i as usize;
                        if i >= a.len() {
                            Err(self.runtime_err(format!(
                                "index {} out of bounds (len {})", i, a.len()
                            )))
                        } else {
                            Ok(a[i].clone())
                        }
                    }
                    (Value::Str(s), Value::Int(i)) => {
                        let i = *i as usize;
                        s.chars().nth(i).map(Value::Char).ok_or_else(|| {
                            self.runtime_err(format!("string index {} out of bounds", i))
                        })
                    }
                    (Value::Tuple(t), Value::Int(i)) => {
                        let i = *i as usize;
                        if i >= t.len() {
                            Err(self.runtime_err(format!(
                                "tuple index {} out of bounds (len {})", i, t.len()
                            )))
                        } else {
                            Ok(t[i].clone())
                        }
                    }
                    _ => Err(self.type_err("invalid index operation".into())),
                }
            }
            Expr::Field(obj, field_name) => {
                let _obj = self.eval_expr(obj)?;
                Err(self.runtime_err(format!("field access .{} not yet supported", field_name)))
            }
            Expr::Block(stmts) => self.exec_block(stmts),
            Expr::Range(start, end, inclusive) => {
                let s = self.eval_expr(start)?;
                let e = self.eval_expr(end)?;
                match (&s, &e) {
                    (Value::Int(a), Value::Int(b)) => {
                        let end_val = if *inclusive { *b + 1 } else { *b };
                        let items: Vec<Value> = (*a..end_val).map(Value::Int).collect();
                        Ok(Value::Array(items))
                    }
                    _ => Err(self.type_err("range requires integers".into())),
                }
            }
            Expr::Match(scrutinee, arms) => {
                let val = self.eval_expr(scrutinee)?;
                for arm in arms {
                    let pattern = self.eval_expr(&arm.pattern)?;
                    if Self::values_equal(&val, &pattern) {
                        return self.exec_block(&arm.body);
                    }
                }
                Ok(Value::Void)
            }
            Expr::Lambda(params, body) => {
                let param_names: Vec<String> = params.iter().map(|p| p.name.clone()).collect();
                Ok(Value::Fn(
                    "<lambda>".into(),
                    param_names,
                    vec![Stmt::Return(Some(body.as_ref().clone()))],
                ))
            }
        }
    }

    // ── Binary operations ───────────────────────────────────

    fn eval_binary(&mut self, left: Value, op: &BinOp, right: Value) -> Result<Value, HexaError> {
        // Coerce int to float if mixed
        let (left, right) = match (&left, &right) {
            (Value::Int(a), Value::Float(b)) => (Value::Float(*a as f64), Value::Float(*b)),
            (Value::Float(a), Value::Int(b)) => (Value::Float(*a), Value::Float(*b as f64)),
            _ => (left, right),
        };

        match (&left, op, &right) {
            // Int arithmetic
            (Value::Int(a), BinOp::Add, Value::Int(b)) => Ok(Value::Int(a + b)),
            (Value::Int(a), BinOp::Sub, Value::Int(b)) => Ok(Value::Int(a - b)),
            (Value::Int(a), BinOp::Mul, Value::Int(b)) => Ok(Value::Int(a * b)),
            (Value::Int(a), BinOp::Div, Value::Int(b)) => {
                if *b == 0 {
                    Err(self.runtime_err("division by zero".into()))
                } else {
                    Ok(Value::Int(a / b))
                }
            }
            (Value::Int(a), BinOp::Rem, Value::Int(b)) => {
                if *b == 0 {
                    Err(self.runtime_err("division by zero".into()))
                } else {
                    Ok(Value::Int(a % b))
                }
            }
            (Value::Int(a), BinOp::Pow, Value::Int(b)) => {
                Ok(Value::Int((*a as f64).powi(*b as i32) as i64))
            }

            // Float arithmetic
            (Value::Float(a), BinOp::Add, Value::Float(b)) => Ok(Value::Float(a + b)),
            (Value::Float(a), BinOp::Sub, Value::Float(b)) => Ok(Value::Float(a - b)),
            (Value::Float(a), BinOp::Mul, Value::Float(b)) => Ok(Value::Float(a * b)),
            (Value::Float(a), BinOp::Div, Value::Float(b)) => {
                if *b == 0.0 {
                    Err(self.runtime_err("division by zero".into()))
                } else {
                    Ok(Value::Float(a / b))
                }
            }
            (Value::Float(a), BinOp::Rem, Value::Float(b)) => Ok(Value::Float(a % b)),
            (Value::Float(a), BinOp::Pow, Value::Float(b)) => Ok(Value::Float(a.powf(*b))),

            // String concatenation
            (Value::Str(a), BinOp::Add, Value::Str(b)) => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }

            // Int comparisons
            (Value::Int(a), BinOp::Eq, Value::Int(b)) => Ok(Value::Bool(a == b)),
            (Value::Int(a), BinOp::Ne, Value::Int(b)) => Ok(Value::Bool(a != b)),
            (Value::Int(a), BinOp::Lt, Value::Int(b)) => Ok(Value::Bool(a < b)),
            (Value::Int(a), BinOp::Gt, Value::Int(b)) => Ok(Value::Bool(a > b)),
            (Value::Int(a), BinOp::Le, Value::Int(b)) => Ok(Value::Bool(a <= b)),
            (Value::Int(a), BinOp::Ge, Value::Int(b)) => Ok(Value::Bool(a >= b)),

            // Float comparisons
            (Value::Float(a), BinOp::Eq, Value::Float(b)) => Ok(Value::Bool(a == b)),
            (Value::Float(a), BinOp::Ne, Value::Float(b)) => Ok(Value::Bool(a != b)),
            (Value::Float(a), BinOp::Lt, Value::Float(b)) => Ok(Value::Bool(a < b)),
            (Value::Float(a), BinOp::Gt, Value::Float(b)) => Ok(Value::Bool(a > b)),
            (Value::Float(a), BinOp::Le, Value::Float(b)) => Ok(Value::Bool(a <= b)),
            (Value::Float(a), BinOp::Ge, Value::Float(b)) => Ok(Value::Bool(a >= b)),

            // Bool comparisons
            (Value::Bool(a), BinOp::Eq, Value::Bool(b)) => Ok(Value::Bool(a == b)),
            (Value::Bool(a), BinOp::Ne, Value::Bool(b)) => Ok(Value::Bool(a != b)),

            // String comparisons
            (Value::Str(a), BinOp::Eq, Value::Str(b)) => Ok(Value::Bool(a == b)),
            (Value::Str(a), BinOp::Ne, Value::Str(b)) => Ok(Value::Bool(a != b)),

            // Bitwise operations
            (Value::Int(a), BinOp::BitAnd, Value::Int(b)) => Ok(Value::Int(a & b)),
            (Value::Int(a), BinOp::BitOr, Value::Int(b)) => Ok(Value::Int(a | b)),
            (Value::Int(a), BinOp::BitXor, Value::Int(b)) => Ok(Value::Int(a ^ b)),

            // Logical XOR
            (Value::Bool(a), BinOp::Xor, Value::Bool(b)) => Ok(Value::Bool(a ^ b)),

            _ => Err(self.type_err(format!(
                "unsupported binary operation: {:?} {:?} {:?}",
                left, op, right
            ))),
        }
    }

    // ── Unary operations ────────────────────────────────────

    fn eval_unary(&mut self, op: &UnaryOp, val: Value) -> Result<Value, HexaError> {
        match (op, &val) {
            (UnaryOp::Neg, Value::Int(n)) => Ok(Value::Int(-n)),
            (UnaryOp::Neg, Value::Float(n)) => Ok(Value::Float(-n)),
            (UnaryOp::Not, Value::Bool(b)) => Ok(Value::Bool(!b)),
            (UnaryOp::BitNot, Value::Int(n)) => Ok(Value::Int(!n)),
            _ => Err(self.type_err(format!("unsupported unary op {:?} on {:?}", op, val))),
        }
    }

    // ── Function calls ──────────────────────────────────────

    fn call_function(&mut self, func: Value, args: Vec<Value>) -> Result<Value, HexaError> {
        match func {
            Value::BuiltinFn(name) => self.call_builtin(&name, args),
            Value::Fn(_name, params, body) => {
                if args.len() != params.len() {
                    return Err(self.runtime_err(format!(
                        "expected {} arguments, got {}",
                        params.len(),
                        args.len()
                    )));
                }
                self.env.push_scope();
                for (param, arg) in params.iter().zip(args) {
                    self.env.define(param, arg);
                }
                let mut result = Value::Void;
                for stmt in &body {
                    result = self.exec_stmt(stmt)?;
                    if self.return_value.is_some() {
                        result = self.return_value.take().unwrap();
                        break;
                    }
                }
                self.env.pop_scope();
                Ok(result)
            }
            _ => Err(self.type_err("not a callable value".into())),
        }
    }

    fn call_builtin(&mut self, name: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match name {
            "print" => {
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 {
                        print!(" ");
                    }
                    print!("{}", arg);
                }
                Ok(Value::Void)
            }
            "println" => {
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 {
                        print!(" ");
                    }
                    print!("{}", arg);
                }
                println!();
                Ok(Value::Void)
            }
            "len" => {
                if args.is_empty() {
                    return Err(self.type_err("len() requires 1 argument".into()));
                }
                match &args[0] {
                    Value::Str(s) => Ok(Value::Int(s.len() as i64)),
                    Value::Array(a) => Ok(Value::Int(a.len() as i64)),
                    _ => Err(self.type_err("len() requires string or array".into())),
                }
            }
            "type_of" => {
                if args.is_empty() {
                    return Err(self.type_err("type_of() requires 1 argument".into()));
                }
                Ok(Value::Str(
                    match &args[0] {
                        Value::Int(_) => "int",
                        Value::Float(_) => "float",
                        Value::Bool(_) => "bool",
                        Value::Char(_) => "char",
                        Value::Str(_) => "string",
                        Value::Byte(_) => "byte",
                        Value::Void => "void",
                        Value::Array(_) => "array",
                        Value::Tuple(_) => "tuple",
                        Value::Fn(..) => "fn",
                        Value::BuiltinFn(_) => "builtin",
                    }
                    .to_string(),
                ))
            }
            "sigma" => {
                if args.is_empty() {
                    return Err(self.type_err("sigma() requires 1 argument".into()));
                }
                if let Value::Int(n) = &args[0] {
                    let mut sum = 0i64;
                    for d in 1..=*n {
                        if n % d == 0 {
                            sum += d;
                        }
                    }
                    Ok(Value::Int(sum))
                } else {
                    Err(self.type_err("sigma() requires int".into()))
                }
            }
            "phi" => {
                if args.is_empty() {
                    return Err(self.type_err("phi() requires 1 argument".into()));
                }
                if let Value::Int(n) = &args[0] {
                    let mut count = 0i64;
                    for k in 1..=*n {
                        if gcd(*n, k) == 1 {
                            count += 1;
                        }
                    }
                    Ok(Value::Int(count))
                } else {
                    Err(self.type_err("phi() requires int".into()))
                }
            }
            "tau" => {
                if args.is_empty() {
                    return Err(self.type_err("tau() requires 1 argument".into()));
                }
                if let Value::Int(n) = &args[0] {
                    let mut count = 0i64;
                    for d in 1..=*n {
                        if n % d == 0 {
                            count += 1;
                        }
                    }
                    Ok(Value::Int(count))
                } else {
                    Err(self.type_err("tau() requires int".into()))
                }
            }
            "gcd" => {
                if args.len() < 2 {
                    return Err(self.type_err("gcd() requires 2 arguments".into()));
                }
                if let (Value::Int(a), Value::Int(b)) = (&args[0], &args[1]) {
                    Ok(Value::Int(gcd(*a, *b)))
                } else {
                    Err(self.type_err("gcd() requires two ints".into()))
                }
            }
            _ => Err(HexaError {
                class: ErrorClass::Name,
                message: format!("unknown builtin: {}", name),
                line: 0,
                col: 0,
            }),
        }
    }

    // ── Helpers ──────────────────────────────────────────────

    fn exec_block(&mut self, stmts: &[Stmt]) -> Result<Value, HexaError> {
        self.env.push_scope();
        let mut last = Value::Void;
        for stmt in stmts {
            last = self.exec_stmt(stmt)?;
            if self.return_value.is_some() {
                self.env.pop_scope();
                return Ok(Value::Void);
            }
        }
        self.env.pop_scope();
        Ok(last)
    }

    fn is_truthy(val: &Value) -> bool {
        match val {
            Value::Bool(b) => *b,
            Value::Int(n) => *n != 0,
            Value::Float(f) => *f != 0.0,
            Value::Str(s) => !s.is_empty(),
            Value::Void => false,
            _ => true,
        }
    }

    fn values_equal(a: &Value, b: &Value) -> bool {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => x == y,
            (Value::Float(x), Value::Float(y)) => x == y,
            (Value::Bool(x), Value::Bool(y)) => x == y,
            (Value::Str(x), Value::Str(y)) => x == y,
            (Value::Char(x), Value::Char(y)) => x == y,
            (Value::Void, Value::Void) => true,
            _ => false,
        }
    }

    fn runtime_err(&self, message: String) -> HexaError {
        HexaError {
            class: ErrorClass::Runtime,
            message,
            line: 0,
            col: 0,
        }
    }

    fn type_err(&self, message: String) -> HexaError {
        HexaError {
            class: ErrorClass::Type,
            message,
            line: 0,
            col: 0,
        }
    }
}

fn gcd(a: i64, b: i64) -> i64 {
    let (mut a, mut b) = (a.abs(), b.abs());
    while b != 0 {
        let t = b;
        b = a % b;
        a = t;
    }
    a
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn eval(src: &str) -> Value {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.run(&stmts).unwrap()
    }

    #[test]
    fn test_arithmetic() {
        assert!(matches!(eval("1 + 2 * 3"), Value::Int(7)));
    }

    #[test]
    fn test_let_binding() {
        assert!(matches!(eval("let x = 6\nlet y = 4\nx * y"), Value::Int(24)));
    }

    #[test]
    fn test_function_call() {
        let src = "fn double(x: int) -> int {\n  return x * 2\n}\ndouble(6)";
        assert!(matches!(eval(src), Value::Int(12)));
    }

    #[test]
    fn test_sigma_builtin() {
        assert!(matches!(eval("sigma(6)"), Value::Int(12)));
    }

    #[test]
    fn test_phi_builtin() {
        assert!(matches!(eval("phi(6)"), Value::Int(2)));
    }

    #[test]
    fn test_tau_builtin() {
        assert!(matches!(eval("tau(6)"), Value::Int(4)));
    }

    #[test]
    fn test_n6_uniqueness() {
        assert!(matches!(
            eval("sigma(6) * phi(6) == 6 * tau(6)"),
            Value::Bool(true)
        ));
    }

    #[test]
    fn test_for_loop() {
        let src = "let mut s = 0\nfor i in 1..7 {\n  s = s + i\n}\ns";
        assert!(matches!(eval(src), Value::Int(21)));
    }

    #[test]
    fn test_if_expression() {
        assert!(matches!(
            eval("if 6 > 5 { 12 } else { 0 }"),
            Value::Int(12)
        ));
    }

    #[test]
    fn test_string_concat() {
        assert!(
            matches!(eval("\"hello\" + \" \" + \"world\""), Value::Str(s) if s == "hello world")
        );
    }

    #[test]
    fn test_boolean_ops() {
        assert!(matches!(eval("true && false"), Value::Bool(false)));
        assert!(matches!(eval("true || false"), Value::Bool(true)));
    }

    #[test]
    fn test_comparison() {
        assert!(matches!(eval("6 == 6"), Value::Bool(true)));
        assert!(matches!(eval("6 != 7"), Value::Bool(true)));
        assert!(matches!(eval("12 > 4"), Value::Bool(true)));
    }

    #[test]
    fn test_while_loop() {
        let src = "let mut n = 0\nlet mut i = 1\nwhile i <= 6 {\n  n = n + i\n  i = i + 1\n}\nn";
        assert!(matches!(eval(src), Value::Int(21)));
    }

    #[test]
    fn test_array_literal() {
        assert!(matches!(eval("[1, 2, 3][0]"), Value::Int(1)));
    }

    #[test]
    fn test_unary_neg() {
        assert!(matches!(eval("-6"), Value::Int(-6)));
    }

    #[test]
    fn test_unary_not() {
        assert!(matches!(eval("!true"), Value::Bool(false)));
    }

    #[test]
    fn test_gcd_builtin() {
        assert!(matches!(eval("gcd(12, 8)"), Value::Int(4)));
    }

    #[test]
    fn test_len_builtin() {
        assert!(matches!(eval("len(\"hexa\")"), Value::Int(4)));
        assert!(matches!(eval("len([1, 2, 3])"), Value::Int(3)));
    }

    #[test]
    fn test_type_of_builtin() {
        assert!(matches!(eval("type_of(42)"), Value::Str(s) if s == "int"));
    }

    #[test]
    fn test_assert_pass() {
        // Should not error
        eval("assert 6 == 6");
    }

    #[test]
    fn test_assert_fail() {
        let tokens = Lexer::new("assert 6 == 7").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err());
    }

    #[test]
    fn test_recursive_function() {
        let src = "fn fact(n: int) -> int {\n  if n <= 1 { return 1 }\n  return n * fact(n - 1)\n}\nfact(6)";
        assert!(matches!(eval(src), Value::Int(720)));
    }
}
