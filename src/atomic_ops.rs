//! Atomic operations for HEXA-LANG.
//!
//! Provides the `atomic` keyword as a language builtin, supporting:
//! - `atomic_load(ref)` — atomic read
//! - `atomic_store(ref, val)` — atomic write
//! - `atomic_cas(ref, expected, desired)` — compare-and-swap
//! - `atomic_add(ref, val)` — atomic fetch-and-add
//! - `atomic_sub(ref, val)` — atomic fetch-and-subtract
//!
//! Atomic values are represented as `Value::Atomic(Arc<AtomicValue>)`.
//! The interpreter exposes these as builtin functions.

#![allow(dead_code)]

use std::sync::atomic::{AtomicI64, AtomicBool, Ordering};
use std::sync::Arc;

use crate::env::Value;

// ── Atomic value container ─────────────────────────────────────

/// An atomic value that can be shared across green threads.
#[derive(Debug)]
pub enum AtomicValue {
    Int(AtomicI64),
    Bool(AtomicBool),
}

impl AtomicValue {
    /// Create a new atomic integer.
    pub fn new_int(val: i64) -> Arc<Self> {
        Arc::new(AtomicValue::Int(AtomicI64::new(val)))
    }

    /// Create a new atomic boolean.
    pub fn new_bool(val: bool) -> Arc<Self> {
        Arc::new(AtomicValue::Bool(AtomicBool::new(val)))
    }

    /// Atomic load.
    pub fn load(&self) -> Value {
        match self {
            AtomicValue::Int(a) => Value::Int(a.load(Ordering::SeqCst)),
            AtomicValue::Bool(a) => Value::Bool(a.load(Ordering::SeqCst)),
        }
    }

    /// Atomic store.
    pub fn store(&self, val: &Value) -> Result<(), String> {
        match (self, val) {
            (AtomicValue::Int(a), Value::Int(v)) => {
                a.store(*v, Ordering::SeqCst);
                Ok(())
            }
            (AtomicValue::Bool(a), Value::Bool(v)) => {
                a.store(*v, Ordering::SeqCst);
                Ok(())
            }
            _ => Err("atomic_store: type mismatch".into()),
        }
    }

    /// Atomic compare-and-swap. Returns (success, old_value).
    pub fn compare_and_swap(&self, expected: &Value, desired: &Value) -> Result<(bool, Value), String> {
        match (self, expected, desired) {
            (AtomicValue::Int(a), Value::Int(exp), Value::Int(des)) => {
                match a.compare_exchange(*exp, *des, Ordering::SeqCst, Ordering::SeqCst) {
                    Ok(old) => Ok((true, Value::Int(old))),
                    Err(old) => Ok((false, Value::Int(old))),
                }
            }
            (AtomicValue::Bool(a), Value::Bool(exp), Value::Bool(des)) => {
                match a.compare_exchange(*exp, *des, Ordering::SeqCst, Ordering::SeqCst) {
                    Ok(old) => Ok((true, Value::Bool(old))),
                    Err(old) => Ok((false, Value::Bool(old))),
                }
            }
            _ => Err("atomic_cas: type mismatch".into()),
        }
    }

    /// Atomic fetch-and-add. Returns previous value.
    pub fn fetch_add(&self, val: &Value) -> Result<Value, String> {
        match (self, val) {
            (AtomicValue::Int(a), Value::Int(v)) => {
                let old = a.fetch_add(*v, Ordering::SeqCst);
                Ok(Value::Int(old))
            }
            _ => Err("atomic_add: only supported for integers".into()),
        }
    }

    /// Atomic fetch-and-subtract. Returns previous value.
    pub fn fetch_sub(&self, val: &Value) -> Result<Value, String> {
        match (self, val) {
            (AtomicValue::Int(a), Value::Int(v)) => {
                let old = a.fetch_sub(*v, Ordering::SeqCst);
                Ok(Value::Int(old))
            }
            _ => Err("atomic_sub: only supported for integers".into()),
        }
    }
}

