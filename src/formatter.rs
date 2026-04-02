#![allow(dead_code)]

//! HEXA-LANG Code Formatter
//!
//! Pretty-prints HEXA source code with consistent formatting:
//! - 4-space indentation
//! - Normalized whitespace
//! - Aligned `=` in consecutive let statements
//! - One blank line between functions
//! - No trailing whitespace

use crate::ast::*;

/// Format a HEXA source string.
pub fn format_source(source: &str) -> Result<String, String> {
    let tokens = crate::lexer::Lexer::new(source).tokenize()
        .map_err(|e| format!("Lexer error: {}", e))?;
    let stmts = crate::parser::Parser::new(tokens).parse()
        .map_err(|e| format!("Parse error: {}", e))?;
    Ok(format_stmts(&stmts, 0))
}

/// Check if source is already formatted (returns true if no changes needed).
pub fn check_format(source: &str) -> Result<bool, String> {
    let formatted = format_source(source)?;
    Ok(source.trim_end() == formatted.trim_end())
}

fn format_stmts(stmts: &[Stmt], indent: usize) -> String {
    let mut out = String::new();
    let mut i = 0;
    while i < stmts.len() {
        // Collect consecutive let statements for alignment
        let mut let_group: Vec<usize> = Vec::new();
        let mut j = i;
        while j < stmts.len() {
            if matches!(&stmts[j], Stmt::Let(..)) {
                let_group.push(j);
                j += 1;
            } else {
                break;
            }
        }

        if let_group.len() > 1 {
            // Align = signs in consecutive lets
            let mut max_name_len = 0usize;
            for &idx in &let_group {
                if let Stmt::Let(name, typ, ..) = &stmts[idx] {
                    let ann_len = typ.as_ref().map(|t| t.len() + 2).unwrap_or(0); // ": type"
                    let total = name.len() + ann_len;
                    if total > max_name_len {
                        max_name_len = total;
                    }
                }
            }
            for &idx in &let_group {
                if let Stmt::Let(name, typ, expr, _vis) = &stmts[idx] {
                    let prefix = ind(indent);
                    let ann = typ.as_ref().map(|t| format!(": {}", t)).unwrap_or_default();
                    let left = format!("{}{}", name, ann);
                    let padded = format!("{:<width$}", left, width = max_name_len);
                    let rhs = expr.as_ref().map(|e| format_expr(e)).unwrap_or_default();
                    out.push_str(&format!("{}let {} = {}\n", prefix, padded, rhs));
                }
            }
            i = j;
        } else {
            // Add blank line between functions
            if i > 0 && matches!(&stmts[i], Stmt::FnDecl(_) | Stmt::AsyncFnDecl(_)) {
                if !out.ends_with("\n\n") {
                    out.push('\n');
                }
            }
            out.push_str(&format_stmt(&stmts[i], indent));
            out.push('\n');
            i += 1;
        }
    }
    // Remove trailing newlines, leave exactly one
    while out.ends_with("\n\n") {
        out.pop();
    }
    out
}

