//! std::net — TCP and basic HTTP networking builtins for HEXA-LANG.
//!
//! Provides:
//!   net_listen(addr)       — bind a TCP listener
//!   net_accept(listener)   — accept one connection
//!   net_connect(addr)      — connect as TCP client
//!   net_read(conn)         — read data from a connection
//!   net_write(conn, data)  — write data to a connection
//!   net_close(conn)        — shutdown + drop a connection
//!   http_get(url)          — simple HTTP/1.1 GET over raw TCP
//!   http_serve(addr, handler) — single-threaded HTTP server

#![allow(dead_code)]

use std::collections::HashMap;
use std::io::{Read, Write, BufRead, BufReader};
use std::net::{TcpListener, TcpStream, Shutdown};
use std::sync::{Arc, Mutex};

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch a networking builtin by name.  Called from `Interpreter::call_builtin`.
pub fn call_net_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "net_listen" => builtin_net_listen(interp, args),
        "net_accept" => builtin_net_accept(interp, args),
        "net_connect" => builtin_net_connect(interp, args),
        "net_read" => builtin_net_read(interp, args),
        "net_write" => builtin_net_write(interp, args),
        "net_close" => builtin_net_close(interp, args),
        "http_get" => builtin_http_get(interp, args),
        "http_serve" => builtin_http_serve(interp, args),
        _ => Err(net_err(interp, format!("unknown net builtin: {}", name))),
    }
}

// ── Helpers ─────────────────────────────────────────────────

fn net_err(interp: &Interpreter, msg: String) -> HexaError {
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

// ── net_listen(addr: string) -> TcpListener ──

fn builtin_net_listen(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(type_err(interp, "net_listen() requires 1 argument (address string)".into()));
    }
    match &args[0] {
        Value::Str(addr) => {
            let listener = TcpListener::bind(addr.as_str())
                .map_err(|e| net_err(interp, format!("net_listen failed: {}", e)))?;
            Ok(Value::TcpListener(Arc::new(Mutex::new(listener))))
        }
        _ => Err(type_err(interp, "net_listen() requires string address".into())),
    }
}

// ── net_accept(listener) -> TcpStream ──

fn builtin_net_accept(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(type_err(interp, "net_accept() requires 1 argument (listener)".into()));
    }
    match &args[0] {
        Value::TcpListener(listener) => {
            let guard = listener.lock().unwrap();
            let (stream, _addr) = guard.accept()
                .map_err(|e| net_err(interp, format!("net_accept failed: {}", e)))?;
            Ok(Value::TcpStream(Arc::new(Mutex::new(stream))))
        }
        _ => Err(type_err(interp, "net_accept() requires a TcpListener".into())),
    }
}

// ── net_connect(addr: string) -> TcpStream ──

fn builtin_net_connect(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(type_err(interp, "net_connect() requires 1 argument (address string)".into()));
    }
    match &args[0] {
        Value::Str(addr) => {
            let stream = TcpStream::connect(addr.as_str())
                .map_err(|e| net_err(interp, format!("net_connect failed: {}", e)))?;
            Ok(Value::TcpStream(Arc::new(Mutex::new(stream))))
        }
        _ => Err(type_err(interp, "net_connect() requires string address".into())),
    }
}

// ── net_read(conn) -> string ──

fn builtin_net_read(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(type_err(interp, "net_read() requires 1 argument (connection)".into()));
    }
    match &args[0] {
        Value::TcpStream(stream) => {
            let mut guard = stream.lock().unwrap();
            let mut buf = [0u8; 4096];
            let n = guard.read(&mut buf)
                .map_err(|e| net_err(interp, format!("net_read failed: {}", e)))?;
            let data = String::from_utf8_lossy(&buf[..n]).to_string();
            Ok(Value::Str(data))
        }
        _ => Err(type_err(interp, "net_read() requires a TcpStream".into())),
    }
}

// ── net_write(conn, data: string) ──

fn builtin_net_write(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(type_err(interp, "net_write() requires 2 arguments (connection, data)".into()));
    }
    match (&args[0], &args[1]) {
        (Value::TcpStream(stream), Value::Str(data)) => {
            let mut guard = stream.lock().unwrap();
            guard.write_all(data.as_bytes())
                .map_err(|e| net_err(interp, format!("net_write failed: {}", e)))?;
            guard.flush()
                .map_err(|e| net_err(interp, format!("net_write flush failed: {}", e)))?;
            Ok(Value::Void)
        }
        _ => Err(type_err(interp, "net_write() requires (TcpStream, string)".into())),
    }
}

// ── net_close(conn) ──

fn builtin_net_close(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(type_err(interp, "net_close() requires 1 argument (connection)".into()));
    }
    match &args[0] {
        Value::TcpStream(stream) => {
            let guard = stream.lock().unwrap();
            let _ = guard.shutdown(Shutdown::Both);
            Ok(Value::Void)
        }
        _ => Err(type_err(interp, "net_close() requires a TcpStream".into())),
    }
}