impl Clone for AtomicValue {
    fn clone(&self) -> Self {
        match self {
            AtomicValue::Int(a) => AtomicValue::Int(AtomicI64::new(a.load(Ordering::SeqCst))),
            AtomicValue::Bool(a) => AtomicValue::Bool(AtomicBool::new(a.load(Ordering::SeqCst))),
        }
    }
}

// ── Builtin function dispatch ──────────────────────────────────

/// Execute an atomic builtin function.
/// Called from the interpreter when it encounters `atomic_*` calls.
pub fn exec_atomic_builtin(name: &str, args: &[Value]) -> Result<Value, String> {
    match name {
        "atomic_new" => {
            // atomic_new(initial_value) -> Atomic
            if args.len() != 1 {
                return Err("atomic_new: expected 1 argument".into());
            }
            match &args[0] {
                Value::Int(n) => {
                    let av = AtomicValue::new_int(*n);
                    Ok(Value::Atomic(av))
                }
                Value::Bool(b) => {
                    let av = AtomicValue::new_bool(*b);
                    Ok(Value::Atomic(av))
                }
                _ => Err("atomic_new: only int and bool supported".into()),
            }
        }
        "atomic_load" => {
            if args.len() != 1 {
                return Err("atomic_load: expected 1 argument".into());
            }
            match &args[0] {
                Value::Atomic(av) => Ok(av.load()),
                _ => Err("atomic_load: argument must be an atomic value".into()),
            }
        }
        "atomic_store" => {
            if args.len() != 2 {
                return Err("atomic_store: expected 2 arguments (atomic, value)".into());
            }
            match &args[0] {
                Value::Atomic(av) => {
                    av.store(&args[1])?;
                    Ok(Value::Void)
                }
                _ => Err("atomic_store: first argument must be an atomic value".into()),
            }
        }
        "atomic_cas" => {
            // atomic_cas(atomic, expected, desired) -> [success, old_value]
            if args.len() != 3 {
                return Err("atomic_cas: expected 3 arguments".into());
            }
            match &args[0] {
                Value::Atomic(av) => {
                    let (success, old) = av.compare_and_swap(&args[1], &args[2])?;
                    Ok(Value::Array(vec![Value::Bool(success), old]))
                }
                _ => Err("atomic_cas: first argument must be an atomic value".into()),
            }
        }
        "atomic_add" => {
            if args.len() != 2 {
                return Err("atomic_add: expected 2 arguments".into());
            }
            match &args[0] {
                Value::Atomic(av) => av.fetch_add(&args[1]),
                _ => Err("atomic_add: first argument must be an atomic value".into()),
            }
        }
        "atomic_sub" => {
            if args.len() != 2 {
                return Err("atomic_sub: expected 2 arguments".into());
            }
            match &args[0] {
                Value::Atomic(av) => av.fetch_sub(&args[1]),
                _ => Err("atomic_sub: first argument must be an atomic value".into()),
            }
        }
        _ => Err(format!("unknown atomic operation: {}", name)),
    }
}

