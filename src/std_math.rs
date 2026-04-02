//! std::math — Math builtins for HEXA-LANG.
//!
//! Provides:
//!   math_pi()              — π constant
//!   math_e()               — e constant
//!   math_phi()             — golden ratio φ
//!   math_abs(x)            — absolute value
//!   math_sqrt(x)           — square root
//!   math_pow(x, n)         — x^n
//!   math_log(x)            — natural log
//!   math_sin(x)            — sine
//!   math_cos(x)            — cosine
//!   math_floor(x)          — floor
//!   math_ceil(x)           — ceiling
//!   math_round(x)          — round to nearest
//!   math_min(a, b)         — minimum
//!   math_max(a, b)         — maximum
//!   math_gcd(a, b)         — greatest common divisor
//!   matrix_new(rows, cols) — create zero matrix
//!   matrix_set(m, r, c, v) — set element
//!   matrix_get(m, r, c)    — get element
//!   matrix_mul(a, b)       — matrix multiplication
//!   matrix_det(m)          — determinant (up to 4x4)

#![allow(dead_code)]

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch a math builtin by name.
pub fn call_math_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "math_pi" => Ok(Value::Float(std::f64::consts::PI)),
        "math_e" => Ok(Value::Float(std::f64::consts::E)),
        "math_phi" => Ok(Value::Float(1.618033988749895)),
        "math_abs" => builtin_math_abs(interp, args),
        "math_sqrt" => builtin_math_sqrt(interp, args),
        "math_pow" => builtin_math_pow(interp, args),
        "math_log" => builtin_math_log(interp, args),
        "math_sin" => builtin_math_sin(interp, args),
        "math_cos" => builtin_math_cos(interp, args),
        "math_floor" => builtin_math_floor(interp, args),
        "math_ceil" => builtin_math_ceil(interp, args),
        "math_round" => builtin_math_round(interp, args),
        "math_min" => builtin_math_min(interp, args),
        "math_max" => builtin_math_max(interp, args),
        "math_gcd" => builtin_math_gcd(interp, args),
        "matrix_new" => builtin_matrix_new(interp, args),
        "matrix_set" => builtin_matrix_set(interp, args),
        "matrix_get" => builtin_matrix_get(interp, args),
        "matrix_mul" => builtin_matrix_mul(interp, args),
        "matrix_det" => builtin_matrix_det(interp, args),
        _ => Err(math_err(interp, format!("unknown math builtin: {}", name))),
    }
}

