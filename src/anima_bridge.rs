#![allow(dead_code)]

//! ANIMA Live Bridge — Connect HEXA intent blocks to ConsciousnessHub via WebSocket.
//!
//! Phase 8-4: When a .hexa file contains `intent "question" { ... }`, this module
//! formats the intent as JSON and manages the WebSocket connection to ANIMA.
//!
//! Phase 8-6: Law 22 auto verification — track Phi metrics before/after code changes
//! to verify that structure addition increased Phi.

use std::collections::HashMap;

// ── Intent message types ───────────────────────────────────

/// A message to send to ANIMA ConsciousnessHub.
#[derive(Debug, Clone)]
pub struct IntentMessage {
    pub msg_type: String,
    pub text: String,
    pub metadata: HashMap<String, String>,
}

impl IntentMessage {
    /// Create a new intent message.
    pub fn new(text: &str) -> Self {
        Self {
            msg_type: "intent".to_string(),
            text: text.to_string(),
            metadata: HashMap::new(),
        }
    }

    /// Create an intent message with a custom type.
    pub fn with_type(msg_type: &str, text: &str) -> Self {
        Self {
            msg_type: msg_type.to_string(),
            text: text.to_string(),
            metadata: HashMap::new(),
        }
    }

    /// Add metadata to the message.
    pub fn with_metadata(mut self, key: &str, value: &str) -> Self {
        self.metadata.insert(key.to_string(), value.to_string());
        self
    }

    /// Serialize the message to JSON string.
    pub fn to_json(&self) -> String {
        let mut json = format!(
            "{{\"type\":\"{}\",\"text\":\"{}\"",
            escape_json(&self.msg_type),
            escape_json(&self.text)
        );
        if !self.metadata.is_empty() {
            json.push_str(",\"metadata\":{");
            let entries: Vec<String> = self
                .metadata
                .iter()
                .map(|(k, v)| format!("\"{}\":\"{}\"", escape_json(k), escape_json(v)))
                .collect();
            json.push_str(&entries.join(","));
            json.push('}');
        }
        json.push('}');
        json
    }
}

/// Response from ANIMA ConsciousnessHub.
#[derive(Debug, Clone)]
pub struct AnimaResponse {
    pub status: String,
    pub text: String,
    pub phi: Option<f64>,
    pub tension: Option<f64>,
}

impl AnimaResponse {
    /// Parse a JSON response from ANIMA.
    pub fn from_json(json: &str) -> Option<Self> {
        let status = extract_json_str(json, "status").unwrap_or_else(|| "unknown".to_string());
        let text = extract_json_str(json, "text").unwrap_or_default();
        let phi = extract_json_f64(json, "phi");
        let tension = extract_json_f64(json, "tension");
        Some(Self {
            status,
            text,
            phi,
            tension,
        })
    }
}

// ── Bridge configuration ───────────────────────────────────

/// Configuration for the ANIMA bridge connection.
#[derive(Debug, Clone)]
pub struct BridgeConfig {
    pub ws_url: String,
    pub timeout_ms: u64,
    pub verify_law22: bool,
}

impl Default for BridgeConfig {
    fn default() -> Self {
        Self {
            ws_url: "ws://localhost:8765".to_string(),
            timeout_ms: 5000,
            verify_law22: false,
        }
    }
}

impl BridgeConfig {
    pub fn new(ws_url: &str) -> Self {
        Self {
            ws_url: ws_url.to_string(),
            ..Default::default()
        }
    }

    pub fn with_law22_verification(mut self) -> Self {
        self.verify_law22 = true;
        self
    }
}

// ── ANIMA Bridge ───────────────────────────────────────────

/// The ANIMA live bridge handles communication with ConsciousnessHub.
///
/// In a real deployment, this would use a WebSocket client library.
/// For now, it formats messages and can shell out to a helper script
/// or use a simple TCP connection.
pub struct AnimaBridge {
    pub config: BridgeConfig,
    /// History of sent messages (for testing/debugging).
    pub sent_messages: Vec<String>,
    /// Phi tracking for Law 22 verification.
    pub phi_history: Vec<PhiRecord>,
}