/// Check if a function name is an atomic builtin.
pub fn is_atomic_builtin(name: &str) -> bool {
    matches!(name,
        "atomic_new" | "atomic_load" | "atomic_store"
        | "atomic_cas" | "atomic_add" | "atomic_sub"
    )
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper to assert a Value matches expected string representation.
    fn assert_val(v: Value, expected: &str) {
        assert_eq!(v.to_string(), expected);
    }

    #[test]
    fn test_atomic_int_new_and_load() {
        let av = AtomicValue::new_int(42);
        assert_val(av.load(), "42");
    }

    #[test]
    fn test_atomic_bool_new_and_load() {
        let av = AtomicValue::new_bool(true);
        assert_val(av.load(), "true");
    }

    #[test]
    fn test_atomic_store() {
        let av = AtomicValue::new_int(0);
        av.store(&Value::Int(99)).unwrap();
        assert_val(av.load(), "99");
    }

    #[test]
    fn test_atomic_store_type_mismatch() {
        let av = AtomicValue::new_int(0);
        assert!(av.store(&Value::Bool(true)).is_err());
    }

    #[test]
    fn test_atomic_cas_success() {
        let av = AtomicValue::new_int(10);
        let (success, old) = av.compare_and_swap(&Value::Int(10), &Value::Int(20)).unwrap();
        assert!(success);
        assert_val(old, "10");
        assert_val(av.load(), "20");
    }

    #[test]
    fn test_atomic_cas_failure() {
        let av = AtomicValue::new_int(10);
        let (success, old) = av.compare_and_swap(&Value::Int(99), &Value::Int(20)).unwrap();
        assert!(!success);
        assert_val(old, "10");
        assert_val(av.load(), "10"); // unchanged
    }

    #[test]
    fn test_atomic_fetch_add() {
        let av = AtomicValue::new_int(5);
        let old = av.fetch_add(&Value::Int(3)).unwrap();
        assert_val(old, "5");
        assert_val(av.load(), "8");
    }

    #[test]
    fn test_atomic_fetch_sub() {
        let av = AtomicValue::new_int(10);
        let old = av.fetch_sub(&Value::Int(4)).unwrap();
        assert_val(old, "10");
        assert_val(av.load(), "6");
    }

    #[test]
    fn test_builtin_atomic_new() {
        let result = exec_atomic_builtin("atomic_new", &[Value::Int(7)]);
        assert!(result.is_ok());
        if let Value::Atomic(av) = result.unwrap() {
            assert_val(av.load(), "7");
        } else {
            panic!("expected Atomic value");
        }
    }

    #[test]
    fn test_builtin_atomic_load() {
        let av = AtomicValue::new_int(55);
        let result = exec_atomic_builtin("atomic_load", &[Value::Atomic(av)]);
        assert_val(result.unwrap(), "55");
    }

    #[test]
    fn test_builtin_atomic_store() {
        let av = AtomicValue::new_int(0);
        let atomic_val = Value::Atomic(av.clone());
        let result = exec_atomic_builtin("atomic_store", &[atomic_val, Value::Int(42)]);
        assert!(result.is_ok());
        assert_val(av.load(), "42");
    }

    #[test]
    fn test_builtin_atomic_cas() {
        let av = AtomicValue::new_int(10);
        let atomic_val = Value::Atomic(av.clone());
        let result = exec_atomic_builtin("atomic_cas", &[atomic_val, Value::Int(10), Value::Int(20)]);
        if let Ok(Value::Array(arr)) = result {
            assert!(matches!(&arr[0], Value::Bool(true)));
            assert_val(arr[1].clone(), "10");
        } else {
            panic!("expected array result");
        }
    }

    #[test]
    fn test_builtin_wrong_args() {
        assert!(exec_atomic_builtin("atomic_new", &[]).is_err());
        assert!(exec_atomic_builtin("atomic_load", &[Value::Int(1)]).is_err());
        assert!(exec_atomic_builtin("atomic_store", &[Value::Int(1)]).is_err());
    }

    #[test]
    fn test_is_atomic_builtin() {
        assert!(is_atomic_builtin("atomic_new"));
        assert!(is_atomic_builtin("atomic_load"));
        assert!(is_atomic_builtin("atomic_store"));
        assert!(is_atomic_builtin("atomic_cas"));
        assert!(is_atomic_builtin("atomic_add"));
        assert!(is_atomic_builtin("atomic_sub"));
        assert!(!is_atomic_builtin("println"));
        assert!(!is_atomic_builtin("atomic_unknown"));
    }

    #[test]
    fn test_atomic_clone() {
        let av = AtomicValue::new_int(42);
        let cloned = av.as_ref().clone();
        assert_val(cloned.load(), "42");
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_atomic_concurrent_access() {
        use std::thread;

        let av = AtomicValue::new_int(0);
        let av_arc = Arc::new(av);

        let mut handles = vec![];
        for _ in 0..10 {
            let av_clone = Arc::clone(&av_arc);
            handles.push(thread::spawn(move || {
                for _ in 0..100 {
                    av_clone.fetch_add(&Value::Int(1)).unwrap();
                }
            }));
        }

        for h in handles {
            h.join().unwrap();
        }

        assert_val(av_arc.load(), "1000");
    }
}
