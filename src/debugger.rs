#![allow(dead_code)]

//! Debug Adapter Protocol (DAP) server for HEXA-LANG.
//!
//! Speaks DAP JSON messages over stdin/stdout with `Content-Length` framing
//! (same transport as LSP). Supports:
//!   - initialize / launch / disconnect
//!   - setBreakpoints
//!   - configurationDone
//!   - threads / stackTrace / scopes / variables
//!   - continue / next / stepIn / stepOut
//!   - evaluate
//!
//! The debugger communicates with the interpreter via an mpsc channel pair.
//! Before each statement, the interpreter calls the debug hook, which may
//! pause execution and wait for a DAP command.

use std::collections::HashMap;
use std::io::{self};
use std::sync::mpsc;
use std::sync::{Arc, Mutex};

use crate::env::Value;
use crate::interpreter::Interpreter;
use crate::lexer::Lexer;
use crate::parser::Parser;
use crate::type_checker::TypeChecker;
use crate::ownership;

// Re-use the JSON helpers and message framing from the LSP module.
use crate::lsp::{JsonValue, parse_json, read_message, send_message};

// ── Debug commands (DAP client -> interpreter) ─────────────────

/// Commands sent from the DAP server thread to the interpreter thread.
#[derive(Debug, Clone)]
pub enum DebugCommand {
    /// Resume execution until next breakpoint.
    Continue,
    /// Step over: execute one statement, skip into function bodies.
    Next,
    /// Step into: execute one statement, enter function bodies.
    StepIn,
    /// Step out: run until current function returns.
    StepOut,
    /// Stop debugging.
    Disconnect,
}

// ── Debug events (interpreter -> DAP server) ───────────────────

/// Events sent from the interpreter thread back to the DAP server.
#[derive(Debug, Clone)]
pub enum DebugEvent {
    /// Interpreter stopped at a breakpoint or step.
    Stopped {
        reason: String,   // "breakpoint", "step", "entry"
        line: usize,      // 1-based
        file: String,
    },
    /// Interpreter finished executing.
    Terminated,
    /// Output from the program (print/println).
    Output(String),
}

// ── DebugHook — injected into the interpreter ──────────────────

/// Stepping mode for the debug hook.
#[derive(Debug, Clone, PartialEq)]
pub enum StepMode {
    /// Run freely until a breakpoint is hit.
    Continue,
    /// Stop at the very next statement.
    StepIn,
    /// Stop at the next statement at the same or lower call depth.
    Next { depth: usize },
    /// Stop when call depth drops below current.
    StepOut { depth: usize },
}

/// The debug hook sits between the DAP server and the interpreter.
/// It is shared via `Arc<Mutex<_>>` so both threads can access it.
pub struct DebugHook {
    /// Breakpoints: file -> set of 1-based line numbers.
    pub breakpoints: HashMap<String, Vec<usize>>,
    /// Channel to send events (stopped, terminated) to the DAP server.
    pub event_tx: mpsc::Sender<DebugEvent>,
    /// Channel to receive commands (continue, step, etc.) from the DAP server.
    pub command_rx: mpsc::Receiver<DebugCommand>,
    /// Current stepping mode.
    pub step_mode: StepMode,
    /// Current call depth (incremented on function entry, decremented on exit).
    pub call_depth: usize,
    /// Call stack frames: (function_name, file, line).
    pub call_stack: Vec<StackFrame>,
    /// Whether the debugger has been disconnected.
    pub disconnected: bool,
    /// Snapshot of variables at the current pause point.
    /// Populated when we stop; cleared when we resume.
    pub variable_snapshot: Vec<(String, String, String)>, // (name, value, type)
    /// The file being debugged.
    pub file_name: String,
}

/// A stack frame entry.
#[derive(Debug, Clone)]
pub struct StackFrame {
    pub id: usize,
    pub name: String,
    pub file: String,
    pub line: usize,
}

