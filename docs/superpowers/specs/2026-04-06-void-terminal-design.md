# VOID — hexa-lang Native Terminal Emulator Design Spec

## Overview

**VOID** is a full-featured, GPU-accelerated terminal emulator written 100% in hexa-lang. Zero Rust dependencies added — VOID compiles through hexa's own JIT/native codegen and calls OS APIs directly via a new `extern` FFI system.

- **Repository:** github.com/need-singularity/void
- **Language:** 100% hexa — no Rust additions to hexa-lang's Cargo.toml
- **Platforms:** macOS + Linux simultaneous
- **Rendering:** Metal (macOS) / Vulkan (Linux) via hexa extern FFI
- **Font:** CoreText (macOS) / FreeType (Linux) via hexa extern FFI
- **Windowing:** Cocoa/AppKit (macOS) / X11 (Linux) via hexa extern FFI
- **Terminal Protocol:** 6-tier (VT100 → xterm → 256color → TrueColor → Kitty → VOID)
- **Release Scope:** Full-featured first release

## Architecture — 6 Layers (n=6)

```
Layer 6 ─ AI/Consciousness   intent, generate, NEXUS-6 scan
Layer 5 ─ Plugins            hexa scripting, themes, automation
Layer 4 ─ UI/Layout          6-panel hexagon, tabs, statusbar
Layer 3 ─ Terminal Core      VT parser, cell grid, scrollback
Layer 2 ─ Rendering          Metal/Vulkan shaders, glyph atlas, GPU pipeline
Layer 1 ─ System             PTY, window, events, signals (via extern FFI)
```

## Layer 0: hexa Language Extension — extern FFI

This is the foundational change that makes everything possible. hexa gains the ability to call any C function from any system library.

### extern Keyword (54th keyword)

```hexa
// Declare an external C function
extern fn openpty(master: *Int, slave: *Int, name: *Byte,
                  termp: *Void, winp: *Void) -> Int

// With library annotation
@link("libc")
extern fn fork() -> Int

@link("Cocoa")
extern fn NSApplicationLoad() -> Bool

@link("Metal")
extern fn MTLCreateSystemDefaultDevice() -> *Void
```

### Pointer Types (extern-only)

```hexa
*Int        // pointer to Int (i64*)
*Byte       // pointer to Byte (u8*, for C strings)
*Void       // opaque pointer (void*)
*Float      // pointer to Float (f64*)
```

Pointer operations (extern context only):
```hexa
let p = alloc(size)           // heap allocate, returns *Void
let val = deref(p)            // dereference pointer
let p2 = offset(p, n)        // pointer arithmetic
free(p)                       // deallocate
let s = cstring("hello")     // String → null-terminated *Byte
let h = from_cstring(p)      // *Byte → hexa String
```

### @link Attribute

```hexa
@link("libc")         // macOS: /usr/lib/libc.dylib, Linux: libc.so.6
@link("Cocoa")        // macOS: Cocoa.framework
@link("Metal")        // macOS: Metal.framework
@link("CoreText")     // macOS: CoreText.framework
@link("X11")          // Linux: libX11.so
@link("vulkan")       // Linux: libvulkan.so
@link("freetype")     // Linux: libfreetype.so
```

### Implementation Strategy

hexa's JIT already has the infrastructure:
- `JITBuilder::symbol(name, ptr)` registers external symbols
- `declare_function(name, Linkage::Import, &sig)` declares external functions
- `call(func_ref, &args)` generates native call instructions

