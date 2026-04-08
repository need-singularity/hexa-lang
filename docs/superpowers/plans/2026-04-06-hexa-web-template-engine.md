# Hexa Web Template Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `template { }` 블록을 파싱하고 HTML 문자열로 평가하는 내장 템플릿 엔진 구현 (Phase 1 of Hexa Web Engine)

**Architecture:** 새로운 AST 노드 `TemplateBlock`을 추가하고, 파서가 `template { h1 { "text" } }` 구문을 파싱. 인터프리터에서 `std_web_template.rs`의 렌더러를 호출해 HTML 문자열 생성. comptime 모드 지원으로 빌드 타임 HTML 확정. `http_serve`와 통합해 Content-Type: text/html 반환.

**Tech Stack:** Rust, 기존 Hexa 파서/인터프리터/comptime 엔진

**Spec:** `docs/superpowers/specs/2026-04-06-hexa-web-engine-design.md` 섹션 5

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `ready/src/ast.rs` | Modify | `TemplateNode`, `TemplateAttr` AST 노드 추가 |
| `ready/src/token.rs` | Modify | `Template` 키워드 토큰 추가 |
| `ready/src/lexer.rs` | Modify | `template` 키워드 렉싱 |
| `ready/src/parser.rs` | Modify | `template { }` 블록 파싱 → `TemplateBlock` AST |
| `ready/src/std_web_template.rs` | Create | 템플릿 렌더러 — AST→HTML 변환, escape, comptime 지원 |
| `ready/src/interpreter.rs` | Modify | `TemplateBlock` 평가 → `std_web_template::render()` 호출 |
| `ready/src/std_net.rs` | Modify | `http_serve` Content-Type 자동 감지 (text/html) |
| `ready/src/lib.rs` | Modify | `pub mod std_web_template;` 추가 |
| `ready/tests/web_template.rs` | Create | 통합 테스트 |

---

### Task 1: AST 노드 정의

**Files:**
- Modify: `ready/src/ast.rs`

- [ ] **Step 1: Write TemplateNode AST types**

`ready/src/ast.rs`의 `Stmt` enum 근처에 추가:

```rust
/// A node in a template tree — represents an HTML element, text, or control flow.
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum TemplateNode {
    /// HTML element: tag { attrs..., children... }
    Element {
        tag: String,
        attrs: Vec<TemplateAttr>,
        children: Vec<TemplateNode>,
    },
    /// Text content: "string with {interpolation}"
    Text(Expr),
    /// Control flow: for name in expr { children }
    For(String, Expr, Vec<TemplateNode>),
    /// Conditional: if expr { children } else { children }
    If(Expr, Vec<TemplateNode>, Option<Vec<TemplateNode>>),
    /// Match expression (sugar for nested ifs)
    Match(Expr, Vec<(Expr, Vec<TemplateNode>)>),
    /// Verify block inside template
    Verify(Expr),
    /// Invariant block inside template
    Invariant(Expr),
}

/// An attribute on a template element: key: expr
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct TemplateAttr {
    pub key: String,
    pub value: Expr,
}
```

`Expr` enum에 추가:

```rust
    /// template { ... } — HTML template block
    Template(Vec<TemplateNode>),
```

- [ ] **Step 2: Verify it compiles**

Run: `cd /Users/ghost/Dev/hexa-lang/ready && cargo check 2>&1 | head -20`
Expected: 컴파일 성공 (TemplateNode은 아직 사용되지 않으므로 dead_code 경고만)

- [ ] **Step 3: Commit**

```bash
cd /Users/ghost/Dev/hexa-lang/ready
git add src/ast.rs
git commit -m "feat(web): add TemplateNode and TemplateAttr AST types"
```

---

### Task 2: Template 키워드 토큰 추가

**Files:**
- Modify: `ready/src/token.rs`
- Modify: `ready/src/lexer.rs`

- [ ] **Step 1: Add Template token**

`ready/src/token.rs`의 `Token` enum에서, AI 키워드 그룹(Group 12) 뒤에 추가:

```rust
    // === Web (추가) ===
    Template,   // template keyword
```

- [ ] **Step 2: Add lexer keyword mapping**