// ── http_get(url: string) -> string ──
//
// Minimal HTTP/1.1 GET using raw TCP.  Supports `http://host[:port]/path` only.

fn builtin_http_get(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(type_err(interp, "http_get() requires 1 argument (url string)".into()));
    }
    match &args[0] {
        Value::Str(url) => {
            let (host, port, path) = parse_http_url(url)
                .map_err(|e| net_err(interp, format!("http_get: {}", e)))?;

            let addr = format!("{}:{}", host, port);
            let mut stream = TcpStream::connect(&addr)
                .map_err(|e| net_err(interp, format!("http_get connect failed: {}", e)))?;

            let request = format!(
                "GET {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n",
                path, host
            );
            stream.write_all(request.as_bytes())
                .map_err(|e| net_err(interp, format!("http_get write failed: {}", e)))?;

            let mut response = String::new();
            stream.read_to_string(&mut response)
                .map_err(|e| net_err(interp, format!("http_get read failed: {}", e)))?;

            // Return the full raw response (headers + body).
            // Users can split on "\r\n\r\n" to separate headers from body.
            Ok(Value::Str(response))
        }
        _ => Err(type_err(interp, "http_get() requires string url".into())),
    }
}

/// Parse `http://host[:port]/path` into (host, port, path).
fn parse_http_url(url: &str) -> Result<(String, u16, String), String> {
    let rest = url.strip_prefix("http://")
        .ok_or_else(|| "url must start with http://".to_string())?;

    let (host_port, path) = match rest.find('/') {
        Some(i) => (&rest[..i], &rest[i..]),
        None => (rest, "/"),
    };

    let (host, port) = match host_port.find(':') {
        Some(i) => {
            let p = host_port[i + 1..].parse::<u16>()
                .map_err(|_| "invalid port number".to_string())?;
            (&host_port[..i], p)
        }
        None => (host_port, 80),
    };

    Ok((host.to_string(), port, path.to_string()))
}

// ── http_serve(addr, handler_fn) ──
//
// Accepts connections in a loop and calls `handler(request_map) -> response_string`.
// The request_map has keys: method, path, headers (map), body.
// Runs until the handler returns the string "__stop__".

fn builtin_http_serve(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(type_err(interp, "http_serve() requires 2 arguments (addr, handler_fn)".into()));
    }

    let addr = match &args[0] {
        Value::Str(s) => s.clone(),
        _ => return Err(type_err(interp, "http_serve() first argument must be string address".into())),
    };

    let handler = args[1].clone();

    // Verify callable
    match &handler {
        Value::Fn(..) | Value::Lambda(..) | Value::BuiltinFn(_) => {}
        _ => return Err(type_err(interp, "http_serve() second argument must be a function".into())),
    }

    let listener = TcpListener::bind(&addr)
        .map_err(|e| net_err(interp, format!("http_serve bind failed: {}", e)))?;

    loop {
        let (mut stream, _peer) = match listener.accept() {
            Ok(conn) => conn,
            Err(e) => {
                // Non-fatal: log and continue
                eprintln!("http_serve accept error: {}", e);
                continue;
            }
        };

        // Read the request
        let request_map = match read_http_request(&mut stream) {
            Ok(m) => m,
            Err(_) => continue,
        };

        // Call the handler
        let request_val = Value::Map(request_map);
        let response = interp.call_function(handler.clone(), vec![request_val])?;

        let response_str = match &response {
            Value::Str(s) => s.clone(),
            other => format!("{}", other),
        };

        // Check for stop sentinel
        if response_str == "__stop__" {
            break;
        }

        // Send HTTP response
        let http_response = format!(
            "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n{}",
            response_str.len(),
            response_str
        );
        let _ = stream.write_all(http_response.as_bytes());
        let _ = stream.flush();
        let _ = stream.shutdown(Shutdown::Both);
    }

    Ok(Value::Void)
}

/// Read and parse an HTTP/1.1 request from a stream into a Value::Map.
fn read_http_request(stream: &mut TcpStream) -> Result<HashMap<String, Value>, std::io::Error> {
    let mut reader = BufReader::new(stream.try_clone()?);
    let mut request_line = String::new();
    reader.read_line(&mut request_line)?;

    let parts: Vec<&str> = request_line.trim().splitn(3, ' ').collect();
    let method = parts.first().unwrap_or(&"GET").to_string();
    let path = parts.get(1).unwrap_or(&"/").to_string();

    // Read headers
    let mut headers = HashMap::new();
    let mut content_length: usize = 0;
    loop {
        let mut line = String::new();
        reader.read_line(&mut line)?;
        let trimmed = line.trim();
        if trimmed.is_empty() {
            break;
        }
        if let Some((key, val)) = trimmed.split_once(':') {
            let key_lower = key.trim().to_lowercase();
            let val_trimmed = val.trim().to_string();
            if key_lower == "content-length" {
                content_length = val_trimmed.parse().unwrap_or(0);
            }
            headers.insert(key.trim().to_string(), Value::Str(val_trimmed));
        }
    }

    // Read body if present
    let body = if content_length > 0 {
        let mut buf = vec![0u8; content_length];
        reader.read_exact(&mut buf)?;
        String::from_utf8_lossy(&buf).to_string()
    } else {
        String::new()
    };

    let mut map = HashMap::new();
    map.insert("method".into(), Value::Str(method));
    map.insert("path".into(), Value::Str(path));
    map.insert("headers".into(), Value::Map(headers));
    map.insert("body".into(), Value::Str(body));
    Ok(map)
}