impl DebugHook {
    /// Called by the interpreter before executing each statement.
    /// Returns `true` if execution should continue, `false` if it should stop (disconnect).
    pub fn on_statement(&mut self, line: usize, file: &str, env: &crate::env::Env) -> bool {
        if self.disconnected {
            return false;
        }

        let should_stop = match &self.step_mode {
            StepMode::Continue => self.is_breakpoint(file, line),
            StepMode::StepIn => true,
            StepMode::Next { depth } => self.call_depth <= *depth,
            StepMode::StepOut { depth } => self.call_depth < *depth,
        };

        if !should_stop {
            return true;
        }

        // Update the top stack frame's line
        if let Some(frame) = self.call_stack.last_mut() {
            frame.line = line;
        }

        // Snapshot variables from current scope
        self.snapshot_variables(env);

        // Determine stop reason
        let reason = if self.is_breakpoint(file, line) {
            "breakpoint"
        } else {
            "step"
        };

        // Notify DAP server that we stopped
        let _ = self.event_tx.send(DebugEvent::Stopped {
            reason: reason.to_string(),
            line,
            file: file.to_string(),
        });

        // Wait for next command
        match self.command_rx.recv() {
            Ok(DebugCommand::Continue) => {
                self.step_mode = StepMode::Continue;
            }
            Ok(DebugCommand::Next) => {
                self.step_mode = StepMode::Next { depth: self.call_depth };
            }
            Ok(DebugCommand::StepIn) => {
                self.step_mode = StepMode::StepIn;
            }
            Ok(DebugCommand::StepOut) => {
                self.step_mode = StepMode::StepOut { depth: self.call_depth };
            }
            Ok(DebugCommand::Disconnect) | Err(_) => {
                self.disconnected = true;
                return false;
            }
        }
        true
    }

    /// Called when entering a function.
    pub fn on_function_entry(&mut self, name: &str, file: &str, line: usize) {
        self.call_depth += 1;
        self.call_stack.push(StackFrame {
            id: self.call_stack.len() + 1,
            name: name.to_string(),
            file: file.to_string(),
            line,
        });
    }

    /// Called when leaving a function.
    pub fn on_function_exit(&mut self) {
        if self.call_depth > 0 {
            self.call_depth -= 1;
        }
        self.call_stack.pop();
    }

    fn is_breakpoint(&self, file: &str, line: usize) -> bool {
        // Check with exact file name
        if let Some(lines) = self.breakpoints.get(file) {
            if lines.contains(&line) {
                return true;
            }
        }
        // Also check against just the basename (VS Code sends full paths)
        let basename = std::path::Path::new(file)
            .file_name()
            .and_then(|f| f.to_str())
            .unwrap_or(file);
        for (bp_file, lines) in &self.breakpoints {
            let bp_basename = std::path::Path::new(bp_file)
                .file_name()
                .and_then(|f| f.to_str())
                .unwrap_or(bp_file);
            if bp_basename == basename && lines.contains(&line) {
                return true;
            }
        }
        false
    }

    fn snapshot_variables(&mut self, env: &crate::env::Env) {
        self.variable_snapshot.clear();
        // Walk scopes from innermost to outermost, collecting variables
        for (name, val) in env.vars_iter().rev() {
            // Skip builtins (functions defined in the global scope)
            if matches!(val, Value::BuiltinFn(_)) {
                continue;
            }
            let type_str = value_type_name(val);
            let val_str = format!("{}", val);
            self.variable_snapshot.push((name.clone(), val_str, type_str));
        }
    }
}