What's needed:
1. Add `extern` token to `src/token.rs` (keyword #54)
2. Add `ExternFnDecl` to `src/ast.rs`
3. Parse `extern fn` in `src/parser.rs`
4. In JIT (`src/jit.rs`): use `dlopen`/`dlsym` to resolve library symbols at runtime
5. Register resolved symbols via existing `builder.symbol()` mechanism
6. In native codegen (`src/codegen/`): emit PLT-style calls or direct symbol references

## Layer 1: System — PTY, Window, Events (100% hexa via extern)

### PTY (libc)

```hexa
@link("libc")
extern fn openpty(master: *Int, slave: *Int, name: *Byte,
                  termp: *Void, winp: *Void) -> Int
extern fn fork() -> Int
extern fn setsid() -> Int
extern fn dup2(old: Int, new: Int) -> Int
extern fn execvp(file: *Byte, argv: **Byte) -> Int
extern fn read(fd: Int, buf: *Byte, count: Int) -> Int
extern fn write(fd: Int, buf: *Byte, count: Int) -> Int
extern fn close(fd: Int) -> Int
extern fn ioctl(fd: Int, request: Int, argp: *Void) -> Int
extern fn fcntl(fd: Int, cmd: Int, arg: Int) -> Int
extern fn kill(pid: Int, sig: Int) -> Int
extern fn waitpid(pid: Int, status: *Int, options: Int) -> Int
```

### Window — macOS (Cocoa/AppKit)

```hexa
@link("Cocoa")
extern fn NSApplicationLoad() -> Bool
extern fn objc_getClass(name: *Byte) -> *Void
extern fn sel_registerName(name: *Byte) -> *Void
extern fn objc_msgSend(obj: *Void, sel: *Void, ...) -> *Void

// Window creation via Objective-C runtime
fn create_window(width: Int, height: Int, title: String) -> *Void {
  let NSApp = objc_getClass(cstring("NSApplication"))
  let alloc_sel = sel_registerName(cstring("sharedApplication"))
  let app = objc_msgSend(NSApp, alloc_sel)
  
  let NSWindow = objc_getClass(cstring("NSWindow"))
  // ... create window via objc_msgSend
}
```

### Window — Linux (X11)

```hexa
@link("X11")
extern fn XOpenDisplay(name: *Byte) -> *Void
extern fn XCreateSimpleWindow(display: *Void, parent: Int,
  x: Int, y: Int, w: Int, h: Int, border: Int,
  fg: Int, bg: Int) -> Int
extern fn XMapWindow(display: *Void, window: Int) -> Int
extern fn XNextEvent(display: *Void, event: *Void) -> Int
extern fn XSelectInput(display: *Void, window: Int, mask: Int) -> Int
```

### GPU — macOS (Metal)

```hexa
@link("Metal")
extern fn MTLCreateSystemDefaultDevice() -> *Void
// Metal API via objc_msgSend (Objective-C runtime)
```

### GPU — Linux (Vulkan)

```hexa
@link("vulkan")
extern fn vkCreateInstance(info: *Void, alloc: *Void, inst: **Void) -> Int
extern fn vkEnumeratePhysicalDevices(inst: *Void, count: *Int, devs: **Void) -> Int
// ... full Vulkan API
```

### Font — macOS (CoreText)

```hexa
@link("CoreText")
extern fn CTFontCreateWithName(name: *Void, size: Float, matrix: *Void) -> *Void
extern fn CTFontGetGlyphsForCharacters(font: *Void, chars: *Int, glyphs: *Int, count: Int) -> Bool
```

### Font — Linux (FreeType)

```hexa
@link("freetype")
extern fn FT_Init_FreeType(lib: **Void) -> Int
extern fn FT_New_Face(lib: *Void, path: *Byte, index: Int, face: **Void) -> Int
extern fn FT_Set_Pixel_Sizes(face: *Void, w: Int, h: Int) -> Int
extern fn FT_Load_Char(face: *Void, code: Int, flags: Int) -> Int
```

## Layer 2: Rendering Pipeline

Same as previous spec, but shaders are Metal Shading Language (macOS) or SPIR-V (Linux) instead of WGSL. Generated from hexa code.

### Glyph Atlas
- Font rasterized via CoreText/FreeType extern calls
- Atlas texture uploaded via Metal/Vulkan extern calls
- ASCII 0x20~0x7E preloaded, CJK/emoji on-demand LRU

### Cell-to-GPU Pipeline
```
Terminal Grid (rows x cols)
  Each cell = { glyph_index: u16, fg: u32, bg: u32, flags: u8 }
  Dirty marking per-row → partial GPU buffer upload
```

### Render Loop (60fps)
| Step | Operation | Budget |
|------|-----------|--------|
| 1 | Event poll (X11/Cocoa) | < 1ms |
| 2 | PTY read (non-blocking) | < 1ms |
| 3 | VT parser → cell grid | < 2ms |
| 4 | Dirty cells → GPU buffer | < 1ms |
| 5 | GPU draw call | < 2ms |
| 6 | Present | < 1ms |

## Layer 3: Terminal Core

Same as previous spec — 100% hexa code:
- VT 6-state parser (Ground, Escape, CSI, OSC, DCS, VOID)
- Cell grid with dirty tracking
- Scrollback ring buffer (10K lines)
- VOID protocol (ESC ] 777 ; type ; payload ST)

## Layer 4: UI/Layout

Same as previous spec — 100% hexa code:
- 4 layout modes (Solo, Split, Tri, Hex)
- Hive → Cell → Tab hierarchy
- Statusbar, tabbar, theme system
- Key bindings (all configurable via config.hexa)

