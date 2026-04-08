//! std::web::template — AST-based HTML template renderer for HEXA-LANG.

#![allow(dead_code)]

use crate::ast::{TemplateNode, TemplateAttr, Expr};
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// HTML void elements that must self-close (no children, no closing tag).
pub const VOID_ELEMENTS: &[&str] = &[
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr",
];

/// Escape HTML special characters to prevent XSS.
pub fn html_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for ch in s.chars() {
        match ch {
            '&'  => out.push_str("&amp;"),
            '<'  => out.push_str("&lt;"),
            '>'  => out.push_str("&gt;"),
            '"'  => out.push_str("&quot;"),
            '\'' => out.push_str("&#39;"),
            c    => out.push(c),
        }
    }
    out
}

/// Evaluate truthiness of a `Value`.
pub fn is_truthy(val: &Value) -> bool {
    match val {
        Value::Bool(b)   => *b,
        Value::Int(n)    => *n != 0,
        Value::Str(s)    => !s.is_empty(),
        Value::Array(a)  => !a.is_empty(),
        Value::Void      => false,
        _                => true,
    }
}

/// Simple equality comparison for template match arms.
pub fn values_eq(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Int(x),   Value::Int(y))   => x == y,
        (Value::Float(x), Value::Float(y)) => x == y,
        (Value::Str(x),   Value::Str(y))   => x == y,
        (Value::Bool(x),  Value::Bool(y))  => x == y,
        _ => false,
    }
}

/// Convert a `Value` to its string representation for template output.
fn value_to_string(val: &Value) -> String {
    match val {
        Value::Str(s)   => s.clone(),
        Value::Int(n)   => n.to_string(),
        Value::Float(f) => f.to_string(),
        Value::Bool(b)  => b.to_string(),
        Value::Char(c)  => c.to_string(),
        Value::Void     => String::new(),
        other           => format!("{:?}", other),
    }
}

/// Build a `HexaError` with Runtime class (mirrors `Interpreter::runtime_err`).
fn template_err(message: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message,
        line: 0,
        col: 0,
        hint: None,
    }
}

/// Main entry point: render a slice of `TemplateNode`s to an HTML string.
pub fn render_template(interp: &mut Interpreter, nodes: &[TemplateNode]) -> Result<String, HexaError> {
    let mut html = String::new();
    for node in nodes {
        render_node(interp, node, &mut html)?;
    }
    Ok(html)
}

/// Recursively render a single `TemplateNode` into `html`.
pub fn render_node(interp: &mut Interpreter, node: &TemplateNode, html: &mut String) -> Result<(), HexaError> {
    match node {
        // ── Text ────────────────────────────────────────────────
        TemplateNode::Text(expr) => {
            let val = interp.eval_expr_pub(expr)?;
            html.push_str(&html_escape(&value_to_string(&val)));
        }

        // ── Element ─────────────────────────────────────────────
        TemplateNode::Element { tag, attrs, children } => {
            let tag_lower = tag.to_lowercase();
            html.push('<');
            html.push_str(&tag_lower);

            // Render attributes
            for TemplateAttr { key, value } in attrs {
                let attr_val = interp.eval_expr_pub(value)?;
                html.push(' ');
                html.push_str(&html_escape(key));
                html.push_str("=\"");
                html.push_str(&html_escape(&value_to_string(&attr_val)));
                html.push('"');
            }

            if VOID_ELEMENTS.contains(&tag_lower.as_str()) {
                html.push_str(" />");
            } else {
                html.push('>');
                for child in children {
                    render_node(interp, child, html)?;
                }
                html.push_str("</");
                html.push_str(&tag_lower);
                html.push('>');
            }
        }

        // ── For loop ────────────────────────────────────────────
        TemplateNode::For(name, iter_expr, children) => {
            let iter_val = interp.eval_expr_pub(iter_expr)?;
            let items = match iter_val {
                Value::Array(arr) => arr,
                other => return Err(template_err(format!(
                    "template for: expected array, got {:?}", other
                ))),
            };
            interp.env.push_scope();
            for item in items {
                interp.env.define(name, item);
                for child in children {
                    render_node(interp, child, html)?;
                }
            }
            interp.env.pop_scope();
        }

        // ── If / else ───────────────────────────────────────────
        TemplateNode::If(cond_expr, then_nodes, else_nodes) => {
            let cond_val = interp.eval_expr_pub(cond_expr)?;
            if is_truthy(&cond_val) {
                for child in then_nodes {
                    render_node(interp, child, html)?;
                }
            } else if let Some(else_branch) = else_nodes {
                for child in else_branch {
                    render_node(interp, child, html)?;
                }
            }
        }

        // ── Match ───────────────────────────────────────────────
        TemplateNode::Match(scrutinee_expr, arms) => {
            let scrutinee = interp.eval_expr_pub(scrutinee_expr)?;
            let mut matched = false;
            for (pattern_expr, arm_nodes) in arms {
                // Wildcard pattern (_) matches everything
                if matches!(pattern_expr, Expr::Wildcard) {
                    for child in arm_nodes {
                        render_node(interp, child, html)?;
                    }
                    matched = true;
                    break;
                }
                let pattern_val = interp.eval_expr_pub(pattern_expr)?;
                if values_eq(&scrutinee, &pattern_val) {
                    for child in arm_nodes {
                        render_node(interp, child, html)?;
                    }
                    matched = true;
                    break;
                }
            }
            let _ = matched; // OK to not match any arm
        }

        // ── Verify ──────────────────────────────────────────────
        TemplateNode::Verify(expr) => {
            let val = interp.eval_expr_pub(expr)?;
            if !is_truthy(&val) {
                return Err(template_err("template verify: assertion failed".to_string()));
            }
        }

        // ── Invariant ───────────────────────────────────────────
        TemplateNode::Invariant(expr) => {
            let val = interp.eval_expr_pub(expr)?;
            if !is_truthy(&val) {
                return Err(template_err("template invariant: invariant violated".to_string()));
            }
        }
    }
    Ok(())
}

