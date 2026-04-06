# VOID Phase 1: Foundation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get `void` to open a GPU-rendered window, spawn a shell via PTY, display text output, and accept keyboard input — a minimal but working terminal emulator.

**Architecture:** Three new hexa std modules (std_sys, std_gpu, std_event) implemented in Rust following the existing std_io pattern. A new `void/` directory in the repo holds the hexa application code. The hexa interpreter runs the VOID app, calling into native functions for PTY, GPU, and events.

**Tech Stack:** Rust (hexa-lang crate), wgpu (GPU), winit (windowing), fontdue (font rasterization), libc (PTY), hexa (application code)

---

## File Structure

### hexa-lang modifications (system bindings)

| File | Action | Responsibility |
|------|--------|---------------|
| `src/std_sys.rs` | Create | PTY management, signals, env vars |
| `src/std_gpu.rs` | Create | wgpu/winit wrapper: window, surface, pipeline, buffers, textures |
| `src/std_event.rs` | Create | Event polling from winit, clipboard |
| `src/std_font.rs` | Create | Font rasterization via fontdue, glyph atlas generation |
| `src/env.rs` | Modify | Add Value variants: PtyHandle, GpuContext, Window, Surface, Pipeline, Texture, Buffer, EventHandle |
| `src/lib.rs` | Modify | Register new std modules |
| `src/interpreter.rs` | Modify | Route new builtin names to std modules |
| `Cargo.toml` | Modify | Add dependencies: wgpu, winit, fontdue, libc |

### VOID application (hexa code)

| File | Responsibility |
|------|---------------|
| `void/src/main.hexa` | Entry point: init GPU, open PTY, run event loop |
| `void/src/terminal/grid.hexa` | Cell struct, Grid struct, basic cell operations |
| `void/src/terminal/vt_parser.hexa` | Minimal VT parser (Ground + CSI for cursor/color) |
| `void/src/render/atlas.hexa` | Glyph atlas management, character lookup |
| `void/src/render/pipeline.hexa` | GPU render pipeline setup, frame rendering |
| `void/src/render/shaders.hexa` | WGSL vertex + fragment shader source strings |

### Tests

| File | Tests |
|------|-------|
| `examples/test_void_sys.hexa` | PTY open/spawn/read/write/close |
| `examples/test_void_grid.hexa` | Grid create, cell set/get, dirty tracking |
| `examples/test_void_vt.hexa` | VT parser state transitions, SGR, cursor |

---

## Task 1: Add Rust Dependencies

**Files:**
- Modify: `Cargo.toml`

- [ ] **Step 1: Add wgpu, winit, fontdue, libc to Cargo.toml**

Add to `[dependencies]` section:

```toml
[target.'cfg(not(target_arch = "wasm32"))'.dependencies]
wgpu = { version = "24", features = ["vulkan", "metal"] }
winit = "30"
fontdue = "0.9"
libc = "0.2"
```

- [ ] **Step 2: Verify dependencies resolve**

Run: `cd /Users/ghost/Dev/hexa-lang && cargo check 2>&1 | tail -5`
Expected: no errors (warnings OK)

- [ ] **Step 3: Commit**

```bash
git add Cargo.toml Cargo.lock
git commit -m "deps: add wgpu, winit, fontdue, libc for VOID terminal"
```

---

## Task 2: Add Value Variants for System Types

**Files:**
- Modify: `src/env.rs`

- [ ] **Step 1: Add opaque handle Value variants**

Add after the existing `TcpStream` variant in `src/env.rs`:

```rust
    // VOID terminal system handles
    #[cfg(not(target_arch = "wasm32"))]
    PtyHandle(Arc<Mutex<crate::std_sys::PtyState>>),
    #[cfg(not(target_arch = "wasm32"))]
    GpuContext(Arc<crate::std_gpu::GpuState>),
    #[cfg(not(target_arch = "wasm32"))]
    GpuWindow(Arc<crate::std_gpu::WindowState>),
    #[cfg(not(target_arch = "wasm32"))]
    GpuSurface(Arc<Mutex<crate::std_gpu::SurfaceState>>),
    #[cfg(not(target_arch = "wasm32"))]
    GpuPipeline(Arc<crate::std_gpu::PipelineState>),
    #[cfg(not(target_arch = "wasm32"))]
    GpuTexture(Arc<crate::std_gpu::TextureState>),
    #[cfg(not(target_arch = "wasm32"))]
    GpuBuffer(Arc<Mutex<crate::std_gpu::BufferState>>),
```

- [ ] **Step 2: Add Display impl for new variants**

In the `Display` impl for Value, add:

```rust
            #[cfg(not(target_arch = "wasm32"))]
            Value::PtyHandle(_) => write!(f, "<pty>"),
            #[cfg(not(target_arch = "wasm32"))]
            Value::GpuContext(_) => write!(f, "<gpu-context>"),
            #[cfg(not(target_arch = "wasm32"))]
            Value::GpuWindow(_) => write!(f, "<gpu-window>"),
            #[cfg(not(target_arch = "wasm32"))]
            Value::GpuSurface(_) => write!(f, "<gpu-surface>"),
            #[cfg(not(target_arch = "wasm32"))]
            Value::GpuPipeline(_) => write!(f, "<gpu-pipeline>"),
            #[cfg(not(target_arch = "wasm32"))]
            Value::GpuTexture(_) => write!(f, "<gpu-texture>"),
            #[cfg(not(target_arch = "wasm32"))]
            Value::GpuBuffer(_) => write!(f, "<gpu-buffer>"),
```

- [ ] **Step 3: Verify compilation**

Run: `cd /Users/ghost/Dev/hexa-lang && cargo check 2>&1 | tail -5`
Expected: errors about missing modules (std_sys, std_gpu) — that's correct, we'll create them next.

- [ ] **Step 4: Commit**

```bash
git add src/env.rs
git commit -m "feat: add Value variants for PTY and GPU handles"
```

---

## Task 3: Implement std_sys (PTY Management)

**Files:**
- Create: `src/std_sys.rs`
- Modify: `src/lib.rs`
- Modify: `src/interpreter.rs`

- [ ] **Step 1: Create src/std_sys.rs with PTY types and functions**