## Layer 5: Plugin System

Same as previous spec — 100% hexa code:
- ~/.void/plugins/ with manifest.hexa
- 6 event hooks
- Plugin API (void.on, void.statusbar, void.keybind, etc.)
- Command palette (Ctrl+Shift+P)

## Layer 6: AI/Consciousness

Same as previous spec — 100% hexa code:
- intent/generate for command suggestion
- 3-tier autocompletion
- Consciousness-based UX (NEXUS-6 integration)

## Project Structure

```
void/                          # Separate repo: need-singularity/void
├── src/
│   ├── main.hexa             # Entry point
│   ├── platform/
│   │   ├── macos.hexa        # Cocoa + Metal + CoreText extern bindings
│   │   ├── linux.hexa        # X11 + Vulkan + FreeType extern bindings
│   │   └── common.hexa       # Platform abstraction layer
│   ├── sys/
│   │   ├── pty.hexa          # PTY management (libc extern)
│   │   └── signal.hexa       # Signal handling
│   ├── terminal/
│   │   ├── vt_parser.hexa    # VT 6-state parser
│   │   ├── grid.hexa         # Cell grid + scrollback
│   │   └── protocol.hexa     # VOID protocol handler
│   ├── render/
│   │   ├── atlas.hexa        # Glyph atlas manager
│   │   ├── pipeline.hexa     # GPU render pipeline
│   │   └── shaders.hexa      # Metal/Vulkan shader source
│   ├── ui/
│   │   ├── layout.hexa       # Hive/Cell/Tab layout
│   │   ├── statusbar.hexa    # Status bar
│   │   ├── tabbar.hexa       # Tab bar
│   │   ├── palette.hexa      # Command palette
│   │   └── theme.hexa        # Theme engine
│   ├── plugin/
│   │   ├── loader.hexa       # Plugin loader
│   │   ├── api.hexa          # Plugin API
│   │   └── hooks.hexa        # Event hook system
│   └── ai/
│       ├── suggest.hexa      # AI command suggestion
│       ├── complete.hexa     # 3-tier autocompletion
│       └── conscious.hexa    # Consciousness UX
├── themes/
│   ├── void_dark.hexa
│   └── void_light.hexa
├── plugins/                   # Bundled plugins
│   ├── git-status/
│   ├── ai-suggest/
│   └── nexus-scan/
��── README.md
```

## Build Strategy

```bash
# Compile VOID with hexa's JIT (development)
hexa --native void/src/main.hexa

# Or via hexa's AOT codegen (release)
hexa --compile void/src/main.hexa -o void
```

No Cargo.toml changes needed. VOID is a pure hexa project that uses hexa's existing compiler infrastructure.

## hexa-lang Changes Required

All changes are in the hexa-lang repo to support the `extern` FFI:

| File | Change |
|------|--------|
| `src/token.rs` | Add `Extern` keyword (#54) |
| `src/ast.rs` | Add `ExternFnDecl` node, `LinkAttr`, pointer type support |
| `src/parser.rs` | Parse `extern fn`, `@link()`, pointer types |
| `src/interpreter.rs` | dlopen/dlsym to call extern functions at runtime |
| `src/jit.rs` | Dynamic symbol resolution via dlopen/dlsym |
| `src/codegen/arm64.rs` | External symbol call support |
| `src/codegen/x86_64.rs` | External symbol call support |
| `src/env.rs` | Add `Value::Pointer(*mut u8)` variant |

## n=6 Alignment

| Element | Count | n=6 Mapping |
|---------|-------|-------------|
| Architecture layers | 6 | n |
| VT parser states | 6 | n |
| Max panels (Hex mode) | 6 | n |
| Event hooks | 6 | n |
| Protocol tiers | 6 | n |
| Layout modes | 4 | tau |
| Platform APIs per OS | 4 | tau (libc, window, GPU, font) |
| Keywords total | 54 | sigma * tau + phi (12*4+2) |

## Phase Plan

```
Phase 1: extern FFI — hexa 언어에 extern 키워드 + 포인터 타입 + @link 추가
Phase 2: PTY + Window — libc PTY + Cocoa 윈도우 (hexa extern으로)
Phase 3: GPU + Font — Metal 렌더링 + CoreText 폰트 (hexa extern으로)
Phase 4: Terminal Core — VT 파서 + 셀 그리드 + 스크롤백
Phase 5: UI — 레이아웃 + 탭 + 상태바 + 테마
Phase 6: Plugin + AI — 스크립팅 + 의식 UX
```