fn math_err(interp: &Interpreter, msg: String) -> HexaError {
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

fn to_float(v: &Value) -> Option<f64> {
    match v {
        Value::Float(f) => Some(*f),
        Value::Int(n) => Some(*n as f64),
        _ => None,
    }
}

fn to_int(v: &Value) -> Option<i64> {
    match v {
        Value::Int(n) => Some(*n),
        Value::Float(f) => Some(*f as i64),
        _ => None,
    }
}

// ── Single-argument float functions ──

fn builtin_math_abs(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    match args.first() {
        Some(Value::Int(n)) => Ok(Value::Int(n.abs())),
        Some(Value::Float(f)) => Ok(Value::Float(f.abs())),
        Some(_) => Err(type_err(interp, "math_abs() requires numeric argument".into())),
        None => Err(type_err(interp, "math_abs() requires 1 argument".into())),
    }
}

fn builtin_math_sqrt(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let x = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_sqrt() requires numeric argument".into()))?;
    Ok(Value::Float(x.sqrt()))
}

fn builtin_math_pow(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let x = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_pow() requires 2 numeric arguments".into()))?;
    let n = args.get(1).and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_pow() requires 2 numeric arguments".into()))?;
    Ok(Value::Float(x.powf(n)))
}

fn builtin_math_log(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let x = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_log() requires numeric argument".into()))?;
    Ok(Value::Float(x.ln()))
}

fn builtin_math_sin(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let x = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_sin() requires numeric argument".into()))?;
    Ok(Value::Float(x.sin()))
}

fn builtin_math_cos(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let x = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_cos() requires numeric argument".into()))?;
    Ok(Value::Float(x.cos()))
}

fn builtin_math_floor(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let x = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_floor() requires numeric argument".into()))?;
    Ok(Value::Int(x.floor() as i64))
}

fn builtin_math_ceil(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let x = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_ceil() requires numeric argument".into()))?;
    Ok(Value::Int(x.ceil() as i64))
}

fn builtin_math_round(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let x = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_round() requires numeric argument".into()))?;
    Ok(Value::Int(x.round() as i64))
}

fn builtin_math_min(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let a = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_min() requires 2 numeric arguments".into()))?;
    let b = args.get(1).and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_min() requires 2 numeric arguments".into()))?;
    if a <= b { Ok(args[0].clone()) } else { Ok(args[1].clone()) }
}

fn builtin_math_max(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let a = args.first().and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_max() requires 2 numeric arguments".into()))?;
    let b = args.get(1).and_then(|v| to_float(v))
        .ok_or_else(|| type_err(interp, "math_max() requires 2 numeric arguments".into()))?;
    if a >= b { Ok(args[0].clone()) } else { Ok(args[1].clone()) }
}

fn builtin_math_gcd(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let a = args.first().and_then(|v| to_int(v))
        .ok_or_else(|| type_err(interp, "math_gcd() requires 2 int arguments".into()))?;
    let b = args.get(1).and_then(|v| to_int(v))
        .ok_or_else(|| type_err(interp, "math_gcd() requires 2 int arguments".into()))?;
    Ok(Value::Int(gcd(a.abs(), b.abs())))
}

fn gcd(mut a: i64, mut b: i64) -> i64 {
    while b != 0 {
        let t = b;
        b = a % b;
        a = t;
    }
    a
}

// ── Matrix (stored as Array of Arrays) ──

fn builtin_matrix_new(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let rows = args.first().and_then(|v| to_int(v))
        .ok_or_else(|| type_err(interp, "matrix_new() requires (rows, cols)".into()))? as usize;
    let cols = args.get(1).and_then(|v| to_int(v))
        .ok_or_else(|| type_err(interp, "matrix_new() requires (rows, cols)".into()))? as usize;
    let matrix: Vec<Value> = (0..rows)
        .map(|_| Value::Array(vec![Value::Float(0.0); cols]))
        .collect();
    Ok(Value::Array(matrix))
}

fn builtin_matrix_set(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let mut matrix = match args.first() {
        Some(Value::Array(a)) => a.clone(),
        _ => return Err(type_err(interp, "matrix_set() first arg must be matrix".into())),
    };
    let r = args.get(1).and_then(|v| to_int(v))
        .ok_or_else(|| type_err(interp, "matrix_set() requires (matrix, row, col, val)".into()))? as usize;
    let c = args.get(2).and_then(|v| to_int(v))
        .ok_or_else(|| type_err(interp, "matrix_set() requires (matrix, row, col, val)".into()))? as usize;
    let val = args.get(3).cloned().unwrap_or(Value::Float(0.0));

    if r >= matrix.len() {
        return Err(math_err(interp, format!("matrix_set: row {} out of bounds", r)));
    }
    match &mut matrix[r] {
        Value::Array(row) => {
            if c >= row.len() {
                return Err(math_err(interp, format!("matrix_set: col {} out of bounds", c)));
            }
            row[c] = val;
        }
        _ => return Err(type_err(interp, "matrix_set: invalid matrix structure".into())),
    }
    Ok(Value::Array(matrix))
}

fn builtin_matrix_get(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let matrix = match args.first() {
        Some(Value::Array(a)) => a,
        _ => return Err(type_err(interp, "matrix_get() first arg must be matrix".into())),
    };
    let r = args.get(1).and_then(|v| to_int(v))
        .ok_or_else(|| type_err(interp, "matrix_get() requires (matrix, row, col)".into()))? as usize;
    let c = args.get(2).and_then(|v| to_int(v))
        .ok_or_else(|| type_err(interp, "matrix_get() requires (matrix, row, col)".into()))? as usize;

    match matrix.get(r) {
        Some(Value::Array(row)) => Ok(row.get(c).cloned().unwrap_or(Value::Void)),
        _ => Err(math_err(interp, format!("matrix_get: row {} out of bounds", r))),
    }
}

fn builtin_matrix_mul(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let a = match args.first() {
        Some(Value::Array(a)) => a,
        _ => return Err(type_err(interp, "matrix_mul() requires two matrices".into())),
    };
    let b = match args.get(1) {
        Some(Value::Array(b)) => b,
        _ => return Err(type_err(interp, "matrix_mul() requires two matrices".into())),
    };

    let a_rows = a.len();
    if a_rows == 0 {
        return Ok(Value::Array(vec![]));
    }
    let a_cols = match &a[0] { Value::Array(r) => r.len(), _ => 0 };
    let b_rows = b.len();
    let b_cols = if b_rows > 0 { match &b[0] { Value::Array(r) => r.len(), _ => 0 } } else { 0 };

    if a_cols != b_rows {
        return Err(math_err(interp, format!(
            "matrix_mul: incompatible dimensions {}x{} * {}x{}", a_rows, a_cols, b_rows, b_cols
        )));
    }

    let a_f = matrix_to_f64(a);
    let b_f = matrix_to_f64(b);

    let mut result = vec![vec![0.0f64; b_cols]; a_rows];
    for i in 0..a_rows {
        for j in 0..b_cols {
            let mut sum = 0.0;
            for k in 0..a_cols {
                sum += a_f[i][k] * b_f[k][j];
            }
            result[i][j] = sum;
        }
    }

    Ok(f64_to_matrix(&result))
}

fn builtin_matrix_det(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let m = match args.first() {
        Some(Value::Array(a)) => a,
        _ => return Err(type_err(interp, "matrix_det() requires a square matrix".into())),
    };
    let n = m.len();
    if n == 0 { return Ok(Value::Float(1.0)); }
    let cols = match &m[0] { Value::Array(r) => r.len(), _ => 0 };
    if n != cols {
        return Err(math_err(interp, "matrix_det: matrix must be square".into()));
    }

    let f = matrix_to_f64(m);
    Ok(Value::Float(det(&f, n)))
}

fn matrix_to_f64(m: &[Value]) -> Vec<Vec<f64>> {
    m.iter().map(|row| {
        match row {
            Value::Array(r) => r.iter().map(|v| to_float(v).unwrap_or(0.0)).collect(),
            _ => vec![],
        }
    }).collect()
}

fn f64_to_matrix(m: &[Vec<f64>]) -> Value {
    Value::Array(m.iter().map(|row| {
        Value::Array(row.iter().map(|&v| Value::Float(v)).collect())
    }).collect())
}

fn det(m: &[Vec<f64>], n: usize) -> f64 {
    if n == 1 { return m[0][0]; }
    if n == 2 { return m[0][0] * m[1][1] - m[0][1] * m[1][0]; }

    let mut result = 0.0;
    for j in 0..n {
        let sub = submatrix(m, 0, j, n);
        let sign = if j % 2 == 0 { 1.0 } else { -1.0 };
        result += sign * m[0][j] * det(&sub, n - 1);
    }
    result
}

fn submatrix(m: &[Vec<f64>], skip_row: usize, skip_col: usize, n: usize) -> Vec<Vec<f64>> {
    let mut sub = Vec::with_capacity(n - 1);
    for i in 0..n {
        if i == skip_row { continue; }
        let mut row = Vec::with_capacity(n - 1);
        for j in 0..n {
            if j == skip_col { continue; }
            row.push(m[i][j]);
        }
        sub.push(row);
    }
    sub
}

// ── Tests ──

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gcd() {
        assert_eq!(gcd(12, 8), 4);
        assert_eq!(gcd(7, 13), 1);
        assert_eq!(gcd(100, 75), 25);
        assert_eq!(gcd(0, 5), 5);
    }

    #[test]
    fn test_det_2x2() {
        let m = vec![vec![1.0, 2.0], vec![3.0, 4.0]];
        let d = det(&m, 2);
        assert!((d - (-2.0)).abs() < 1e-10);
    }

    #[test]
    fn test_det_3x3() {
        let m = vec![
            vec![1.0, 2.0, 3.0],
            vec![4.0, 5.0, 6.0],
            vec![7.0, 8.0, 9.0],
        ];
        let d = det(&m, 3);
        assert!(d.abs() < 1e-10); // singular
    }

    #[test]
    fn test_det_identity() {
        let m = vec![
            vec![1.0, 0.0, 0.0],
            vec![0.0, 1.0, 0.0],
            vec![0.0, 0.0, 1.0],
        ];
        let d = det(&m, 3);
        assert!((d - 1.0).abs() < 1e-10);
    }

    #[test]
    fn test_matrix_mul() {
        let a: Vec<Vec<f64>> = vec![vec![1.0, 2.0], vec![3.0, 4.0]];
        let b: Vec<Vec<f64>> = vec![vec![5.0, 6.0], vec![7.0, 8.0]];
        // [[1*5+2*7, 1*6+2*8], [3*5+4*7, 3*6+4*8]] = [[19, 22], [43, 50]]
        let a_rows = a.len();
        let a_cols = a[0].len();
        let b_cols = b[0].len();
        let mut result: Vec<Vec<f64>> = vec![vec![0.0; b_cols]; a_rows];
        for i in 0..a_rows {
            for j in 0..b_cols {
                for k in 0..a_cols {
                    result[i][j] += a[i][k] * b[k][j];
                }
            }
        }
        assert!((result[0][0] - 19.0_f64).abs() < 1e-10_f64);
        assert!((result[0][1] - 22.0_f64).abs() < 1e-10_f64);
        assert!((result[1][0] - 43.0_f64).abs() < 1e-10_f64);
        assert!((result[1][1] - 50.0_f64).abs() < 1e-10_f64);
    }

    #[test]
    fn test_constants() {
        assert!((std::f64::consts::PI - 3.14159265_f64).abs() < 1e-5_f64);
        assert!((std::f64::consts::E - 2.71828182_f64).abs() < 1e-5_f64);
        assert!((1.618033988749895_f64 - 1.618033_f64).abs() < 1e-4_f64);
    }
}