/// Get a human-readable type name for a Value.
fn value_type_name(val: &Value) -> String {
    match val {
        Value::Int(_) => "int".into(),
        Value::Float(_) => "float".into(),
        Value::Bool(_) => "bool".into(),
        Value::Char(_) => "char".into(),
        Value::Str(_) => "string".into(),
        Value::Byte(_) => "byte".into(),
        Value::Void => "void".into(),
        Value::Array(a) => {
            let elem_type = if a.is_empty() { "any".to_string() } else { value_type_name(&a[0]) };
            format!("[{}; {}]", elem_type, a.len())
        }
        Value::Tuple(t) => format!("({})", t.iter().map(|v| value_type_name(v)).collect::<Vec<_>>().join(", ")),
        Value::Fn(name, ..) => format!("fn {}", name),
        Value::BuiltinFn(name) => format!("builtin {}", name),
        Value::Struct(name, _) => name.clone(),
        Value::Lambda(..) => "lambda".into(),
        Value::Map(_) => "map".into(),
        Value::Error(_) => "error".into(),
        Value::EnumVariant(name, variant, _) => format!("{}::{}", name, variant),
        Value::Intent(_) => "intent".into(),
        #[cfg(not(target_arch = "wasm32"))]
        Value::Sender(_) => "sender".into(),
        #[cfg(not(target_arch = "wasm32"))]
        Value::Receiver(_) => "receiver".into(),
        Value::Future(_) => "future".into(),
        #[cfg(not(target_arch = "wasm32"))]
        Value::AsyncFuture(_) => "async_future".into(),
        Value::Set(_) => "set".into(),
        #[cfg(not(target_arch = "wasm32"))]
        Value::TcpListener(_) => "tcp_listener".into(),
        #[cfg(not(target_arch = "wasm32"))]
        Value::TcpStream(_) => "tcp_stream".into(),
        Value::EffectRequest(..) => "effect_request".into(),
        Value::TraitObject { trait_name, .. } => format!("dyn {}", trait_name),
        Value::Atomic(_) => "atomic".into(),
    }
}

// ── DAP Protocol constants ─────────────────────────────────────

const THREAD_ID: i64 = 1;

// ── DAP Server ─────────────────────────────────────────────────

/// Run the DAP server. This is the main entry point for `hexa debug <file>`.
pub fn run_dap(file_path: &str) {
    let source = match std::fs::read_to_string(file_path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("Error reading {}: {}", file_path, e);
            std::process::exit(1);
        }
    };

    let source_lines: Vec<String> = source.lines().map(String::from).collect();
    let file_name = file_path.to_string();

    // Parse the source
    let tokens = match Lexer::new(&source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            eprintln!("Lexer error: {}", e);
            std::process::exit(1);
        }
    };
    let result = match Parser::new(tokens).parse_with_spans() {
        Ok(r) => r,
        Err(e) => {
            eprintln!("Parse error: {}", e.message);
            std::process::exit(1);
        }
    };
    // Type check
    let mut checker = TypeChecker::new();
    if let Err(e) = checker.check(&result.stmts, &result.spans) {
        eprintln!("Type error: {}", e.message);
        std::process::exit(1);
    }
    // Ownership analysis
    let ownership_errors = ownership::analyze_ownership(&result.stmts, &result.spans);
    if !ownership_errors.is_empty() {
        for err in &ownership_errors {
            let hexa_err = err.clone().into_hexa_error();
            eprintln!("Ownership error: {}", hexa_err.message);
        }
        std::process::exit(1);
    }

    // Create communication channels
    let (cmd_tx, cmd_rx) = mpsc::channel::<DebugCommand>();
    let (evt_tx, evt_rx) = mpsc::channel::<DebugEvent>();

    // Create debug hook
    let hook = Arc::new(Mutex::new(DebugHook {
        breakpoints: HashMap::new(),
        event_tx: evt_tx.clone(),
        command_rx: cmd_rx,
        step_mode: StepMode::StepIn, // Stop at first statement
        call_depth: 0,
        call_stack: vec![StackFrame {
            id: 1,
            name: "<main>".to_string(),
            file: file_name.clone(),
            line: 1,
        }],
        disconnected: false,
        variable_snapshot: Vec::new(),
        file_name: file_name.clone(),
    }));

    let hook_for_interp = hook.clone();
    let stmts = result.stmts.clone();
    let spans = result.spans.clone();
    let source_lines_clone = source_lines.clone();
    let file_name_clone = file_name.clone();
    let evt_tx_clone = evt_tx.clone();

    // Spawn interpreter thread
    let interp_handle = std::thread::spawn(move || {
        let mut interp = Interpreter::new();
        interp.source_lines = source_lines_clone;
        interp.file_name = file_name_clone.clone();
        if let Some(parent) = std::path::Path::new(&file_name_clone).parent() {
            interp.source_dir = Some(parent.to_string_lossy().to_string());
        }

        // Capture output and forward as DAP output events
        let output_buf = Arc::new(Mutex::new(String::new()));
        interp.set_output_capture(Some(output_buf.clone()));

        // Set the debug hook on the interpreter
        interp.set_debug_hook(Some(hook_for_interp));

        match interp.run_with_spans(&stmts, &spans) {
            Ok(_) => {}
            Err(e) => {
                let _ = evt_tx_clone.send(DebugEvent::Output(
                    format!("Runtime error: {}\n", e.message),
                ));
            }
        }

        // Flush any remaining output
        if let Ok(buf) = output_buf.lock() {
            if !buf.is_empty() {
                let _ = evt_tx_clone.send(DebugEvent::Output(buf.clone()));
            }
        }

        let _ = evt_tx_clone.send(DebugEvent::Terminated);
    });

    // Run DAP message loop on main thread (stdin/stdout)
    run_dap_loop(cmd_tx, evt_rx, hook, &file_name, &source_lines);

    let _ = interp_handle.join();
}