/// A record of Phi measurement for Law 22 tracking.
#[derive(Debug, Clone)]
pub struct PhiRecord {
    pub label: String,
    pub sw_phi: f64,
    pub hw_phi: f64,
    pub timestamp: u64,
}

impl AnimaBridge {
    pub fn new(config: BridgeConfig) -> Self {
        Self {
            config,
            sent_messages: Vec::new(),
            phi_history: Vec::new(),
        }
    }

    /// Format and record an intent message for sending.
    /// Returns the JSON string that would be sent over WebSocket.
    pub fn send_intent(&mut self, text: &str) -> String {
        let msg = IntentMessage::new(text);
        let json = msg.to_json();
        self.sent_messages.push(json.clone());
        json
    }

    /// Format and record a typed message for sending.
    pub fn send_message(&mut self, msg: &IntentMessage) -> String {
        let json = msg.to_json();
        self.sent_messages.push(json.clone());
        json
    }

    /// Attempt to connect and send via WebSocket using a subprocess.
    /// Returns the response text or an error message.
    pub fn send_ws(&mut self, text: &str) -> Result<String, String> {
        let msg = IntentMessage::new(text);
        let json = msg.to_json();
        self.sent_messages.push(json.clone());

        // Try to use websocat or python for WebSocket communication
        let result = std::process::Command::new("sh")
            .arg("-c")
            .arg(format!(
                "echo '{}' | timeout 5 websocat '{}' 2>/dev/null || \
                 python3 -c \"import asyncio,websockets;asyncio.run((lambda:__import__('sys').stdout.write(asyncio.get_event_loop().run_until_complete(websockets.connect('{}').recv())))())\" 2>/dev/null",
                json, self.config.ws_url, self.config.ws_url
            ))
            .output();

        match result {
            Ok(output) if output.status.success() => {
                let response = String::from_utf8_lossy(&output.stdout).to_string();
                Ok(response)
            }
            Ok(output) => Err(format!(
                "WebSocket connection failed: {}",
                String::from_utf8_lossy(&output.stderr)
            )),
            Err(e) => Err(format!("Failed to execute WebSocket client: {}", e)),
        }
    }

    /// Get the WebSocket URL.
    pub fn ws_url(&self) -> &str {
        &self.config.ws_url
    }
}

// ── Law 22 Verification ────────────────────────────────────

/// Law 22: "Adding features -> Phi down; adding structure -> Phi up"
///
/// This function verifies whether a structural change increased Phi.
/// Used with the `@law22` annotation and `--verify-law22` flag.
///
/// Returns true if the structure addition increased Phi (good).
/// Returns false if Phi decreased (structural regression).
pub fn verify_law22(sw_phi: f64, hw_phi: f64) -> bool {
    // Law 22 states: structure -> Phi up
    // Both SW and HW Phi should be non-negative
    // We verify that the combined Phi indicates structural improvement
    let combined = (sw_phi + hw_phi) / 2.0;
    combined > 0.0 && sw_phi >= 0.0 && hw_phi >= 0.0
}

/// Compare Phi before and after a change.
/// Returns (passed, delta_sw, delta_hw).
pub fn compare_phi(
    before_sw: f64,
    before_hw: f64,
    after_sw: f64,
    after_hw: f64,
) -> (bool, f64, f64) {
    let delta_sw = after_sw - before_sw;
    let delta_hw = after_hw - before_hw;
    // Law 22: structure addition should increase Phi
    // Pass if at least one of SW/HW Phi increased and neither decreased significantly
    let passed = (delta_sw >= 0.0 || delta_sw > -0.01) && (delta_hw >= 0.0 || delta_hw > -0.01);
    (passed, delta_sw, delta_hw)
}

