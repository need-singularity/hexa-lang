#![allow(dead_code)]

use std::collections::HashMap;

#[derive(Debug, Clone)]
pub enum Value {
    Int(i64),
    Float(f64),
    Bool(bool),
    Char(char),
    Str(String),
    Byte(u8),
    Void,
    Array(Vec<Value>),
    Tuple(Vec<Value>),
    Fn(String, Vec<String>, Vec<crate::ast::Stmt>), // name, param_names, body
    BuiltinFn(String),  // name of builtin
    Struct(String, HashMap<String, Value>),  // name, fields
    Lambda(Vec<String>, Vec<crate::ast::Stmt>, Vec<(String, Value)>), // params, body, captured env
    Map(HashMap<String, Value>),  // key-value map
    Error(String),  // error value for try/catch
}

impl std::fmt::Display for Value {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Value::Int(n) => write!(f, "{}", n),
            Value::Float(n) => write!(f, "{}", n),
            Value::Bool(b) => write!(f, "{}", b),
            Value::Char(c) => write!(f, "{}", c),
            Value::Str(s) => write!(f, "{}", s),
            Value::Byte(b) => write!(f, "{}", b),
            Value::Void => write!(f, "void"),
            Value::Array(a) => write!(f, "[{}]", a.iter().map(|v| format!("{}", v)).collect::<Vec<_>>().join(", ")),
            Value::Tuple(t) => write!(f, "({})", t.iter().map(|v| format!("{}", v)).collect::<Vec<_>>().join(", ")),
            Value::Fn(name, ..) => write!(f, "<fn {}>", name),
            Value::BuiltinFn(name) => write!(f, "<builtin {}>", name),
            Value::Struct(name, fields) => {
                write!(f, "{} {{ ", name)?;
                for (i, (k, v)) in fields.iter().enumerate() {
                    if i > 0 { write!(f, ", ")?; }
                    write!(f, "{}: {}", k, v)?;
                }
                write!(f, " }}")
            }
            Value::Lambda(..) => write!(f, "<lambda>"),
            Value::Map(map) => {
                write!(f, "{{")?;
                for (i, (k, v)) in map.iter().enumerate() {
                    if i > 0 { write!(f, ", ")?; }
                    write!(f, "{}: {}", k, v)?;
                }
                write!(f, "}}")
            }
            Value::Error(msg) => write!(f, "Error({})", msg),
        }
    }
}

pub struct Env {
    pub scopes: Vec<HashMap<String, Value>>,
}

impl Env {
    pub fn new() -> Self {
        let mut env = Self { scopes: vec![HashMap::new()] };
        // Register builtins
        env.define("print", Value::BuiltinFn("print".into()));
        env.define("println", Value::BuiltinFn("println".into()));
        env.define("len", Value::BuiltinFn("len".into()));
        env.define("type_of", Value::BuiltinFn("type_of".into()));
        env.define("sigma", Value::BuiltinFn("sigma".into()));
        env.define("phi", Value::BuiltinFn("phi".into()));
        env.define("tau", Value::BuiltinFn("tau".into()));
        env.define("gcd", Value::BuiltinFn("gcd".into()));
        env.define("read_file", Value::BuiltinFn("read_file".into()));
        env.define("write_file", Value::BuiltinFn("write_file".into()));
        env.define("file_exists", Value::BuiltinFn("file_exists".into()));
        env.define("keys", Value::BuiltinFn("keys".into()));
        env.define("values", Value::BuiltinFn("values".into()));
        env.define("has_key", Value::BuiltinFn("has_key".into()));
        env.define("to_string", Value::BuiltinFn("to_string".into()));
        env.define("to_int", Value::BuiltinFn("to_int".into()));
        env
    }

    pub fn push_scope(&mut self) { self.scopes.push(HashMap::new()); }
    pub fn pop_scope(&mut self) { self.scopes.pop(); }

    pub fn define(&mut self, name: &str, val: Value) {
        self.scopes.last_mut().unwrap().insert(name.to_string(), val);
    }

    pub fn get(&self, name: &str) -> Option<Value> {
        for scope in self.scopes.iter().rev() {
            if let Some(val) = scope.get(name) {
                return Some(val.clone());
            }
        }
        None
    }

    pub fn set(&mut self, name: &str, val: Value) -> bool {
        for scope in self.scopes.iter_mut().rev() {
            if scope.contains_key(name) {
                scope.insert(name.to_string(), val);
                return true;
            }
        }
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::Visibility;

    #[test]
    fn test_visibility_levels_is_tau() {
        // τ=4 visibility levels
        let vis = [Visibility::Private, Visibility::Module, Visibility::Crate, Visibility::Public];
        assert_eq!(vis.len(), 4);
    }

    #[test]
    fn test_scope_push_pop() {
        let mut env = Env::new();
        env.define("x", Value::Int(42));
        assert_eq!(env.get("x").unwrap().to_string(), "42");
        env.push_scope();
        env.define("x", Value::Int(100));
        assert_eq!(env.get("x").unwrap().to_string(), "100");
        env.pop_scope();
        assert_eq!(env.get("x").unwrap().to_string(), "42");
    }
}