`ready/src/lexer.rs`에서 키워드 매핑 테이블(보통 `match` 또는 `HashMap`)을 찾아서 추가:

```rust
"template" => Token::Template,
```

- [ ] **Step 3: Verify it compiles**

Run: `cd /Users/ghost/Dev/hexa-lang/ready && cargo check 2>&1 | head -20`
Expected: 컴파일 성공

- [ ] **Step 4: Commit**

```bash
cd /Users/ghost/Dev/hexa-lang/ready
git add src/token.rs src/lexer.rs
git commit -m "feat(web): add Template keyword token and lexer mapping"
```

---

### Task 3: 템플릿 파서 구현

**Files:**
- Modify: `ready/src/parser.rs`

- [ ] **Step 1: Add template parsing method**

`ready/src/parser.rs`의 `Parser` impl 블록에 추가. 기존 `parse_expr` 또는 `parse_primary`에서 `Token::Template`을 만나면 호출:

```rust
    /// Parse a template { ... } block into a Template expression.
    fn parse_template(&mut self) -> Result<Expr, HexaError> {
        // 'template' keyword already consumed
        self.expect(Token::LBrace)?;
        let nodes = self.parse_template_children()?;
        self.expect(Token::RBrace)?;
        Ok(Expr::Template(nodes))
    }

    /// Parse template children until closing brace.
    fn parse_template_children(&mut self) -> Result<Vec<TemplateNode>, HexaError> {
        let mut nodes = Vec::new();
        while !self.check(&Token::RBrace) && !self.is_at_end() {
            nodes.push(self.parse_template_node()?);
        }
        Ok(nodes)
    }

    /// Parse a single template node: element, text, for, if, verify, invariant.
    fn parse_template_node(&mut self) -> Result<TemplateNode, HexaError> {
        // verify { expr }
        if self.check(&Token::Verify) {
            self.advance();
            self.expect(Token::LBrace)?;
            let expr = self.parse_expr()?;
            self.expect(Token::RBrace)?;
            return Ok(TemplateNode::Verify(expr));
        }
        // invariant { expr }
        if self.check(&Token::Invariant) {
            self.advance();
            self.expect(Token::LBrace)?;
            let expr = self.parse_expr()?;
            self.expect(Token::RBrace)?;
            return Ok(TemplateNode::Invariant(expr));
        }
        // for name in expr { children }
        if self.check(&Token::For) {
            self.advance();
            let name = self.expect_ident()?;
            self.expect_keyword("in")?;
            let iter_expr = self.parse_expr()?;
            self.expect(Token::LBrace)?;
            let children = self.parse_template_children()?;
            self.expect(Token::RBrace)?;
            return Ok(TemplateNode::For(name, iter_expr, children));
        }
        // if expr { children } else { children }
        if self.check(&Token::If) {
            self.advance();
            let cond = self.parse_expr()?;
            self.expect(Token::LBrace)?;
            let then_nodes = self.parse_template_children()?;
            self.expect(Token::RBrace)?;
            let else_nodes = if self.check(&Token::Else) {
                self.advance();
                self.expect(Token::LBrace)?;
                let nodes = self.parse_template_children()?;
                self.expect(Token::RBrace)?;
                Some(nodes)
            } else {
                None
            };
            return Ok(TemplateNode::If(cond, then_nodes, else_nodes));
        }
        // String literal → Text node
        if let Some(Token::StringLit(_)) = self.peek_token() {
            let expr = self.parse_expr()?;
            return Ok(TemplateNode::Text(expr));
        }
        // Identifier → HTML element: tag { attrs..., children... }
        if let Some(Token::Ident(_)) = self.peek_token() {
            let tag = self.expect_ident()?;
            self.expect(Token::LBrace)?;
            let (attrs, children) = self.parse_element_body()?;
            self.expect(Token::RBrace)?;
            return Ok(TemplateNode::Element { tag, attrs, children });
        }
        Err(self.parse_err("expected template node: element, text, for, if, verify, or invariant"))
    }

    /// Parse element body: mix of attrs (key: expr) and child nodes.
    fn parse_element_body(&mut self) -> Result<(Vec<TemplateAttr>, Vec<TemplateNode>), HexaError> {
        let mut attrs = Vec::new();
        let mut children = Vec::new();
        while !self.check(&Token::RBrace) && !self.is_at_end() {
            // Try to parse as attr: ident : expr ,
            if self.is_attr_start() {
                let key = self.expect_ident()?;
                self.expect(Token::Colon)?;
                let value = self.parse_expr()?;
                attrs.push(TemplateAttr { key, value });
                // optional comma
                if self.check(&Token::Comma) {
                    self.advance();
                }
            } else {
                children.push(self.parse_template_node()?);
            }
        }
        Ok((attrs, children))
    }

    /// Check if current position looks like an attribute (ident followed by colon).
    fn is_attr_start(&self) -> bool {
        matches!(self.peek_token(), Some(Token::Ident(_)))
            && matches!(self.peek_nth(1), Some(Token::Colon))
    }
```