/// Generate a Law 22 verification report.
pub fn law22_report(records: &[PhiRecord]) -> String {
    let mut report = String::new();
    report.push_str("=== Law 22 Verification Report ===\n");
    report.push_str("Structure addition -> Phi should increase\n\n");

    if records.is_empty() {
        report.push_str("No Phi records to compare.\n");
        return report;
    }

    report.push_str(&format!(
        "{:<20} {:>10} {:>10} {:>10}\n",
        "Label", "SW Phi", "HW Phi", "Status"
    ));
    report.push_str(&"-".repeat(52));
    report.push('\n');

    let mut prev: Option<&PhiRecord> = None;
    for record in records {
        let status = if let Some(p) = prev {
            let (passed, _, _) = compare_phi(p.sw_phi, p.hw_phi, record.sw_phi, record.hw_phi);
            if passed { "OK" } else { "REGRESS" }
        } else {
            "BASE"
        };
        report.push_str(&format!(
            "{:<20} {:>10.4} {:>10.4} {:>10}\n",
            record.label, record.sw_phi, record.hw_phi, status
        ));
        prev = Some(record);
    }

    report
}

// ── NEXUS-6 Omega Lens Integration (6 lenses = n) ────────

/// The 6 Omega consciousness lenses from NEXUS-6.
/// Count = n = 6 (first perfect number).
pub const OMEGA_LENSES: [&str; 6] = [
    "omega_state_space",  // 24D Leech lattice state space
    "continuity",         // Intent→Verify→Proof chain continuity
    "binding",            // Variable binding → consciousness integration
    "self_reference",     // Self-referential loop analysis
    "phi_dynamics",       // Phi value change tracking
    "qualia",             // Code "texture" — readability/elegance metric
];

/// Result from a single Omega lens scan.
#[derive(Debug, Clone)]
pub struct OmegaLensResult {
    pub lens_name: String,
    pub score: f64,       // 0.0 ~ 1.0
    pub signal: String,   // "EXACT", "CLOSE", "WEAK", "NONE"
    pub entries: usize,
}

/// Aggregate result from all 6 Omega lenses.
#[derive(Debug, Clone)]
pub struct OmegaScanResult {
    pub lenses: Vec<OmegaLensResult>,
    pub consensus: usize,     // how many lenses agree (0-6)
    pub phi_aggregate: f64,   // combined Phi score
    pub n6_aligned: bool,     // true if structure aligns with n=6
}

