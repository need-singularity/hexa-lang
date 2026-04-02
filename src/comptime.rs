//! Compile-time evaluation engine for `comptime { ... }` blocks and `comptime fn`.
//!
//! Creates a sandboxed interpreter that forbids I/O, concurrency, and other
//! side-effectful operations.  Only pure computation is allowed: arithmetic,
//! string ops, arrays, maps, control flow, and user-defined pure functions.

use std::collections::HashMap;
use crate::ast::*;
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Names of built-in functions that are forbidden inside comptime contexts.
const FORBIDDEN_BUILTINS: &[&str] = &[
    "print", "println", "read_file", "write_file", "input",
    "sleep", "channel", "spawn", "exit",
    // std::fs
    "fs_read", "fs_write", "fs_append", "fs_exists", "fs_remove",
    "fs_mkdir", "fs_list", "fs_copy", "fs_move", "fs_metadata",
    "fs_glob", "fs_watch",
    // std::io
    "io_stdin", "io_read_lines", "io_write_bytes", "io_pipe",
    "io_tempfile", "io_buffered_reader", "io_reader_next",
    // std::net
    "net_listen", "net_accept", "net_connect", "net_read", "net_write", "net_close",
    "http_get", "http_serve",
    // std::time (sleep has side effects)
    "time_sleep",
    // std::log (I/O side effects)
    "log_debug", "log_info", "log_warn", "log_error",
    "log_set_level", "log_get_entries",
    // std::crypto (random_bytes uses time seed)
    "random_bytes",
];

/// Evaluate a comptime expression in a sandboxed interpreter.
///
/// `comptime_fns` are compile-time function definitions visible to the block.
/// Returns the resulting `Value`, which must be a comptime-compatible type.
pub fn eval_comptime(
    expr: &Expr,
    comptime_fns: &HashMap<String, (Vec<String>, Vec<Stmt>)>,
) -> Result<Value, HexaError> {
    let mut interp = Interpreter::new();
    interp.set_comptime_mode(true);

    // Register comptime functions into the interpreter's environment
    for (name, (params, body)) in comptime_fns {
        interp.env.define(
            name,
            Value::Fn(name.clone(), params.clone(), body.clone()),
        );
    }

    let result = interp.eval_expr_pub(expr)?;

    // Validate the result is comptime-compatible
    validate_comptime_value(&result)?;

    Ok(result)
}

/// Check that a value can be represented as a compile-time constant.
fn validate_comptime_value(val: &Value) -> Result<(), HexaError> {
    match val {
        Value::Int(_)
        | Value::Float(_)
        | Value::Bool(_)
        | Value::Char(_)
        | Value::Str(_)
        | Value::Void => Ok(()),
        Value::Array(items) => {
            for item in items {
                validate_comptime_value(item)?;
            }
            Ok(())
        }
        Value::Tuple(items) => {
            for item in items {
                validate_comptime_value(item)?;
            }
            Ok(())
        }
        Value::Map(map) => {
            for v in map.values() {
                validate_comptime_value(v)?;
            }
            Ok(())
        }
        _ => Err(HexaError {
            class: ErrorClass::Type,
            message: format!(
                "comptime block returned non-constant value: {}",
                val
            ),
            line: 0,
            col: 0,
            hint: Some("comptime blocks can only return int, float, bool, char, string, array, tuple, or map".into()),
        }),
    }
}

/// Check if a builtin name is forbidden in comptime mode.
pub fn is_forbidden_builtin(name: &str) -> bool {
    FORBIDDEN_BUILTINS.contains(&name)
}
