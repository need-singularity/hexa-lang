//! std::crypto — Cryptographic builtins for HEXA-LANG.
//!
//! Provides:
//!   sha256(data)              — SHA-256 hash (pure Rust, returns hex string)
//!   xor_cipher(data, key)    — simple XOR cipher (symmetric encrypt/decrypt)
//!   hash_djb2(data)          — DJB2 hash (fast, non-crypto)
//!   random_bytes(n)          — generate N pseudo-random bytes (not crypto-safe)
//!   hmac_sha256(key, data)   — HMAC-SHA256

#![allow(dead_code)]

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch a crypto builtin by name.
pub fn call_crypto_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "sha256" => builtin_sha256(interp, args),
        "xor_cipher" => builtin_xor_cipher(interp, args),
        "hash_djb2" => builtin_hash_djb2(interp, args),
        "random_bytes" => builtin_random_bytes(interp, args),
        "hmac_sha256" => builtin_hmac_sha256(interp, args),
        _ => Err(crypto_err(interp, format!("unknown crypto builtin: {}", name))),
    }
}

fn crypto_err(interp: &Interpreter, msg: String) -> HexaError {
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

// ── SHA-256 (pure Rust implementation) ──

const SHA256_K: [u32; 64] = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

fn sha256_hash(data: &[u8]) -> [u8; 32] {
    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    ];

    // Pre-processing: padding
    let bit_len = (data.len() as u64) * 8;
    let mut msg = data.to_vec();
    msg.push(0x80);
    while (msg.len() % 64) != 56 {
        msg.push(0x00);
    }
    msg.extend_from_slice(&bit_len.to_be_bytes());

    // Process each 512-bit (64-byte) block
    for chunk in msg.chunks(64) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes([chunk[i*4], chunk[i*4+1], chunk[i*4+2], chunk[i*4+3]]);
        }
        for i in 16..64 {
            let s0 = w[i-15].rotate_right(7) ^ w[i-15].rotate_right(18) ^ (w[i-15] >> 3);
            let s1 = w[i-2].rotate_right(17) ^ w[i-2].rotate_right(19) ^ (w[i-2] >> 10);
            w[i] = w[i-16].wrapping_add(s0).wrapping_add(w[i-7]).wrapping_add(s1);
        }

        let mut a = h[0]; let mut b = h[1]; let mut c = h[2]; let mut d = h[3];
        let mut e = h[4]; let mut f = h[5]; let mut g = h[6]; let mut hh = h[7];

        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let temp1 = hh.wrapping_add(s1).wrapping_add(ch).wrapping_add(SHA256_K[i]).wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = s0.wrapping_add(maj);

            hh = g; g = f; f = e; e = d.wrapping_add(temp1);
            d = c; c = b; b = a; a = temp1.wrapping_add(temp2);
        }

        h[0] = h[0].wrapping_add(a); h[1] = h[1].wrapping_add(b);
        h[2] = h[2].wrapping_add(c); h[3] = h[3].wrapping_add(d);
        h[4] = h[4].wrapping_add(e); h[5] = h[5].wrapping_add(f);
        h[6] = h[6].wrapping_add(g); h[7] = h[7].wrapping_add(hh);
    }

    let mut result = [0u8; 32];
    for (i, &val) in h.iter().enumerate() {
        result[i*4..i*4+4].copy_from_slice(&val.to_be_bytes());
    }
    result
}

fn sha256_hex(data: &[u8]) -> String {
    sha256_hash(data).iter().map(|b| format!("{:02x}", b)).collect()
}

fn builtin_sha256(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    match args.first() {
        Some(Value::Str(s)) => Ok(Value::Str(sha256_hex(s.as_bytes()))),
        Some(_) => Err(type_err(interp, "sha256() requires string argument".into())),
        None => Err(type_err(interp, "sha256() requires 1 argument".into())),
    }
}

// ── XOR cipher ──

fn builtin_xor_cipher(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let data = match args.first() {
        Some(Value::Str(s)) => s.as_bytes().to_vec(),
        Some(_) => return Err(type_err(interp, "xor_cipher() first argument must be string".into())),
        None => return Err(type_err(interp, "xor_cipher() requires 2 arguments".into())),
    };
    let key = match args.get(1) {
        Some(Value::Str(s)) if !s.is_empty() => s.as_bytes().to_vec(),
        Some(_) => return Err(type_err(interp, "xor_cipher() key must be non-empty string".into())),
        None => return Err(type_err(interp, "xor_cipher() requires 2 arguments".into())),
    };

    let result: Vec<u8> = data.iter().enumerate()
        .map(|(i, &b)| b ^ key[i % key.len()])
        .collect();

    // Return as hex string (binary safe)
    let hex: String = result.iter().map(|b| format!("{:02x}", b)).collect();
    Ok(Value::Str(hex))
}

