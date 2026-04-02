//! std::testing — Testing builtins for HEXA-LANG.
//!
//! Provides:
//!   assert_eq(a, b)          — assert two values are equal
//!   assert_ne(a, b)          — assert two values are not equal
//!   assert_true(v)           — assert value is truthy
//!   assert_false(v)          — assert value is falsy
//!   test_run(name, fn)       — run a test function, return result map
//!   test_suite(tests)        — run array of [name, fn] pairs, return summary
//!   bench_fn(name, fn, n)    — benchmark a function N times, return timing map

#![allow(dead_code)]

use std::collections::HashMap;
use std::time::Instant;

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch a testing builtin by name.
pub fn call_testing_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "assert_eq" => builtin_assert_eq(interp, args),
        "assert_ne" => builtin_assert_ne(interp, args),
        "assert_true" => builtin_assert_true(interp, args),
        "assert_false" => builtin_assert_false(interp, args),
        "test_run" => builtin_test_run(interp, args),
        "test_suite" => builtin_test_suite(interp, args),
        "bench_fn" => builtin_bench_fn(interp, args),
        _ => Err(test_err(interp, format!("unknown testing builtin: {}", name))),
    }
}

fn test_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

fn type_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Type,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

fn values_equal(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Int(a), Value::Int(b)) => a == b,
        (Value::Float(a), Value::Float(b)) => (a - b).abs() < 1e-10,
        (Value::Int(a), Value::Float(b)) | (Value::Float(b), Value::Int(a)) => (*a as f64 - b).abs() < 1e-10,
        (Value::Bool(a), Value::Bool(b)) => a == b,
        (Value::Str(a), Value::Str(b)) => a == b,
        (Value::Char(a), Value::Char(b)) => a == b,
        (Value::Void, Value::Void) => true,
        (Value::Array(a), Value::Array(b)) => {
            a.len() == b.len() && a.iter().zip(b.iter()).all(|(x, y)| values_equal(x, y))
        }
        _ => false,
    }
}

fn is_truthy(v: &Value) -> bool {
    match v {
        Value::Bool(b) => *b,
        Value::Int(n) => *n != 0,
        Value::Float(f) => *f != 0.0,
        Value::Str(s) => !s.is_empty(),
        Value::Void => false,
        Value::Array(a) => !a.is_empty(),
        _ => true,
    }
}

// ── assert_eq(a, b) ──

fn builtin_assert_eq(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(type_err(interp, "assert_eq() requires 2 arguments".into()));
    }
    if values_equal(&args[0], &args[1]) {
        Ok(Value::Bool(true))
    } else {
        Err(test_err(interp, format!(
            "assert_eq failed: {} != {}", args[0], args[1]
        )))
    }
}

// ── assert_ne(a, b) ──

fn builtin_assert_ne(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(type_err(interp, "assert_ne() requires 2 arguments".into()));
    }
    if !values_equal(&args[0], &args[1]) {
        Ok(Value::Bool(true))
    } else {
        Err(test_err(interp, format!(
            "assert_ne failed: {} == {}", args[0], args[1]
        )))
    }
}

// ── assert_true(v) ──

fn builtin_assert_true(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    match args.first() {
        Some(v) if is_truthy(v) => Ok(Value::Bool(true)),
        Some(v) => Err(test_err(interp, format!("assert_true failed: {} is falsy", v))),
        None => Err(type_err(interp, "assert_true() requires 1 argument".into())),
    }
}

// ── assert_false(v) ──

fn builtin_assert_false(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    match args.first() {
        Some(v) if !is_truthy(v) => Ok(Value::Bool(true)),
        Some(v) => Err(test_err(interp, format!("assert_false failed: {} is truthy", v))),
        None => Err(type_err(interp, "assert_false() requires 1 argument".into())),
    }
}

// ── test_run(name: string, fn) -> map ──

fn builtin_test_run(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let name = match args.first() {
        Some(Value::Str(s)) => s.clone(),
        _ => return Err(type_err(interp, "test_run() requires (name, fn)".into())),
    };
    let func = match args.get(1) {
        Some(v @ (Value::Fn(..) | Value::Lambda(..) | Value::BuiltinFn(_))) => v.clone(),
        _ => return Err(type_err(interp, "test_run() second argument must be a function".into())),
    };

    let start = Instant::now();
    let result = interp.call_function(func, vec![]);
    let elapsed_ms = start.elapsed().as_micros() as f64 / 1000.0;

    let mut map = HashMap::new();
    map.insert("name".into(), Value::Str(name.clone()));
    map.insert("elapsed_ms".into(), Value::Float(elapsed_ms));

    match result {
        Ok(_) => {
            map.insert("passed".into(), Value::Bool(true));
            map.insert("error".into(), Value::Void);
        }
        Err(e) => {
            map.insert("passed".into(), Value::Bool(false));
            map.insert("error".into(), Value::Str(e.message));
        }
    }
    Ok(Value::Map(map))
}