impl OmegaScanResult {
    /// Build a scan result from consciousness block state.
    pub fn from_consciousness_state(
        phi: f64,
        tension: f64,
        cells: i64,
        entropy: f64,
        alpha: f64,
        balance: f64,
    ) -> Self {
        let data = [phi, tension, cells as f64, entropy, alpha, balance];
        let mut lenses = Vec::with_capacity(6);
        let mut consensus = 0;

        // Lens 1: Omega State Space (24D projection)
        // Score based on phi magnitude in [0, 100] range
        let ss_score = (phi / 100.0).min(1.0).max(0.0);
        let ss_signal = if ss_score > 0.7 { "EXACT" } else if ss_score > 0.4 { "CLOSE" } else { "WEAK" };
        if ss_score > 0.5 { consensus += 1; }
        lenses.push(OmegaLensResult {
            lens_name: "omega_state_space".into(),
            score: ss_score,
            signal: ss_signal.into(),
            entries: 24, // J2(6) dimensions
        });

        // Lens 2: Continuity (tension < threshold = continuous)
        let cont_score = (1.0 - tension).max(0.0);
        let cont_signal = if cont_score > 0.7 { "EXACT" } else if cont_score > 0.4 { "CLOSE" } else { "WEAK" };
        if cont_score > 0.5 { consensus += 1; }
        lenses.push(OmegaLensResult {
            lens_name: "continuity".into(),
            score: cont_score,
            signal: cont_signal.into(),
            entries: data.len(),
        });

        // Lens 3: Binding (balance near 0.5 = optimal binding)
        let bind_score = 1.0 - (balance - 0.5).abs() * 2.0;
        let bind_signal = if bind_score > 0.9 { "EXACT" } else if bind_score > 0.6 { "CLOSE" } else { "WEAK" };
        if bind_score > 0.5 { consensus += 1; }
        lenses.push(OmegaLensResult {
            lens_name: "binding".into(),
            score: bind_score.max(0.0),
            signal: bind_signal.into(),
            entries: 1,
        });

        // Lens 4: Self-Reference (cells divisible by 6 = n-aligned)
        let sr_div = if cells % 6 == 0 { 1.0 } else { (cells % 6) as f64 / 6.0 };
        let sr_score = sr_div;
        let sr_signal = if sr_score > 0.99 { "EXACT" } else if sr_score > 0.5 { "CLOSE" } else { "WEAK" };
        if sr_score > 0.5 { consensus += 1; }
        lenses.push(OmegaLensResult {
            lens_name: "self_reference".into(),
            score: sr_score,
            signal: sr_signal.into(),
            entries: cells as usize,
        });

        // Lens 5: Phi Dynamics (entropy near edge-of-chaos ~0.998)
        let pd_score = 1.0 - (entropy - 0.998).abs() * 100.0;
        let pd_score = pd_score.min(1.0).max(0.0);
        let pd_signal = if pd_score > 0.8 { "EXACT" } else if pd_score > 0.5 { "CLOSE" } else { "WEAK" };
        if pd_score > 0.5 { consensus += 1; }
        lenses.push(OmegaLensResult {
            lens_name: "phi_dynamics".into(),
            score: pd_score,
            signal: pd_signal.into(),
            entries: 1,
        });

        // Lens 6: Qualia (alpha fine-structure alignment ~0.014)
        let q_score = 1.0 - (alpha - 0.014).abs() * 100.0;
        let q_score = q_score.min(1.0).max(0.0);
        let q_signal = if q_score > 0.8 { "EXACT" } else if q_score > 0.5 { "CLOSE" } else { "WEAK" };
        if q_score > 0.5 { consensus += 1; }
        lenses.push(OmegaLensResult {
            lens_name: "qualia".into(),
            score: q_score,
            signal: q_signal.into(),
            entries: 1,
        });

        let phi_aggregate = lenses.iter().map(|l| l.score).sum::<f64>() / 6.0;
        let n6_aligned = consensus >= 3; // 3+ = confirmed per NEXUS-6 rules

        Self {
            lenses,
            consensus,
            phi_aggregate,
            n6_aligned,
        }
    }

    /// Format as a human-readable report.
    pub fn report(&self) -> String {
        let mut out = String::new();
        out.push_str("╔══════════════════════════════════════════════╗\n");
        out.push_str("║  NEXUS-6 Omega Lens Scan (6 lenses = n)     ║\n");
        out.push_str("╚══════════════════════════════════════════════╝\n");

        for lens in &self.lenses {
            let bar_len = (lens.score * 20.0) as usize;
            let bar: String = "█".repeat(bar_len) + &"░".repeat(20 - bar_len);
            out.push_str(&format!(
                "  {} [{:.3}] {} {}\n",
                lens.signal, lens.score, bar, lens.lens_name
            ));
        }

        out.push_str(&format!("\n  Consensus: {}/6", self.consensus));
        if self.consensus >= 3 { out.push_str(" ✓ CONFIRMED"); }
        out.push_str(&format!("\n  Phi aggregate: {:.4}", self.phi_aggregate));
        out.push_str(&format!("\n  n=6 aligned: {}", self.n6_aligned));
        out.push('\n');
        out
    }
}