- [ ] **Step 2: Wire into parse_primary**

`parse_primary` (또는 `parse_expr`에서 primary를 파싱하는 곳)에서 `Token::Template` 분기 추가:

```rust
Token::Template => {
    self.advance();
    self.parse_template()
}
```

- [ ] **Step 3: Verify it compiles**

Run: `cd /Users/ghost/Dev/hexa-lang/ready && cargo check 2>&1 | head -30`
Expected: 컴파일 성공 (일부 메서드 미사용 경고는 OK)

- [ ] **Step 4: Commit**

```bash
cd /Users/ghost/Dev/hexa-lang/ready
git add src/parser.rs
git commit -m "feat(web): parse template { } blocks into TemplateNode AST"
```

---

### Task 4: 템플릿 렌더러 (`std_web_template.rs`) 구현

**Files:**
- Create: `ready/src/std_web_template.rs`
- Modify: `ready/src/lib.rs`

- [ ] **Step 1: Write the template renderer**

Create `ready/src/std_web_template.rs`:

```rust
//! std::web::template — AST-based HTML template renderer for HEXA-LANG.
//!
//! Renders TemplateNode trees into HTML strings with:
//!   - Automatic HTML escaping for interpolated values
//!   - Control flow (for, if, match)
//!   - Verify/invariant enforcement
//!   - comptime compatibility (pure computation only)

#![allow(dead_code)]

use crate::ast::{TemplateNode, TemplateAttr, Expr};
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Escape HTML special characters to prevent XSS.
pub fn html_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&#x27;"),
            _ => out.push(c),
        }
    }
    out
}

/// Void elements that must not have a closing tag.
const VOID_ELEMENTS: &[&str] = &[
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr",
];

/// Render a list of template nodes into an HTML string.
pub fn render_template(
    interp: &mut Interpreter,
    nodes: &[TemplateNode],
) -> Result<String, HexaError> {
    let mut html = String::new();
    for node in nodes {
        render_node(interp, node, &mut html)?;
    }
    Ok(html)
}

fn render_node(
    interp: &mut Interpreter,
    node: &TemplateNode,
    html: &mut String,
) -> Result<(), HexaError> {
    match node {
        TemplateNode::Text(expr) => {
            let val = interp.eval_expr(expr)?;
            let text = format!("{}", val);
            html.push_str(&html_escape(&text));
        }
        TemplateNode::Element { tag, attrs, children } => {
            html.push('<');
            html.push_str(tag);
            for attr in attrs {
                html.push(' ');
                html.push_str(&attr.key);
                html.push_str("=\"");
                let val = interp.eval_expr(&attr.value)?;
                html.push_str(&html_escape(&format!("{}", val)));
                html.push('"');
            }
            if VOID_ELEMENTS.contains(&tag.as_str()) && children.is_empty() {
                html.push_str(" />");
            } else {
                html.push('>');
                for child in children {
                    render_node(interp, child, html)?;
                }
                html.push_str("</");
                html.push_str(tag);
                html.push('>');
            }
        }
        TemplateNode::For(name, iter_expr, children) => {
            let iter_val = interp.eval_expr(iter_expr)?;
            match iter_val {
                Value::Array(items) => {
                    for item in &items {
                        interp.env.set(name.clone(), item.clone());
                        for child in children {
                            render_node(interp, child, html)?;
                        }
                    }
                }
                _ => {
                    return Err(template_err(interp, format!(
                        "template for loop requires array, got {}", iter_val
                    )));
                }
            }
        }
        TemplateNode::If(cond, then_nodes, else_nodes) => {
            let cond_val = interp.eval_expr(cond)?;
            if is_truthy(&cond_val) {
                for child in then_nodes {
                    render_node(interp, child, html)?;
                }
            } else if let Some(else_children) = else_nodes {
                for child in else_children {
                    render_node(interp, child, html)?;
                }
            }
        }
        TemplateNode::Match(expr, arms) => {
            let val = interp.eval_expr(expr)?;
            for (pattern, children) in arms {
                let pat_val = interp.eval_expr(pattern)?;
                if values_eq(&val, &pat_val) {
                    for child in children {
                        render_node(interp, child, html)?;
                    }
                    return Ok(());
                }
            }
        }
        TemplateNode::Verify(expr) => {
            let val = interp.eval_expr(expr)?;
            if !is_truthy(&val) {
                return Err(template_err(interp, "template verify assertion failed".into()));
            }
        }
        TemplateNode::Invariant(expr) => {
            let val = interp.eval_expr(expr)?;
            if !is_truthy(&val) {
                return Err(template_err(interp, "template invariant violated".into()));
            }
        }
    }
    Ok(())
}

fn is_truthy(val: &Value) -> bool {
    match val {
        Value::Bool(b) => *b,
        Value::Int(n) => *n != 0,
        Value::Str(s) => !s.is_empty(),
        Value::Void => false,
        Value::Array(a) => !a.is_empty(),
        _ => true,
    }
}

fn values_eq(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Int(x), Value::Int(y)) => x == y,
        (Value::Float(x), Value::Float(y)) => x == y,
        (Value::Str(x), Value::Str(y)) => x == y,
        (Value::Bool(x), Value::Bool(y)) => x == y,
        _ => false,
    }
}

fn template_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_html_escape() {
        assert_eq!(html_escape("<script>alert('xss')</script>"),
                   "&lt;script&gt;alert(&#x27;xss&#x27;)&lt;/script&gt;");
        assert_eq!(html_escape("hello & world"), "hello &amp; world");
        assert_eq!(html_escape("a\"b"), "a&quot;b");
        assert_eq!(html_escape("clean text"), "clean text");
    }

    #[test]
    fn test_void_elements() {
        assert!(VOID_ELEMENTS.contains(&"br"));
        assert!(VOID_ELEMENTS.contains(&"img"));
        assert!(!VOID_ELEMENTS.contains(&"div"));
    }
}
```