fn format_stmt(stmt: &Stmt, indent: usize) -> String {
    let prefix = ind(indent);
    match stmt {
        Stmt::Let(name, typ, expr, _vis) => {
            let ann = typ.as_ref().map(|t| format!(": {}", t)).unwrap_or_default();
            let rhs = expr.as_ref().map(|e| format!(" = {}", format_expr(e))).unwrap_or_default();
            format!("{}let {}{}{}", prefix, name, ann, rhs)
        }
        Stmt::Const(name, typ, expr, _vis) => {
            let ann = typ.as_ref().map(|t| format!(": {}", t)).unwrap_or_default();
            format!("{}const {}{} = {}", prefix, name, ann, format_expr(expr))
        }
        Stmt::Static(name, typ, expr, _vis) => {
            let ann = typ.as_ref().map(|t| format!(": {}", t)).unwrap_or_default();
            format!("{}static {}{} = {}", prefix, name, ann, format_expr(expr))
        }
        Stmt::Assign(lhs, rhs) => {
            format!("{}{} = {}", prefix, format_expr(lhs), format_expr(rhs))
        }
        Stmt::Expr(expr) => {
            format!("{}{}", prefix, format_expr(expr))
        }
        Stmt::Return(expr) => match expr {
            Some(e) => format!("{}return {}", prefix, format_expr(e)),
            None => format!("{}return", prefix),
        },
        Stmt::FnDecl(decl) => format_fn_decl(decl, indent, false),
        Stmt::AsyncFnDecl(decl) => format_fn_decl(decl, indent, true),
        Stmt::StructDecl(decl) => {
            let mut s = format!("{}struct {} {{\n", prefix, decl.name);
            for (fname, ftype, _vis) in &decl.fields {
                s.push_str(&format!("{}    {}: {},\n", prefix, fname, ftype));
            }
            s.push_str(&format!("{}}}", prefix));
            s
        }
        Stmt::EnumDecl(decl) => {
            let mut s = format!("{}enum {} {{\n", prefix, decl.name);
            for (vname, vdata) in &decl.variants {
                if let Some(types) = vdata {
                    s.push_str(&format!("{}    {}({}),\n", prefix, vname, types.join(", ")));
                } else {
                    s.push_str(&format!("{}    {},\n", prefix, vname));
                }
            }
            s.push_str(&format!("{}}}", prefix));
            s
        }
        Stmt::TraitDecl(decl) => {
            let mut s = format!("{}trait {} {{\n", prefix, decl.name);
            for m in &decl.methods {
                s.push_str(&format_fn_decl(m, indent + 1, false));
                s.push('\n');
            }
            s.push_str(&format!("{}}}", prefix));
            s
        }
        Stmt::ImplBlock(impl_block) => {
            let header = if let Some(trait_name) = &impl_block.trait_name {
                format!("{}impl {} for {} {{\n", prefix, trait_name, impl_block.target)
            } else {
                format!("{}impl {} {{\n", prefix, impl_block.target)
            };
            let mut s = header;
            for (mi, m) in impl_block.methods.iter().enumerate() {
                if mi > 0 { s.push('\n'); }
                s.push_str(&format_fn_decl(m, indent + 1, false));
                s.push('\n');
            }
            s.push_str(&format!("{}}}", prefix));
            s
        }
        Stmt::For(var, iter_expr, body) => {
            format!("{}for {} in {} {}", prefix, var, format_expr(iter_expr), format_block(body, indent))
        }
        Stmt::While(cond, body) => {
            format!("{}while {} {}", prefix, format_expr(cond), format_block(body, indent))
        }
        Stmt::Loop(body) => {
            format!("{}loop {}", prefix, format_block(body, indent))
        }
        Stmt::Proof(name, body) => {
            format!("{}proof {} {}", prefix, name, format_block(body, indent))
        }
        Stmt::Assert(expr) => {
            format!("{}assert {}", prefix, format_expr(expr))
        }
        Stmt::Intent(desc, fields) => {
            let mut s = format!("{}intent \"{}\" {{\n", prefix, desc);
            for (k, v) in fields {
                s.push_str(&format!("{}    {}: {},\n", prefix, k, format_expr(v)));
            }
            s.push_str(&format!("{}}}", prefix));
            s
        }
        Stmt::Verify(name, body) => {
            format!("{}verify \"{}\" {}", prefix, name, format_block(body, indent))
        }
        Stmt::Mod(name, body) => {
            let mut s = format!("{}mod {} {{\n", prefix, name);
            s.push_str(&format_stmts(body, indent + 1));
            s.push_str(&format!("{}}}", prefix));
            s
        }
        Stmt::Use(path) => {
            format!("{}use {}", prefix, path.join("::"))
        }
        Stmt::TryCatch(try_block, err_name, catch_block) => {
            format!("{}try {} catch {} {}",
                prefix,
                format_block(try_block, indent),
                err_name,
                format_block(catch_block, indent))
        }
        Stmt::Throw(expr) => {
            format!("{}throw {}", prefix, format_expr(expr))
        }
        Stmt::Spawn(body) => {
            format!("{}spawn {}", prefix, format_block(body, indent))
        }
        Stmt::SpawnNamed(name, body) => {
            format!("{}spawn \"{}\" {}", prefix, name, format_block(body, indent))
        }
        Stmt::DropStmt(name) => {
            format!("{}drop {}", prefix, name)
        }
        Stmt::Select(arms) => {
            let mut s = format!("{}select {{\n", prefix);
            for arm in arms {
                s.push_str(&format!("{}    {} as {} -> {},\n",
                    prefix,
                    format_expr(&arm.receiver),
                    arm.binding,
                    format_block(&arm.body, indent + 1)));
            }
            s.push_str(&format!("{}}}", prefix));
            s
        }
        Stmt::MacroDef(def) => {
            format!("{}macro! {} {{ ... }}", prefix, def.name)
        }
        Stmt::DeriveDecl(target, traits) => {
            format!("{}derive({}) for {}", prefix, traits.join(", "), target)
        }
        Stmt::Generate(_) => {
            format!("{}generate {{ ... }}", prefix)
        }
        Stmt::Optimize(decl) => {
            format!("{}optimize fn {}", prefix, decl.name)
        }
        Stmt::ComptimeFn(decl) => {
            format_fn_decl(decl, indent, false).replacen("fn ", "comptime fn ", 1)
        }
        Stmt::EffectDecl(_) => {
            format!("{}effect {{ ... }}", prefix)
        }
    }
}