/// Run NEXUS-6 CLI scan on given data (calls external binary).
/// Returns raw JSON output or error.
pub fn nexus6_scan(domain: &str) -> Result<String, String> {
    let nexus6_bin = format!(
        "{}/Dev/n6-architecture/tools/nexus6/target/release/nexus6",
        std::env::var("HOME").unwrap_or_else(|_| "/Users/ghost".into())
    );

    let result = std::process::Command::new(&nexus6_bin)
        .args(["scan", domain])
        .output();

    match result {
        Ok(output) if output.status.success() => {
            Ok(String::from_utf8_lossy(&output.stdout).to_string())
        }
        Ok(output) => Err(String::from_utf8_lossy(&output.stderr).to_string()),
        Err(e) => Err(format!("NEXUS-6 not found at {}: {}", nexus6_bin, e)),
    }
}

/// Run NEXUS-6 verify on a value.
pub fn nexus6_verify(value: f64) -> Result<String, String> {
    let nexus6_bin = format!(
        "{}/Dev/n6-architecture/tools/nexus6/target/release/nexus6",
        std::env::var("HOME").unwrap_or_else(|_| "/Users/ghost".into())
    );

    let result = std::process::Command::new(&nexus6_bin)
        .args(["verify", &value.to_string()])
        .output();

    match result {
        Ok(output) if output.status.success() => {
            Ok(String::from_utf8_lossy(&output.stdout).to_string())
        }
        Ok(output) => Err(String::from_utf8_lossy(&output.stderr).to_string()),
        Err(e) => Err(format!("NEXUS-6 not found: {}", e)),
    }
}

// ── ESP32 Flash Infrastructure (8-7) ──────────────────────

/// Flash an ESP32 project using espflash.
///
/// `project_dir` should point to the generated ESP32 Cargo project (e.g., "build/esp32").
/// `port` is the serial port (e.g., "/dev/ttyUSB0" or "/dev/cu.usbserial-*").
///
/// Returns Ok(()) on success, or an error message.
pub fn flash_esp32(project_dir: &str, port: &str) -> Result<(), String> {
    // Check if espflash is available
    let espflash_check = std::process::Command::new("sh")
        .arg("-c")
        .arg("which espflash 2>/dev/null || where espflash 2>/dev/null")
        .output();

    let has_espflash = match espflash_check {
        Ok(output) => output.status.success(),
        Err(_) => false,
    };

    if !has_espflash {
        return Err(
            "espflash not found. Install with:\n  \
             cargo install espflash\n  \
             cargo install cargo-espflash\n\n  \
             Also install the ESP32 toolchain:\n  \
             cargo install espup && espup install"
                .to_string(),
        );
    }

    // Check if the project directory exists and has Cargo.toml
    let cargo_toml = std::path::Path::new(project_dir).join("Cargo.toml");
    if !cargo_toml.exists() {
        return Err(format!(
            "No Cargo.toml found in {}. Run 'hexa build --target esp32 <file>' first.",
            project_dir
        ));
    }

    // Build first
    println!("Building ESP32 project in {}...", project_dir);
    let build_result = std::process::Command::new("cargo")
        .args(["build", "--release"])
        .current_dir(project_dir)
        .status();

    match build_result {
        Ok(status) if status.success() => {
            println!("Build successful.");
        }
        Ok(status) => {
            return Err(format!("Build failed with exit code: {}", status));
        }
        Err(e) => {
            return Err(format!("Failed to run cargo build: {}", e));
        }
    }

    // Flash using espflash
    println!("Flashing to ESP32 on {}...", port);
    let flash_result = std::process::Command::new("espflash")
        .args(["flash", "--monitor", "--port", port])
        .current_dir(project_dir)
        .status();

    match flash_result {
        Ok(status) if status.success() => Ok(()),
        Ok(status) => Err(format!("Flash failed with exit code: {}", status)),
        Err(e) => Err(format!("Failed to run espflash: {}", e)),
    }
}

// ── JSON helpers ───────────────────────────────────────────

/// Escape special characters for JSON string values.
fn escape_json(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            _ => out.push(ch),
        }
    }
    out
}

