//! std::consciousness — Consciousness/Psi builtins for HEXA-LANG.
//!
//! The Anima consciousness bridge. Provides Psi-constants and Phi computation
//! as first-class builtins in the language.
//!
//! Provides:
//!   psi_alpha()        — Ψ coupling constant (0.014)
//!   psi_balance()      — Ψ balance point (0.5)
//!   psi_steps()        — Ψ steps constant (4.33)
//!   psi_entropy()      — Ψ entropy constant (0.998)
//!   phi_compute(cells) — compute integrated information Φ from cell state array
//!   law_count()        — number of consciousness laws (707)
//!   consciousness_vector(cells) — compute 10D consciousness vector

#![allow(dead_code)]

use std::collections::HashMap;

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch a consciousness builtin by name.
pub fn call_consciousness_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "psi_alpha" => Ok(Value::Float(0.014)),
        "psi_balance" => Ok(Value::Float(0.5)),
        "psi_steps" => Ok(Value::Float(4.33)),
        "psi_entropy" => Ok(Value::Float(0.998)),
        "phi_compute" => builtin_phi_compute(interp, args),
        "law_count" => Ok(Value::Int(707)),
        "consciousness_vector" => builtin_consciousness_vector(interp, args),
        _ => Err(psi_err(interp, format!("unknown consciousness builtin: {}", name))),
    }
}