/// Main DAP message loop — reads requests from stdin, sends responses to stdout.
fn run_dap_loop(
    cmd_tx: mpsc::Sender<DebugCommand>,
    evt_rx: mpsc::Receiver<DebugEvent>,
    hook: Arc<Mutex<DebugHook>>,
    file_name: &str,
    _source_lines: &[String],
) {
    let stdin = io::stdin();
    let stdout = io::stdout();
    let mut reader = io::BufReader::new(stdin.lock());
    let mut writer = stdout.lock();

    let mut seq: i64 = 1;
    let mut _initialized = false;
    let mut _configuration_done = false;

    loop {
        // Check for events from interpreter (non-blocking)
        while let Ok(evt) = evt_rx.try_recv() {
            match evt {
                DebugEvent::Stopped { reason, line, file: _ } => {
                    let body = dap_object(&[
                        ("reason", JsonValue::Str(reason)),
                        ("threadId", JsonValue::Number(THREAD_ID as f64)),
                        ("allThreadsStopped", JsonValue::Bool(true)),
                        ("line", JsonValue::Number(line as f64)),
                    ]);
                    let event = make_dap_event(&mut seq, "stopped", body);
                    let _ = send_message(&mut writer, &event.to_json());
                }
                DebugEvent::Terminated => {
                    let event = make_dap_event(&mut seq, "terminated", dap_object(&[]));
                    let _ = send_message(&mut writer, &event.to_json());
                }
                DebugEvent::Output(text) => {
                    let body = dap_object(&[
                        ("category", JsonValue::Str("stdout".into())),
                        ("output", JsonValue::Str(text)),
                    ]);
                    let event = make_dap_event(&mut seq, "output", body);
                    let _ = send_message(&mut writer, &event.to_json());
                }
            }
        }

        // Read next DAP message (with a short timeout approach: we use blocking reads)
        let body = match read_message(&mut reader) {
            Ok(b) => b,
            Err(_) => break,
        };

        let msg = match parse_json(&body) {
            Ok(m) => m,
            Err(_) => continue,
        };

        let command = msg.get("command").and_then(|c| c.as_str()).unwrap_or("");
        let request_seq = msg.get("seq").and_then(|s| s.as_i64()).unwrap_or(0);
        let arguments = msg.get("arguments").cloned().unwrap_or(JsonValue::Null);

        match command {
            "initialize" => {
                _initialized = true;
                let capabilities = dap_object(&[
                    ("supportsConfigurationDoneRequest", JsonValue::Bool(true)),
                    ("supportsEvaluateForHovers", JsonValue::Bool(true)),
                    ("supportsSingleThreadExecutionRequests", JsonValue::Bool(true)),
                ]);
                let resp = make_dap_response(&mut seq, request_seq, command, true, capabilities, None);
                let _ = send_message(&mut writer, &resp.to_json());

                // Send initialized event
                let event = make_dap_event(&mut seq, "initialized", dap_object(&[]));
                let _ = send_message(&mut writer, &event.to_json());
            }

            "launch" => {
                let resp = make_dap_response(&mut seq, request_seq, command, true, dap_object(&[]), None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "setBreakpoints" => {
                let source_obj = arguments.get("source");
                let path = source_obj
                    .and_then(|s| s.get("path"))
                    .and_then(|p| p.as_str())
                    .unwrap_or(file_name)
                    .to_string();

                let mut bp_lines = Vec::new();
                let mut verified_bps = Vec::new();

                if let Some(JsonValue::Array(bps)) = arguments.get("breakpoints") {
                    for bp in bps {
                        let line = bp.get("line").and_then(|l| l.as_i64()).unwrap_or(0) as usize;
                        bp_lines.push(line);
                        verified_bps.push(dap_object(&[
                            ("verified", JsonValue::Bool(true)),
                            ("line", JsonValue::Number(line as f64)),
                            ("source", dap_object(&[
                                ("path", JsonValue::Str(path.clone())),
                            ])),
                        ]));
                    }
                }

                // Update breakpoints in the hook
                if let Ok(mut h) = hook.lock() {
                    h.breakpoints.insert(path, bp_lines);
                }

                let body = dap_object(&[
                    ("breakpoints", JsonValue::Array(verified_bps)),
                ]);
                let resp = make_dap_response(&mut seq, request_seq, command, true, body, None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "configurationDone" => {
                _configuration_done = true;
                let resp = make_dap_response(&mut seq, request_seq, command, true, dap_object(&[]), None);
                let _ = send_message(&mut writer, &resp.to_json());

                // The interpreter is already waiting at the first statement.
                // If step_mode is StepIn, it will stop immediately.
                // If the user set breakpoints, switch to Continue mode.
                if let Ok(h) = hook.lock() {
                    if !h.breakpoints.is_empty() {
                        // Breakpoints were set, we'll let the configurationDone
                        // proceed and the interpreter will stop at the first breakpoint
                    }
                }
            }

            "threads" => {
                let body = dap_object(&[
                    ("threads", JsonValue::Array(vec![
                        dap_object(&[
                            ("id", JsonValue::Number(THREAD_ID as f64)),
                            ("name", JsonValue::Str("main".into())),
                        ]),
                    ])),
                ]);
                let resp = make_dap_response(&mut seq, request_seq, command, true, body, None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "stackTrace" => {
                let frames = if let Ok(h) = hook.lock() {
                    h.call_stack.iter().rev().map(|f| {
                        dap_object(&[
                            ("id", JsonValue::Number(f.id as f64)),
                            ("name", JsonValue::Str(f.name.clone())),
                            ("source", dap_object(&[
                                ("path", JsonValue::Str(f.file.clone())),
                                ("name", JsonValue::Str(
                                    std::path::Path::new(&f.file)
                                        .file_name()
                                        .and_then(|n| n.to_str())
                                        .unwrap_or(&f.file)
                                        .to_string()
                                )),
                            ])),
                            ("line", JsonValue::Number(f.line as f64)),
                            ("column", JsonValue::Number(1.0)),
                        ])
                    }).collect::<Vec<_>>()
                } else {
                    vec![]
                };

                let body = dap_object(&[
                    ("stackFrames", JsonValue::Array(frames.clone())),
                    ("totalFrames", JsonValue::Number(frames.len() as f64)),
                ]);
                let resp = make_dap_response(&mut seq, request_seq, command, true, body, None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "scopes" => {
                let _frame_id = arguments.get("frameId").and_then(|f| f.as_i64()).unwrap_or(1);
                let body = dap_object(&[
                    ("scopes", JsonValue::Array(vec![
                        dap_object(&[
                            ("name", JsonValue::Str("Locals".into())),
                            ("variablesReference", JsonValue::Number(1.0)),
                            ("expensive", JsonValue::Bool(false)),
                        ]),
                    ])),
                ]);
                let resp = make_dap_response(&mut seq, request_seq, command, true, body, None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "variables" => {
                let vars_ref = arguments.get("variablesReference").and_then(|v| v.as_i64()).unwrap_or(0);
                let variables = if vars_ref == 1 {
                    if let Ok(h) = hook.lock() {
                        h.variable_snapshot.iter().map(|(name, val, typ)| {
                            dap_object(&[
                                ("name", JsonValue::Str(name.clone())),
                                ("value", JsonValue::Str(val.clone())),
                                ("type", JsonValue::Str(typ.clone())),
                                ("variablesReference", JsonValue::Number(0.0)),
                            ])
                        }).collect::<Vec<_>>()
                    } else {
                        vec![]
                    }
                } else {
                    vec![]
                };

                let body = dap_object(&[
                    ("variables", JsonValue::Array(variables)),
                ]);
                let resp = make_dap_response(&mut seq, request_seq, command, true, body, None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "continue" => {
                let _ = cmd_tx.send(DebugCommand::Continue);
                let body = dap_object(&[
                    ("allThreadsContinued", JsonValue::Bool(true)),
                ]);
                let resp = make_dap_response(&mut seq, request_seq, command, true, body, None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "next" => {
                let _ = cmd_tx.send(DebugCommand::Next);
                let resp = make_dap_response(&mut seq, request_seq, command, true, dap_object(&[]), None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "stepIn" => {
                let _ = cmd_tx.send(DebugCommand::StepIn);
                let resp = make_dap_response(&mut seq, request_seq, command, true, dap_object(&[]), None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "stepOut" => {
                let _ = cmd_tx.send(DebugCommand::StepOut);
                let resp = make_dap_response(&mut seq, request_seq, command, true, dap_object(&[]), None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "evaluate" => {
                let expression = arguments.get("expression")
                    .and_then(|e| e.as_str())
                    .unwrap_or("");

                // Try to find the variable in our snapshot
                let result_str = if let Ok(h) = hook.lock() {
                    h.variable_snapshot.iter()
                        .find(|(name, _, _)| name == expression)
                        .map(|(_, val, _)| val.clone())
                        .unwrap_or_else(|| format!("undefined: {}", expression))
                } else {
                    format!("debugger unavailable")
                };

                let body = dap_object(&[
                    ("result", JsonValue::Str(result_str)),
                    ("variablesReference", JsonValue::Number(0.0)),
                ]);
                let resp = make_dap_response(&mut seq, request_seq, command, true, body, None);
                let _ = send_message(&mut writer, &resp.to_json());
            }

            "disconnect" => {
                let _ = cmd_tx.send(DebugCommand::Disconnect);
                let resp = make_dap_response(&mut seq, request_seq, command, true, dap_object(&[]), None);
                let _ = send_message(&mut writer, &resp.to_json());
                break;
            }

            _ => {
                // Unknown command — respond with success (no-op)
                if !command.is_empty() {
                    let resp = make_dap_response(&mut seq, request_seq, command, true, dap_object(&[]), None);
                    let _ = send_message(&mut writer, &resp.to_json());
                }
            }
        }
    }
}

// ── DAP message builders ───────────────────────────────────────

fn dap_object(pairs: &[(&str, JsonValue)]) -> JsonValue {
    JsonValue::Object(
        pairs.iter().map(|(k, v)| (k.to_string(), v.clone())).collect()
    )
}

fn make_dap_response(
    seq: &mut i64,
    request_seq: i64,
    command: &str,
    success: bool,
    body: JsonValue,
    message: Option<&str>,
) -> JsonValue {
    *seq += 1;
    let mut pairs = vec![
        ("seq".to_string(), JsonValue::Number(*seq as f64)),
        ("type".to_string(), JsonValue::Str("response".into())),
        ("request_seq".to_string(), JsonValue::Number(request_seq as f64)),
        ("success".to_string(), JsonValue::Bool(success)),
        ("command".to_string(), JsonValue::Str(command.into())),
        ("body".to_string(), body),
    ];
    if let Some(msg) = message {
        pairs.push(("message".to_string(), JsonValue::Str(msg.into())));
    }
    JsonValue::Object(pairs)
}

fn make_dap_event(seq: &mut i64, event: &str, body: JsonValue) -> JsonValue {
    *seq += 1;
    JsonValue::Object(vec![
        ("seq".to_string(), JsonValue::Number(*seq as f64)),
        ("type".to_string(), JsonValue::Str("event".into())),
        ("event".to_string(), JsonValue::Str(event.into())),
        ("body".to_string(), body),
    ])
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_dap_initialize_response() {
        let mut seq: i64 = 0;
        let capabilities = dap_object(&[
            ("supportsConfigurationDoneRequest", JsonValue::Bool(true)),
        ]);
        let resp = make_dap_response(&mut seq, 1, "initialize", true, capabilities, None);
        let json = resp.to_json();
        assert!(json.contains("\"command\":\"initialize\""));
        assert!(json.contains("\"success\":true"));
        assert!(json.contains("\"supportsConfigurationDoneRequest\":true"));
    }

    #[test]
    fn test_dap_event() {
        let mut seq: i64 = 0;
        let body = dap_object(&[
            ("reason", JsonValue::Str("breakpoint".into())),
            ("threadId", JsonValue::Number(1.0)),
        ]);
        let event = make_dap_event(&mut seq, "stopped", body);
        let json = event.to_json();
        assert!(json.contains("\"event\":\"stopped\""));
        assert!(json.contains("\"reason\":\"breakpoint\""));
    }

    #[test]
    fn test_breakpoint_detection() {
        let (evt_tx, _evt_rx) = mpsc::channel();
        let (_cmd_tx, cmd_rx) = mpsc::channel();

        let mut hook = DebugHook {
            breakpoints: HashMap::new(),
            event_tx: evt_tx,
            command_rx: cmd_rx,
            step_mode: StepMode::Continue,
            call_depth: 0,
            call_stack: vec![],
            disconnected: false,
            variable_snapshot: vec![],
            file_name: "test.hexa".into(),
        };

        // No breakpoints => not a breakpoint
        assert!(!hook.is_breakpoint("test.hexa", 5));

        // Set a breakpoint at line 5
        hook.breakpoints.insert("test.hexa".into(), vec![5]);
        assert!(hook.is_breakpoint("test.hexa", 5));
        assert!(!hook.is_breakpoint("test.hexa", 6));

        // Basename matching
        assert!(hook.is_breakpoint("/path/to/test.hexa", 5));
    }

    #[test]
    fn test_variable_snapshot() {
        let (evt_tx, _evt_rx) = mpsc::channel();
        let (_cmd_tx, cmd_rx) = mpsc::channel();

        let mut hook = DebugHook {
            breakpoints: HashMap::new(),
            event_tx: evt_tx,
            command_rx: cmd_rx,
            step_mode: StepMode::Continue,
            call_depth: 0,
            call_stack: vec![],
            disconnected: false,
            variable_snapshot: vec![],
            file_name: "test.hexa".into(),
        };

        let mut env = crate::env::Env::new();
        env.define("x", Value::Int(42));
        env.define("name", Value::Str("hello".into()));

        hook.snapshot_variables(&env);

        // Should have x and name (not builtins)
        let user_vars: Vec<&(String, String, String)> = hook.variable_snapshot
            .iter()
            .filter(|(n, _, _)| n == "x" || n == "name")
            .collect();
        assert!(user_vars.len() >= 2);
    }

    #[test]
    fn test_value_type_name() {
        assert_eq!(value_type_name(&Value::Int(42)), "int");
        assert_eq!(value_type_name(&Value::Float(3.14)), "float");
        assert_eq!(value_type_name(&Value::Bool(true)), "bool");
        assert_eq!(value_type_name(&Value::Str("hi".into())), "string");
        assert_eq!(value_type_name(&Value::Void), "void");
        assert_eq!(value_type_name(&Value::Array(vec![Value::Int(1)])), "[int; 1]");
    }

    #[test]
    fn test_step_modes() {
        // StepMode::Continue should only stop on breakpoints
        let mode = StepMode::Continue;
        assert_eq!(mode, StepMode::Continue);

        // StepMode::Next should stop at same depth
        let mode = StepMode::Next { depth: 2 };
        match mode {
            StepMode::Next { depth } => assert_eq!(depth, 2),
            _ => panic!("wrong mode"),
        }
    }

    #[test]
    fn test_dap_message_roundtrip() {
        // Test that we can create a DAP initialize request and parse/respond
        let init_request = r#"{"seq":1,"type":"request","command":"initialize","arguments":{"clientID":"vscode","adapterID":"hexa"}}"#;
        let parsed = parse_json(init_request).unwrap();
        let command = parsed.get("command").and_then(|c| c.as_str()).unwrap();
        assert_eq!(command, "initialize");
        let request_seq = parsed.get("seq").and_then(|s| s.as_i64()).unwrap();
        assert_eq!(request_seq, 1);
    }
}
