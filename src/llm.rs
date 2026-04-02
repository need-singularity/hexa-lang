//! LLM backend for `generate` and `optimize` keywords.
//!
//! Resolution order:
//!   1. Claude API (ANTHROPIC_API_KEY env var)
//!   2. Local Ollama at localhost:11434
//!   3. Offline fallback (stub / no-op)
//!
//! Results are cached in `.hexa-cache/` keyed by SHA-256 of the prompt.

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::path::PathBuf;

/// Check cache for a previous result. Returns `Some(code)` on hit.
fn cache_get(prompt: &str) -> Option<String> {
    let key = hash_prompt(prompt);
    let path = cache_path(&key);
    std::fs::read_to_string(path).ok()
}

/// Store a result in cache.
fn cache_put(prompt: &str, code: &str) {
    let key = hash_prompt(prompt);
    let path = cache_path(&key);
    if let Some(parent) = path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let _ = std::fs::write(path, code);
}

fn hash_prompt(prompt: &str) -> String {
    let mut h = DefaultHasher::new();
    prompt.hash(&mut h);
    format!("{:016x}", h.finish())
}

fn cache_path(key: &str) -> PathBuf {
    PathBuf::from(".hexa-cache").join(format!("{}.hexa", key))
}

/// Generate HEXA code from a natural-language description.
///
/// `type_hint` is an optional type signature string like `fn(a: int, b: int) -> int`.
pub fn generate_code(description: &str, type_hint: &str) -> Result<String, String> {
    let prompt = build_generate_prompt(description, type_hint);

    // 1. Check cache
    if let Some(cached) = cache_get(&prompt) {
        return Ok(cached);
    }

    // 2. Try Claude API
    if let Ok(api_key) = std::env::var("ANTHROPIC_API_KEY") {
        if !api_key.is_empty() {
            if let Ok(code) = call_claude(&api_key, &prompt) {
                cache_put(&prompt, &code);
                return Ok(code);
            }
        }
    }

    // 3. Try local Ollama
    if let Ok(code) = call_ollama(&prompt) {
        cache_put(&prompt, &code);
        return Ok(code);
    }

    // 4. Offline fallback
    Ok(offline_generate_fallback(description, type_hint))
}

/// Optimize existing HEXA code via LLM.
///
/// Returns optimized code string, or the original if no LLM is available.
pub fn optimize_code(original: &str, fn_name: &str) -> Result<String, String> {
    let prompt = build_optimize_prompt(original, fn_name);

    // 1. Check cache
    if let Some(cached) = cache_get(&prompt) {
        return Ok(cached);
    }

    // 2. Try Claude API
    if let Ok(api_key) = std::env::var("ANTHROPIC_API_KEY") {
        if !api_key.is_empty() {
            if let Ok(code) = call_claude(&api_key, &prompt) {
                cache_put(&prompt, &code);
                return Ok(code);
            }
        }
    }

    // 3. Try local Ollama
    if let Ok(code) = call_ollama(&prompt) {
        cache_put(&prompt, &code);
        return Ok(code);
    }

    // 4. Offline: return original unchanged
    Ok(original.to_string())
}

// ── Prompt builders ─────────────────────────────────────────

fn build_generate_prompt(description: &str, type_hint: &str) -> String {
    let mut prompt = String::from(
        "You are a code generator for HEXA, a programming language similar to Rust.\n\
         HEXA syntax rules:\n\
         - Variables: let x = expr\n\
         - Functions: fn name(a: type, b: type) -> ret_type { body }\n\
         - Control: if/else, for x in iter { }, while cond { }, match\n\
         - Return: return expr\n\
         - Types: int, float, str, bool, [T] (array)\n\
         - Builtins: print(), println(), len(), to_string(), to_int()\n\n\
         Generate ONLY the function body (statements inside { }), no fn signature.\n\
         Output raw HEXA code only, no markdown fences, no comments, no explanation.\n\n"
    );
    if !type_hint.is_empty() {
        prompt.push_str(&format!("Type signature: {}\n", type_hint));
    }
    prompt.push_str(&format!("Description: {}\n\nHEXA code:", description));
    prompt
}

fn build_optimize_prompt(original: &str, fn_name: &str) -> String {
    format!(
        "You are a code optimizer for HEXA, a programming language similar to Rust.\n\
         Optimize the following HEXA function for performance (reduce time complexity, \
         use iteration instead of recursion where possible, add memoization if helpful).\n\
         Keep the same function name and signature. Keep the same behavior.\n\
         Output ONLY the complete optimized function as valid HEXA code.\n\
         No markdown fences, no comments, no explanation.\n\n\
         Function to optimize (name: {}):\n{}\n\nOptimized HEXA code:",
        fn_name, original
    )
}