- [ ] **Step 2: Register module in lib.rs**

`ready/src/lib.rs`에 추가 (`std_net` 근처):

```rust
#[cfg(not(target_arch = "wasm32"))]
#[allow(dead_code)]
pub mod std_web_template;
```

- [ ] **Step 3: Verify it compiles and tests pass**

Run: `cd /Users/ghost/Dev/hexa-lang/ready && cargo test std_web_template 2>&1 | tail -10`
Expected: 2 tests passed

- [ ] **Step 4: Commit**

```bash
cd /Users/ghost/Dev/hexa-lang/ready
git add src/std_web_template.rs src/lib.rs
git commit -m "feat(web): implement template renderer with HTML escape and control flow"
```

---

### Task 5: 인터프리터에 Template 평가 연결

**Files:**
- Modify: `ready/src/interpreter.rs`

- [ ] **Step 1: Add Template evaluation in eval_expr**

`interpreter.rs`의 `eval_expr` 메서드에서 `Expr::Template` 분기 추가:

```rust
Expr::Template(nodes) => {
    crate::std_web_template::render_template(self, nodes)
        .map(Value::Str)
}
```

- [ ] **Step 2: Verify it compiles**

Run: `cd /Users/ghost/Dev/hexa-lang/ready && cargo check 2>&1 | head -20`
Expected: 컴파일 성공

- [ ] **Step 3: Commit**

```bash
cd /Users/ghost/Dev/hexa-lang/ready
git add src/interpreter.rs
git commit -m "feat(web): wire Template expr evaluation to std_web_template renderer"
```

---

### Task 6: http_serve HTML Content-Type 자동 감지

**Files:**
- Modify: `ready/src/std_net.rs`

- [ ] **Step 1: Update http_serve response to detect HTML**