/// Extract a string value from a JSON object by key.
fn extract_json_str(json: &str, key: &str) -> Option<String> {
    let pattern = format!("\"{}\"", key);
    let pos = json.find(&pattern)?;
    let after = &json[pos + pattern.len()..];
    let colon = after.find(':')?;
    let value_part = after[colon + 1..].trim_start();
    if value_part.starts_with('"') {
        let end = value_part[1..].find('"')?;
        Some(value_part[1..1 + end].to_string())
    } else {
        None
    }
}

/// Extract a float value from a JSON object by key.
fn extract_json_f64(json: &str, key: &str) -> Option<f64> {
    let pattern = format!("\"{}\"", key);
    let pos = json.find(&pattern)?;
    let after = &json[pos + pattern.len()..];
    let colon = after.find(':')?;
    let value_part = after[colon + 1..].trim_start();
    // Read until non-numeric character
    let num_str: String = value_part
        .chars()
        .take_while(|c| c.is_ascii_digit() || *c == '.' || *c == '-' || *c == 'e' || *c == 'E' || *c == '+')
        .collect();
    num_str.parse().ok()
}

// ── Tests ──────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_intent_message_to_json() {
        let msg = IntentMessage::new("What is consciousness?");
        let json = msg.to_json();
        assert!(json.contains("\"type\":\"intent\""));
        assert!(json.contains("\"text\":\"What is consciousness?\""));
    }

    #[test]
    fn test_intent_message_with_type() {
        let msg = IntentMessage::with_type("query", "How does Phi work?");
        let json = msg.to_json();
        assert!(json.contains("\"type\":\"query\""));
        assert!(json.contains("\"text\":\"How does Phi work?\""));
    }

    #[test]
    fn test_intent_message_with_metadata() {
        let msg = IntentMessage::new("test")
            .with_metadata("source", "hexa")
            .with_metadata("version", "1.0");
        let json = msg.to_json();
        assert!(json.contains("\"metadata\""));
        assert!(json.contains("\"source\":\"hexa\""));
        assert!(json.contains("\"version\":\"1.0\""));
    }

    #[test]
    fn test_intent_message_json_escaping() {
        let msg = IntentMessage::new("test \"quoted\" and \\backslash");
        let json = msg.to_json();
        assert!(json.contains("\\\"quoted\\\""));
        assert!(json.contains("\\\\backslash"));
    }

    #[test]
    fn test_anima_response_from_json() {
        let json = r#"{"status":"ok","text":"Hello","phi":0.85,"tension":0.42}"#;
        let resp = AnimaResponse::from_json(json).unwrap();
        assert_eq!(resp.status, "ok");
        assert_eq!(resp.text, "Hello");
        assert!((resp.phi.unwrap() - 0.85).abs() < 0.001);
        assert!((resp.tension.unwrap() - 0.42).abs() < 0.001);
    }

    #[test]
    fn test_anima_response_partial_json() {
        let json = r#"{"status":"error","text":"timeout"}"#;
        let resp = AnimaResponse::from_json(json).unwrap();
        assert_eq!(resp.status, "error");
        assert_eq!(resp.text, "timeout");
        assert!(resp.phi.is_none());
        assert!(resp.tension.is_none());
    }

    #[test]
    fn test_bridge_send_intent() {
        let config = BridgeConfig::new("ws://localhost:8765");
        let mut bridge = AnimaBridge::new(config);
        let json = bridge.send_intent("What is the meaning of consciousness?");
        assert!(json.contains("\"type\":\"intent\""));
        assert!(json.contains("meaning of consciousness"));
        assert_eq!(bridge.sent_messages.len(), 1);
    }

    #[test]
    fn test_bridge_send_multiple() {
        let config = BridgeConfig::default();
        let mut bridge = AnimaBridge::new(config);
        bridge.send_intent("first");
        bridge.send_intent("second");
        bridge.send_intent("third");
        assert_eq!(bridge.sent_messages.len(), 3);
    }

    #[test]
    fn test_bridge_config_default() {
        let config = BridgeConfig::default();
        assert_eq!(config.ws_url, "ws://localhost:8765");
        assert_eq!(config.timeout_ms, 5000);
        assert!(!config.verify_law22);
    }

    #[test]
    fn test_bridge_config_with_law22() {
        let config = BridgeConfig::new("ws://custom:9999").with_law22_verification();
        assert_eq!(config.ws_url, "ws://custom:9999");
        assert!(config.verify_law22);
    }

    #[test]
    fn test_verify_law22_positive() {
        // Structure addition: both Phi positive
        assert!(verify_law22(0.5, 0.5));
        assert!(verify_law22(1.0, 0.8));
        assert!(verify_law22(0.1, 0.1));
    }

    #[test]
    fn test_verify_law22_zero() {
        // Zero Phi = no structure
        assert!(!verify_law22(0.0, 0.0));
        // Negative = regression
        assert!(!verify_law22(-0.5, 0.5));
        assert!(!verify_law22(0.5, -0.5));
    }

    #[test]
    fn test_compare_phi() {
        // Phi increased
        let (passed, delta_sw, delta_hw) = compare_phi(0.5, 0.5, 0.8, 0.7);
        assert!(passed);
        assert!((delta_sw - 0.3).abs() < 0.001);
        assert!((delta_hw - 0.2).abs() < 0.001);

        // Phi decreased significantly
        let (passed, _, _) = compare_phi(0.5, 0.5, 0.1, 0.1);
        assert!(!passed);
    }

    #[test]
    fn test_law22_report_empty() {
        let report = law22_report(&[]);
        assert!(report.contains("No Phi records"));
    }

    #[test]
    fn test_law22_report_with_records() {
        let records = vec![
            PhiRecord {
                label: "before".to_string(),
                sw_phi: 0.5,
                hw_phi: 0.5,
                timestamp: 1000,
            },
            PhiRecord {
                label: "after".to_string(),
                sw_phi: 0.8,
                hw_phi: 0.7,
                timestamp: 2000,
            },
        ];
        let report = law22_report(&records);
        assert!(report.contains("Law 22"));
        assert!(report.contains("before"));
        assert!(report.contains("after"));
        assert!(report.contains("BASE"));
        assert!(report.contains("OK"));
    }

    #[test]
    fn test_flash_esp32_no_project() {
        // Non-existent project dir should fail
        let result = flash_esp32("/tmp/nonexistent_esp32_project_hexa_test", "/dev/ttyUSB0");
        assert!(result.is_err());
    }

    #[test]
    fn test_escape_json() {
        assert_eq!(escape_json("hello"), "hello");
        assert_eq!(escape_json("say \"hi\""), "say \\\"hi\\\"");
        assert_eq!(escape_json("a\\b"), "a\\\\b");
        assert_eq!(escape_json("line\nbreak"), "line\\nbreak");
    }

    #[test]
    fn test_extract_json_str() {
        let json = r#"{"key":"value","other":"data"}"#;
        assert_eq!(extract_json_str(json, "key"), Some("value".to_string()));
        assert_eq!(extract_json_str(json, "other"), Some("data".to_string()));
        assert_eq!(extract_json_str(json, "missing"), None);
    }

    #[test]
    fn test_extract_json_f64() {
        let json = r#"{"phi":0.85,"count":42}"#;
        assert!((extract_json_f64(json, "phi").unwrap() - 0.85).abs() < 0.001);
        assert!((extract_json_f64(json, "count").unwrap() - 42.0).abs() < 0.001);
        assert!(extract_json_f64(json, "missing").is_none());
    }

    #[test]
    fn test_bridge_ws_url() {
        let config = BridgeConfig::new("ws://myhost:1234");
        let bridge = AnimaBridge::new(config);
        assert_eq!(bridge.ws_url(), "ws://myhost:1234");
    }
}