// ── Unit tests ──────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_html_escape_basic() {
        assert_eq!(html_escape("hello"), "hello");
        assert_eq!(html_escape("<b>"), "&lt;b&gt;");
        assert_eq!(html_escape("&"), "&amp;");
        assert_eq!(html_escape("\""), "&quot;");
        assert_eq!(html_escape("'"), "&#39;");
    }

    #[test]
    fn test_html_escape_xss() {
        let xss = "<script>alert('xss')</script>";
        let escaped = html_escape(xss);
        assert!(!escaped.contains('<'));
        assert!(!escaped.contains('>'));
        assert!(!escaped.contains('\''));
        assert_eq!(
            escaped,
            "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;"
        );
    }

    #[test]
    fn test_html_escape_amp_in_url() {
        assert_eq!(html_escape("a=1&b=2"), "a=1&amp;b=2");
    }

    #[test]
    fn test_void_elements_contains_common() {
        assert!(VOID_ELEMENTS.contains(&"br"));
        assert!(VOID_ELEMENTS.contains(&"img"));
        assert!(VOID_ELEMENTS.contains(&"input"));
        assert!(VOID_ELEMENTS.contains(&"hr"));
        assert!(VOID_ELEMENTS.contains(&"meta"));
        assert!(VOID_ELEMENTS.contains(&"link"));
    }

    #[test]
    fn test_void_elements_does_not_contain_regular() {
        assert!(!VOID_ELEMENTS.contains(&"div"));
        assert!(!VOID_ELEMENTS.contains(&"span"));
        assert!(!VOID_ELEMENTS.contains(&"p"));
        assert!(!VOID_ELEMENTS.contains(&"script"));
    }

    #[test]
    fn test_is_truthy() {
        assert!(is_truthy(&Value::Bool(true)));
        assert!(!is_truthy(&Value::Bool(false)));
        assert!(is_truthy(&Value::Int(1)));
        assert!(!is_truthy(&Value::Int(0)));
        assert!(is_truthy(&Value::Str("hello".into())));
        assert!(!is_truthy(&Value::Str(String::new())));
        assert!(is_truthy(&Value::Array(vec![Value::Int(1)])));
        assert!(!is_truthy(&Value::Array(vec![])));
        assert!(!is_truthy(&Value::Void));
    }

    #[test]
    fn test_values_eq() {
        assert!(values_eq(&Value::Int(42), &Value::Int(42)));
        assert!(!values_eq(&Value::Int(1), &Value::Int(2)));
        assert!(values_eq(&Value::Str("hi".into()), &Value::Str("hi".into())));
        assert!(!values_eq(&Value::Bool(true), &Value::Bool(false)));
        assert!(!values_eq(&Value::Int(1), &Value::Str("1".into())));
    }
}