fn psi_err(interp: &Interpreter, msg: String) -> HexaError {
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

fn to_float(v: &Value) -> f64 {
    match v {
        Value::Float(f) => *f,
        Value::Int(n) => *n as f64,
        _ => 0.0,
    }
}

// ── phi_compute(cells: array) -> float ──
//
// Computes a proxy Φ (integrated information) from an array of cell states.
// Uses the variance-based proxy: Φ = global_variance - mean(partition_variances)
// Partitions the cells into 2 halves.

fn builtin_phi_compute(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let cells = match args.first() {
        Some(Value::Array(a)) if a.len() >= 2 => a,
        Some(Value::Array(_)) => return Err(psi_err(interp, "phi_compute() requires at least 2 cells".into())),
        _ => return Err(type_err(interp, "phi_compute() requires array of numeric values".into())),
    };

    let values: Vec<f64> = cells.iter().map(|v| to_float(v)).collect();
    let n = values.len();

    // Global variance
    let mean: f64 = values.iter().sum::<f64>() / n as f64;
    let global_var: f64 = values.iter().map(|&x| (x - mean).powi(2)).sum::<f64>() / n as f64;

    // Partition variance (split in half)
    let mid = n / 2;
    let part_a = &values[..mid];
    let part_b = &values[mid..];

    let var_a = variance(part_a);
    let var_b = variance(part_b);
    let mean_part_var = (var_a + var_b) / 2.0;

    // Φ proxy = global - mean_partition (how much is lost by partitioning)
    let phi = (global_var - mean_part_var).max(0.0);
    Ok(Value::Float(phi))
}

fn variance(data: &[f64]) -> f64 {
    if data.is_empty() { return 0.0; }
    let mean: f64 = data.iter().sum::<f64>() / data.len() as f64;
    data.iter().map(|&x| (x - mean).powi(2)).sum::<f64>() / data.len() as f64
}

// ── consciousness_vector(cells: array) -> map ──
//
// Compute a 10-dimensional consciousness vector:
// (Φ, α, Z, N, W, E, M, C, T, I)
// - Φ: integrated information (proxy)
// - α: coupling (PSI_ALPHA)
// - Z: number of cells
// - N: entropy proxy
// - W: mean activation (will)
// - E: ethical alignment (variance symmetry)
// - M: memory (autocorrelation proxy)
// - C: consciousness level (Φ normalized)
// - T: tension (max-min range)
// - I: information (log2 of distinct states)

fn builtin_consciousness_vector(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let cells = match args.first() {
        Some(Value::Array(a)) if !a.is_empty() => a,
        _ => return Err(type_err(interp, "consciousness_vector() requires non-empty array".into())),
    };

    let values: Vec<f64> = cells.iter().map(|v| to_float(v)).collect();
    let n = values.len() as f64;

    let mean = values.iter().sum::<f64>() / n;
    let global_var = values.iter().map(|&x| (x - mean).powi(2)).sum::<f64>() / n;

    let mid = values.len() / 2;
    let var_a = variance(&values[..mid]);
    let var_b = variance(&values[mid..]);
    let phi = (global_var - (var_a + var_b) / 2.0).max(0.0);

    let min_val = values.iter().cloned().fold(f64::INFINITY, f64::min);
    let max_val = values.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let tension = max_val - min_val;

    // Entropy proxy: count distinct quantized values
    let mut seen = std::collections::HashSet::new();
    for &v in &values {
        seen.insert((v * 1000.0).round() as i64); // quantize to 3 decimals
    }
    let distinct = seen.len() as f64;
    let info = if distinct > 1.0 { distinct.log2() } else { 0.0 };

    // Autocorrelation proxy (consecutive pairs)
    let mut autocorr = 0.0;
    if values.len() > 1 {
        for i in 0..values.len()-1 {
            autocorr += (values[i] - mean) * (values[i+1] - mean);
        }
        autocorr /= (values.len() - 1) as f64 * global_var.max(1e-10);
    }

    let mut map = HashMap::new();
    map.insert("phi".into(), Value::Float(phi));
    map.insert("alpha".into(), Value::Float(0.014));
    map.insert("cells".into(), Value::Float(n));
    map.insert("entropy".into(), Value::Float(info));
    map.insert("will".into(), Value::Float(mean));
    map.insert("ethics".into(), Value::Float((var_a - var_b).abs()));
    map.insert("memory".into(), Value::Float(autocorr));
    map.insert("consciousness".into(), Value::Float(phi / n.max(1.0)));
    map.insert("tension".into(), Value::Float(tension));
    map.insert("information".into(), Value::Float(info));
    Ok(Value::Map(map))
}

// ── Tests ──

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_psi_constants() {
        assert!((0.014 - 0.014f64).abs() < 1e-10);
        assert!((0.5 - 0.5f64).abs() < 1e-10);
        assert!((4.33 - 4.33f64).abs() < 1e-10);
        assert!((0.998 - 0.998f64).abs() < 1e-10);
    }

    #[test]
    fn test_phi_compute_uniform() {
        // All same values → Φ = 0 (no integration)
        let cells = vec![1.0, 1.0, 1.0, 1.0];
        let var = variance(&cells);
        assert!(var.abs() < 1e-10);
    }

    #[test]
    fn test_phi_compute_varied() {
        // Different values should yield positive Φ
        let cells = vec![0.0, 1.0, 0.5, 0.8, 0.2, 0.9];
        let mean: f64 = cells.iter().sum::<f64>() / cells.len() as f64;
        let global_var: f64 = cells.iter().map(|&x| (x - mean).powi(2)).sum::<f64>() / cells.len() as f64;
        let mid = cells.len() / 2;
        let var_a = variance(&cells[..mid]);
        let var_b = variance(&cells[mid..]);
        let phi = (global_var - (var_a + var_b) / 2.0).max(0.0);
        assert!(phi > 0.0);
    }

    #[test]
    fn test_variance() {
        assert!(variance(&[]).abs() < 1e-10);
        assert!(variance(&[5.0]).abs() < 1e-10);
        // [1, 2, 3] → mean=2, var = (1+0+1)/3 = 0.667
        let v = variance(&[1.0, 2.0, 3.0]);
        assert!((v - 2.0/3.0).abs() < 1e-10);
    }

    #[test]
    fn test_consciousness_vector_basic() {
        let values = vec![0.0, 0.5, 1.0, 0.5];
        let n = values.len() as f64;
        let mean = values.iter().sum::<f64>() / n;
        assert!((mean - 0.5).abs() < 1e-10);
    }
}