```rust
//! std::sys — System builtins for HEXA-LANG (VOID terminal).
//!
//! Provides:
//!   sys_pty_open()                       — open a new PTY pair
//!   sys_pty_spawn(handle, cmd)           — fork+exec shell in PTY
//!   sys_pty_read(handle)                 — non-blocking read from PTY
//!   sys_pty_write(handle, data)          — write data to PTY
//!   sys_pty_resize(handle, rows, cols)   — resize PTY (TIOCSWINSZ)
//!   sys_pty_close(handle)                — close PTY file descriptors
//!   sys_env_get(key)                     — get environment variable
//!   sys_env_set(key, value)              — set environment variable

#![allow(dead_code)]

use std::sync::{Arc, Mutex};
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Opaque PTY state holding master/slave file descriptors and child PID.
pub struct PtyState {
    pub master_fd: i32,
    pub slave_fd: i32,
    pub child_pid: Option<i32>,
    pub closed: bool,
}

fn sys_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

pub fn call_sys_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "sys_pty_open" => builtin_pty_open(interp, args),
        "sys_pty_spawn" => builtin_pty_spawn(interp, args),
        "sys_pty_read" => builtin_pty_read(interp, args),
        "sys_pty_write" => builtin_pty_write(interp, args),
        "sys_pty_resize" => builtin_pty_resize(interp, args),
        "sys_pty_close" => builtin_pty_close(interp, args),
        "sys_env_get" => builtin_env_get(interp, args),
        "sys_env_set" => builtin_env_set(interp, args),
        _ => Err(sys_err(interp, format!("unknown sys builtin: {}", name))),
    }
}

// ── PTY open ──────────────────────────────────────────────

fn builtin_pty_open(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    unsafe {
        let mut master_fd: libc::c_int = 0;
        let mut slave_fd: libc::c_int = 0;
        let mut win: libc::winsize = std::mem::zeroed();
        win.ws_row = 24;
        win.ws_col = 80;
        win.ws_xpixel = 0;
        win.ws_ypixel = 0;

        let ret = libc::openpty(
            &mut master_fd,
            &mut slave_fd,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            &win,
        );
        if ret != 0 {
            return Err(sys_err(_interp, "failed to open PTY".to_string()));
        }

        // Set master to non-blocking
        let flags = libc::fcntl(master_fd, libc::F_GETFL);
        libc::fcntl(master_fd, libc::F_SETFL, flags | libc::O_NONBLOCK);

        let state = PtyState {
            master_fd,
            slave_fd,
            child_pid: None,
            closed: false,
        };

        Ok(Value::PtyHandle(Arc::new(Mutex::new(state))))
    }
}

// ── PTY spawn ─────────────────────────────────────────────

fn builtin_pty_spawn(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(sys_err(interp, "sys_pty_spawn(handle, cmd) requires 2 args".to_string()));
    }
    let handle = match &args[0] {
        Value::PtyHandle(h) => h.clone(),
        _ => return Err(sys_err(interp, "first arg must be PtyHandle".to_string())),
    };
    let cmd = match &args[1] {
        Value::Str(s) => s.clone(),
        _ => return Err(sys_err(interp, "second arg must be String (shell command)".to_string())),
    };

    let mut pty = handle.lock().unwrap();

    unsafe {
        let pid = libc::fork();
        if pid < 0 {
            return Err(sys_err(interp, "fork failed".to_string()));
        }
        if pid == 0 {
            // Child process
            libc::close(pty.master_fd);
            libc::setsid();
            libc::ioctl(pty.slave_fd, libc::TIOCSCTTY as _, 0);
            libc::dup2(pty.slave_fd, 0); // stdin
            libc::dup2(pty.slave_fd, 1); // stdout
            libc::dup2(pty.slave_fd, 2); // stderr
            if pty.slave_fd > 2 {
                libc::close(pty.slave_fd);
            }

            let shell = std::ffi::CString::new(cmd.as_str()).unwrap();
            let args = [shell.as_ptr(), std::ptr::null()];
            libc::execvp(shell.as_ptr(), args.as_ptr());
            libc::_exit(1);
        }
        // Parent process
        libc::close(pty.slave_fd);
        pty.child_pid = Some(pid);
    }

    Ok(Value::Int(pty.child_pid.unwrap() as i64))
}

// ── PTY read ──────────────────────────────────────────────

fn builtin_pty_read(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(sys_err(interp, "sys_pty_read(handle) requires 1 arg".to_string()));
    }
    let handle = match &args[0] {
        Value::PtyHandle(h) => h.clone(),
        _ => return Err(sys_err(interp, "arg must be PtyHandle".to_string())),
    };

    let pty = handle.lock().unwrap();
    if pty.closed {
        return Ok(Value::Void);
    }

    let mut buf = [0u8; 4096];
    let n = unsafe { libc::read(pty.master_fd, buf.as_mut_ptr() as *mut _, buf.len()) };
    if n <= 0 {
        // EAGAIN (non-blocking, no data) or EOF
        return Ok(Value::Str(String::new()));
    }

    let data = String::from_utf8_lossy(&buf[..n as usize]).to_string();
    Ok(Value::Str(data))
}

// ── PTY write ─────────────────────────────────────────────

fn builtin_pty_write(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(sys_err(interp, "sys_pty_write(handle, data) requires 2 args".to_string()));
    }
    let handle = match &args[0] {
        Value::PtyHandle(h) => h.clone(),
        _ => return Err(sys_err(interp, "first arg must be PtyHandle".to_string())),
    };
    let data = match &args[1] {
        Value::Str(s) => s.clone(),
        _ => return Err(sys_err(interp, "second arg must be String".to_string())),
    };

    let pty = handle.lock().unwrap();
    if pty.closed {
        return Err(sys_err(interp, "PTY is closed".to_string()));
    }

    let bytes = data.as_bytes();
    unsafe {
        libc::write(pty.master_fd, bytes.as_ptr() as *const _, bytes.len());
    }

    Ok(Value::Void)
}

// ── PTY resize ────────────────────────────────────────────

fn builtin_pty_resize(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 3 {
        return Err(sys_err(interp, "sys_pty_resize(handle, rows, cols) requires 3 args".to_string()));
    }
    let handle = match &args[0] {
        Value::PtyHandle(h) => h.clone(),
        _ => return Err(sys_err(interp, "first arg must be PtyHandle".to_string())),
    };
    let rows = match &args[1] {
        Value::Int(n) => *n as u16,
        _ => return Err(sys_err(interp, "rows must be Int".to_string())),
    };
    let cols = match &args[2] {
        Value::Int(n) => *n as u16,
        _ => return Err(sys_err(interp, "cols must be Int".to_string())),
    };

    let pty = handle.lock().unwrap();
    unsafe {
        let win = libc::winsize {
            ws_row: rows,
            ws_col: cols,
            ws_xpixel: 0,
            ws_ypixel: 0,
        };
        libc::ioctl(pty.master_fd, libc::TIOCSWINSZ as _, &win);
    }

    Ok(Value::Void)
}

// ── PTY close ─────────────────────────────────────────────

fn builtin_pty_close(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(sys_err(interp, "sys_pty_close(handle) requires 1 arg".to_string()));
    }
    let handle = match &args[0] {
        Value::PtyHandle(h) => h.clone(),
        _ => return Err(sys_err(interp, "arg must be PtyHandle".to_string())),
    };

    let mut pty = handle.lock().unwrap();
    if !pty.closed {
        unsafe {
            libc::close(pty.master_fd);
            if let Some(pid) = pty.child_pid {
                libc::kill(pid, libc::SIGTERM);
                libc::waitpid(pid, std::ptr::null_mut(), 0);
            }
        }
        pty.closed = true;
    }

    Ok(Value::Void)
}

// ── Environment ───────────────────────────────────────────

fn builtin_env_get(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(sys_err(interp, "sys_env_get(key) requires 1 arg".to_string()));
    }
    let key = match &args[0] {
        Value::Str(s) => s.clone(),
        _ => return Err(sys_err(interp, "key must be String".to_string())),
    };
    match std::env::var(&key) {
        Ok(val) => Ok(Value::Str(val)),
        Err(_) => Ok(Value::Str(String::new())),
    }
}

fn builtin_env_set(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(sys_err(interp, "sys_env_set(key, val) requires 2 args".to_string()));
    }
    let key = match &args[0] {
        Value::Str(s) => s.clone(),
        _ => return Err(sys_err(interp, "key must be String".to_string())),
    };
    let val = match &args[1] {
        Value::Str(s) => s.clone(),
        _ => return Err(sys_err(interp, "val must be String".to_string())),
    };
    std::env::set_var(&key, &val);
    Ok(Value::Void)
}
```

- [ ] **Step 2: Register std_sys module in src/lib.rs**

Add after `pub mod std_nexus6;` line:

```rust
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_sys;
```

- [ ] **Step 3: Route sys builtins in src/interpreter.rs**

Add after the `std::time` builtins block (around line 3737):

```rust
            // ── std::sys builtins (VOID terminal) ─────────────────
            "sys_pty_open" | "sys_pty_spawn" | "sys_pty_read" | "sys_pty_write"
            | "sys_pty_resize" | "sys_pty_close" | "sys_env_get" | "sys_env_set" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_sys::call_sys_builtin(self, name, args)
                }
            }
```

- [ ] **Step 4: Verify compilation**

