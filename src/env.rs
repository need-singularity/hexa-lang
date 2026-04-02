#![allow(dead_code)]

use std::collections::HashMap;
use std::sync::{Arc, Mutex, mpsc};

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
    EnumVariant(String, String, Option<Box<Value>>),  // enum_name, variant_name, data
    Sender(Arc<Mutex<mpsc::Sender<Value>>>),    // channel sender
    Receiver(Arc<Mutex<mpsc::Receiver<Value>>>), // channel receiver
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
            Value::EnumVariant(enum_name, variant, data) => {
                match data {
                    Some(d) => write!(f, "{}::{}({})", enum_name, variant, d),
                    None => write!(f, "{}::{}", enum_name, variant),
                }
            }
            Value::Sender(_) => write!(f, "<sender>"),
            Value::Receiver(_) => write!(f, "<receiver>"),
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
        env.define("to_float", Value::BuiltinFn("to_float".into()));
        // Math builtins
        env.define("abs", Value::BuiltinFn("abs".into()));
        env.define("min", Value::BuiltinFn("min".into()));
        env.define("max", Value::BuiltinFn("max".into()));
        env.define("floor", Value::BuiltinFn("floor".into()));
        env.define("ceil", Value::BuiltinFn("ceil".into()));
        env.define("round", Value::BuiltinFn("round".into()));
        env.define("sqrt", Value::BuiltinFn("sqrt".into()));
        env.define("pow", Value::BuiltinFn("pow".into()));
        env.define("log", Value::BuiltinFn("log".into()));
        env.define("log2", Value::BuiltinFn("log2".into()));
        env.define("sin", Value::BuiltinFn("sin".into()));
        env.define("cos", Value::BuiltinFn("cos".into()));
        env.define("tan", Value::BuiltinFn("tan".into()));
        // Math constants
        env.define("PI", Value::Float(std::f64::consts::PI));
        env.define("E", Value::Float(std::f64::consts::E));
        // Format builtins
        env.define("format", Value::BuiltinFn("format".into()));
        // OS builtins
        env.define("args", Value::BuiltinFn("args".into()));
        env.define("env_var", Value::BuiltinFn("env_var".into()));
        env.define("exit", Value::BuiltinFn("exit".into()));
        env.define("clock", Value::BuiltinFn("clock".into()));
        // Built-in Option/Result constructors
        env.define("Some", Value::BuiltinFn("Some".into()));
        env.define("None", Value::EnumVariant("Option".into(), "None".into(), None));
        env.define("Ok", Value::BuiltinFn("Ok".into()));
        env.define("Err", Value::BuiltinFn("Err".into()));
        // Concurrency builtins
        env.define("channel", Value::BuiltinFn("channel".into()));
        // Random builtins
        env.define("random", Value::BuiltinFn("random".into()));
        env.define("random_int", Value::BuiltinFn("random_int".into()));
        // System builtins
        env.define("sleep", Value::BuiltinFn("sleep".into()));
        env.define("print_err", Value::BuiltinFn("print_err".into()));
        // JSON builtins
        env.define("json_parse", Value::BuiltinFn("json_parse".into()));
        env.define("json_stringify", Value::BuiltinFn("json_stringify".into()));
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