// ── test_suite(tests: array) -> map ──

fn builtin_test_suite(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let tests = match args.first() {
        Some(Value::Array(a)) => a.clone(),
        _ => return Err(type_err(interp, "test_suite() requires array of [name, fn]".into())),
    };

    let mut results = Vec::new();
    let mut passed = 0i64;
    let mut failed = 0i64;
    let total_start = Instant::now();

    for test in &tests {
        match test {
            Value::Array(pair) if pair.len() >= 2 => {
                let name = match &pair[0] { Value::Str(s) => s.clone(), _ => "unnamed".into() };
                let func = pair[1].clone();

                let start = Instant::now();
                let result = interp.call_function(func, vec![]);
                let elapsed_ms = start.elapsed().as_micros() as f64 / 1000.0;

                let mut map = HashMap::new();
                map.insert("name".into(), Value::Str(name));
                map.insert("elapsed_ms".into(), Value::Float(elapsed_ms));
                match result {
                    Ok(_) => {
                        map.insert("passed".into(), Value::Bool(true));
                        passed += 1;
                    }
                    Err(e) => {
                        map.insert("passed".into(), Value::Bool(false));
                        map.insert("error".into(), Value::Str(e.message));
                        failed += 1;
                    }
                }
                results.push(Value::Map(map));
            }
            _ => {}
        }
    }

    let total_ms = total_start.elapsed().as_micros() as f64 / 1000.0;

    let mut summary = HashMap::new();
    summary.insert("total".into(), Value::Int(passed + failed));
    summary.insert("passed".into(), Value::Int(passed));
    summary.insert("failed".into(), Value::Int(failed));
    summary.insert("elapsed_ms".into(), Value::Float(total_ms));
    summary.insert("results".into(), Value::Array(results));
    Ok(Value::Map(summary))
}

// ── bench_fn(name: string, fn, iterations: int) -> map ──

fn builtin_bench_fn(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let name = match args.first() {
        Some(Value::Str(s)) => s.clone(),
        _ => return Err(type_err(interp, "bench_fn() requires (name, fn, iterations)".into())),
    };
    let func = match args.get(1) {
        Some(v @ (Value::Fn(..) | Value::Lambda(..) | Value::BuiltinFn(_))) => v.clone(),
        _ => return Err(type_err(interp, "bench_fn() second argument must be a function".into())),
    };
    let iterations = match args.get(2) {
        Some(Value::Int(n)) => *n as u64,
        _ => 1000, // default
    };

    let start = Instant::now();
    for _ in 0..iterations {
        let _ = interp.call_function(func.clone(), vec![]);
    }
    let total_us = start.elapsed().as_micros() as f64;
    let avg_us = total_us / iterations as f64;

    let mut map = HashMap::new();
    map.insert("name".into(), Value::Str(name));
    map.insert("iterations".into(), Value::Int(iterations as i64));
    map.insert("total_us".into(), Value::Float(total_us));
    map.insert("avg_us".into(), Value::Float(avg_us));
    map.insert("ops_per_sec".into(), Value::Float(if avg_us > 0.0 { 1_000_000.0 / avg_us } else { 0.0 }));
    Ok(Value::Map(map))
}

// ── Tests ──

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_values_equal() {
        assert!(values_equal(&Value::Int(42), &Value::Int(42)));
        assert!(!values_equal(&Value::Int(1), &Value::Int(2)));
        assert!(values_equal(&Value::Str("hello".into()), &Value::Str("hello".into())));
        assert!(values_equal(&Value::Float(3.14), &Value::Float(3.14)));
        assert!(values_equal(&Value::Bool(true), &Value::Bool(true)));
        assert!(values_equal(&Value::Void, &Value::Void));
    }

    #[test]
    fn test_values_equal_arrays() {
        let a = Value::Array(vec![Value::Int(1), Value::Int(2)]);
        let b = Value::Array(vec![Value::Int(1), Value::Int(2)]);
        let c = Value::Array(vec![Value::Int(1), Value::Int(3)]);
        assert!(values_equal(&a, &b));
        assert!(!values_equal(&a, &c));
    }

    #[test]
    fn test_is_truthy() {
        assert!(is_truthy(&Value::Bool(true)));
        assert!(!is_truthy(&Value::Bool(false)));
        assert!(is_truthy(&Value::Int(1)));
        assert!(!is_truthy(&Value::Int(0)));
        assert!(is_truthy(&Value::Str("hi".into())));
        assert!(!is_truthy(&Value::Str("".into())));
        assert!(!is_truthy(&Value::Void));
    }

    #[test]
    fn test_int_float_equality() {
        assert!(values_equal(&Value::Int(3), &Value::Float(3.0)));
        assert!(!values_equal(&Value::Int(3), &Value::Float(3.1)));
    }
}