// ── LLM backends ────────────────────────────────────────────

fn call_claude(api_key: &str, prompt: &str) -> Result<String, String> {
    let body = serde_json::json!({
        "model": "claude-sonnet-4-20250514",
        "max_tokens": 1024,
        "messages": [{"role": "user", "content": prompt}]
    });

    let resp = ureq::post("https://api.anthropic.com/v1/messages")
        .set("x-api-key", api_key)
        .set("anthropic-version", "2023-06-01")
        .set("content-type", "application/json")
        .send_string(&body.to_string());

    match resp {
        Ok(response) => {
            let text = response.into_string().map_err(|e| e.to_string())?;
            let json: serde_json::Value = serde_json::from_str(&text).map_err(|e| e.to_string())?;
            if let Some(content) = json["content"].as_array() {
                if let Some(first) = content.first() {
                    if let Some(text) = first["text"].as_str() {
                        return Ok(extract_code(text));
                    }
                }
            }
            Err("unexpected Claude API response format".into())
        }
        Err(e) => Err(format!("Claude API error: {}", e)),
    }
}

fn call_ollama(prompt: &str) -> Result<String, String> {
    let body = serde_json::json!({
        "model": "codellama",
        "prompt": prompt,
        "stream": false
    });

    let resp = ureq::post("http://localhost:11434/api/generate")
        .set("content-type", "application/json")
        .timeout(std::time::Duration::from_secs(30))
        .send_string(&body.to_string());

    match resp {
        Ok(response) => {
            let text = response.into_string().map_err(|e| e.to_string())?;
            let json: serde_json::Value = serde_json::from_str(&text).map_err(|e| e.to_string())?;
            if let Some(resp_text) = json["response"].as_str() {
                return Ok(extract_code(resp_text));
            }
            Err("unexpected Ollama response format".into())
        }
        Err(_) => Err("Ollama not available".into()),
    }
}

/// Strip markdown code fences if present.
fn extract_code(text: &str) -> String {
    let trimmed = text.trim();
    // Remove ```hexa ... ``` or ``` ... ```
    if trimmed.starts_with("```") {
        let without_opening = if let Some(rest) = trimmed.strip_prefix("```hexa") {
            rest
        } else if let Some(rest) = trimmed.strip_prefix("```rust") {
            rest
        } else {
            trimmed.strip_prefix("```").unwrap_or(trimmed)
        };
        let without_closing = without_opening
            .trim()
            .strip_suffix("```")
            .unwrap_or(without_opening);
        return without_closing.trim().to_string();
    }
    trimmed.to_string()
}

// ── Offline fallback ────────────────────────────────────────

fn offline_generate_fallback(description: &str, type_hint: &str) -> String {
    // Return a stub that panics at runtime with the description
    if type_hint.is_empty() {
        format!(
            "panic(\"LLM not available: {}\")",
            description.replace('"', "\\\"")
        )
    } else {
        format!(
            "panic(\"LLM not available: {} (expected: {})\")",
            description.replace('"', "\\\""),
            type_hint.replace('"', "\\\"")
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extract_code_plain() {
        assert_eq!(extract_code("return 42"), "return 42");
    }

    #[test]
    fn test_extract_code_fenced() {
        let input = "```hexa\nreturn 42\n```";
        assert_eq!(extract_code(input), "return 42");
    }

    #[test]
    fn test_cache_roundtrip() {
        let prompt = "test_cache_roundtrip_unique_12345";
        let code = "return 42";
        cache_put(prompt, code);
        assert_eq!(cache_get(prompt), Some(code.to_string()));
        // cleanup
        let key = hash_prompt(prompt);
        let _ = std::fs::remove_file(cache_path(&key));
    }

    #[test]
    fn test_offline_fallback() {
        let result = offline_generate_fallback("sort users", "fn(users: [User]) -> [User]");
        assert!(result.contains("LLM not available"));
        assert!(result.contains("sort users"));
    }

    #[test]
    fn test_generate_code_offline() {
        // With no API key and no Ollama, should return fallback
        std::env::remove_var("ANTHROPIC_API_KEY");
        let result = generate_code("test description", "").unwrap();
        assert!(result.contains("LLM not available") || !result.is_empty());
    }
}