`ready/src/std_net.rs`의 `builtin_http_serve` 함수에서 응답 전송 부분 수정:

```rust
        // Detect content type from response
        let content_type = if response_str.trim_start().starts_with('<') {
            "text/html; charset=utf-8"
        } else {
            "text/plain; charset=utf-8"
        };

        let http_response = format!(
            "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: {}\r\nConnection: close\r\n\r\n{}",
            response_str.len(),
            content_type,
            response_str
        );
```

기존 하드코딩된 `text/plain` 줄을 위 코드로 교체.

- [ ] **Step 2: Verify it compiles**

Run: `cd /Users/ghost/Dev/hexa-lang/ready && cargo check 2>&1 | head -20`
Expected: 컴파일 성공

- [ ] **Step 3: Commit**

```bash
cd /Users/ghost/Dev/hexa-lang/ready
git add src/std_net.rs
git commit -m "feat(web): auto-detect text/html Content-Type in http_serve"
```

---

### Task 7: 통합 테스트

**Files:**
- Create: `ready/tests/web_template.rs`

- [ ] **Step 1: Write integration tests**

Create `ready/tests/web_template.rs`:

```rust
//! Integration tests for template { } blocks.

use hexa_lang::run_source;

#[test]
fn test_template_simple_element() {
    let (out, err) = run_source(r#"
        let html = template {
            h1 { "Hello World" }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert_eq!(out.trim(), "<h1>Hello World</h1>");
}

#[test]
fn test_template_nested_elements() {
    let (out, err) = run_source(r#"
        let html = template {
            div {
                h1 { "Title" }
                p { "Body text" }
            }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert_eq!(out.trim(), "<div><h1>Title</h1><p>Body text</p></div>");
}

#[test]
fn test_template_with_attributes() {
    let (out, err) = run_source(r#"
        let html = template {
            a { href: "/about", "About Us" }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert_eq!(out.trim(), r#"<a href="/about">About Us</a>"#);
}

#[test]
fn test_template_variable_interpolation() {
    let (out, err) = run_source(r#"
        let name = "Hexa"
        let html = template {
            h1 { name }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert_eq!(out.trim(), "<h1>Hexa</h1>");
}

#[test]
fn test_template_xss_escape() {
    let (out, err) = run_source(r#"
        let evil = "<script>alert('xss')</script>"
        let html = template {
            p { evil }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert!(out.contains("&lt;script&gt;"));
    assert!(!out.contains("<script>"));
}

#[test]
fn test_template_for_loop() {
    let (out, err) = run_source(r#"
        let items = ["apple", "banana", "cherry"]
        let html = template {
            ul {
                for item in items {
                    li { item }
                }
            }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert_eq!(out.trim(), "<ul><li>apple</li><li>banana</li><li>cherry</li></ul>");
}

#[test]
fn test_template_if_condition() {
    let (out, err) = run_source(r#"
        let show = true
        let html = template {
            if show {
                p { "visible" }
            }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert_eq!(out.trim(), "<p>visible</p>");
}

#[test]
fn test_template_if_else() {
    let (out, err) = run_source(r#"
        let logged_in = false
        let html = template {
            if logged_in {
                p { "Welcome" }
            } else {
                p { "Please login" }
            }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert_eq!(out.trim(), "<p>Please login</p>");
}

#[test]
fn test_template_void_element() {
    let (out, err) = run_source(r#"
        let html = template {
            br {}
            img { src: "/logo.png", alt: "Logo" }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert!(out.contains("<br />"));
    assert!(out.contains(r#"<img src="/logo.png" alt="Logo" />"#));
}

#[test]
fn test_template_verify_pass() {
    let (out, err) = run_source(r#"
        let name = "Hexa"
        let html = template {
            verify { len(name) < 100 }
            h1 { name }
        }
        println(html)
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert_eq!(out.trim(), "<h1>Hexa</h1>");
}

#[test]
fn test_template_verify_fail() {
    let (_out, err) = run_source(r#"
        let name = ""
        let html = template {
            verify { len(name) > 0 }
            h1 { name }
        }
        println(html)
    "#);
    assert!(!err.is_empty());
    assert!(err.contains("verify"));
}

#[test]
fn test_template_comptime() {
    let (out, err) = run_source(r#"
        comptime {
            let html = template {
                h1 { "Static Page" }
                p { "Built at compile time" }
            }
            println(html)
        }
    "#);
    assert!(err.is_empty(), "error: {}", err);
    assert!(out.contains("<h1>Static Page</h1>"));
}
```