Run: `cd /Users/ghost/Dev/hexa-lang && cargo check 2>&1 | tail -10`
Expected: compiles (may warn about unused std_gpu/std_event — that's fine, we create them next)

- [ ] **Step 5: Commit**

```bash
git add src/std_sys.rs src/lib.rs src/interpreter.rs
git commit -m "feat: add std_sys module — PTY management for VOID terminal"
```

---

## Task 4: Implement std_gpu (Window + GPU Rendering)

**Files:**
- Create: `src/std_gpu.rs`
- Modify: `src/lib.rs`
- Modify: `src/interpreter.rs`

- [ ] **Step 1: Create src/std_gpu.rs with GPU types**

```rust
//! std::gpu — GPU rendering builtins for HEXA-LANG (VOID terminal).
//!
//! Provides:
//!   gpu_init()                              — create wgpu instance+adapter+device+queue
//!   gpu_window_create(w, h, title)          — create platform window
//!   gpu_surface_create(ctx, window)         — create GPU surface
//!   gpu_pipeline_create(ctx, surface, wgsl) — create render pipeline
//!   gpu_texture_create(ctx, w, h)           — create RGBA texture
//!   gpu_texture_write(ctx, tex, x, y, w, h, data) — upload pixel data to texture
//!   gpu_buffer_create(ctx, size)            — create storage buffer
//!   gpu_buffer_write(ctx, buf, data)        — upload data to buffer
//!   gpu_render_frame(ctx, surface, pipeline, bindings) — render one frame
//!   gpu_window_size(window)                 — get window inner size

#![allow(dead_code)]

use std::sync::{Arc, Mutex};
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

// ── Opaque State Types ────────────────────────────────────

pub struct GpuState {
    pub instance: wgpu::Instance,
    pub adapter: wgpu::Adapter,
    pub device: wgpu::Device,
    pub queue: wgpu::Queue,
}

pub struct WindowState {
    pub window: Arc<winit::window::Window>,
}

pub struct SurfaceState {
    pub surface: wgpu::Surface<'static>,
    pub config: wgpu::SurfaceConfiguration,
}

pub struct PipelineState {
    pub pipeline: wgpu::RenderPipeline,
    pub bind_group_layout: wgpu::BindGroupLayout,
}

pub struct TextureState {
    pub texture: wgpu::Texture,
    pub view: wgpu::TextureView,
    pub width: u32,
    pub height: u32,
}

pub struct BufferState {
    pub buffer: wgpu::Buffer,
    pub size: u64,
}

fn gpu_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

pub fn call_gpu_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "gpu_init" => builtin_gpu_init(interp, args),
        "gpu_window_create" => builtin_window_create(interp, args),
        "gpu_surface_create" => builtin_surface_create(interp, args),
        "gpu_pipeline_create" => builtin_pipeline_create(interp, args),
        "gpu_texture_create" => builtin_texture_create(interp, args),
        "gpu_texture_write" => builtin_texture_write(interp, args),
        "gpu_buffer_create" => builtin_buffer_create(interp, args),
        "gpu_buffer_write" => builtin_buffer_write(interp, args),
        "gpu_render_frame" => builtin_render_frame(interp, args),
        "gpu_window_size" => builtin_window_size(interp, args),
        _ => Err(gpu_err(interp, format!("unknown gpu builtin: {}", name))),
    }
}

// ── GPU Init ──────────────────────────────────────────────

fn builtin_gpu_init(interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
        backends: wgpu::Backends::all(),
        ..Default::default()
    });

    let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
        power_preference: wgpu::PowerPreference::HighPerformance,
        compatible_surface: None,
        force_fallback_adapter: false,
    }))
    .ok_or_else(|| gpu_err(interp, "no GPU adapter found".to_string()))?;

    let (device, queue) = pollster::block_on(adapter.request_device(
        &wgpu::DeviceDescriptor {
            label: Some("VOID GPU"),
            required_features: wgpu::Features::empty(),
            required_limits: wgpu::Limits::default(),
            memory_hints: Default::default(),
        },
        None,
    ))
    .map_err(|e| gpu_err(interp, format!("GPU device error: {}", e)))?;

    Ok(Value::GpuContext(Arc::new(GpuState {
        instance,
        adapter,
        device,
        queue,
    })))
}

// ── Window Create ─────────────────────────────────────────

fn builtin_window_create(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 3 {
        return Err(gpu_err(interp, "gpu_window_create(w, h, title) requires 3 args".to_string()));
    }
    let w = match &args[0] {
        Value::Int(n) => *n as u32,
        _ => return Err(gpu_err(interp, "width must be Int".to_string())),
    };
    let h = match &args[1] {
        Value::Int(n) => *n as u32,
        _ => return Err(gpu_err(interp, "height must be Int".to_string())),
    };
    let title = match &args[2] {
        Value::Str(s) => s.clone(),
        _ => return Err(gpu_err(interp, "title must be String".to_string())),
    };

    // NOTE: Window creation requires an event loop. In Phase 1, we use
    // a simplified approach where the event loop is managed by std_event.
    // This function creates the window attributes; actual window creation
    // happens inside the event loop in gpu_run_loop.
    // For now, we store the desired attributes.
    // The actual winit integration is in std_event::event_run_loop.

    // Placeholder: we'll refactor this in Task 6 when we integrate the event loop.
    // For compilation, return a map with window config.
    let mut config = std::collections::HashMap::new();
    config.insert("width".to_string(), Value::Int(w as i64));
    config.insert("height".to_string(), Value::Int(h as i64));
    config.insert("title".to_string(), Value::Str(title));
    Ok(Value::Map(Box::new(config)))
}

// ── Surface Create ────────────────────────────────────────

fn builtin_surface_create(interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    // Surface creation is tightly coupled with the event loop.
    // This will be called from inside the event loop callback.
    Err(gpu_err(interp, "gpu_surface_create: call from within event_run_loop callback".to_string()))
}

// ── Pipeline Create ───────────────────────────────────────

fn builtin_pipeline_create(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 3 {
        return Err(gpu_err(interp, "gpu_pipeline_create(ctx, surface, wgsl) requires 3 args".to_string()));
    }
    let gpu = match &args[0] {
        Value::GpuContext(g) => g.clone(),
        _ => return Err(gpu_err(interp, "first arg must be GpuContext".to_string())),
    };
    let surface_state = match &args[1] {
        Value::GpuSurface(s) => s.clone(),
        _ => return Err(gpu_err(interp, "second arg must be GpuSurface".to_string())),
    };
    let wgsl = match &args[2] {
        Value::Str(s) => s.clone(),
        _ => return Err(gpu_err(interp, "third arg must be String (WGSL source)".to_string())),
    };

    let shader = gpu.device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("VOID shader"),
        source: wgpu::ShaderSource::Wgsl(std::borrow::Cow::Owned(wgsl)),
    });

    let bind_group_layout = gpu.device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
        label: Some("VOID bind group layout"),
        entries: &[
            // Binding 0: glyph atlas texture
            wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Texture {
                    sample_type: wgpu::TextureSampleType::Float { filterable: true },
                    view_dimension: wgpu::TextureViewDimension::D2,
                    multisampled: false,
                },
                count: None,
            },
            // Binding 1: sampler
            wgpu::BindGroupLayoutEntry {
                binding: 1,
                visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                count: None,
            },
            // Binding 2: cell data (storage buffer)
            wgpu::BindGroupLayoutEntry {
                binding: 2,
                visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Storage { read_only: true },
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            },
            // Binding 3: uniforms (grid size, cell size, etc)
            wgpu::BindGroupLayoutEntry {
                binding: 3,
                visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            },
        ],
    });

    let pipeline_layout = gpu.device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
        label: Some("VOID pipeline layout"),
        bind_group_layouts: &[&bind_group_layout],
        push_constant_ranges: &[],
    });

    let surface_lock = surface_state.lock().unwrap();
    let pipeline = gpu.device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("VOID render pipeline"),
        layout: Some(&pipeline_layout),
        vertex: wgpu::VertexState {
            module: &shader,
            entry_point: Some("vs_main"),
            buffers: &[],
            compilation_options: Default::default(),
        },
        fragment: Some(wgpu::FragmentState {
            module: &shader,
            entry_point: Some("fs_main"),
            targets: &[Some(wgpu::ColorTargetState {
                format: surface_lock.config.format,
                blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                write_mask: wgpu::ColorWrites::ALL,
            })],
            compilation_options: Default::default(),
        }),
        primitive: wgpu::PrimitiveState {
            topology: wgpu::PrimitiveTopology::TriangleList,
            strip_index_format: None,
            front_face: wgpu::FrontFace::Ccw,
            cull_mode: None,
            polygon_mode: wgpu::PolygonMode::Fill,
            unclipped_depth: false,
            conservative: false,
        },
        depth_stencil: None,
        multisample: wgpu::MultisampleState::default(),
        multiview: None,
        cache: None,
    });
    drop(surface_lock);

    Ok(Value::GpuPipeline(Arc::new(PipelineState {
        pipeline,
        bind_group_layout,
    })))
}

// ── Texture Create ────────────────────────────────────────

fn builtin_texture_create(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 3 {
        return Err(gpu_err(interp, "gpu_texture_create(ctx, w, h) requires 3 args".to_string()));
    }
    let gpu = match &args[0] {
        Value::GpuContext(g) => g.clone(),
        _ => return Err(gpu_err(interp, "first arg must be GpuContext".to_string())),
    };
    let w = match &args[1] {
        Value::Int(n) => *n as u32,
        _ => return Err(gpu_err(interp, "width must be Int".to_string())),
    };
    let h = match &args[2] {
        Value::Int(n) => *n as u32,
        _ => return Err(gpu_err(interp, "height must be Int".to_string())),
    };

    let texture = gpu.device.create_texture(&wgpu::TextureDescriptor {
        label: Some("VOID texture"),
        size: wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba8UnormSrgb,
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });
    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());

    Ok(Value::GpuTexture(Arc::new(TextureState {
        texture,
        view,
        width: w,
        height: h,
    })))
}

// ── Texture Write ─────────────────────────────────────────

fn builtin_texture_write(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 7 {
        return Err(gpu_err(interp, "gpu_texture_write(ctx, tex, x, y, w, h, data) requires 7 args".to_string()));
    }
    let gpu = match &args[0] {
        Value::GpuContext(g) => g.clone(),
        _ => return Err(gpu_err(interp, "first arg must be GpuContext".to_string())),
    };
    let tex = match &args[1] {
        Value::GpuTexture(t) => t.clone(),
        _ => return Err(gpu_err(interp, "second arg must be GpuTexture".to_string())),
    };
    let x = match &args[2] { Value::Int(n) => *n as u32, _ => return Err(gpu_err(interp, "x must be Int".to_string())) };
    let y = match &args[3] { Value::Int(n) => *n as u32, _ => return Err(gpu_err(interp, "y must be Int".to_string())) };
    let w = match &args[4] { Value::Int(n) => *n as u32, _ => return Err(gpu_err(interp, "w must be Int".to_string())) };
    let h = match &args[5] { Value::Int(n) => *n as u32, _ => return Err(gpu_err(interp, "h must be Int".to_string())) };
    let data = match &args[6] {
        Value::Array(arr) => arr.iter().map(|v| match v {
            Value::Int(n) => *n as u8,
            _ => 0,
        }).collect::<Vec<u8>>(),
        _ => return Err(gpu_err(interp, "data must be Array of Int (bytes)".to_string())),
    };

    gpu.queue.write_texture(
        wgpu::TexelCopyTextureInfo {
            texture: &tex.texture,
            mip_level: 0,
            origin: wgpu::Origin3d { x, y, z: 0 },
            aspect: wgpu::TextureAspect::All,
        },
        &data,
        wgpu::TexelCopyBufferLayout {
            offset: 0,
            bytes_per_row: Some(4 * w),
            rows_per_image: Some(h),
        },
        wgpu::Extent3d { width: w, height: h, depth_or_array_layers: 1 },
    );

    Ok(Value::Void)
}

// ── Buffer Create ─────────────────────────────────────────

fn builtin_buffer_create(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(gpu_err(interp, "gpu_buffer_create(ctx, size) requires 2 args".to_string()));
    }
    let gpu = match &args[0] {
        Value::GpuContext(g) => g.clone(),
        _ => return Err(gpu_err(interp, "first arg must be GpuContext".to_string())),
    };
    let size = match &args[1] {
        Value::Int(n) => *n as u64,
        _ => return Err(gpu_err(interp, "size must be Int".to_string())),
    };

    let buffer = gpu.device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("VOID buffer"),
        size,
        usage: wgpu::BufferUsages::STORAGE | wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        mapped_at_creation: false,
    });

    Ok(Value::GpuBuffer(Arc::new(Mutex::new(BufferState {
        buffer,
        size,
    }))))
}

// ── Buffer Write ──────────────────────────────────────────

fn builtin_buffer_write(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 3 {
        return Err(gpu_err(interp, "gpu_buffer_write(ctx, buf, data) requires 3 args".to_string()));
    }
    let gpu = match &args[0] {
        Value::GpuContext(g) => g.clone(),
        _ => return Err(gpu_err(interp, "first arg must be GpuContext".to_string())),
    };
    let buf = match &args[1] {
        Value::GpuBuffer(b) => b.clone(),
        _ => return Err(gpu_err(interp, "second arg must be GpuBuffer".to_string())),
    };
    let data = match &args[2] {
        Value::Array(arr) => arr.iter().map(|v| match v {
            Value::Int(n) => *n as u8,
            _ => 0,
        }).collect::<Vec<u8>>(),
        _ => return Err(gpu_err(interp, "data must be Array of Int (bytes)".to_string())),
    };

    let buf_lock = buf.lock().unwrap();
    gpu.queue.write_buffer(&buf_lock.buffer, 0, &data);

    Ok(Value::Void)
}

// ── Render Frame ──────────────────────────────────────────

fn builtin_render_frame(interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    // Render frame is handled inside the event loop callback in std_event.
    // This is a placeholder — the actual rendering is triggered by the event loop.
    Err(gpu_err(interp, "gpu_render_frame: call from within event_run_loop callback".to_string()))
}

// ── Window Size ───────────────────────────────────────────

fn builtin_window_size(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(gpu_err(interp, "gpu_window_size(window) requires 1 arg".to_string()));
    }
    let window = match &args[0] {
        Value::GpuWindow(w) => w.clone(),
        _ => return Err(gpu_err(interp, "arg must be GpuWindow".to_string())),
    };

    let size = window.window.inner_size();
    let mut map = std::collections::HashMap::new();
    map.insert("width".to_string(), Value::Int(size.width as i64));
    map.insert("height".to_string(), Value::Int(size.height as i64));
    Ok(Value::Map(Box::new(map)))
}
```

- [ ] **Step 2: Register std_gpu module in src/lib.rs**

Add after `pub mod std_sys;`:

```rust
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_gpu;
```

- [ ] **Step 3: Add pollster dependency to Cargo.toml**

```toml
pollster = "0.4"
```

- [ ] **Step 4: Route gpu builtins in src/interpreter.rs**

Add after the sys builtins block:

```rust
            // ── std::gpu builtins (VOID terminal) ─────────────────
            "gpu_init" | "gpu_window_create" | "gpu_surface_create"
            | "gpu_pipeline_create" | "gpu_texture_create" | "gpu_texture_write"
            | "gpu_buffer_create" | "gpu_buffer_write"
            | "gpu_render_frame" | "gpu_window_size" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_gpu::call_gpu_builtin(self, name, args)
                }
            }
```

- [ ] **Step 5: Verify compilation**

Run: `cd /Users/ghost/Dev/hexa-lang && cargo check 2>&1 | tail -10`
Expected: compiles without errors

- [ ] **Step 6: Commit**

```bash
git add src/std_gpu.rs src/lib.rs src/interpreter.rs Cargo.toml
git commit -m "feat: add std_gpu module — wgpu rendering for VOID terminal"
```

---

## Task 5: Implement std_event (Event Loop + Input)

**Files:**
- Create: `src/std_event.rs`
- Modify: `src/lib.rs`
- Modify: `src/interpreter.rs`

- [ ] **Step 1: Create src/std_event.rs with winit event loop integration**

```rust
//! std::event — Event loop and input builtins for HEXA-LANG (VOID terminal).
//!
//! Provides:
//!   event_run_loop(config, on_init, on_frame, on_key, on_resize)
//!     — runs the winit event loop, calling hexa callbacks
//!
//! The event loop owns the window and surface lifecycle. Hexa callbacks receive
//! GPU resources and events as arguments.

#![allow(dead_code)]

use std::sync::{Arc, Mutex};
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

fn event_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

pub fn call_event_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "event_run_loop" => builtin_event_run_loop(interp, args),
        "clipboard_get" => builtin_clipboard_get(interp, args),
        "clipboard_set" => builtin_clipboard_set(interp, args),
        _ => Err(event_err(interp, format!("unknown event builtin: {}", name))),
    }
}

/// event_run_loop(config_map, on_init_fn, on_frame_fn, on_key_fn, on_resize_fn)
///
/// config_map: { width: Int, height: Int, title: String }
/// on_init_fn(ctx, window, surface): called once after window+surface are created
/// on_frame_fn(ctx, window, surface): called each frame (~60fps)
/// on_key_fn(key_event_map): called on key press
/// on_resize_fn(width, height): called on window resize
fn builtin_event_run_loop(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 5 {
        return Err(event_err(interp, "event_run_loop requires 5 args: config, on_init, on_frame, on_key, on_resize".to_string()));
    }

    let config = match &args[0] {
        Value::Map(m) => m.as_ref().clone(),
        _ => return Err(event_err(interp, "first arg must be config Map".to_string())),
    };
    let on_init = args[1].clone();
    let on_frame = args[2].clone();
    let on_key = args[3].clone();
    let on_resize = args[4].clone();

    let width = match config.get("width") {
        Some(Value::Int(n)) => *n as u32,
        _ => 1200,
    };
    let height = match config.get("height") {
        Some(Value::Int(n)) => *n as u32,
        _ => 800,
    };
    let title = match config.get("title") {
        Some(Value::Str(s)) => s.clone(),
        _ => "VOID".to_string(),
    };

    // We need to clone the interpreter state for callbacks.
    // The event loop takes ownership of the thread.
    let interp_env = interp.env.clone();
    let struct_defs = interp.struct_defs.clone();
    let type_methods = interp.type_methods.clone();
    let source_lines = interp.source_lines.clone();
    let file_name = interp.file_name.clone();

    let event_loop = winit::event_loop::EventLoop::new()
        .map_err(|e| event_err(interp, format!("event loop error: {}", e)))?;

    event_loop.set_control_flow(winit::event_loop::ControlFlow::Poll);

    // Shared state across event loop iterations
    let gpu_ctx: Arc<Mutex<Option<Arc<crate::std_gpu::GpuState>>>> = Arc::new(Mutex::new(None));
    let surface_state: Arc<Mutex<Option<Arc<Mutex<crate::std_gpu::SurfaceState>>>>> = Arc::new(Mutex::new(None));
    let window_state: Arc<Mutex<Option<Arc<crate::std_gpu::WindowState>>>> = Arc::new(Mutex::new(None));
    let initialized = Arc::new(Mutex::new(false));

    let gpu_ctx_c = gpu_ctx.clone();
    let surface_c = surface_state.clone();
    let window_c = window_state.clone();
    let init_c = initialized.clone();

    // Create a sub-interpreter for callbacks
    let on_init_c = on_init.clone();
    let on_frame_c = on_frame.clone();
    let on_key_c = on_key.clone();
    let on_resize_c = on_resize.clone();

    let mut sub_interp = Interpreter::new();
    sub_interp.env = interp_env;
    sub_interp.struct_defs = struct_defs;
    sub_interp.type_methods = type_methods;
    sub_interp.source_lines = source_lines;
    sub_interp.file_name = file_name;

    let sub_interp = Arc::new(Mutex::new(sub_interp));

    event_loop.run(move |event, elwt| {
        use winit::event::{Event, WindowEvent, ElementState};
        use winit::keyboard::{Key, NamedKey};

        match event {
            Event::Resumed => {
                let mut init_guard = init_c.lock().unwrap();
                if !*init_guard {
                    // Create window
                    let win_attrs = winit::window::WindowAttributes::default()
                        .with_title(&title)
                        .with_inner_size(winit::dpi::LogicalSize::new(width, height));
                    let window = Arc::new(elwt.create_window(win_attrs).expect("window creation failed"));

                    // Create GPU context
                    let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
                        backends: wgpu::Backends::all(),
                        ..Default::default()
                    });

                    let surface = instance.create_surface(window.clone()).expect("surface creation failed");
                    let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
                        power_preference: wgpu::PowerPreference::HighPerformance,
                        compatible_surface: Some(&surface),
                        force_fallback_adapter: false,
                    })).expect("no GPU adapter");

                    let (device, queue) = pollster::block_on(adapter.request_device(
                        &wgpu::DeviceDescriptor {
                            label: Some("VOID"),
                            required_features: wgpu::Features::empty(),
                            required_limits: wgpu::Limits::default(),
                            memory_hints: Default::default(),
                        },
                        None,
                    )).expect("GPU device error");

                    let size = window.inner_size();
                    let surface_caps = surface.get_capabilities(&adapter);
                    let format = surface_caps.formats.iter()
                        .find(|f| f.is_srgb())
                        .copied()
                        .unwrap_or(surface_caps.formats[0]);

                    let config = wgpu::SurfaceConfiguration {
                        usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
                        format,
                        width: size.width.max(1),
                        height: size.height.max(1),
                        present_mode: wgpu::PresentMode::Fifo,
                        desired_maximum_frame_latency: 2,
                        alpha_mode: surface_caps.alpha_modes[0],
                        view_formats: vec![],
                    };
                    surface.configure(&device, &config);

                    let ctx = Arc::new(crate::std_gpu::GpuState { instance, adapter, device, queue });
                    let win_st = Arc::new(crate::std_gpu::WindowState { window: window.clone() });
                    let surf_st = Arc::new(Mutex::new(crate::std_gpu::SurfaceState { surface, config }));

                    *gpu_ctx_c.lock().unwrap() = Some(ctx.clone());
                    *window_c.lock().unwrap() = Some(win_st.clone());
                    *surface_c.lock().unwrap() = Some(surf_st.clone());

                    // Call on_init(ctx, window, surface)
                    let mut si = sub_interp.lock().unwrap();
                    let _ = si.call_value(
                        on_init_c.clone(),
                        vec![
                            Value::GpuContext(ctx),
                            Value::GpuWindow(win_st),
                            Value::GpuSurface(surf_st),
                        ],
                    );

                    *init_guard = true;
                }
            }
            Event::AboutToWait => {
                if let Some(win) = window_c.lock().unwrap().as_ref() {
                    win.window.request_redraw();
                }
            }
            Event::WindowEvent { event: win_event, .. } => {
                match win_event {
                    WindowEvent::CloseRequested => {
                        elwt.exit();
                    }
                    WindowEvent::RedrawRequested => {
                        let ctx_opt = gpu_ctx.lock().unwrap().clone();
                        let surf_opt = surface_state.lock().unwrap().clone();
                        let win_opt = window_state.lock().unwrap().clone();

                        if let (Some(ctx), Some(surf), Some(win)) = (ctx_opt, surf_opt, win_opt) {
                            let mut si = sub_interp.lock().unwrap();
                            let _ = si.call_value(
                                on_frame_c.clone(),
                                vec![
                                    Value::GpuContext(ctx),
                                    Value::GpuWindow(win),
                                    Value::GpuSurface(surf),
                                ],
                            );
                        }
                    }
                    WindowEvent::Resized(new_size) => {
                        let ctx_opt = gpu_ctx.lock().unwrap().clone();
                        let surf_opt = surface_state.lock().unwrap().clone();

                        if let (Some(ctx), Some(surf)) = (ctx_opt, surf_opt) {
                            let mut s = surf.lock().unwrap();
                            s.config.width = new_size.width.max(1);
                            s.config.height = new_size.height.max(1);
                            s.surface.configure(&ctx.device, &s.config);
                            drop(s);

                            let mut si = sub_interp.lock().unwrap();
                            let _ = si.call_value(
                                on_resize_c.clone(),
                                vec![
                                    Value::Int(new_size.width as i64),
                                    Value::Int(new_size.height as i64),
                                ],
                            );
                        }
                    }
                    WindowEvent::KeyboardInput { event: key_event, .. } => {
                        if key_event.state == ElementState::Pressed {
                            let mut key_map = std::collections::HashMap::new();

                            let text = key_event.text.as_ref()
                                .map(|s| s.to_string())
                                .unwrap_or_default();
                            key_map.insert("text".to_string(), Value::Str(text));

                            let key_name = match &key_event.logical_key {
                                Key::Named(named) => format!("{:?}", named),
                                Key::Character(c) => c.to_string(),
                                _ => "Unknown".to_string(),
                            };
                            key_map.insert("key".to_string(), Value::Str(key_name));

                            let mut si = sub_interp.lock().unwrap();
                            let _ = si.call_value(
                                on_key_c.clone(),
                                vec![Value::Map(Box::new(key_map))],
                            );
                        }
                    }
                    _ => {}
                }
            }
            _ => {}
        }
    }).map_err(|e| event_err(interp, format!("event loop error: {}", e)))?;

    Ok(Value::Void)
}

fn builtin_clipboard_get(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    // Clipboard requires platform-specific code. Stub for now.
    Ok(Value::Str(String::new()))
}

fn builtin_clipboard_set(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    Ok(Value::Void)
}
```

- [ ] **Step 2: Add call_value method to Interpreter if missing**

Check if `Interpreter` has a `call_value` method. If not, add to `src/interpreter.rs`:

```rust
    /// Call a Value as a function (Fn, Lambda, or BuiltinFn) with given args.
    /// Used by std_event for callbacks from the event loop.
    pub fn call_value(&mut self, callee: Value, args: Vec<Value>) -> Result<Value, HexaError> {
        match callee {
            Value::Fn(inner) => {
                let (name, params, body) = *inner;
                self.call_function(&name, &params, &body, args)
            }
            Value::Lambda(inner) => {
                let (params, body, captures) = *inner;
                self.call_lambda(&params, &body, &captures, args)
            }
            Value::BuiltinFn(name) => {
                self.call_builtin(&name, args)
            }
            _ => Err(self.runtime_err("not callable".to_string())),
        }
    }
```

- [ ] **Step 3: Register std_event module in src/lib.rs**

```rust
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_event;
```

- [ ] **Step 4: Route event builtins in src/interpreter.rs**

```rust
            // ── std::event builtins (VOID terminal) ───────────────
            "event_run_loop" | "clipboard_get" | "clipboard_set" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_event::call_event_builtin(self, name, args)
                }
            }
```

- [ ] **Step 5: Verify compilation**

Run: `cd /Users/ghost/Dev/hexa-lang && cargo check 2>&1 | tail -10`
Expected: compiles without errors

- [ ] **Step 6: Commit**

```bash
git add src/std_event.rs src/lib.rs src/interpreter.rs
git commit -m "feat: add std_event module — winit event loop for VOID terminal"
```

---

## Task 6: Implement std_font (Font Rasterization)

**Files:**
- Create: `src/std_font.rs`
- Modify: `src/lib.rs`
- Modify: `src/interpreter.rs`

- [ ] **Step 1: Create src/std_font.rs**

```rust
//! std::font — Font rasterization builtins for HEXA-LANG (VOID terminal).
//!
//! Provides:
//!   font_load(path_or_name, size) — load a font file and set pixel size
//!   font_rasterize(font, char)    — rasterize a single character, return bitmap
//!   font_metrics(font)            — get cell width, cell height, baseline
//!   font_atlas_create(font, ctx)  — create GPU atlas texture with ASCII preloaded

#![allow(dead_code)]

use std::sync::{Arc, Mutex};
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

pub struct FontState {
    pub font: fontdue::Font,
    pub size: f32,
    pub cell_width: u32,
    pub cell_height: u32,
    pub baseline: u32,
}

pub struct AtlasState {
    pub texture: Arc<crate::std_gpu::TextureState>,
    pub atlas_width: u32,
    pub atlas_height: u32,
    pub cell_width: u32,
    pub cell_height: u32,
    /// Maps char -> (atlas_x, atlas_y) in pixel coords
    pub glyph_positions: std::collections::HashMap<char, (u32, u32)>,
}

fn font_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

pub fn call_font_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "font_load" => builtin_font_load(interp, args),
        "font_metrics" => builtin_font_metrics(interp, args),
        "font_atlas_create" => builtin_font_atlas_create(interp, args),
        _ => Err(font_err(interp, format!("unknown font builtin: {}", name))),
    }
}

fn builtin_font_load(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(font_err(interp, "font_load(path, size) requires 2 args".to_string()));
    }
    let path = match &args[0] {
        Value::Str(s) => s.clone(),
        _ => return Err(font_err(interp, "path must be String".to_string())),
    };
    let size = match &args[1] {
        Value::Int(n) => *n as f32,
        Value::Float(n) => *n as f32,
        _ => return Err(font_err(interp, "size must be number".to_string())),
    };

    let font_data = std::fs::read(&path)
        .map_err(|e| font_err(interp, format!("cannot read font file '{}': {}", path, e)))?;

    let font = fontdue::Font::from_bytes(font_data, fontdue::FontSettings::default())
        .map_err(|e| font_err(interp, format!("font parse error: {}", e)))?;

    // Calculate cell metrics from 'M' glyph
    let metrics = font.metrics('M', size);
    let cell_width = metrics.advance_width.ceil() as u32;
    let line_metrics = font.horizontal_line_metrics(size)
        .unwrap_or(fontdue::LineMetrics {
            ascent: size * 0.8,
            descent: size * -0.2,
            line_gap: 0.0,
            new_line_size: size,
        });
    let cell_height = (line_metrics.ascent - line_metrics.descent + line_metrics.line_gap).ceil() as u32;
    let baseline = line_metrics.ascent.ceil() as u32;

    let state = FontState { font, size, cell_width, cell_height, baseline };

    // Return as a Map with opaque handle
    // We store it similarly to how buffered readers work — in a global registry.
    let id = NEXT_FONT_ID.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
    font_registry::get().lock().unwrap().insert(id, state);

    let mut map = std::collections::HashMap::new();
    map.insert("_type".to_string(), Value::Str("font".to_string()));
    map.insert("_id".to_string(), Value::Int(id as i64));
    Ok(Value::Map(Box::new(map)))
}

static NEXT_FONT_ID: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);

mod font_registry {
    use super::*;
    use std::sync::OnceLock;
    use std::collections::HashMap;

    static INSTANCE: OnceLock<Mutex<HashMap<u64, FontState>>> = OnceLock::new();

    pub fn get() -> &'static Mutex<HashMap<u64, FontState>> {
        INSTANCE.get_or_init(|| Mutex::new(HashMap::new()))
    }
}

fn get_font_state(id: u64) -> Option<()> {
    font_registry::get().lock().unwrap().get(&id).map(|_| ())
}

fn builtin_font_metrics(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.is_empty() {
        return Err(font_err(interp, "font_metrics(font) requires 1 arg".to_string()));
    }
    let font_id = match &args[0] {
        Value::Map(m) => {
            match m.get("_id") {
                Some(Value::Int(id)) => *id as u64,
                _ => return Err(font_err(interp, "invalid font handle".to_string())),
            }
        }
        _ => return Err(font_err(interp, "arg must be font handle".to_string())),
    };

    let fonts = font_registry::get().lock().unwrap();
    let font_state = fonts.get(&font_id)
        .ok_or_else(|| font_err(interp, "font not found".to_string()))?;

    let mut map = std::collections::HashMap::new();
    map.insert("cell_width".to_string(), Value::Int(font_state.cell_width as i64));
    map.insert("cell_height".to_string(), Value::Int(font_state.cell_height as i64));
    map.insert("baseline".to_string(), Value::Int(font_state.baseline as i64));
    Ok(Value::Map(Box::new(map)))
}

fn builtin_font_atlas_create(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(font_err(interp, "font_atlas_create(font, ctx) requires 2 args".to_string()));
    }
    let font_id = match &args[0] {
        Value::Map(m) => match m.get("_id") {
            Some(Value::Int(id)) => *id as u64,
            _ => return Err(font_err(interp, "invalid font handle".to_string())),
        },
        _ => return Err(font_err(interp, "first arg must be font handle".to_string())),
    };
    let gpu = match &args[1] {
        Value::GpuContext(g) => g.clone(),
        _ => return Err(font_err(interp, "second arg must be GpuContext".to_string())),
    };

    let fonts = font_registry::get().lock().unwrap();
    let font_state = fonts.get(&font_id)
        .ok_or_else(|| font_err(interp, "font not found".to_string()))?;

    let cw = font_state.cell_width;
    let ch = font_state.cell_height;
    let baseline = font_state.baseline;

    // ASCII printable: 0x20..=0x7E (95 chars)
    // Layout: 16 chars per row, ceil(95/16) = 6 rows
    let cols = 16u32;
    let rows = 6u32;
    let atlas_w = cols * cw;
    let atlas_h = rows * ch;

    // Create RGBA pixel buffer
    let mut pixels = vec![0u8; (atlas_w * atlas_h * 4) as usize];
    let mut glyph_positions = std::collections::HashMap::new();

    for (i, ch_code) in (0x20u8..=0x7Eu8).enumerate() {
        let c = ch_code as char;
        let col = (i % cols as usize) as u32;
        let row = (i / cols as usize) as u32;

        let (metrics, bitmap) = font_state.font.rasterize(c, font_state.size);

        let gx = col * cw;
        let gy = row * ch;
        glyph_positions.insert(c, (gx, gy));

        // Blit glyph into atlas (centered in cell, baseline-aligned)
        let offset_x = metrics.xmin.max(0) as u32;
        let offset_y = baseline.saturating_sub(metrics.height as u32 + metrics.ymin.max(0) as u32);

        for by in 0..metrics.height {
            for bx in 0..metrics.width {
                let px = gx + offset_x + bx as u32;
                let py = gy + offset_y + by as u32;
                if px < atlas_w && py < atlas_h {
                    let alpha = bitmap[by * metrics.width + bx];
                    let idx = ((py * atlas_w + px) * 4) as usize;
                    pixels[idx] = 255;     // R
                    pixels[idx + 1] = 255; // G
                    pixels[idx + 2] = 255; // B
                    pixels[idx + 3] = alpha; // A
                }
            }
        }
    }

    // Upload to GPU texture
    let texture = gpu.device.create_texture(&wgpu::TextureDescriptor {
        label: Some("VOID glyph atlas"),
        size: wgpu::Extent3d { width: atlas_w, height: atlas_h, depth_or_array_layers: 1 },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Rgba8UnormSrgb,
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
        view_formats: &[],
    });

    gpu.queue.write_texture(
        wgpu::TexelCopyTextureInfo {
            texture: &texture,
            mip_level: 0,
            origin: wgpu::Origin3d::ZERO,
            aspect: wgpu::TextureAspect::All,
        },
        &pixels,
        wgpu::TexelCopyBufferLayout {
            offset: 0,
            bytes_per_row: Some(4 * atlas_w),
            rows_per_image: Some(atlas_h),
        },
        wgpu::Extent3d { width: atlas_w, height: atlas_h, depth_or_array_layers: 1 },
    );

    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
    let tex_state = Arc::new(crate::std_gpu::TextureState {
        texture,
        view,
        width: atlas_w,
        height: atlas_h,
    });

    // Return atlas info as a Map
    let mut map = std::collections::HashMap::new();
    map.insert("texture".to_string(), Value::GpuTexture(tex_state));
    map.insert("atlas_width".to_string(), Value::Int(atlas_w as i64));
    map.insert("atlas_height".to_string(), Value::Int(atlas_h as i64));
    map.insert("cell_width".to_string(), Value::Int(cw as i64));
    map.insert("cell_height".to_string(), Value::Int(ch as i64));
    map.insert("cols".to_string(), Value::Int(cols as i64));

    // Glyph position map: char_code -> [x, y]
    let mut pos_map = std::collections::HashMap::new();
    for (c, (gx, gy)) in &glyph_positions {
        pos_map.insert(
            format!("{}", *c as u32),
            Value::Array(vec![Value::Int(*gx as i64), Value::Int(*gy as i64)]),
        );
    }
    map.insert("glyphs".to_string(), Value::Map(Box::new(pos_map)));

    Ok(Value::Map(Box::new(map)))
}
```

- [ ] **Step 2: Register std_font in src/lib.rs**

```rust
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_font;
```

- [ ] **Step 3: Route font builtins in src/interpreter.rs**

```rust
            // ── std::font builtins (VOID terminal) ────────────────
            "font_load" | "font_metrics" | "font_atlas_create" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_font::call_font_builtin(self, name, args)
                }
            }
```

- [ ] **Step 4: Verify compilation**

Run: `cd /Users/ghost/Dev/hexa-lang && cargo check 2>&1 | tail -10`
Expected: compiles

- [ ] **Step 5: Commit**

```bash
git add src/std_font.rs src/lib.rs src/interpreter.rs
git commit -m "feat: add std_font module — font rasterization + glyph atlas for VOID"
```

---

## Task 7: Test PTY System with hexa

**Files:**
- Create: `examples/test_void_sys.hexa`

- [ ] **Step 1: Write PTY test**

```hexa
// test_void_sys.hexa — Test std_sys PTY functions
println("=== VOID std_sys test ===")

// Test env get/set
sys_env_set("VOID_TEST", "hello")
let val = sys_env_get("VOID_TEST")
assert(val == "hello", "env get/set failed")
println("[PASS] env get/set")

// Test PTY open
let pty = sys_pty_open()
println("[PASS] pty_open")

// Test PTY spawn (echo a simple command)
let pid = sys_pty_spawn(pty, "/bin/sh")
assert(pid > 0, "spawn failed")
println("[PASS] pty_spawn, pid=" + str(pid))

// Write a command
sys_pty_write(pty, "echo VOID_TEST_OK\n")

// Wait a bit then read
time_sleep(100)
let output = sys_pty_read(pty)
println("[INFO] pty output: " + output)

// Test resize
sys_pty_resize(pty, 40, 120)
println("[PASS] pty_resize")

// Cleanup
sys_pty_write(pty, "exit\n")
time_sleep(50)
sys_pty_close(pty)
println("[PASS] pty_close")

println("=== All std_sys tests passed ===")
```

- [ ] **Step 2: Build hexa**

Run: `cd /Users/ghost/Dev/hexa-lang && bash build.sh 2>&1 | tail -5`
Expected: build succeeds

- [ ] **Step 3: Run the test**

Run: `cd /Users/ghost/Dev/hexa-lang && ./hexa examples/test_void_sys.hexa`
Expected: All tests pass, output shows "VOID_TEST_OK"

- [ ] **Step 4: Commit**

```bash
git add examples/test_void_sys.hexa
git commit -m "test: add PTY system test for VOID terminal"
```

---

## Task 8: Create VOID Application — Minimal Terminal

**Files:**
- Create: `void/src/main.hexa`

- [ ] **Step 1: Create void directory structure**

```bash
mkdir -p /Users/ghost/Dev/hexa-lang/void/src/terminal
mkdir -p /Users/ghost/Dev/hexa-lang/void/src/render
```

- [ ] **Step 2: Write void/src/main.hexa — the VOID terminal entry point**

```hexa
// VOID — hexa-lang native terminal emulator
// Phase 1: Basic PTY + GPU text rendering

// ── Configuration ────────────────────────────────────────
let FONT_PATH = sys_env_get("VOID_FONT")
if FONT_PATH == "" {
  // Default: use system monospace font path
  // macOS: /System/Library/Fonts/Menlo.ttc
  // Linux: /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
  FONT_PATH = "/System/Library/Fonts/Menlo.ttc"
}
let FONT_SIZE = 16
let WINDOW_WIDTH = 1200
let WINDOW_HEIGHT = 800
let TERM_COLS = 80
let TERM_ROWS = 24

// ── WGSL Shader ──────────────────────────────────────────
let SHADER_SOURCE = "
struct Uniforms {
  grid_cols: u32,
  grid_rows: u32,
  cell_width: f32,
  cell_height: f32,
  atlas_cols: u32,
  atlas_cell_w: f32,
  atlas_cell_h: f32,
  atlas_tex_w: f32,
  atlas_tex_h: f32,
  screen_width: f32,
  screen_height: f32,
  _pad: u32,
}

struct CellData {
  glyph: u32,
  fg: u32,
  bg: u32,
  flags: u32,
}

@group(0) @binding(0) var atlas_tex: texture_2d<f32>;
@group(0) @binding(1) var atlas_sampler: sampler;
@group(0) @binding(2) var<storage, read> cells: array<CellData>;
@group(0) @binding(3) var<uniform> uniforms: Uniforms;

struct VertexOutput {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
  @location(1) fg_color: vec4<f32>,
  @location(2) bg_color: vec4<f32>,
  @location(3) cell_uv: vec2<f32>,
}

fn unpack_color(c: u32) -> vec4<f32> {
  let r = f32((c >> 16u) & 0xFFu) / 255.0;
  let g = f32((c >> 8u) & 0xFFu) / 255.0;
  let b = f32(c & 0xFFu) / 255.0;
  return vec4<f32>(r, g, b, 1.0);
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32, @builtin(instance_index) iid: u32) -> VertexOutput {
  let col = iid % uniforms.grid_cols;
  let row = iid / uniforms.grid_cols;

  // Quad vertices (2 triangles)
  let quad_x = array<f32, 6>(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);
  let quad_y = array<f32, 6>(0.0, 0.0, 1.0, 0.0, 1.0, 1.0);

  let qx = quad_x[vid];
  let qy = quad_y[vid];

  // Screen position (NDC)
  let px = (f32(col) + qx) * uniforms.cell_width / uniforms.screen_width * 2.0 - 1.0;
  let py = 1.0 - (f32(row) + qy) * uniforms.cell_height / uniforms.screen_height * 2.0;

  // Atlas UV
  let cell = cells[iid];
  let glyph_idx = cell.glyph - 32u; // ASCII offset
  let atlas_col = glyph_idx % uniforms.atlas_cols;
  let atlas_row = glyph_idx / uniforms.atlas_cols;

  let u0 = f32(atlas_col) * uniforms.atlas_cell_w / uniforms.atlas_tex_w;
  let v0 = f32(atlas_row) * uniforms.atlas_cell_h / uniforms.atlas_tex_h;
  let u1 = u0 + uniforms.atlas_cell_w / uniforms.atlas_tex_w;
  let v1 = v0 + uniforms.atlas_cell_h / uniforms.atlas_tex_h;

  let u = mix(u0, u1, qx);
  let v = mix(v0, v1, qy);

  var out: VertexOutput;
  out.pos = vec4<f32>(px, py, 0.0, 1.0);
  out.uv = vec2<f32>(u, v);
  out.fg_color = unpack_color(cell.fg);
  out.bg_color = unpack_color(cell.bg);
  out.cell_uv = vec2<f32>(qx, qy);
  return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
  let alpha = textureSample(atlas_tex, atlas_sampler, in.uv).a;
  let color = mix(in.bg_color, in.fg_color, alpha);
  return color;
}
"

// ── State ────────────────────────────────────────────────
let grid = []       // Array of cell data: [glyph, fg, bg, flags] per cell
let cursor_row = 0
let cursor_col = 0
let pty = null
let gpu_ctx = null
let pipeline_ref = null
let cell_buffer = null
let uniform_buffer = null
let atlas_info = null
let bind_group_ref = null

// ── Grid Helpers ─────────────────────────────────────────
fn init_grid(rows, cols) {
  let g = []
  for i in 0..(rows * cols) {
    // Each cell: [glyph_code, fg_color, bg_color, flags]
    g = g + [[32, 0xE0E0E0, 0x0A0A0F, 0]]
  }
  return g
}

fn grid_set(row, col, char_code) {
  if row >= 0 && row < TERM_ROWS && col >= 0 && col < TERM_COLS {
    let idx = row * TERM_COLS + col
    grid[idx] = [char_code, 0xE0E0E0, 0x0A0A0F, 0]
  }
}

fn grid_to_bytes() {
  // Pack grid into byte array for GPU buffer
  // Each cell = 4 u32 = 16 bytes
  let bytes = []
  for cell in grid {
    let g = cell[0]
    let fg = cell[1]
    let bg = cell[2]
    let flags = cell[3]
    // Little-endian u32 packing
    bytes = bytes + [g & 0xFF, (g >> 8) & 0xFF, (g >> 16) & 0xFF, (g >> 24) & 0xFF]
    bytes = bytes + [fg & 0xFF, (fg >> 8) & 0xFF, (fg >> 16) & 0xFF, (fg >> 24) & 0xFF]
    bytes = bytes + [bg & 0xFF, (bg >> 8) & 0xFF, (bg >> 16) & 0xFF, (bg >> 24) & 0xFF]
    bytes = bytes + [flags & 0xFF, (flags >> 8) & 0xFF, (flags >> 16) & 0xFF, (flags >> 24) & 0xFF]
  }
  return bytes
}

// ── Simple VT Processing ─────────────────────────────────
fn process_pty_output(data) {
  for ch in data {
    let code = ord(ch)
    if code == 10 {
      // Newline
      cursor_row = cursor_row + 1
      cursor_col = 0
      if cursor_row >= TERM_ROWS {
        scroll_up()
        cursor_row = TERM_ROWS - 1
      }
    } else if code == 13 {
      // Carriage return
      cursor_col = 0
    } else if code == 8 {
      // Backspace
      if cursor_col > 0 {
        cursor_col = cursor_col - 1
      }
    } else if code == 27 {
      // ESC - skip for now (Phase 2 will parse properly)
    } else if code >= 32 {
      // Printable character
      grid_set(cursor_row, cursor_col, code)
      cursor_col = cursor_col + 1
      if cursor_col >= TERM_COLS {
        cursor_col = 0
        cursor_row = cursor_row + 1
        if cursor_row >= TERM_ROWS {
          scroll_up()
          cursor_row = TERM_ROWS - 1
        }
      }
    }
  }
}

fn scroll_up() {
  // Shift all rows up by one
  for row in 0..(TERM_ROWS - 1) {
    for col in 0..TERM_COLS {
      let src = (row + 1) * TERM_COLS + col
      let dst = row * TERM_COLS + col
      grid[dst] = grid[src]
    }
  }
  // Clear last row
  for col in 0..TERM_COLS {
    let idx = (TERM_ROWS - 1) * TERM_COLS + col
    grid[idx] = [32, 0xE0E0E0, 0x0A0A0F, 0]
  }
}

// ── Callbacks ────────────────────────────────────────────
fn on_init(ctx, window, surface) {
  gpu_ctx = ctx

  // Load font and create atlas
  let font = font_load(FONT_PATH, FONT_SIZE)
  let metrics = font_metrics(font)
  atlas_info = font_atlas_create(font, ctx)

  // Open PTY and spawn shell
  pty = sys_pty_open()
  let shell = sys_env_get("SHELL")
  if shell == "" { shell = "/bin/zsh" }
  sys_pty_spawn(pty, shell)
  sys_pty_resize(pty, TERM_ROWS, TERM_COLS)

  // Initialize grid
  grid = init_grid(TERM_ROWS, TERM_COLS)

  // Create GPU resources
  let cell_count = TERM_ROWS * TERM_COLS
  cell_buffer = gpu_buffer_create(ctx, cell_count * 16) // 16 bytes per cell
  uniform_buffer = gpu_buffer_create(ctx, 48)           // 12 u32 = 48 bytes

  // Create pipeline
  pipeline_ref = gpu_pipeline_create(ctx, surface, SHADER_SOURCE)

  println("VOID initialized: " + str(TERM_COLS) + "x" + str(TERM_ROWS))
}

fn on_frame(ctx, window, surface) {
  // Read PTY output (non-blocking)
  let data = sys_pty_read(pty)
  if data != "" {
    process_pty_output(data)
  }

  // Update GPU buffers
  let cell_bytes = grid_to_bytes()
  gpu_buffer_write(ctx, cell_buffer, cell_bytes)

  // Update uniforms
  let cw = atlas_info["cell_width"]
  let ch_px = atlas_info["cell_height"]
  let win_size = gpu_window_size(window)
  let uniforms = [
    TERM_COLS & 0xFF, (TERM_COLS >> 8) & 0xFF, 0, 0,  // grid_cols
    TERM_ROWS & 0xFF, (TERM_ROWS >> 8) & 0xFF, 0, 0,  // grid_rows
    // ... (simplified — full packing in Phase 2)
  ]
  // For now, just render the cells
  // Full uniform packing will be refined when we integrate the shader properly

  // TODO: render_pass with bind group — requires surface texture acquisition
  // This will be completed when we wire up the full render loop
}

fn on_key(event) {
  let text = event["text"]
  if text != "" {
    sys_pty_write(pty, text)
  }
}

fn on_resize(width, height) {
  // Recalculate grid dimensions
  // Phase 2: proper reflow
  println("Resized: " + str(width) + "x" + str(height))
}

// ── Main ─────────────────────────────────────────────────
println("VOID — hexa-lang terminal emulator")
println("Starting...")

let config = {
  "width": WINDOW_WIDTH,
  "height": WINDOW_HEIGHT,
  "title": "VOID"
}

event_run_loop(config, on_init, on_frame, on_key, on_resize)
```

- [ ] **Step 3: Commit**

```bash
git add void/
git commit -m "feat: VOID Phase 1 — minimal terminal entry point (hexa)"
```

---

## Task 9: Build, Test, and Fix Integration

**Files:**
- Potentially modify any file from Tasks 1-8

- [ ] **Step 1: Build hexa with new modules**

Run: `cd /Users/ghost/Dev/hexa-lang && cargo build --release 2>&1 | tail -20`
Expected: compiles (may have warnings)

- [ ] **Step 2: Run PTY test**

Run: `cd /Users/ghost/Dev/hexa-lang && ./target/release/hexa examples/test_void_sys.hexa`
Expected: All PTY tests pass

- [ ] **Step 3: Run VOID terminal**

Run: `cd /Users/ghost/Dev/hexa-lang && ./target/release/hexa void/src/main.hexa`
Expected: Window opens, shell prompt visible, keyboard input works

- [ ] **Step 4: Fix any compilation or runtime errors**

Iterate on errors found in steps 1-3. Common issues:
- Value enum match arms need updating for new variants
- winit API changes between versions
- wgpu surface lifetime issues
- PTY read/write buffering

- [ ] **Step 5: Final commit**

```bash
git add -A
git commit -m "feat: VOID Phase 1 complete — working terminal with GPU rendering"
```

---

## Phase 1 Success Criteria

After completing all tasks:
1. `./hexa examples/test_void_sys.hexa` passes all PTY tests
2. `./hexa void/src/main.hexa` opens a GPU-rendered window
3. Shell prompt is visible in the window
4. Typing sends keystrokes to the shell
5. Shell output appears as text in the window
6. Window can be resized without crashing

## What Phase 2 Builds On

Phase 2 (Terminal Core) will replace the simple `process_pty_output` with a proper 6-state VT parser, implement full SGR color/style parsing, add scrollback buffer, and handle the alternate screen buffer.