// ── DJB2 hash ──

fn builtin_hash_djb2(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    match args.first() {
        Some(Value::Str(s)) => {
            let mut hash: u64 = 5381;
            for &b in s.as_bytes() {
                hash = hash.wrapping_mul(33).wrapping_add(b as u64);
            }
            Ok(Value::Int(hash as i64))
        }
        Some(_) => Err(type_err(interp, "hash_djb2() requires string argument".into())),
        None => Err(type_err(interp, "hash_djb2() requires 1 argument".into())),
    }
}

// ── random_bytes(n) — simple PRNG (not crypto-safe) ──

fn builtin_random_bytes(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let n = match args.first() {
        Some(Value::Int(n)) if *n > 0 => *n as usize,
        Some(_) => return Err(type_err(interp, "random_bytes() requires positive int".into())),
        None => return Err(type_err(interp, "random_bytes() requires 1 argument".into())),
    };

    // Use time-based seed for basic randomness
    let seed = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos() as u64;

    let mut state = seed;
    let bytes: Vec<Value> = (0..n).map(|_| {
        // xorshift64
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        Value::Int((state & 0xFF) as i64)
    }).collect();

    Ok(Value::Array(bytes))
}

// ── HMAC-SHA256 ──

fn builtin_hmac_sha256(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let key = match args.first() {
        Some(Value::Str(s)) => s.as_bytes().to_vec(),
        Some(_) => return Err(type_err(interp, "hmac_sha256() first argument must be string".into())),
        None => return Err(type_err(interp, "hmac_sha256() requires 2 arguments".into())),
    };
    let data = match args.get(1) {
        Some(Value::Str(s)) => s.as_bytes().to_vec(),
        Some(_) => return Err(type_err(interp, "hmac_sha256() second argument must be string".into())),
        None => return Err(type_err(interp, "hmac_sha256() requires 2 arguments".into())),
    };

    // HMAC: H((K' XOR opad) || H((K' XOR ipad) || message))
    let block_size = 64;
    let key_prime = if key.len() > block_size {
        sha256_hash(&key).to_vec()
    } else {
        let mut k = key.clone();
        k.resize(block_size, 0);
        k
    };

    let mut key_padded = key_prime.clone();
    key_padded.resize(block_size, 0);

    let mut ipad = vec![0x36u8; block_size];
    let mut opad = vec![0x5cu8; block_size];
    for i in 0..block_size {
        ipad[i] ^= key_padded[i];
        opad[i] ^= key_padded[i];
    }

    let mut inner = ipad;
    inner.extend_from_slice(&data);
    let inner_hash = sha256_hash(&inner);

    let mut outer = opad;
    outer.extend_from_slice(&inner_hash);
    let result = sha256_hex(&outer);

    Ok(Value::Str(result))
}

// ── Tests ──

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sha256_empty() {
        let hash = sha256_hex(b"");
        assert_eq!(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    }

    #[test]
    fn test_sha256_hello() {
        let hash = sha256_hex(b"hello");
        assert_eq!(hash, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
    }

    #[test]
    fn test_sha256_abc() {
        let hash = sha256_hex(b"abc");
        assert_eq!(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }

    #[test]
    fn test_xor_cipher_roundtrip() {
        let data = b"Hello HEXA";
        let key = b"secret";
        let encrypted: Vec<u8> = data.iter().enumerate()
            .map(|(i, &b)| b ^ key[i % key.len()])
            .collect();
        let decrypted: Vec<u8> = encrypted.iter().enumerate()
            .map(|(i, &b)| b ^ key[i % key.len()])
            .collect();
        assert_eq!(&decrypted, data);
    }

    #[test]
    fn test_djb2() {
        let mut hash: u64 = 5381;
        for &b in b"hello" {
            hash = hash.wrapping_mul(33).wrapping_add(b as u64);
        }
        assert_eq!(hash, 210714636441); // known DJB2 for "hello"
    }

    #[test]
    fn test_hmac_sha256() {
        // Test vector: HMAC-SHA256("key", "The quick brown fox jumps over the lazy dog")
        // Known result: f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8
        let key = b"key";
        let data = b"The quick brown fox jumps over the lazy dog";

        let block_size = 64;
        let mut key_padded = key.to_vec();
        key_padded.resize(block_size, 0);

        let mut ipad = vec![0x36u8; block_size];
        let mut opad = vec![0x5cu8; block_size];
        for i in 0..block_size {
            ipad[i] ^= key_padded[i];
            opad[i] ^= key_padded[i];
        }

        let mut inner = ipad;
        inner.extend_from_slice(data);
        let inner_hash = sha256_hash(&inner);

        let mut outer = opad;
        outer.extend_from_slice(&inner_hash);
        let result = sha256_hex(&outer);

        assert_eq!(result, "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
    }
}