fn format_fn_decl(decl: &FnDecl, indent: usize, is_async: bool) -> String {
    let prefix = ind(indent);
    let async_prefix = if is_async { "async " } else { "" };
    let params: Vec<String> = decl.params.iter().map(|p| {
        if let Some(t) = &p.typ {
            format!("{}: {}", p.name, t)
        } else {
            p.name.clone()
        }
    }).collect();
    let ret = decl.ret_type.as_ref().map(|t| format!(" -> {}", t)).unwrap_or_default();
    format!("{}{}fn {}({}){}  {}",
        prefix, async_prefix, decl.name, params.join(", "), ret,
        format_block(&decl.body, indent))
}

fn format_block(stmts: &[Stmt], indent: usize) -> String {
    if stmts.is_empty() {
        return "{}".to_string();
    }
    let mut s = String::from("{\n");
    s.push_str(&format_stmts(stmts, indent + 1));
    s.push_str(&format!("{}}}", ind(indent)));
    s
}

fn format_expr(expr: &Expr) -> String {
    match expr {
        Expr::IntLit(n) => format!("{}", n),
        Expr::FloatLit(f) => format!("{}", f),
        Expr::BoolLit(b) => format!("{}", b),
        Expr::StringLit(s) => format!("\"{}\"", s.replace('\\', "\\\\").replace('"', "\\\"")),
        Expr::CharLit(c) => format!("'{}'", c),
        Expr::Ident(name) => name.clone(),
        Expr::Binary(left, op, right) => {
            format!("{} {} {}", format_expr(left), binop_str(op), format_expr(right))
        }
        Expr::Unary(op, operand) => {
            let op_str = match op {
                UnaryOp::Neg => "-",
                UnaryOp::Not => "!",
                UnaryOp::BitNot => "~",
            };
            format!("{}{}", op_str, format_expr(operand))
        }
        Expr::Call(callee, args) => {
            let arg_strs: Vec<String> = args.iter().map(|a| format_expr(a)).collect();
            format!("{}({})", format_expr(callee), arg_strs.join(", "))
        }
        Expr::Lambda(params, body) => {
            let param_names: Vec<&str> = params.iter().map(|p| p.name.as_str()).collect();
            format!("|{}| {}", param_names.join(", "), format_expr(body))
        }
        Expr::Array(items) => {
            let strs: Vec<String> = items.iter().map(|i| format_expr(i)).collect();
            format!("[{}]", strs.join(", "))
        }
        Expr::Tuple(items) => {
            let strs: Vec<String> = items.iter().map(|i| format_expr(i)).collect();
            format!("({})", strs.join(", "))
        }
        Expr::Field(obj, field) => {
            format!("{}.{}", format_expr(obj), field)
        }
        Expr::Index(obj, idx) => {
            format!("{}[{}]", format_expr(obj), format_expr(idx))
        }
        Expr::If(cond, then_block, else_block) => {
            let then_str = format_block(then_block, 0);
            match else_block {
                Some(eb) => format!("if {} {} else {}", format_expr(cond), then_str, format_block(eb, 0)),
                None => format!("if {} {}", format_expr(cond), then_str),
            }
        }
        Expr::Match(scrutinee, arms) => {
            let mut s = format!("match {} {{\n", format_expr(scrutinee));
            for arm in arms {
                let guard_str = arm.guard.as_ref().map(|g| format!(" if {}", format_expr(g))).unwrap_or_default();
                s.push_str(&format!("    {}{} -> {{\n", format_expr(&arm.pattern), guard_str));
                for stmt in &arm.body {
                    s.push_str(&format!("        {}\n", format_stmt(stmt, 0).trim()));
                }
                s.push_str("    },\n");
            }
            s.push('}');
            s
        }
        Expr::Block(stmts) => format_block(stmts, 0),
        Expr::Range(start, end, inclusive) => {
            let op = if *inclusive { "..=" } else { ".." };
            format!("{}{}{}", format_expr(start), op, format_expr(end))
        }
        Expr::StructInit(name, fields) => {
            let field_strs: Vec<String> = fields.iter()
                .map(|(k, v)| format!("{}: {}", k, format_expr(v)))
                .collect();
            format!("{} {{ {} }}", name, field_strs.join(", "))
        }
        Expr::MapLit(pairs) => {
            let pair_strs: Vec<String> = pairs.iter()
                .map(|(k, v)| format!("{}: {}", format_expr(k), format_expr(v)))
                .collect();
            format!("{{{}}}", pair_strs.join(", "))
        }
        Expr::EnumPath(enum_name, variant, data) => {
            match data {
                Some(d) => format!("{}::{}({})", enum_name, variant, format_expr(d)),
                None => format!("{}::{}", enum_name, variant),
            }
        }
        Expr::Wildcard => "_".to_string(),
        Expr::Own(inner) => format!("own {}", format_expr(inner)),
        Expr::MoveExpr(inner) => format!("move {}", format_expr(inner)),
        Expr::Borrow(inner) => format!("borrow {}", format_expr(inner)),
        Expr::Await(inner) => format!("await {}", format_expr(inner)),
        Expr::MacroInvoc(invoc) => {
            format!("{}!(...)", invoc.name)
        }
        Expr::Comptime(inner) => {
            format!("comptime {}", format_expr(inner))
        }
        Expr::HandleWith(_, _) => "handle { ... } with { ... }".to_string(),
        Expr::EffectCall(effect, op, _) => format!("{}.{}(...)", effect, op),
        Expr::Resume(inner) => format!("resume({})", format_expr(inner)),
    }
}

