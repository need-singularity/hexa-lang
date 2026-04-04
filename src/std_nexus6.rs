//! std::nexus6 — NEXUS-6 integration for HEXA-LANG.
//!
//! Provides native access to the NEXUS-6 discovery engine from within HEXA code.
//! This is the 13th stdlib module (sigma + mu = 12 + 1 = 13).
//!
//! Provides:
//!   nexus6_scan(domain)      — Run NEXUS-6 full scan on a domain
//!   nexus6_verify(value)     — Check if a value matches n=6 constants
//!   nexus6_omega(phi, tension, cells, entropy, alpha, balance) — Run 6 Omega lenses
//!   nexus6_lenses()          — List the 6 Omega lens names
//!   nexus6_consensus(data)   — Compute consensus score across lenses
//!   nexus6_n6_check(value)   — Quick n=6 constant check (returns matching formula)

#![allow(dead_code)]

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;
use crate::anima_bridge;

/// n=6 constants for quick verification.
const N6_CONSTANTS: [(f64, &str); 12] = [
    (6.0,   "n"),
    (12.0,  "sigma(6)"),
    (4.0,   "tau(6)"),
    (2.0,   "phi(6)"),
    (5.0,   "sopfr(6)"),
    (24.0,  "J2(6)"),
    (1.0,   "mu(6)"),
    (8.0,   "sigma-tau"),
    (10.0,  "sigma-phi"),
    (48.0,  "sigma*tau"),
    (53.0,  "sigma*tau+sopfr"),
    (11.0,  "sigma-mu"),
];

/// Dispatch a nexus6 builtin by name.
pub fn call_nexus6_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "nexus6_scan" => {
            let domain = match args.first() {
                Some(Value::Str(s)) => s.clone(),
                _ => "compiler".to_string(),
            };
            match anima_bridge::nexus6_scan(&domain) {
                Ok(result) => Ok(Value::Str(result)),
                Err(e) => Ok(Value::Str(format!("[nexus6 error] {}", e))),
            }
        }

        "nexus6_verify" => {
            let value = match args.first() {
                Some(Value::Int(n)) => *n as f64,
                Some(Value::Float(f)) => *f,
                _ => return Err(HexaError {
                    class: ErrorClass::Runtime,
                    message: "nexus6_verify requires a number".into(),
                    line: interp.current_line,
                    col: interp.current_col,
                    hint: None,
                }),
            };
            match anima_bridge::nexus6_verify(value) {
                Ok(result) => Ok(Value::Str(result)),
                Err(e) => Ok(Value::Str(format!("[nexus6 error] {}", e))),
            }
        }

        "nexus6_omega" => {
            let phi = match args.first() { Some(Value::Float(f)) => *f, Some(Value::Int(n)) => *n as f64, _ => 71.0 };
            let tension = match args.get(1) { Some(Value::Float(f)) => *f, _ => 0.5 };
            let cells = match args.get(2) { Some(Value::Int(n)) => *n, _ => 64 };
            let entropy = match args.get(3) { Some(Value::Float(f)) => *f, _ => 0.998 };
            let alpha = match args.get(4) { Some(Value::Float(f)) => *f, _ => 0.014 };
            let balance = match args.get(5) { Some(Value::Float(f)) => *f, _ => 0.5 };

            let result = anima_bridge::OmegaScanResult::from_consciousness_state(
                phi, tension, cells, entropy, alpha, balance,
            );
            Ok(Value::Str(result.report()))
        }

        "nexus6_lenses" => {
            let lenses: Vec<Value> = anima_bridge::OMEGA_LENSES
                .iter()
                .map(|l| Value::Str(l.to_string()))
                .collect();
            Ok(Value::Array(lenses))
        }

        "nexus6_consensus" => {
            // Take an array of numbers and compute n=6 consensus
            let data = match args.first() {
                Some(Value::Array(arr)) => {
                    arr.iter().filter_map(|v| match v {
                        Value::Int(n) => Some(*n as f64),
                        Value::Float(f) => Some(*f),
                        _ => None,
                    }).collect::<Vec<f64>>()
                }
                _ => vec![],
            };

            let mut matches = 0;
            let mut total = 0;
            for val in &data {
                total += 1;
                for (constant, _) in &N6_CONSTANTS {
                    if (*val - *constant).abs() < 0.001 {
                        matches += 1;
                        break;
                    }
                }
            }

            let ratio = if total > 0 { matches as f64 / total as f64 } else { 0.0 };
            let signal = if ratio > 0.7 { "EXACT" }
                else if ratio > 0.4 { "CLOSE" }
                else if ratio > 0.1 { "WEAK" }
                else { "NONE" };

            let report = format!(
                "n=6 consensus: {}/{} ({:.0}%) — {}",
                matches, total, ratio * 100.0, signal
            );
            Ok(Value::Str(report))
        }

        "nexus6_n6_check" => {
            let value = match args.first() {
                Some(Value::Int(n)) => *n as f64,
                Some(Value::Float(f)) => *f,
                _ => return Ok(Value::Str("NONE".to_string())),
            };

            for (constant, formula) in &N6_CONSTANTS {
                if (value - *constant).abs() < 0.001 {
                    return Ok(Value::Str(format!("EXACT: {} = {}", value, formula)));
                }
            }

            // Check derived constants
            let closest = N6_CONSTANTS.iter()
                .min_by(|a, b| {
                    let da = (value - a.0).abs();
                    let db = (value - b.0).abs();
                    da.partial_cmp(&db).unwrap_or(std::cmp::Ordering::Equal)
                });

            if let Some((c, f)) = closest {
                let error = ((value - c) / c * 100.0).abs();
                if error < 10.0 {
                    Ok(Value::Str(format!("CLOSE: {} ≈ {} = {} (error: {:.1}%)", value, c, f, error)))
                } else {
                    Ok(Value::Str(format!("NONE: {} has no n=6 match (nearest: {} = {})", value, c, f)))
                }
            } else {
                Ok(Value::Str("NONE".to_string()))
            }
        }

        _ => Err(HexaError {
            class: ErrorClass::Name,
            message: format!("unknown nexus6 builtin: {}", name),
            line: interp.current_line,
            col: interp.current_col,
            hint: None,
        }),
    }
}