/// Parse a raw HTTP request string into a map (for testing / scripting).
pub fn parse_http_request_string(raw: &str) -> HashMap<String, Value> {
    let mut lines = raw.lines();

    let request_line = lines.next().unwrap_or("GET / HTTP/1.1");
    let parts: Vec<&str> = request_line.splitn(3, ' ').collect();
    let method = parts.first().unwrap_or(&"GET").to_string();
    let path = parts.get(1).unwrap_or(&"/").to_string();

    let mut headers = HashMap::new();
    let mut body_start = false;
    let mut body_lines = Vec::new();

    for line in lines {
        if body_start {
            body_lines.push(line);
            continue;
        }
        if line.is_empty() {
            body_start = true;
            continue;
        }
        if let Some((key, val)) = line.split_once(':') {
            headers.insert(key.trim().to_string(), Value::Str(val.trim().to_string()));
        }
    }

    let mut map = HashMap::new();
    map.insert("method".into(), Value::Str(method));
    map.insert("path".into(), Value::Str(path));
    map.insert("headers".into(), Value::Map(headers));
    map.insert("body".into(), Value::Str(body_lines.join("\n")));
    map
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_http_url() {
        let (h, p, path) = parse_http_url("http://example.com/foo").unwrap();
        assert_eq!(h, "example.com");
        assert_eq!(p, 80);
        assert_eq!(path, "/foo");

        let (h, p, path) = parse_http_url("http://localhost:8080/bar/baz").unwrap();
        assert_eq!(h, "localhost");
        assert_eq!(p, 8080);
        assert_eq!(path, "/bar/baz");

        let (h, p, path) = parse_http_url("http://127.0.0.1").unwrap();
        assert_eq!(h, "127.0.0.1");
        assert_eq!(p, 80);
        assert_eq!(path, "/");

        assert!(parse_http_url("https://secure.com").is_err());
    }

    #[test]
    fn test_parse_http_request_string() {
        let raw = "POST /api/data HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\n\r\n{\"key\": \"value\"}";
        let map = parse_http_request_string(raw);

        match map.get("method") {
            Some(Value::Str(s)) => assert_eq!(s, "POST"),
            other => panic!("expected method=POST, got {:?}", other),
        }
        match map.get("path") {
            Some(Value::Str(s)) => assert_eq!(s, "/api/data"),
            other => panic!("expected path=/api/data, got {:?}", other),
        }
        match map.get("body") {
            Some(Value::Str(s)) => assert_eq!(s, "{\"key\": \"value\"}"),
            other => panic!("expected body, got {:?}", other),
        }

        if let Some(Value::Map(headers)) = map.get("headers") {
            match headers.get("Host") {
                Some(Value::Str(s)) => assert_eq!(s, "localhost"),
                other => panic!("expected Host header, got {:?}", other),
            }
            match headers.get("Content-Type") {
                Some(Value::Str(s)) => assert_eq!(s, "application/json"),
                other => panic!("expected Content-Type header, got {:?}", other),
            }
        } else {
            panic!("expected headers map");
        }
    }

    #[test]
    fn test_tcp_echo() {
        use std::thread;

        // Bind to a random port
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let addr = listener.local_addr().unwrap();

        // Spawn echo server thread
        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut buf = [0u8; 256];
            let n = stream.read(&mut buf).unwrap();
            stream.write_all(&buf[..n]).unwrap();
            stream.flush().unwrap();
        });

        // Client: connect, send, receive
        let mut client = TcpStream::connect(addr).unwrap();
        let msg = b"hello hexa";
        client.write_all(msg).unwrap();
        client.flush().unwrap();
        // Shutdown write side so server knows we're done
        client.shutdown(Shutdown::Write).unwrap();

        let mut response = String::new();
        client.read_to_string(&mut response).unwrap();
        assert_eq!(response, "hello hexa");

        server.join().unwrap();
    }

    #[test]
    fn test_http_request_parse_get() {
        let raw = "GET /index.html HTTP/1.1\r\nHost: example.com\r\nAccept: text/html\r\n\r\n";
        let map = parse_http_request_string(raw);
        match map.get("method") {
            Some(Value::Str(s)) => assert_eq!(s, "GET"),
            other => panic!("expected GET, got {:?}", other),
        }
        match map.get("path") {
            Some(Value::Str(s)) => assert_eq!(s, "/index.html"),
            other => panic!("expected /index.html, got {:?}", other),
        }
        match map.get("body") {
            Some(Value::Str(s)) => assert!(s.is_empty()),
            other => panic!("expected empty body, got {:?}", other),
        }
    }
}