fn binop_str(op: &BinOp) -> &'static str {
    match op {
        BinOp::Add => "+",
        BinOp::Sub => "-",
        BinOp::Mul => "*",
        BinOp::Div => "/",
        BinOp::Rem => "%",
        BinOp::Pow => "**",
        BinOp::Eq => "==",
        BinOp::Ne => "!=",
        BinOp::Lt => "<",
        BinOp::Gt => ">",
        BinOp::Le => "<=",
        BinOp::Ge => ">=",
        BinOp::And => "&&",
        BinOp::Or => "||",
        BinOp::Xor => "^^",
        BinOp::BitAnd => "&",
        BinOp::BitOr => "|",
        BinOp::BitXor => "^",
        BinOp::Assign => "=",
        BinOp::DeclAssign => ":=",
        BinOp::Range => "..",
        BinOp::Arrow => "->",
    }
}

fn ind(level: usize) -> String {
    "    ".repeat(level)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_format_let_binding() {
        let formatted = format_source("let   x  =   42").unwrap();
        assert!(formatted.contains("let x = 42"));
    }

    #[test]
    fn test_format_function() {
        let src = "fn add(a, b) {\nreturn a + b\n}";
        let formatted = format_source(src).unwrap();
        assert!(formatted.contains("fn add(a, b)"));
        assert!(formatted.contains("    return a + b"));
    }

    #[test]
    fn test_format_consecutive_lets_aligned() {
        let src = "let x = 1\nlet long_name = 2\nlet y = 3";
        let formatted = format_source(src).unwrap();
        // All = signs should be at the same column
        let lines: Vec<&str> = formatted.lines().collect();
        assert!(lines.len() >= 3);
        let eq_positions: Vec<usize> = lines.iter()
            .filter(|l| l.contains(" = "))
            .map(|l| l.find(" = ").unwrap())
            .collect();
        assert!(eq_positions.len() >= 3);
        assert!(eq_positions.windows(2).all(|w| w[0] == w[1]), "= signs not aligned: {:?}", eq_positions);
    }

    #[test]
    fn test_format_blank_line_between_functions() {
        let src = "fn a() {\nreturn 1\n}\nfn b() {\nreturn 2\n}";
        let formatted = format_source(src).unwrap();
        assert!(formatted.contains("}\n\nfn b"));
    }

    #[test]
    fn test_format_check_pass() {
        let src = "let x = 42\n";
        let formatted = format_source(src).unwrap();
        assert!(check_format(&formatted).unwrap());
    }

    #[test]
    fn test_format_no_trailing_whitespace() {
        let src = "let x = 42  \nlet y = 10  ";
        let formatted = format_source(src).unwrap();
        for line in formatted.lines() {
            assert!(!line.ends_with(' '), "trailing whitespace in: {:?}", line);
        }
    }

    #[test]
    fn test_format_struct() {
        let src = "struct Point {\n  x: float,\n  y: float\n}";
        let formatted = format_source(src).unwrap();
        assert!(formatted.contains("struct Point {"));
        assert!(formatted.contains("    x: float,"));
    }

    #[test]
    fn test_format_if_else() {
        let src = "if x > 0 { return 1 } else { return 0 }";
        let formatted = format_source(src).unwrap();
        assert!(formatted.contains("if x > 0"));
    }
}