- [ ] **Step 2: Run integration tests**

Run: `cd /Users/ghost/Dev/hexa-lang/ready && cargo test web_template 2>&1 | tail -20`
Expected: All tests pass

- [ ] **Step 3: Fix any failures, re-run**

If tests fail, fix the relevant source file and re-run. Common issues:
- Parser not recognizing `template` in expression position → check `parse_primary`
- Variable name as text node not evaluating → check `TemplateNode::Text` handling
- comptime forbidding template → may need to allow `template` in comptime context

- [ ] **Step 4: Commit**

```bash
cd /Users/ghost/Dev/hexa-lang/ready
git add tests/web_template.rs
git commit -m "test(web): add 12 integration tests for template engine"
```

---

### Task 8: http_serve + template 통합 예제 테스트

**Files:**
- Create: `ready/examples/web_hello.hexa`

- [ ] **Step 1: Create example file**

Create `ready/examples/web_hello.hexa`:

```hexa
use std.net
use std.web

let title = "Hexa Web"
let items = ["Home", "About", "Contact"]

http_serve("0.0.0.0:8080", fn(req) {
    let path = req["path"]

    match path {
        "/" => template {
            html {
                head {
                    title { title }
                }
                body {
                    h1 { title }
                    nav {
                        ul {
                            for item in items {
                                li {
                                    a { href: "/" + item, item }
                                }
                            }
                        }
                    }
                    p { "Welcome to Hexa Web Engine" }
                }
            }
        }
        _ => template {
            html {
                body {
                    h1 { "404 Not Found" }
                    p { "Path not found: " + path }
                    a { href: "/", "Go Home" }
                }
            }
        }
    }
})
```

- [ ] **Step 2: Verify it parses (dry run)**

Run: `cd /Users/ghost/Dev/hexa-lang/ready && cargo run -- --check examples/web_hello.hexa 2>&1 | head -10`
(또는 파서만 실행하는 방법이 있으면 그걸로)
Expected: 파싱 성공 (서버 실행은 안 함)

- [ ] **Step 3: Commit**

```bash
cd /Users/ghost/Dev/hexa-lang/ready
git add examples/web_hello.hexa
git commit -m "feat(web): add web_hello.hexa example — template + http_serve"
```

---

### Task 9: 전체 테스트 스위트 실행

**Files:** None (검증만)

- [ ] **Step 1: Run full test suite**

Run: `cd /Users/ghost/Dev/hexa-lang/ready && cargo test 2>&1 | tail -20`
Expected: 기존 테스트 전부 통과 + 새 web_template 테스트 통과

- [ ] **Step 2: Fix any regressions**

새 코드가 기존 테스트를 깨뜨리면 수정. 주의점:
- `Token` enum 변경으로 exhaustive match 누락
- `Expr` enum 변경으로 interpreter의 `eval_expr` match 누락
- AST 타입 변경으로 formatter/linter 영향

- [ ] **Step 3: Final commit**

```bash
cd /Users/ghost/Dev/hexa-lang/ready
git add -A
git commit -m "feat(web): Hexa Template Engine — Phase 1 complete

template { } 블록 파싱, HTML 렌더링, XSS escape, for/if 제어,
verify/invariant 검증, comptime 지원, http_serve text/html 자동 감지.
12 integration tests, 2 unit tests."
```

---

## Summary

| Task | 내용 | 파일 수 |
|------|------|---------|
| 1 | AST 노드 정의 | 1 |
| 2 | 토큰 + 렉서 | 2 |
| 3 | 파서 | 1 |
| 4 | 렌더러 | 2 |
| 5 | 인터프리터 연결 | 1 |
| 6 | http_serve HTML 감지 | 1 |
| 7 | 통합 테스트 | 1 |
| 8 | 예제 파일 | 1 |
| 9 | 전체 검증 | 0 |

총 9 tasks, ~10 files, 예상 커밋 9개.
