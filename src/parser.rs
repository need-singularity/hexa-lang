// ⛔ CORE — L0 불변식 (파서 문법 정의. 수정 전 유저 승인 필수)
#![allow(dead_code)]

use crate::token::{Token, Spanned, Span};
use crate::ast::*;
use crate::error::{HexaError, ErrorClass};

/// Parser output: statements + parallel span info for each statement.
pub struct ParseResult {
    pub stmts: Vec<Stmt>,
    pub spans: Vec<(usize, usize)>,  // (line, col) for each stmt, parallel to stmts
}

pub struct Parser {
    tokens: Vec<Spanned>,
    pos: usize,
    /// Pending @link("lib") attribute for the next extern fn declaration.
    pending_link_attr: Option<String>,
    /// AI-native: pending attributes for next declaration.
    pending_attrs: Vec<Attribute>,
    /// When true, parse_primary skips struct init (for while/if conditions)
    no_struct_init: bool,
}

impl Parser {
    pub fn new(tokens: Vec<Spanned>) -> Self {
        Parser { tokens, pos: 0, pending_link_attr: None, pending_attrs: vec![], no_struct_init: false }
    }

    /// Construct from plain tokens (no span info). Convenience for tests.
    pub fn from_plain(tokens: Vec<Token>) -> Self {
        let spanned = tokens.into_iter()
            .map(|t| Spanned::new(t, 0, 0))
            .collect();
        Parser { tokens: spanned, pos: 0, pending_link_attr: None, pending_attrs: vec![], no_struct_init: false }
    }

    /// Drain pending AI-native attributes.
    fn take_attrs(&mut self) -> Vec<Attribute> {
        std::mem::take(&mut self.pending_attrs)
    }

    // ── Helpers ──────────────────────────────────────────────

    fn peek(&self) -> &Token {
        self.tokens.get(self.pos)
            .map(|s| &s.token)
            .unwrap_or(&Token::Eof)
    }

    fn peek_ahead(&self, offset: usize) -> &Token {
        self.tokens.get(self.pos + offset)
            .map(|s| &s.token)
            .unwrap_or(&Token::Eof)
    }

    /// Get the span of the current token.
    fn current_span(&self) -> Span {
        self.tokens.get(self.pos)
            .map(|s| s.span.clone())
            .unwrap_or(Span::new(0, 0))
    }

    fn at_end(&self) -> bool {
        matches!(self.peek(), Token::Eof)
    }

    fn advance(&mut self) -> Token {
        let tok = self.tokens.get(self.pos)
            .map(|s| s.token.clone())
            .unwrap_or(Token::Eof);
        self.pos += 1;
        tok
    }

    fn check(&self, expected: &Token) -> bool {
        std::mem::discriminant(self.peek()) == std::mem::discriminant(expected)
    }

    fn expect(&mut self, expected: &Token) -> Result<Token, HexaError> {
        if self.check(expected) {
            Ok(self.advance())
        } else {
            let span = self.current_span();
            Err(self.error_at(span, format!("expected {:?}, got {:?}", expected, self.peek())))
        }
    }

    fn skip_newlines(&mut self) {
        while matches!(self.peek(), Token::Newline | Token::Semicolon) {
            self.advance();
        }
    }

    fn error(&self, message: String) -> HexaError {
        let span = self.current_span();
        HexaError {
            class: ErrorClass::Syntax,
            message,
            line: span.line,
            col: span.col,
            hint: None,
        }
    }

    fn error_at(&self, span: Span, message: String) -> HexaError {
        HexaError {
            class: ErrorClass::Syntax,
            message,
            line: span.line,
            col: span.col,
            hint: None,
        }
    }

    fn expect_ident(&mut self) -> Result<String, HexaError> {
        let span = self.current_span();
        match self.advance() {
            Token::Ident(name) => Ok(name.to_string()),
            other => Err(self.error_at(span, format!("expected identifier, got {:?}", other))),
        }
    }

    /// Parse a return type: either an identifier or `[type]` for array types.
    fn parse_ret_type(&mut self) -> Result<String, HexaError> {
        if matches!(self.peek(), Token::LBracket) {
            self.advance(); // consume [
            let inner = if matches!(self.peek(), Token::RBracket) {
                String::new() // [] empty array
            } else {
                self.parse_ret_type()? // recursive: [T], [[T]], [(T,T)]
            };
            self.expect(&Token::RBracket)?;
            Ok(format!("[{}]", inner))
        } else if matches!(self.peek(), Token::LParen) {
            self.advance(); // consume (
            if matches!(self.peek(), Token::RParen) {
                self.advance(); // consume )
                return Ok("()".to_string());
            }
            let mut types = vec![self.parse_ret_type()?]; // recursive for nested types
            while matches!(self.peek(), Token::Comma) {
                self.advance(); // consume ,
                types.push(self.parse_ret_type()?);
            }
            self.expect(&Token::RParen)?;
            Ok(format!("({})", types.join(", ")))
        } else {
            self.expect_ident()
        }
    }

    // ── Level 1: Program ────────────────────────────────────

    pub fn parse(&mut self) -> Result<Vec<Stmt>, HexaError> {
        let result = self.parse_with_spans()?;
        Ok(result.stmts)
    }

    pub fn parse_with_spans(&mut self) -> Result<ParseResult, HexaError> {
        let mut stmts = Vec::new();
        let mut spans = Vec::new();
        self.skip_newlines();
        while !self.at_end() {
            let span = self.current_span();
            stmts.push(self.parse_stmt()?);
            spans.push((span.line, span.col));
            self.skip_newlines();
        }
        Ok(ParseResult { stmts, spans })
    }

    // ── Level 2: Statement ──────────────────────────────────

    fn parse_stmt(&mut self) -> Result<Stmt, HexaError> {
        self.skip_newlines();
        // Check for visibility prefix
        let vis = self.parse_visibility();

        match self.peek().clone() {
            Token::Let => self.parse_let(vis),
            Token::Const => self.parse_const(vis),
            Token::Static => self.parse_static(vis),
            Token::Fn => self.parse_fn_decl(vis),
            Token::Struct => self.parse_struct_decl(vis),
            Token::Enum => self.parse_enum_decl(vis),
            Token::Trait => self.parse_trait_decl(vis),
            Token::Impl => self.parse_impl_block(),
            Token::For => self.parse_for_stmt(),
            Token::While => self.parse_while_stmt(),
            Token::Loop => self.parse_loop_stmt(),
            Token::Break => { self.advance(); Ok(Stmt::Break) }
            Token::Continue => { self.advance(); Ok(Stmt::Continue) }
            Token::Return => self.parse_return(),
            Token::Proof => self.parse_proof_or_proof_block(),
            Token::Theorem => self.parse_theorem(),
            Token::Panic => self.parse_panic(),
            Token::Type => self.parse_type_alias(vis),
            Token::Atomic => self.parse_atomic_let(vis),
            Token::Assert => self.parse_assert(),
            Token::Intent => self.parse_intent(),
            Token::Verify => self.parse_verify(),
            Token::Mod => self.parse_mod_decl(),
            Token::Use => self.parse_use_decl(),
            Token::Try => self.parse_try_catch(),
            Token::Throw => self.parse_throw(),
            Token::Async => self.parse_async_fn(vis),
            Token::Select => self.parse_select(),
            Token::Spawn => self.parse_spawn(),
            Token::Scope => self.parse_scope(),
            Token::Drop => self.parse_drop_stmt(),
            Token::Macro => self.parse_macro_def(),
            Token::Derive => self.parse_derive_decl(),
            Token::Generate => self.parse_generate(),
            Token::Optimize => self.parse_optimize(),
            Token::Comptime => self.parse_comptime_stmt(),
            Token::Effect => self.parse_effect_decl(),
            Token::Extern => self.parse_extern_fn_decl(),
            Token::Pure => self.parse_pure_fn(vis),
            Token::Ident(ref s) if s == "consciousness" && !matches!(self.peek_ahead(1), Token::ColonColon | Token::Dot | Token::Eq | Token::LParen) => {
                self.advance(); // consume 'consciousness'
                let name = match self.peek().clone() {
                    Token::StringLit(s) => { self.advance(); s.to_string() }
                    Token::Ident(_) if !matches!(self.peek_ahead(1), Token::ColonColon) => self.expect_ident()?,
                    Token::LBrace => "default".to_string(),
                    _ => return Err(self.error(format!(
                        "expected name after 'consciousness', got {:?}", self.peek()
                    ))),
                };
                let body = self.parse_block()?;
                Ok(Stmt::ConsciousnessBlock(name, body))
            }
            Token::Attribute(ref s) if &**s == "link" => {
                self.advance(); // consume @link
                self.expect(&Token::LParen)?;
                let lib_name = match self.peek().clone() {
                    Token::StringLit(s) => { self.advance(); s.to_string() }
                    _ => return Err(self.error("expected string literal after @link(".to_string())),
                };
                self.expect(&Token::RParen)?;
                self.pending_link_attr = Some(lib_name);
                self.skip_newlines();
                return self.parse_stmt();
            }
            Token::Attribute(ref s) if &**s == "evolve" => {
                self.advance(); // consume @evolve
                self.expect(&Token::Fn)?;
                let name = self.expect_ident()?;
                let type_params = if matches!(self.peek(), Token::Lt) {
                    self.parse_type_params()?
                } else { vec![] };
                self.expect(&Token::LParen)?;
                let params = self.parse_params()?;
                self.expect(&Token::RParen)?;
                let ret_type = if matches!(self.peek(), Token::Arrow) {
                    self.advance(); Some(self.parse_ret_type()?)
                } else { None };
                let where_clauses = if matches!(self.peek(), Token::Where) {
                    self.parse_where_clauses()?
                } else { vec![] };
                let body = self.parse_block()?;
                Ok(Stmt::EvolveFn(FnDecl {
                    name, type_params, params, ret_type, where_clauses, precondition: None, postcondition: None, body, vis, is_pure: false,
                    attrs: vec![Attribute::new(crate::token::AttrKind::Evolve)],
                }))
            }
            // AI-native: any other @attr before a declaration
            Token::Attribute(_) => {
                // Collect all consecutive attributes
                let mut attrs = Vec::new();
                while let Token::Attribute(ref attr_name) = self.peek().clone() {
                    let name_str = attr_name.to_string();
                    self.advance();
                    let mut attr_args: Vec<(String, crate::ast::Expr)> = Vec::new();
                    let kind = if matches!(self.peek(), Token::LParen) {
                        self.advance(); // (
                        match name_str.as_str() {
                            "link" => {
                                let lib = match self.peek().clone() {
                                    Token::StringLit(s) => { self.advance(); s.to_string() }
                                    _ => return Err(self.error("expected string after @link(".into())),
                                };
                                self.expect(&Token::RParen)?;
                                crate::token::AttrKind::Link(lib)
                            }
                            "bounded" => {
                                let n = match self.peek().clone() {
                                    Token::IntLit(n) => { self.advance(); n }
                                    _ => return Err(self.error("expected int after @bounded(".into())),
                                };
                                self.expect(&Token::RParen)?;
                                crate::token::AttrKind::Bounded(n)
                            }
                            "deprecated" => {
                                let msg = match self.peek().clone() {
                                    Token::StringLit(s) => { self.advance(); Some(s.to_string()) }
                                    _ => None,
                                };
                                self.expect(&Token::RParen)?;
                                crate::token::AttrKind::Deprecated(msg)
                            }
                            _ => {
                                // Try key:expr, key:expr, ... form (e.g. @contract(requires: x>0, ensures: r>0))
                                if matches!(self.peek(), Token::Ident(_))
                                    && matches!(self.peek_ahead(1), Token::Colon)
                                {
                                    loop {
                                        let key = self.expect_ident()?;
                                        self.expect(&Token::Colon)?;
                                        let val = self.parse_expr()?;
                                        attr_args.push((key, val));
                                        if matches!(self.peek(), Token::Comma) {
                                            self.advance();
                                            continue;
                                        }
                                        break;
                                    }
                                    self.expect(&Token::RParen)?;
                                } else {
                                    while !matches!(self.peek(), Token::RParen | Token::Eof) { self.advance(); }
                                    self.expect(&Token::RParen)?;
                                }
                                crate::token::AttrKind::from_name(&name_str)
                            }
                        }
                    } else {
                        crate::token::AttrKind::from_name(&name_str)
                    };
                    attrs.push(Attribute::with_args(kind, attr_args));
                    self.skip_newlines();
                }
                // Store attrs for next declaration to consume
                self.pending_attrs = attrs;
                self.parse_stmt()
            }
            _ => {
                let expr = self.parse_expr()?;
                // Check for assignment
                if matches!(self.peek(), Token::Eq) {
                    self.advance(); // consume =
                    let rhs = self.parse_expr()?;
                    Ok(Stmt::Assign(expr, rhs))
                } else {
                    Ok(Stmt::Expr(expr))
                }
            }
        }
    }

    fn parse_visibility(&mut self) -> Visibility {
        match self.peek() {
            Token::Pub => {
                self.advance();
                Visibility::Public
            }
            Token::Crate => {
                self.advance();
                Visibility::Crate
            }
            _ => Visibility::Private,
        }
    }

    fn parse_let(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'let'
        // optional 'mut'
        if matches!(self.peek(), Token::Mut) {
            self.advance();
        }
        // tuple destructure: let (a, b) = expr
        if matches!(self.peek(), Token::LParen) {
            self.advance(); // consume '('
            let mut names = Vec::new();
            loop {
                names.push(self.expect_ident()?);
                if matches!(self.peek(), Token::Comma) {
                    self.advance();
                } else {
                    break;
                }
            }
            self.expect(&Token::RParen)?;
            self.expect(&Token::Eq)?;
            let expr = self.parse_expr()?;
            return Ok(Stmt::LetTuple(names, expr));
        }
        let name = self.expect_ident()?;
        // optional type annotation (supports [T], (T1, T2), etc.)
        let typ = if matches!(self.peek(), Token::Colon) {
            self.advance();
            Some(self.parse_ret_type()?)
        } else {
            None
        };
        // = expr
        self.expect(&Token::Eq)?;
        let expr = self.parse_expr()?;
        Ok(Stmt::Let(name, typ, Some(expr), vis))
    }

    fn parse_const(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'const'
        let name = self.expect_ident()?;
        // optional type annotation (supports [T], (T1, T2), etc.)
        let typ = if matches!(self.peek(), Token::Colon) {
            self.advance();
            Some(self.parse_ret_type()?)
        } else {
            None
        };
        // = expr (required for const)
        self.expect(&Token::Eq)?;
        let expr = self.parse_expr()?;
        Ok(Stmt::Const(name, typ, expr, vis))
    }

    fn parse_static(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'static'
        let name = self.expect_ident()?;
        // optional type annotation (supports [T], (T1, T2), etc.)
        let typ = if matches!(self.peek(), Token::Colon) {
            self.advance();
            Some(self.parse_ret_type()?)
        } else {
            None
        };
        // = expr (required for static)
        self.expect(&Token::Eq)?;
        let expr = self.parse_expr()?;
        Ok(Stmt::Static(name, typ, expr, vis))
    }

    fn parse_return(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'return'
        if matches!(self.peek(), Token::Newline | Token::Semicolon | Token::Eof | Token::RBrace) {
            Ok(Stmt::Return(None))
        } else {
            let expr = self.parse_expr()?;
            Ok(Stmt::Return(Some(expr)))
        }
    }

    fn parse_proof_or_proof_block(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'proof'
        let name = self.expect_ident()?;
        // Look ahead: if block contains forall/exists/assert/check/invariant, parse as ProofBlock
        // We use a simple heuristic: peek into the block for proof-specific keywords
        let saved_pos = self.pos;
        self.skip_newlines();
        if matches!(self.peek(), Token::LBrace) {
            // Peek inside the block to see if it contains proof-specific statements
            let brace_pos = self.pos;
            self.advance(); // consume {
            self.skip_newlines();
            let is_formal = matches!(self.peek(), Token::Invariant)
                || matches!(self.peek(),
                Token::Ident(ref s) if s == "forall" || s == "exists" || s == "check");
            self.pos = brace_pos; // backtrack to before {
            if is_formal {
                return self.parse_proof_block_body(name);
            }
        }
        self.pos = saved_pos; // backtrack
        let body = self.parse_block()?;
        Ok(Stmt::Proof(name, body))
    }

    fn parse_proof_block_body(&mut self, name: String) -> Result<Stmt, HexaError> {
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut stmts = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            stmts.push(self.parse_proof_block_stmt()?);
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::ProofBlock(name, stmts))
    }

    fn parse_proof_block_stmt(&mut self) -> Result<ProofBlockStmt, HexaError> {
        match self.peek().clone() {
            Token::Ident(ref s) if s == "forall" => {
                self.advance(); // consume 'forall'
                let var = self.expect_ident()?;
                self.expect(&Token::Colon)?;
                let typ = self.expect_ident()?;
                self.expect(&Token::Comma)?;
                let condition = self.parse_expr()?;
                self.expect(&Token::FatArrow)?;
                let conclusion = self.parse_expr()?;
                Ok(ProofBlockStmt::ForAll { var, typ, condition, conclusion })
            }
            Token::Ident(ref s) if s == "exists" => {
                self.advance(); // consume 'exists'
                let var = self.expect_ident()?;
                self.expect(&Token::Colon)?;
                let typ = self.expect_ident()?;
                self.expect(&Token::Comma)?;
                let condition = self.parse_expr()?;
                Ok(ProofBlockStmt::Exists { var, typ, condition })
            }
            Token::Ident(ref s) if s == "check" => {
                self.advance(); // consume 'check'
                // expect 'law' identifier
                match self.peek().clone() {
                    Token::Ident(ref s) if s == "law" => { self.advance(); }
                    _ => return Err(self.error(format!("expected 'law' after 'check', got {:?}", self.peek()))),
                }
                self.expect(&Token::LParen)?;
                let n = match self.advance() {
                    Token::IntLit(n) => n,
                    other => return Err(self.error(format!("expected integer in check law(), got {:?}", other))),
                };
                self.expect(&Token::RParen)?;
                Ok(ProofBlockStmt::CheckLaw(n))
            }
            Token::Assert => {
                self.advance(); // consume 'assert'
                let expr = self.parse_expr()?;
                Ok(ProofBlockStmt::Assert(expr))
            }
            Token::Invariant => {
                self.advance(); // consume 'invariant'
                let expr = self.parse_expr()?;
                Ok(ProofBlockStmt::Invariant(expr))
            }
            _ => Err(self.error(format!("expected proof statement (forall/exists/assert/check/invariant), got {:?}", self.peek()))),
        }
    }

    fn parse_theorem(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'theorem'
        let name = self.expect_ident()?;
        let stmts = self.parse_proof_block_body_inner()?;
        Ok(Stmt::Theorem(name, stmts))
    }

    fn parse_proof_block_body_inner(&mut self) -> Result<Vec<ProofBlockStmt>, HexaError> {
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut stmts = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            stmts.push(self.parse_proof_block_stmt()?);
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(stmts)
    }

    fn parse_panic(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'panic'
        let expr = self.parse_expr()?;
        Ok(Stmt::Panic(expr))
    }

    fn parse_type_alias(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'type'
        let name = self.expect_ident()?;
        self.expect(&Token::Eq)?;
        let target = self.expect_ident()?;
        Ok(Stmt::TypeAlias(name, target, vis))
    }

    fn parse_atomic_let(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'atomic'
        self.expect(&Token::Let)?;
        // optional 'mut'
        if matches!(self.peek(), Token::Mut) {
            self.advance();
        }
        let name = self.expect_ident()?;
        let typ = if matches!(self.peek(), Token::Colon) {
            self.advance();
            Some(self.parse_ret_type()?)
        } else {
            None
        };
        self.expect(&Token::Eq)?;
        let expr = self.parse_expr()?;
        Ok(Stmt::AtomicLet(name, typ, Some(expr), vis))
    }

    fn parse_assert(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'assert'
        let expr = self.parse_expr()?;
        Ok(Stmt::Assert(expr))
    }

    fn parse_intent(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'intent'
        // intent "description" { key: value, ... }
        let description = match self.peek().clone() {
            Token::StringLit(s) => { self.advance(); s.to_string() }
            Token::Ident(_) => { self.expect_ident()? }
            _ => return Err(self.error(format!("expected string or identifier after 'intent', got {:?}", self.peek()))),
        };
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut fields = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            let key = self.expect_ident()?;
            self.expect(&Token::Colon)?;
            let value = self.parse_expr()?;
            fields.push((key, value));
            // consume optional comma or newline
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            }
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::Intent(description, fields))
    }

    fn parse_verify(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'verify'
        // verify "name" { ... } or verify name { ... }
        let name = match self.peek().clone() {
            Token::StringLit(s) => { self.advance(); s.to_string() }
            Token::Ident(_) => { self.expect_ident()? }
            _ => return Err(self.error(format!("expected string or identifier after 'verify', got {:?}", self.peek()))),
        };
        let body = self.parse_block()?;
        Ok(Stmt::Verify(name, body))
    }

    fn parse_try_catch(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'try'
        let try_block = self.parse_block()?;
        self.skip_newlines();
        // Accept 'catch' or 'recover'
        let is_recover = matches!(self.peek(), Token::Recover);
        match self.peek() {
            Token::Catch | Token::Recover => { self.advance(); }
            _ => return Err(self.error(format!("expected 'catch' or 'recover' after try block, got {:?}", self.peek()))),
        }
        let err_name = self.expect_ident()?;
        let tagged_name = if is_recover {
            format!("__recover__{}", err_name)
        } else {
            err_name
        };
        let catch_block = self.parse_block()?;
        Ok(Stmt::TryCatch(try_block, tagged_name, catch_block))
    }

    fn parse_throw(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'throw'
        let expr = self.parse_expr()?;
        Ok(Stmt::Throw(expr))
    }

    fn parse_spawn(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'spawn'
        // Check for named spawn: spawn "name" { ... }
        if let Token::StringLit(name) = self.peek().clone() {
            self.advance(); // consume name
            let body = self.parse_block()?;
            return Ok(Stmt::SpawnNamed(name.to_string(), body));
        }
        let body = self.parse_block()?;
        Ok(Stmt::Spawn(body))
    }

    fn parse_scope(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'scope'
        let body = self.parse_block()?;
        Ok(Stmt::Scope(body))
    }

    fn parse_comptime_stmt(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'comptime'
        // comptime fn name(...) -> ret { ... } — compile-time function declaration
        if matches!(self.peek(), Token::Fn) {
            self.advance(); // consume 'fn'
            let name = self.expect_ident()?;
            let type_params = if matches!(self.peek(), Token::Lt) {
                self.parse_type_params()?
            } else {
                vec![]
            };
            self.expect(&Token::LParen)?;
            let params = self.parse_params()?;
            self.expect(&Token::RParen)?;
            let ret_type = if matches!(self.peek(), Token::Arrow) {
                self.advance();
                Some(self.parse_ret_type()?)
            } else {
                None
            };
            let where_clauses = if matches!(self.peek(), Token::Where) {
                self.parse_where_clauses()?
            } else {
                vec![]
            };
            let body = self.parse_block()?;
            return Ok(Stmt::ComptimeFn(FnDecl {
                name, type_params, params, ret_type, where_clauses, precondition: None, postcondition: None, body,
                vis: Visibility::Private, is_pure: false, attrs: vec![],
            }));
        }
        // comptime const name = expr — compile-time constant
        if matches!(self.peek(), Token::Const) {
            self.advance(); // consume 'const'
            let name = self.expect_ident()?;
            let typ = if matches!(self.peek(), Token::Colon) {
                self.advance();
                Some(self.parse_ret_type()?)
            } else {
                None
            };
            self.expect(&Token::Eq)?;
            let expr = self.parse_expr()?;
            return Ok(Stmt::Const(name, typ, Expr::Comptime(Box::new(expr)), Visibility::Private));
        }
        // comptime let name = expr — compile-time variable
        if matches!(self.peek(), Token::Let) {
            self.advance(); // consume 'let'
            if matches!(self.peek(), Token::Mut) {
                self.advance();
            }
            let name = self.expect_ident()?;
            let typ = if matches!(self.peek(), Token::Colon) {
                self.advance();
                Some(self.parse_ret_type()?)
            } else {
                None
            };
            self.expect(&Token::Eq)?;
            let expr = self.parse_expr()?;
            return Ok(Stmt::Let(name, typ, Some(Expr::Comptime(Box::new(expr))), Visibility::Private));
        }
        // comptime assert expr — compile-time assertion (evaluated eagerly)
        if matches!(self.peek(), Token::Assert) {
            self.advance(); // consume 'assert'
            let expr = self.parse_expr()?;
            return Ok(Stmt::Assert(expr));
        }
        // comptime { ... } — compile-time block as expression statement
        let block = self.parse_block()?;
        Ok(Stmt::Expr(Expr::Comptime(Box::new(Expr::Block(block)))))
    }

    fn parse_async_fn(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'async'
        // Expect 'fn' after 'async'
        self.expect(&Token::Fn)?;
        let name = self.expect_ident()?;
        let type_params = if matches!(self.peek(), Token::Lt) {
            self.parse_type_params()?
        } else {
            vec![]
        };
        self.expect(&Token::LParen)?;
        let params = self.parse_params()?;
        self.expect(&Token::RParen)?;
        let ret_type = if matches!(self.peek(), Token::Arrow) {
            self.advance();
            Some(self.parse_ret_type()?)
        } else {
            None
        };
        let body = self.parse_block()?;
        Ok(Stmt::AsyncFnDecl(FnDecl { name, type_params, params, ret_type, where_clauses: vec![], precondition: None, postcondition: None, body, vis, is_pure: false, attrs: vec![] }))
    }

    fn parse_select(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'select'
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut arms = Vec::new();
        let mut timeout_arm = None;
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            // Check for timeout arm: timeout(ms) => { body }
            if let Token::Ident(ref s) = self.peek().clone() {
                if &**s == "timeout" {
                    self.advance(); // consume 'timeout'
                    self.expect(&Token::LParen)?;
                    let duration_expr = self.parse_expr()?;
                    self.expect(&Token::RParen)?;
                    self.expect(&Token::FatArrow)?;
                    let body = if matches!(self.peek(), Token::LBrace) {
                        self.parse_block()?
                    } else {
                        let expr = self.parse_expr()?;
                        vec![Stmt::Expr(expr)]
                    };
                    timeout_arm = Some(crate::ast::TimeoutArm { duration_ms: duration_expr, body });
                    if matches!(self.peek(), Token::Comma) {
                        self.advance();
                    }
                    self.skip_newlines();
                    continue;
                }
            }
            // Try new syntax: binding from channel_expr => { body }
            // Or old syntax: receiver_expr as binding => { body }
            //
            // Detect "from" syntax: identifier followed by "from"
            let is_from_syntax = if let Token::Ident(_) = self.peek() {
                matches!(self.peek_ahead(1), Token::Ident(ref s) if &**s == "from")
            } else {
                false
            };

            if is_from_syntax {
                // New syntax: msg from channel => { body }
                let binding = self.expect_ident()?;
                // consume 'from'
                self.advance();
                let receiver = self.parse_expr()?;
                self.expect(&Token::FatArrow)?;
                let body = if matches!(self.peek(), Token::LBrace) {
                    self.parse_block()?
                } else {
                    let expr = self.parse_expr()?;
                    vec![Stmt::Expr(expr)]
                };
                arms.push(crate::ast::SelectArm { receiver, binding, body });
            } else {
                // Old syntax: rx.recv() as val => { body }
                let receiver = self.parse_expr()?;
                // Expect 'as' keyword (parsed as ident) or '=>' for futures
                match self.peek().clone() {
                    Token::Ident(ref s) if s == "as" => {
                        self.advance();
                        let binding = self.expect_ident()?;
                        // Support both -> and => for backwards compat
                        if matches!(self.peek(), Token::FatArrow) {
                            self.advance();
                        } else {
                            self.expect(&Token::Arrow)?;
                        }
                        let body = if matches!(self.peek(), Token::LBrace) {
                            self.parse_block()?
                        } else {
                            let expr = self.parse_expr()?;
                            vec![Stmt::Expr(expr)]
                        };
                        arms.push(crate::ast::SelectArm { receiver, binding, body });
                    }
                    Token::FatArrow => {
                        // Future without binding: await expr => { body }
                        self.advance();
                        let body = if matches!(self.peek(), Token::LBrace) {
                            self.parse_block()?
                        } else {
                            let expr = self.parse_expr()?;
                            vec![Stmt::Expr(expr)]
                        };
                        arms.push(crate::ast::SelectArm { receiver, binding: "_".into(), body });
                    }
                    _ => return Err(self.error(format!("expected 'as', 'from', or '=>' in select arm, got {:?}", self.peek()))),
                }
            }
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            }
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::Select(arms, timeout_arm))
    }

    fn parse_drop_stmt(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'drop'
        let name = self.expect_ident()?;
        Ok(Stmt::DropStmt(name))
    }

    /// Parse `generate fn name(params) -> ret { "description" }`
    /// or `generate type { "description" }` (as expression statement).
    fn parse_generate(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'generate'
        if matches!(self.peek(), Token::Fn) {
            // generate fn name(params) -> ret { "description" }
            self.advance(); // consume 'fn'
            let name = self.expect_ident()?;
            self.expect(&Token::LParen)?;
            let params = self.parse_params()?;
            self.expect(&Token::RParen)?;
            let ret_type = if matches!(self.peek(), Token::Arrow) {
                self.advance();
                Some(self.parse_ret_type()?)
            } else {
                None
            };
            self.expect(&Token::LBrace)?;
            self.skip_newlines();
            // Body must be a single string literal (the description)
            let description = match self.peek().clone() {
                Token::StringLit(s) => { self.advance(); s.to_string() }
                _ => return Err(self.error(format!(
                    "generate fn body must be a string description, got {:?}", self.peek()
                ))),
            };
            self.skip_newlines();
            self.expect(&Token::RBrace)?;
            Ok(Stmt::Generate(GenerateTarget::Fn {
                name,
                params,
                ret_type,
                description,
            }))
        } else {
            // generate type { "description" } — expression form
            let type_hint = if !matches!(self.peek(), Token::LBrace) {
                Some(self.expect_ident()?)
            } else {
                None
            };
            self.expect(&Token::LBrace)?;
            self.skip_newlines();
            let description = match self.peek().clone() {
                Token::StringLit(s) => { self.advance(); s.to_string() }
                _ => return Err(self.error(format!(
                    "generate body must be a string description, got {:?}", self.peek()
                ))),
            };
            self.skip_newlines();
            self.expect(&Token::RBrace)?;
            Ok(Stmt::Generate(GenerateTarget::Expr {
                type_hint,
                description,
            }))
        }
    }

    /// Parse `optimize fn name(params) -> ret { body }`.
    fn parse_optimize(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'optimize'
        self.expect(&Token::Fn)?;
        let name = self.expect_ident()?;
        let type_params = if matches!(self.peek(), Token::Lt) {
            self.parse_type_params()?
        } else {
            vec![]
        };
        self.expect(&Token::LParen)?;
        let params = self.parse_params()?;
        self.expect(&Token::RParen)?;
        let ret_type = if matches!(self.peek(), Token::Arrow) {
            self.advance();
            Some(self.parse_ret_type()?)
        } else {
            None
        };
        let body = self.parse_block()?;
        let decl = FnDecl {
            name,
            type_params,
            params,
            ret_type,
            where_clauses: vec![],
            precondition: None,
            postcondition: None,
            body,
            vis: Visibility::Private,
            is_pure: false,
            attrs: vec![],
        };
        Ok(Stmt::Optimize(decl))
    }

    // ── Macros ──────────────────────────────────────────────

    /// Parse: macro! name { (pattern) => { body } }
    fn parse_macro_def(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'macro'
        self.expect(&Token::Not)?; // consume '!'
        let name = self.expect_ident()?;
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut rules = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            let pattern = self.parse_macro_pattern()?;
            self.expect(&Token::FatArrow)?;
            let body = self.parse_macro_body()?;
            rules.push(MacroRule { pattern, body });
            if matches!(self.peek(), Token::Comma | Token::Semicolon) {
                self.advance();
            }
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::MacroDef(MacroDef { name, rules }))
    }

    fn parse_macro_pattern(&mut self) -> Result<Vec<MacroPatternToken>, HexaError> {
        self.expect(&Token::LParen)?;
        self.skip_newlines();
        let mut tokens = Vec::new();
        while !matches!(self.peek(), Token::RParen | Token::Eof) {
            tokens.push(self.parse_macro_pattern_token()?);
            self.skip_newlines();
        }
        self.expect(&Token::RParen)?;
        Ok(tokens)
    }

    fn parse_macro_pattern_token(&mut self) -> Result<MacroPatternToken, HexaError> {
        if let Token::Ident(ref s) = self.peek().clone() {
            if &**s == "$" {
                self.advance();
                if matches!(self.peek(), Token::LParen) {
                    self.advance();
                    let mut inner = Vec::new();
                    while !matches!(self.peek(), Token::RParen | Token::Eof) {
                        inner.push(self.parse_macro_pattern_token()?);
                    }
                    self.expect(&Token::RParen)?;
                    let sep = if matches!(self.peek(), Token::Comma) {
                        self.advance();
                        Some(MacroBodyToken::Literal(MacroLitToken::Comma))
                    } else {
                        None
                    };
                    let kind = if matches!(self.peek(), Token::Star) {
                        self.advance(); RepeatKind::ZeroOrMore
                    } else if matches!(self.peek(), Token::Plus) {
                        self.advance(); RepeatKind::OneOrMore
                    } else {
                        return Err(self.error("expected * or + after macro repetition".into()));
                    };
                    return Ok(MacroPatternToken::Repetition(inner, sep, kind));
                }
                let cap_name = self.expect_ident()?;
                self.expect(&Token::Colon)?;
                let spec_name = self.expect_ident()?;
                let spec = match spec_name.as_str() {
                    "expr" => MacroFragSpec::Expr,
                    "ident" => MacroFragSpec::Ident,
                    "ty" => MacroFragSpec::Ty,
                    "literal" => MacroFragSpec::Literal,
                    "block" => MacroFragSpec::Block,
                    _ => return Err(self.error(format!("unknown fragment specifier: {}", spec_name))),
                };
                return Ok(MacroPatternToken::Capture(cap_name, spec));
            }
        }
        let lit = self.parse_macro_lit_token()?;
        Ok(MacroPatternToken::Literal(lit))
    }

    fn parse_macro_body(&mut self) -> Result<Vec<MacroBodyToken>, HexaError> {
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut tokens = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            tokens.push(self.parse_macro_body_token()?);
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(tokens)
    }

    fn parse_macro_body_token(&mut self) -> Result<MacroBodyToken, HexaError> {
        if let Token::Ident(ref s) = self.peek().clone() {
            if &**s == "$" {
                self.advance();
                if matches!(self.peek(), Token::LParen) {
                    self.advance();
                    let mut inner = Vec::new();
                    while !matches!(self.peek(), Token::RParen | Token::Eof) {
                        inner.push(self.parse_macro_body_token()?);
                    }
                    self.expect(&Token::RParen)?;
                    let kind = if matches!(self.peek(), Token::Star) {
                        self.advance(); RepeatKind::ZeroOrMore
                    } else if matches!(self.peek(), Token::Plus) {
                        self.advance(); RepeatKind::OneOrMore
                    } else {
                        return Err(self.error("expected * or + after macro repetition".into()));
                    };
                    return Ok(MacroBodyToken::Repetition(inner, kind));
                }
                let var_name = self.expect_ident()?;
                return Ok(MacroBodyToken::Var(var_name));
            }
        }
        let lit = self.parse_macro_lit_token()?;
        Ok(MacroBodyToken::Literal(lit))
    }

    fn parse_macro_lit_token(&mut self) -> Result<MacroLitToken, HexaError> {
        let tok = self.peek().clone();
        let lit = match tok {
            Token::Ident(ref s) => MacroLitToken::Ident(s.to_string()),
            Token::IntLit(n) => MacroLitToken::IntLit(n),
            Token::FloatLit(n) => MacroLitToken::FloatLit(n),
            Token::StringLit(ref s) => MacroLitToken::StringLit(s.to_string()),
            Token::BoolLit(b) => MacroLitToken::BoolLit(b),
            Token::LParen => MacroLitToken::LParen,
            Token::RParen => MacroLitToken::RParen,
            Token::LBrace => MacroLitToken::LBrace,
            Token::RBrace => MacroLitToken::RBrace,
            Token::LBracket => MacroLitToken::LBracket,
            Token::RBracket => MacroLitToken::RBracket,
            Token::Comma => MacroLitToken::Comma,
            Token::Colon => MacroLitToken::Colon,
            Token::ColonColon => MacroLitToken::ColonColon,
            Token::Semicolon => MacroLitToken::Semicolon,
            Token::Dot => MacroLitToken::Dot,
            Token::Plus => MacroLitToken::Plus,
            Token::Minus => MacroLitToken::Minus,
            Token::Star => MacroLitToken::Star,
            Token::Slash => MacroLitToken::Slash,
            Token::Percent => MacroLitToken::Percent,
            Token::Power => MacroLitToken::Power,
            Token::Eq => MacroLitToken::Eq,
            Token::EqEq => MacroLitToken::EqEq,
            Token::NotEq => MacroLitToken::NotEq,
            Token::Lt => MacroLitToken::Lt,
            Token::Gt => MacroLitToken::Gt,
            Token::LtEq => MacroLitToken::LtEq,
            Token::GtEq => MacroLitToken::GtEq,
            Token::And => MacroLitToken::And,
            Token::Or => MacroLitToken::Or,
            Token::Not => MacroLitToken::Not,
            Token::Arrow => MacroLitToken::Arrow,
            Token::DotDot => MacroLitToken::DotDot,
            Token::DotDotEq => MacroLitToken::DotDotEq,
            Token::BitAnd => MacroLitToken::BitAnd,
            Token::BitOr => MacroLitToken::BitOr,
            Token::BitXor => MacroLitToken::BitXor,
            Token::BitNot => MacroLitToken::BitNot,
            Token::Let => MacroLitToken::Let,
            Token::Mut => MacroLitToken::Mut,
            Token::Fn => MacroLitToken::Fn,
            Token::Return => MacroLitToken::Return,
            Token::If => MacroLitToken::If,
            Token::Else => MacroLitToken::Else,
            Token::For => MacroLitToken::For,
            Token::While => MacroLitToken::While,
            Token::Loop => MacroLitToken::Loop,
            Token::Struct => MacroLitToken::Struct,
            Token::Enum => MacroLitToken::Enum,
            Token::Trait => MacroLitToken::Trait,
            Token::Impl => MacroLitToken::Impl,
            Token::Match => MacroLitToken::Match,
            Token::Newline => {
                self.advance();
                self.skip_newlines();
                if matches!(self.peek(), Token::RBrace | Token::RParen | Token::Eof) {
                    return Ok(MacroLitToken::Newline);
                }
                return self.parse_macro_lit_token();
            }
            _ => return Err(self.error(format!("unexpected token in macro: {:?}", tok))),
        };
        self.advance();
        Ok(lit)
    }

    /// Parse macro invocation after name and ! have been consumed.
    fn parse_macro_invocation(&mut self, name: String) -> Result<Expr, HexaError> {
        let (open, close) = if matches!(self.peek(), Token::LParen) {
            (Token::LParen, Token::RParen)
        } else {
            (Token::LBracket, Token::RBracket)
        };
        self.advance(); // consume opening delimiter
        let mut tokens = Vec::new();
        let mut depth = 1u32;
        loop {
            if self.at_end() {
                return Err(self.error("unterminated macro invocation".into()));
            }
            if std::mem::discriminant(self.peek()) == std::mem::discriminant(&close) {
                depth -= 1;
                if depth == 0 {
                    self.advance();
                    break;
                }
            }
            if std::mem::discriminant(self.peek()) == std::mem::discriminant(&open) {
                depth += 1;
            }
            if matches!(self.peek(), Token::Newline | Token::Semicolon) {
                self.advance();
                self.skip_newlines();
                continue;
            }
            let lit = self.parse_macro_lit_token()?;
            tokens.push(lit);
        }
        Ok(Expr::MacroInvoc(MacroInvocation { name, tokens }))
    }

    /// Parse derive declaration: derive(Trait1, Trait2) for TypeName
    fn parse_derive_decl(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'derive'
        self.expect(&Token::LParen)?;
        let mut traits = Vec::new();
        loop {
            traits.push(self.expect_ident()?);
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            } else {
                break;
            }
        }
        self.expect(&Token::RParen)?;
        match self.peek() {
            Token::For => { self.advance(); }
            Token::Ident(ref s) if s == "for" => { self.advance(); }
            _ => return Err(self.error(format!("expected 'for' after derive traits, got {:?}", self.peek()))),
        }
        let type_name = self.expect_ident()?;
        Ok(Stmt::DeriveDecl(type_name, traits))
    }

    fn parse_mod_decl(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'mod'
        let name = self.expect_ident()?;
        let body = self.parse_block()?;
        Ok(Stmt::Mod(name, body))
    }

    fn parse_use_decl(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'use' / 'import'
        // Support string path: import "path/to/module"
        if let Token::StringLit(path) = self.peek().clone() {
            self.advance();
            // Convert "path/to/module" or "path/to/module.hexa" to path segments
            let path_str: &str = &path;
            let path_str = path_str.trim_end_matches(".hexa");
            let segments: Vec<String> = path_str.split('/').map(String::from).collect();
            return Ok(Stmt::Use(segments));
        }
        let mut prefix = vec![self.expect_ident()?];
        while matches!(self.peek(), Token::ColonColon) {
            self.advance(); // consume ::
            // Check for grouped import: use module::{a, b, c}
            if matches!(self.peek(), Token::LBrace) {
                self.advance(); // consume '{'
                self.skip_newlines();
                let mut uses = Vec::new();
                while !matches!(self.peek(), Token::RBrace | Token::Eof) {
                    let item = self.expect_ident()?;
                    let mut full_path = prefix.clone();
                    full_path.push(item);
                    uses.push(Stmt::Use(full_path));
                    self.skip_newlines();
                    if matches!(self.peek(), Token::Comma) {
                        self.advance();
                        self.skip_newlines();
                    }
                }
                self.expect(&Token::RBrace)?;
                // Wrap multiple use stmts in a block expression
                return Ok(Stmt::Expr(Expr::Block(uses)));
            }
            prefix.push(self.expect_ident()?);
        }
        Ok(Stmt::Use(prefix))
    }

    // ── Level 3: Declarations ───────────────────────────────

    fn parse_fn_decl(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'fn'
        let name = self.expect_ident()?;
        // Parse optional type parameters: <T>, <T: Bound>, <T, U>
        let type_params = if matches!(self.peek(), Token::Lt) {
            self.parse_type_params()?
        } else {
            vec![]
        };
        self.expect(&Token::LParen)?;
        let params = self.parse_params()?;
        self.expect(&Token::RParen)?;
        let ret_type = if matches!(self.peek(), Token::Arrow) {
            self.advance();
            Some(self.parse_ret_type()?)
        } else {
            None
        };
        // Parse optional where clause: type bounds or precondition expression
        let (where_clauses, precondition) = self.parse_where_or_precondition()?;
        // Parse optional ensures clause: postcondition expression
        let postcondition = self.parse_ensures_clause()?;
        let mut body = self.parse_block()?;
        let attrs = self.take_attrs();
        let is_evolve = attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Evolve));
        let is_pure = attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Pure));
        // @contract Option A — parser desugar (phase 1):
        //  - requires: prepend `assert(expr)` at function entry
        //  - ensures:  append `assert(expr)` at function body end
        //  NOTE phase 1 단순화: ensures는 body 끝에만 삽입되므로 이른 return 경로에서는 검사 누락.
        //  phase 2에서 return 재작성 + result 바인딩 처리 예정.
        for a in attrs.iter() {
            if !matches!(a.kind, crate::token::AttrKind::Contract) { continue; }
            let mut prepend: Vec<Stmt> = Vec::new();
            let mut append: Vec<Stmt> = Vec::new();
            for (key, expr) in a.args.iter() {
                match key.as_str() {
                    "requires" => prepend.push(Stmt::Assert(expr.clone())),
                    "ensures"  => append.push(Stmt::Assert(expr.clone())),
                    _ => {}
                }
            }
            if !prepend.is_empty() {
                let mut new_body = prepend;
                new_body.extend(body.drain(..));
                body = new_body;
            }
            body.extend(append);
        }
        let decl = FnDecl { name, type_params, params, ret_type, where_clauses, precondition, postcondition, body, vis, is_pure, attrs };
        if is_evolve {
            Ok(Stmt::EvolveFn(decl))
        } else {
            Ok(Stmt::FnDecl(decl))
        }
    }

    /// Parse type parameters: `<T>`, `<T: Display>`, `<T, U>`, `<T: Display, U: Clone>`
    fn parse_type_params(&mut self) -> Result<Vec<TypeParam>, HexaError> {
        self.expect(&Token::Lt)?; // consume <
        let mut type_params = Vec::new();
        loop {
            let name = self.expect_ident()?;
            let bound = if matches!(self.peek(), Token::Colon) {
                self.advance(); // consume :
                Some(self.expect_ident()?)
            } else {
                None
            };
            type_params.push(TypeParam { name, bound });
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            } else {
                break;
            }
        }
        self.expect(&Token::Gt)?; // consume >
        Ok(type_params)
    }

    /// Parse where clause: `where T: Display, U: Clone`
    fn parse_where_clauses(&mut self) -> Result<Vec<WhereClause>, HexaError> {
        self.advance(); // consume 'where'
        let mut clauses = Vec::new();
        loop {
            let type_name = self.expect_ident()?;
            self.expect(&Token::Colon)?;
            let bound = self.expect_ident()?;
            clauses.push(WhereClause { type_name, bound });
            if matches!(self.peek(), Token::Comma) {
                self.advance();
                // Stop if next token is '{' (start of body)
                if matches!(self.peek(), Token::LBrace) {
                    break;
                }
            } else {
                break;
            }
        }
        Ok(clauses)
    }

    /// Determine if `where` is a type-bound clause (where T: Bound) or a precondition expression (where x > 0).
    /// Returns (where_clauses, precondition).
    fn parse_where_or_precondition(&mut self) -> Result<(Vec<WhereClause>, Option<Expr>), HexaError> {
        if !matches!(self.peek(), Token::Where) {
            return Ok((vec![], None));
        }
        // Lookahead: if pattern is Ident Colon Ident, it's a type-bound where clause
        if matches!(self.peek_ahead(1), Token::Ident(_)) && matches!(self.peek_ahead(2), Token::Colon) {
            let clauses = self.parse_where_clauses()?;
            Ok((clauses, None))
        } else {
            // It's a precondition expression: where <expr>
            self.advance(); // consume 'where'
            let expr = self.parse_expr()?;
            Ok((vec![], Some(expr)))
        }
    }

    /// Parse optional `ensures <expr>` clause (postcondition).
    fn parse_ensures_clause(&mut self) -> Result<Option<Expr>, HexaError> {
        if matches!(self.peek(), Token::Ident(s) if &**s == "ensures") {
            self.advance(); // consume 'ensures'
            let expr = self.parse_expr()?;
            Ok(Some(expr))
        } else {
            Ok(None)
        }
    }

    fn parse_params(&mut self) -> Result<Vec<Param>, HexaError> {
        let mut params = Vec::new();
        if matches!(self.peek(), Token::RParen) {
            return Ok(params);
        }
        loop {
            let name = self.expect_ident()?;
            let typ = if matches!(self.peek(), Token::Colon) {
                self.advance();
                Some(self.parse_ret_type()?)
            } else {
                None
            };
            params.push(Param { name, typ });
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            } else {
                break;
            }
        }
        Ok(params)
    }

    fn parse_struct_decl(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'struct'
        let name = self.expect_ident()?;
        // Parse optional type parameters: struct Pair<T> { ... }
        let type_params = if matches!(self.peek(), Token::Lt) {
            self.parse_type_params()?
        } else {
            Vec::new()
        };
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut fields = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            let field_vis = self.parse_visibility();
            let field_name = self.expect_ident()?;
            self.expect(&Token::Colon)?;
            let field_type = self.parse_ret_type()?;
            fields.push((field_name, field_type, field_vis));
            // consume comma or newline
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            }
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        let attrs = self.take_attrs();
        Ok(Stmt::StructDecl(StructDecl { name, type_params, fields, vis, attrs }))
    }

    fn parse_enum_decl(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'enum'
        let name = self.expect_ident()?;
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut variants = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            let var_name = self.expect_ident()?;
            let var_data = if matches!(self.peek(), Token::LParen) {
                self.advance();
                let mut types = Vec::new();
                while !matches!(self.peek(), Token::RParen | Token::Eof) {
                    types.push(self.expect_ident()?);
                    if matches!(self.peek(), Token::Comma) {
                        self.advance();
                    }
                }
                self.expect(&Token::RParen)?;
                Some(types)
            } else {
                None
            };
            variants.push((var_name, var_data));
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            }
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::EnumDecl(EnumDecl { name, variants, vis }))
    }

    fn parse_trait_decl(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'trait'
        let name = self.expect_ident()?;
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut methods = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            self.skip_newlines();
            if matches!(self.peek(), Token::RBrace) {
                break;
            }
            let m_vis = self.parse_visibility();
            self.expect(&Token::Fn)?;
            let m_name = self.expect_ident()?;
            self.expect(&Token::LParen)?;
            let m_params = self.parse_params()?;
            self.expect(&Token::RParen)?;
            let m_ret = if matches!(self.peek(), Token::Arrow) {
                self.advance();
                Some(self.parse_ret_type()?)
            } else {
                None
            };
            // trait methods can have body or not
            let m_body = if matches!(self.peek(), Token::LBrace) {
                self.parse_block()?
            } else {
                vec![]
            };
            methods.push(FnDecl { name: m_name, type_params: vec![], params: m_params, ret_type: m_ret, where_clauses: vec![], precondition: None, postcondition: None, body: m_body, vis: m_vis, is_pure: false, attrs: vec![] });
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::TraitDecl(TraitDecl { name, methods, vis }))
    }

    fn parse_impl_block(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'impl'
        let first_name = self.expect_ident()?;
        let (trait_name, target) = if matches!(self.peek(), Token::For) {
            self.advance();
            let target = self.expect_ident()?;
            (Some(first_name), target)
        } else {
            (None, first_name)
        };
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut methods = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            self.skip_newlines();
            if matches!(self.peek(), Token::RBrace) {
                break;
            }
            let m_vis = self.parse_visibility();
            self.expect(&Token::Fn)?;
            let m_name = self.expect_ident()?;
            self.expect(&Token::LParen)?;
            let m_params = self.parse_params()?;
            self.expect(&Token::RParen)?;
            let m_ret = if matches!(self.peek(), Token::Arrow) {
                self.advance();
                Some(self.parse_ret_type()?)
            } else {
                None
            };
            let m_body = self.parse_block()?;
            methods.push(FnDecl { name: m_name, type_params: vec![], params: m_params, ret_type: m_ret, where_clauses: vec![], precondition: None, postcondition: None, body: m_body, vis: m_vis, is_pure: false, attrs: vec![] });
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::ImplBlock(ImplBlock { trait_name, target, methods }))
    }

    // ── Level 4: Control Flow ───────────────────────────────

    fn parse_for_stmt(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'for'
        let var = self.expect_ident()?;
        // expect 'in' keyword — it's not a keyword token, it's an ident
        match self.advance() {
            Token::Ident(ref s) if s == "in" => {}
            other => return Err(self.error(format!("expected 'in' after for variable, got {:?}", other))),
        }
        // G5: disable struct literal in iter-expression position so that the `{`
        // following the iter expr is parsed as the for-body, not a struct init.
        let saved = self.no_struct_init; self.no_struct_init = true;
        let iter_expr = self.parse_expr()?;
        self.no_struct_init = saved;
        let body = self.parse_block()?;
        Ok(Stmt::For(var, iter_expr, body))
    }

    fn parse_while_stmt(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'while'
        // G5: save/restore to keep correct behaviour under nesting.
        let saved = self.no_struct_init; self.no_struct_init = true;
        let cond = self.parse_expr()?;
        self.no_struct_init = saved;
        let body = self.parse_block()?;
        Ok(Stmt::While(cond, body))
    }

    fn parse_loop_stmt(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'loop'
        let body = self.parse_block()?;
        Ok(Stmt::Loop(body))
    }

    fn parse_if_expr(&mut self) -> Result<Expr, HexaError> {
        self.advance(); // consume 'if'
        // G5: disable struct literal in the condition position so that
        // `if Foo { ... }` parses as `if Foo` + body, not `if (Foo{...})`.
        // Use save/restore for correct behaviour under nesting.
        let saved = self.no_struct_init; self.no_struct_init = true;
        let cond = self.parse_expr()?;
        self.no_struct_init = saved;
        let then_block = self.parse_block()?;
        // Save position to allow backtracking if newlines precede else
        let saved_pos = self.pos;
        self.skip_newlines();
        let else_block = if matches!(self.peek(), Token::Else) {
            self.advance();
            if matches!(self.peek(), Token::If) {
                // else if -> desugar to else { if ... }
                let nested_if = self.parse_if_expr()?;
                Some(vec![Stmt::Expr(nested_if)])
            } else {
                Some(self.parse_block()?)
            }
        } else {
            // No else — backtrack past any consumed newlines
            self.pos = saved_pos;
            None
        };
        Ok(Expr::If(Box::new(cond), then_block, else_block))
    }

    fn parse_match_expr(&mut self) -> Result<Expr, HexaError> {
        self.advance(); // consume 'match'
        // G5: disable struct literal in scrutinee position so `match Foo { ... }`
        // treats the `{` as the arms brace, not a struct init.
        let saved = self.no_struct_init; self.no_struct_init = true;
        let scrutinee = self.parse_expr()?;
        self.no_struct_init = saved;
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        // G5: inside match arms body we are past the `{`, so struct
        // literals are unambiguous again for arm bodies and guards.
        let g5_arms_saved = self.no_struct_init; self.no_struct_init = false;
        let mut arms = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            let pattern = self.parse_match_pattern()?;
            // Optional guard: `if cond`
            let guard = if matches!(self.peek(), Token::If) {
                self.advance(); // consume 'if'
                // Guard is `if <expr>` — same restriction as a normal if head.
                let gsaved = self.no_struct_init; self.no_struct_init = true;
                let g = self.parse_expr()?;
                self.no_struct_init = gsaved;
                Some(g)
            } else {
                None
            };
            self.expect(&Token::Arrow)?;
            let body = if matches!(self.peek(), Token::LBrace) {
                self.parse_block()?
            } else {
                let expr = self.parse_expr()?;
                vec![Stmt::Expr(expr)]
            };
            arms.push(MatchArm { pattern, guard, body });
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            }
            self.skip_newlines();
        }
        self.no_struct_init = g5_arms_saved;
        self.expect(&Token::RBrace)?;
        Ok(Expr::Match(Box::new(scrutinee), arms))
    }

    /// Parse a match pattern. Handles `_`, `[a, b, ...rest]`, `EnumName::Variant(binding)`, and regular expressions.
    fn parse_match_pattern(&mut self) -> Result<Expr, HexaError> {
        // Check for array pattern `[a, b, ...rest]`
        if matches!(self.peek(), Token::LBracket) {
            self.advance(); // consume [
            self.skip_newlines();
            let mut patterns = Vec::new();
            let mut rest_name: Option<String> = None;
            while !matches!(self.peek(), Token::RBracket | Token::Eof) {
                // Check for ...rest spread pattern
                if matches!(self.peek(), Token::DotDot) {
                    self.advance(); // consume ..
                    // Check for optional trailing dot (... = DotDot + Dot)
                    if matches!(self.peek(), Token::Dot) {
                        self.advance(); // consume the third dot
                    }
                    if let Token::Ident(ref name) = self.peek().clone() {
                        rest_name = Some(name.to_string());
                        self.advance();
                    }
                    // Skip trailing comma if present
                    if matches!(self.peek(), Token::Comma) {
                        self.advance();
                    }
                    self.skip_newlines();
                    break;
                }
                let pat = self.parse_match_pattern()?;
                patterns.push(pat);
                if matches!(self.peek(), Token::Comma) {
                    self.advance();
                    self.skip_newlines();
                } else {
                    break;
                }
            }
            self.skip_newlines();
            self.expect(&Token::RBracket)?;
            return Ok(Expr::ArrayPattern(patterns, rest_name));
        }
        // Check for wildcard `_`
        if let Token::Ident(ref name) = self.peek().clone() {
            if &**name == "_" {
                self.advance();
                return Ok(Expr::Wildcard);
            }
            // Check for EnumPath: `Ident :: Ident` or `Ident :: Ident ( expr )`
            if matches!(self.peek_ahead(1), Token::ColonColon) {
                let enum_name = name.to_string();
                self.advance(); // consume enum name
                self.advance(); // consume ::
                let variant = self.expect_ident()?;
                let data = if matches!(self.peek(), Token::LParen) {
                    self.advance(); // consume (
                    let inner = self.parse_expr()?;
                    self.expect(&Token::RParen)?;
                    Some(Box::new(inner))
                } else {
                    None
                };
                return Ok(Expr::EnumPath(enum_name, variant, data));
            }
        }
        // Fall through to regular expression parsing
        self.parse_expr()
    }

    // ── Block ───────────────────────────────────────────────

    fn parse_block(&mut self) -> Result<Block, HexaError> {
        self.skip_newlines();
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut stmts = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            stmts.push(self.parse_stmt()?);
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(stmts)
    }

    // ── Level 5: Expression (precedence climbing) ───────────

    pub fn parse_expr(&mut self) -> Result<Expr, HexaError> {
        self.parse_or()
    }

    // Level 5.1: || ^^
    fn parse_or(&mut self) -> Result<Expr, HexaError> {
        let mut left = self.parse_and()?;
        loop {
            let op = match self.peek() {
                Token::Or => BinOp::Or,
                Token::Xor => BinOp::Xor,
                _ => break,
            };
            self.advance();
            self.skip_newlines();
            let right = self.parse_and()?;
            left = Expr::Binary(Box::new(left), op, Box::new(right));
        }
        Ok(left)
    }

    // Level 5.2: &&
    fn parse_and(&mut self) -> Result<Expr, HexaError> {
        let mut left = self.parse_comparison()?;
        loop {
            if !matches!(self.peek(), Token::And) {
                break;
            }
            self.advance();
            self.skip_newlines();
            let right = self.parse_comparison()?;
            left = Expr::Binary(Box::new(left), BinOp::And, Box::new(right));
        }
        Ok(left)
    }

    // Level 5.3: == != < > <= >=
    fn parse_comparison(&mut self) -> Result<Expr, HexaError> {
        let mut left = self.parse_addition()?;
        loop {
            let op = match self.peek() {
                Token::EqEq => BinOp::Eq,
                Token::NotEq => BinOp::Ne,
                Token::Lt => BinOp::Lt,
                Token::Gt => BinOp::Gt,
                Token::LtEq => BinOp::Le,
                Token::GtEq => BinOp::Ge,
                _ => break,
            };
            self.advance();
            self.skip_newlines();
            let right = self.parse_addition()?;
            left = Expr::Binary(Box::new(left), op, Box::new(right));
        }
        Ok(left)
    }

    // Level 5.4: + - | ^ &  (bitwise mixed into additive for simplicity)
    fn parse_addition(&mut self) -> Result<Expr, HexaError> {
        let mut left = self.parse_multiplication()?;
        loop {
            let op = match self.peek() {
                Token::Plus => BinOp::Add,
                Token::Minus => BinOp::Sub,
                Token::BitXor => BinOp::BitXor,
                Token::BitAnd => BinOp::BitAnd,
                _ => break,
            };
            self.advance();
            self.skip_newlines();
            let right = self.parse_multiplication()?;
            left = Expr::Binary(Box::new(left), op, Box::new(right));
        }
        Ok(left)
    }

    // Level 5.5: * / % **
    fn parse_multiplication(&mut self) -> Result<Expr, HexaError> {
        let mut left = self.parse_unary()?;
        loop {
            let op = match self.peek() {
                Token::Star => BinOp::Mul,
                Token::Slash => BinOp::Div,
                Token::Percent => BinOp::Rem,
                Token::Power => BinOp::Pow,
                _ => break,
            };
            self.advance();
            self.skip_newlines();
            let right = self.parse_unary()?;
            left = Expr::Binary(Box::new(left), op, Box::new(right));
        }
        Ok(left)
    }

    // Level 5.6: Unary ! - ~
    fn parse_unary(&mut self) -> Result<Expr, HexaError> {
        let op = match self.peek() {
            Token::Not => Some(UnaryOp::Not),
            Token::Minus => Some(UnaryOp::Neg),
            Token::BitNot => Some(UnaryOp::BitNot),
            _ => None,
        };
        if let Some(op) = op {
            self.advance();
            let operand = self.parse_unary()?;
            Ok(Expr::Unary(op, Box::new(operand)))
        } else {
            self.parse_postfix()
        }
    }

    // Postfix: call, field, index
    fn parse_postfix(&mut self) -> Result<Expr, HexaError> {
        let mut expr = self.parse_primary()?;
        loop {
            match self.peek() {
                Token::LParen => {
                    self.advance();
                    // G5: inside call args, struct literals are unambiguous.
                    let g5_saved = self.no_struct_init; self.no_struct_init = false;
                    let args = self.parse_args()?;
                    self.no_struct_init = g5_saved;
                    self.expect(&Token::RParen)?;
                    expr = Expr::Call(Box::new(expr), args);
                }
                Token::Dot => {
                    self.advance();
                    // Tuple field access: expr.0, expr.1, ...
                    if let Token::IntLit(n) = self.peek() {
                        let idx = *n;
                        self.advance();
                        expr = Expr::Index(Box::new(expr), Box::new(Expr::IntLit(idx)));
                    } else {
                        let field = self.expect_ident()?;
                        expr = Expr::Field(Box::new(expr), field);
                    }
                }
                Token::LBracket => {
                    self.advance();
                    // G5: inside index, struct literals are unambiguous.
                    let g5_saved = self.no_struct_init; self.no_struct_init = false;
                    let index = self.parse_expr()?;
                    self.no_struct_init = g5_saved;
                    self.expect(&Token::RBracket)?;
                    expr = Expr::Index(Box::new(expr), Box::new(index));
                }
                Token::DotDot => {
                    self.advance();
                    let end = self.parse_addition()?;
                    expr = Expr::Range(Box::new(expr), Box::new(end), false);
                }
                Token::DotDotEq => {
                    self.advance();
                    let end = self.parse_addition()?;
                    expr = Expr::Range(Box::new(expr), Box::new(end), true);
                }
                _ => break,
            }
        }
        Ok(expr)
    }

    fn parse_lambda(&mut self) -> Result<Expr, HexaError> {
        self.advance(); // consume first |
        let mut params = Vec::new();
        // |x, y| body  OR  || body (no params)
        if !matches!(self.peek(), Token::BitOr) {
            loop {
                let name = self.expect_ident()?;
                params.push(Param { name, typ: None });
                if matches!(self.peek(), Token::Comma) {
                    self.advance();
                } else {
                    break;
                }
            }
        }
        self.expect(&Token::BitOr)?; // consume closing |
        // Body can be a block or a single expression
        let body_expr = if matches!(self.peek(), Token::LBrace) {
            let block = self.parse_block()?;
            // Return a Block expression
            return Ok(Expr::Lambda(params, Box::new(Expr::Block(block))));
        } else {
            self.parse_expr()?
        };
        Ok(Expr::Lambda(params, Box::new(body_expr)))
    }

    fn parse_args(&mut self) -> Result<Vec<Expr>, HexaError> {
        let mut args = Vec::new();
        self.skip_newlines();
        if matches!(self.peek(), Token::RParen) {
            return Ok(args);
        }
        loop {
            self.skip_newlines();
            args.push(self.parse_expr()?);
            self.skip_newlines();
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            } else {
                break;
            }
        }
        self.skip_newlines();
        Ok(args)
    }

    // ── Level 6: Primary ────────────────────────────────────

    fn parse_primary(&mut self) -> Result<Expr, HexaError> {
        match self.peek().clone() {
            Token::IntLit(n) => { self.advance(); Ok(Expr::IntLit(n)) }
            Token::FloatLit(n) => { self.advance(); Ok(Expr::FloatLit(n)) }
            Token::BoolLit(b) => { self.advance(); Ok(Expr::BoolLit(b)) }
            Token::StringLit(s) => { self.advance(); Ok(Expr::StringLit(s.to_string())) }
            Token::CharLit(c) => { self.advance(); Ok(Expr::CharLit(c)) }
            Token::Ident(ref rc_name) => {
                let name = rc_name.to_string();
                self.advance();
                // Check for macro invocation: name!(...) or name![...]
                if matches!(self.peek(), Token::Not) {
                    // Could be macro invocation — peek ahead for ( or [
                    let saved = self.pos;
                    self.advance(); // consume !
                    if matches!(self.peek(), Token::LParen | Token::LBracket) {
                        return self.parse_macro_invocation(name);
                    } else {
                        // Not a macro invocation, backtrack
                        self.pos = saved;
                    }
                }
                // Check for enum path: Name::Variant
                // Parenthesized data is handled by postfix Call:
                //   Name::Variant(data) → Call(EnumPath(Name, Variant, None), [data])
                if matches!(self.peek(), Token::ColonColon) {
                    self.advance(); // consume ::
                    let variant = self.expect_ident()?;
                    Ok(Expr::EnumPath(name, variant, None))
                }
                // Check for struct instantiation: Name { field: val, ... }
                // Only if name starts with uppercase (convention)
                else if !self.no_struct_init && matches!(self.peek(), Token::LBrace) && name.chars().next().map_or(false, |c| c.is_uppercase()) {
                    self.advance(); // consume {
                    self.skip_newlines();
                    // G5: inside a struct literal body, we are past the `{`, so
                    // nested struct literals are unambiguous again.
                    let g5_saved = self.no_struct_init; self.no_struct_init = false;
                    let mut fields = Vec::new();
                    while !matches!(self.peek(), Token::RBrace | Token::Eof) {
                        let field_name = self.expect_ident()?;
                        self.expect(&Token::Colon)?;
                        let field_val = self.parse_expr()?;
                        fields.push((field_name, field_val));
                        if matches!(self.peek(), Token::Comma) {
                            self.advance();
                        }
                        self.skip_newlines();
                    }
                    self.no_struct_init = g5_saved;
                    self.expect(&Token::RBrace)?;
                    Ok(Expr::StructInit(name, fields))
                } else {
                    Ok(Expr::Ident(name))
                }
            }
            Token::LParen => {
                self.advance();
                // G5: inside parentheses, struct literals are unambiguously allowed
                // (they cannot be confused with a control-flow body). Save/restore.
                let saved_nsi = self.no_struct_init; self.no_struct_init = false;
                let expr = self.parse_expr()?;
                // Tuple or grouped
                let result = if matches!(self.peek(), Token::Comma) {
                    let mut items = vec![expr];
                    while matches!(self.peek(), Token::Comma) {
                        self.advance();
                        if matches!(self.peek(), Token::RParen) {
                            break;
                        }
                        items.push(self.parse_expr()?);
                    }
                    self.expect(&Token::RParen)?;
                    Expr::Tuple(items)
                } else {
                    self.expect(&Token::RParen)?;
                    expr // grouped expression
                };
                self.no_struct_init = saved_nsi;
                Ok(result)
            }
            Token::LBracket => {
                self.advance();
                self.skip_newlines();
                // G5: inside brackets, struct literals are unambiguously allowed.
                let saved_nsi = self.no_struct_init; self.no_struct_init = false;
                let mut items = Vec::new();
                while !matches!(self.peek(), Token::RBracket | Token::Eof) {
                    items.push(self.parse_expr()?);
                    self.skip_newlines();
                    if matches!(self.peek(), Token::Comma) {
                        self.advance();
                        self.skip_newlines();
                    }
                }
                self.expect(&Token::RBracket)?;
                self.no_struct_init = saved_nsi;
                Ok(Expr::Array(items))
            }
            Token::Dyn => {
                self.advance(); // consume 'dyn'
                let trait_name = self.expect_ident()?;
                self.expect(&Token::LParen)?;
                let expr = self.parse_expr()?;
                self.expect(&Token::RParen)?;
                Ok(Expr::DynCast(trait_name, Box::new(expr)))
            }
            Token::If => self.parse_if_expr(),
            Token::Match => self.parse_match_expr(),
            Token::BitOr => self.parse_lambda(),
            // Channel keyword: `channel()` calls builtin, bare `channel` also creates one
            Token::Channel => {
                self.advance();
                // If followed by '(', parse as function call (channel() or channel(cap))
                if matches!(self.peek(), Token::LParen) {
                    self.advance(); // consume '('
                    let mut args = Vec::new();
                    if !matches!(self.peek(), Token::RParen) {
                        args.push(self.parse_expr()?);
                    }
                    self.expect(&Token::RParen)?;
                    Ok(Expr::Call(Box::new(Expr::Ident("channel".into())), args))
                } else {
                    // Bare `channel` — create a channel with no args
                    Ok(Expr::Call(Box::new(Expr::Ident("channel".into())), vec![]))
                }
            }
            // Await expression
            Token::Await => {
                self.advance();
                let expr = self.parse_expr()?;
                Ok(Expr::Await(Box::new(expr)))
            }
            // Ownership expressions
            Token::Own => {
                self.advance();
                let expr = self.parse_expr()?;
                Ok(Expr::Own(Box::new(expr)))
            }
            Token::Move => {
                self.advance();
                let expr = self.parse_expr()?;
                Ok(Expr::MoveExpr(Box::new(expr)))
            }
            Token::Borrow => {
                self.advance();
                let expr = self.parse_expr()?;
                Ok(Expr::Borrow(Box::new(expr)))
            }
            // Comptime block expression: comptime { ... }
            Token::Comptime => {
                self.advance();
                let block = self.parse_block()?;
                Ok(Expr::Comptime(Box::new(Expr::Block(block))))
            }
            // Map literal: #{ key: val, ... }
            Token::HashLBrace => {
                self.advance(); // consume #{
                self.skip_newlines();
                let mut pairs = Vec::new();
                while !matches!(self.peek(), Token::RBrace | Token::Eof) {
                    let key = self.parse_expr()?;
                    self.expect(&Token::Colon)?;
                    let val = self.parse_expr()?;
                    pairs.push((key, val));
                    if matches!(self.peek(), Token::Comma) {
                        self.advance();
                    }
                    self.skip_newlines();
                }
                self.expect(&Token::RBrace)?;
                Ok(Expr::MapLit(pairs))
            }
            // Yield expression
            Token::Yield => {
                self.advance();
                let expr = self.parse_expr()?;
                Ok(Expr::Yield(Box::new(expr)))
            }
            // Algebraic effects: handle { ... } with { ... }
            Token::Handle => self.parse_handle_with_expr(),
            // Algebraic effects: resume(val)
            Token::Resume => {
                self.advance();
                self.expect(&Token::LParen)?;
                let val = self.parse_expr()?;
                self.expect(&Token::RParen)?;
                Ok(Expr::Resume(Box::new(val)))
            }
            // try { ... } catch e { ... } as expression — returns last value of block
            Token::Try => self.parse_try_expr(),
            other => Err(self.error(format!("unexpected token in expression: {:?}", other))),
        }
    }

    fn parse_try_expr(&mut self) -> Result<Expr, HexaError> {
        self.advance(); // consume 'try'
        let try_block = self.parse_block()?;
        self.skip_newlines();
        let is_recover = matches!(self.peek(), Token::Recover);
        match self.peek() {
            Token::Catch | Token::Recover => { self.advance(); }
            _ => return Err(self.error(format!("expected 'catch' or 'recover' after try block, got {:?}", self.peek()))),
        }
        let err_name = self.expect_ident()?;
        let tagged_name = if is_recover {
            format!("__recover__{}", err_name)
        } else {
            err_name
        };
        let catch_block = self.parse_block()?;
        Ok(Expr::TryCatch(try_block, tagged_name, catch_block))
    }

    // ── Algebraic effects parsing ────────────────────────────

    /// Parse `effect Name { fn op(params) -> ret; ... }`
    fn parse_effect_decl(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'effect'
        let name = self.expect_ident()?;
        // Optional type params: effect State<T> { ... }
        let type_params = if matches!(self.peek(), Token::Lt) {
            self.advance(); // consume <
            let mut params = Vec::new();
            loop {
                params.push(self.expect_ident()?);
                if matches!(self.peek(), Token::Comma) {
                    self.advance();
                } else {
                    break;
                }
            }
            self.expect(&Token::Gt)?;
            params
        } else {
            vec![]
        };
        self.skip_newlines();
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut operations = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            self.expect(&Token::Fn)?;
            let op_name = self.expect_ident()?;
            self.expect(&Token::LParen)?;
            let params = self.parse_params()?;
            self.expect(&Token::RParen)?;
            let ret_type = if matches!(self.peek(), Token::Arrow) {
                self.advance();
                let t = self.parse_ret_type()?;
                if t == "()" { None } else { Some(t) }
            } else {
                None
            };
            operations.push(EffectOp { name: op_name, params, return_type: ret_type });
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::EffectDecl(EffectDecl { name, type_params, operations }))
    }

    /// Parse `extern fn name(param: Type, ...) -> RetType`
    /// Optionally preceded by `@link("libname")` attribute.
    fn parse_extern_fn_decl(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'extern'
        self.expect(&Token::Fn)?;
        let name = self.expect_ident()?;
        self.expect(&Token::LParen)?;

        // Parse extern params with types
        let mut params = Vec::new();
        while !matches!(self.peek(), Token::RParen | Token::Eof) {
            let param_name = self.expect_ident()?;
            self.expect(&Token::Colon)?;
            let typ = self.parse_extern_type()?;
            params.push(crate::ast::ExternParam { name: param_name, typ });
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            }
        }
        self.expect(&Token::RParen)?;

        // Optional return type
        let ret_type = if matches!(self.peek(), Token::Arrow) {
            self.advance();
            Some(self.parse_extern_type()?)
        } else {
            None
        };

        // Check if there's a pending @link attribute
        let link_lib = self.pending_link_attr.take();

        Ok(Stmt::Extern(crate::ast::ExternFnDecl {
            name,
            params,
            ret_type,
            link_lib,
        }))
    }

    /// Parse a type for extern declarations (supports pointer types like *Void, *Byte, *Int)
    fn parse_extern_type(&mut self) -> Result<String, HexaError> {
        if matches!(self.peek(), Token::Star) {
            self.advance(); // consume '*'
            let inner = self.expect_ident()?;
            Ok(format!("*{}", inner))
        } else {
            self.expect_ident()
        }
    }

    /// Parse `pure fn name(...) { ... }`
    fn parse_pure_fn(&mut self, vis: Visibility) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'pure'
        self.expect(&Token::Fn)?;
        let name = self.expect_ident()?;
        let type_params = if matches!(self.peek(), Token::Lt) {
            self.parse_type_params()?
        } else {
            vec![]
        };
        self.expect(&Token::LParen)?;
        let params = self.parse_params()?;
        self.expect(&Token::RParen)?;
        let ret_type = if matches!(self.peek(), Token::Arrow) {
            self.advance();
            Some(self.parse_ret_type()?)
        } else {
            None
        };
        let where_clauses = if matches!(self.peek(), Token::Where) {
            self.parse_where_clauses()?
        } else {
            vec![]
        };
        let body = self.parse_block()?;
        Ok(Stmt::FnDecl(FnDecl { name, type_params, params, ret_type, where_clauses, precondition: None, postcondition: None, body, vis, is_pure: true, attrs: vec![] }))
    }

    /// Parse `handle { body } with { Effect.op(params) => { handler }, ... }`
    fn parse_handle_with_expr(&mut self) -> Result<Expr, HexaError> {
        self.advance(); // consume 'handle'
        let body_block = self.parse_block()?;
        self.skip_newlines();
        // Expect 'with' — it's an identifier, not a keyword
        match self.peek().clone() {
            Token::Ident(ref s) if s == "with" => { self.advance(); }
            _ => return Err(self.error(format!("expected 'with' after handle block, got {:?}", self.peek()))),
        }
        self.skip_newlines();
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut handlers = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            // Parse: Effect.op(params) => { body }
            let effect_name = self.expect_ident()?;
            self.expect(&Token::Dot)?;
            let op_name = self.expect_ident()?;
            // Optional params
            let mut params = Vec::new();
            if matches!(self.peek(), Token::LParen) {
                self.advance();
                while !matches!(self.peek(), Token::RParen | Token::Eof) {
                    params.push(self.expect_ident()?);
                    if matches!(self.peek(), Token::Comma) {
                        self.advance();
                    }
                }
                self.expect(&Token::RParen)?;
            }
            self.expect(&Token::FatArrow)?;
            let body = self.parse_block()?;
            handlers.push(EffectHandler { effect: effect_name, op: op_name, params, body });
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Expr::HandleWith(
            Box::new(Expr::Block(body_block)),
            handlers,
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;

    fn parse_source(src: &str) -> Vec<Stmt> {
        let tokens = Lexer::new(src).tokenize().unwrap();
        Parser::new(tokens).parse().unwrap()
    }

    #[test]
    fn test_parse_let_binding() {
        let tokens = Lexer::new("let x = 42").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        assert!(matches!(&stmts[0], Stmt::Let(name, ..) if name == "x"));
    }

    #[test]
    fn test_parse_fn_decl() {
        let tokens = Lexer::new("fn add(a: int, b: int) -> int {\n  return a + b\n}").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        assert!(matches!(&stmts[0], Stmt::FnDecl(f) if f.name == "add" && f.params.len() == 2));
    }

    #[test]
    fn test_parse_if_expr() {
        let tokens = Lexer::new("if x > 0 { x } else { 0 }").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        assert!(matches!(&stmts[0], Stmt::Expr(Expr::If(..))));
    }

    #[test]
    fn test_parse_struct_decl() {
        let tokens = Lexer::new("struct Point {\n  x: float,\n  y: float\n}").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        assert!(matches!(&stmts[0], Stmt::StructDecl(s) if s.name == "Point" && s.fields.len() == 2));
    }

    #[test]
    fn test_parse_for_loop() {
        let tokens = Lexer::new("for i in 0..10 {\n  print(i)\n}").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        assert!(matches!(&stmts[0], Stmt::For(..)));
    }

    // ── G1: tuple return type ────────────────────────────────
    #[test]
    fn test_g1_tuple_return_type() {
        let stmts = parse_source("fn pair() -> (int, int) {\n  return (1, 2)\n}");
        if let Stmt::FnDecl(f) = &stmts[0] {
            assert_eq!(f.ret_type.as_deref(), Some("(int, int)"));
        } else {
            panic!("Expected FnDecl");
        }
    }

    // ── G2: array type annotation on let/const/static/struct field ────
    #[test]
    fn test_g2_let_array_annotation() {
        let stmts = parse_source("let xs: [int] = [1, 2, 3]");
        if let Stmt::Let(name, typ, _, _) = &stmts[0] {
            assert_eq!(name, "xs");
            assert_eq!(typ.as_deref(), Some("[int]"));
        } else {
            panic!("Expected Let");
        }
    }

    #[test]
    fn test_g2_nested_array_annotation() {
        let stmts = parse_source("let m: [[int]] = [[1, 2], [3, 4]]");
        if let Stmt::Let(_, typ, _, _) = &stmts[0] {
            assert_eq!(typ.as_deref(), Some("[[int]]"));
        } else {
            panic!("Expected Let");
        }
    }

    #[test]
    fn test_g2_static_array_annotation() {
        let stmts = parse_source("static G: [int] = [1, 2, 3]");
        if let Stmt::Static(name, typ, _, _) = &stmts[0] {
            assert_eq!(name, "G");
            assert_eq!(typ.as_deref(), Some("[int]"));
        } else {
            panic!("Expected Static");
        }
    }

    #[test]
    fn test_g2_struct_field_array_type() {
        let stmts = parse_source("struct Bag {\n  items: [int]\n}");
        if let Stmt::StructDecl(s) = &stmts[0] {
            assert_eq!(s.fields.len(), 1);
            assert_eq!(s.fields[0].0, "items");
            assert_eq!(s.fields[0].1, "[int]");
        } else {
            panic!("Expected StructDecl");
        }
    }

    #[test]
    fn test_operator_precedence() {
        // 1 + 2 * 3 should parse as 1 + (2 * 3)
        let tokens = Lexer::new("1 + 2 * 3").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        // Should be Binary(1, Add, Binary(2, Mul, 3))
        if let Stmt::Expr(Expr::Binary(_, BinOp::Add, right)) = &stmts[0] {
            assert!(matches!(right.as_ref(), Expr::Binary(_, BinOp::Mul, _)));
        } else {
            panic!("Expected Binary Add at top level");
        }
    }

    #[test]
    fn test_parse_effect_decl() {
        let stmts = parse_source("effect Console {\n  fn log(msg: str)\n  fn read() -> str\n}");
        if let Stmt::EffectDecl(decl) = &stmts[0] {
            assert_eq!(decl.name, "Console");
            assert_eq!(decl.operations.len(), 2);
            assert_eq!(decl.operations[0].name, "log");
            assert_eq!(decl.operations[1].name, "read");
            assert!(decl.operations[1].return_type.is_some());
        } else {
            panic!("Expected EffectDecl");
        }
    }

    #[test]
    fn test_parse_effect_decl_with_type_params() {
        let stmts = parse_source("effect State<T> {\n  fn get() -> T\n  fn set(val: T)\n}");
        if let Stmt::EffectDecl(decl) = &stmts[0] {
            assert_eq!(decl.name, "State");
            assert_eq!(decl.type_params, vec!["T".to_string()]);
            assert_eq!(decl.operations.len(), 2);
        } else {
            panic!("Expected EffectDecl");
        }
    }

    #[test]
    fn test_parse_pure_fn() {
        let stmts = parse_source("pure fn add(a: int, b: int) -> int {\n  a + b\n}");
        if let Stmt::FnDecl(decl) = &stmts[0] {
            assert_eq!(decl.name, "add");
            assert!(decl.is_pure);
            assert_eq!(decl.params.len(), 2);
        } else {
            panic!("Expected FnDecl");
        }
    }

    #[test]
    fn test_parse_handle_with() {
        let stmts = parse_source("handle {\n  42\n} with {\n  E.op(x) => {\n    resume(x)\n  }\n}");
        if let Stmt::Expr(Expr::HandleWith(body, handlers)) = &stmts[0] {
            assert!(matches!(body.as_ref(), Expr::Block(_)));
            assert_eq!(handlers.len(), 1);
            assert_eq!(handlers[0].effect, "E");
            assert_eq!(handlers[0].op, "op");
            assert_eq!(handlers[0].params, vec!["x".to_string()]);
        } else {
            panic!("Expected HandleWith expr, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_resume() {
        let stmts = parse_source("resume(42)");
        assert!(matches!(&stmts[0], Stmt::Expr(Expr::Resume(_))));
    }

    #[test]
    fn test_parse_scope() {
        let stmts = parse_source("scope {\n  spawn { 1 + 2 }\n  spawn { 3 + 4 }\n}");
        assert!(matches!(&stmts[0], Stmt::Scope(_)));
        if let Stmt::Scope(body) = &stmts[0] {
            assert_eq!(body.len(), 2);
            assert!(matches!(&body[0], Stmt::Spawn(_)));
            assert!(matches!(&body[1], Stmt::Spawn(_)));
        }
    }

    #[test]
    fn test_parse_nested_scope() {
        let src = "scope {\n  spawn { 1 }\n  scope {\n    spawn { 2 }\n  }\n}";
        let stmts = parse_source(src);
        assert!(matches!(&stmts[0], Stmt::Scope(_)));
        if let Stmt::Scope(body) = &stmts[0] {
            assert_eq!(body.len(), 2);
            assert!(matches!(&body[0], Stmt::Spawn(_)));
            assert!(matches!(&body[1], Stmt::Scope(_)));
        }
    }

    #[test]
    fn test_parse_consciousness_block_string_name() {
        let src = "consciousness \"test\" {\n  let x = phi\n}";
        let stmts = parse_source(src);
        assert!(matches!(&stmts[0], Stmt::ConsciousnessBlock(name, _) if name == "test"));
    }

    #[test]
    fn test_parse_consciousness_block_ident_name() {
        let src = "consciousness engine {\n  let x = 1\n}";
        let stmts = parse_source(src);
        assert!(matches!(&stmts[0], Stmt::ConsciousnessBlock(name, _) if name == "engine"));
    }

    #[test]
    fn test_parse_evolve_fn_basic() {
        let src = "@evolve fn improve(x) {\n  x * 2\n}";
        let stmts = parse_source(src);
        assert!(matches!(&stmts[0], Stmt::EvolveFn(decl) if decl.name == "improve"));
    }

    #[test]
    fn test_parse_evolve_fn_typed() {
        let src = "@evolve fn compute(x: int) -> int {\n  x + 1\n}";
        let stmts = parse_source(src);
        if let Stmt::EvolveFn(decl) = &stmts[0] {
            assert_eq!(decl.name, "compute");
            assert_eq!(decl.ret_type.as_deref(), Some("int"));
        } else {
            panic!("expected EvolveFn");
        }
    }

    #[test]
    fn test_parse_memoize_attr() {
        let src = "@memoize fn fib(n) {\n  n\n}";
        let stmts = parse_source(src);
        if let Stmt::FnDecl(decl) = &stmts[0] {
            assert_eq!(decl.name, "fib");
            assert!(!decl.attrs.is_empty(), "attrs should not be empty");
            assert!(matches!(decl.attrs[0].kind, crate::token::AttrKind::Memoize));
        } else {
            panic!("expected FnDecl, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_multiple_attrs() {
        let src = "@pure @inline fn add(a, b) {\n  a + b\n}";
        let stmts = parse_source(src);
        if let Stmt::FnDecl(decl) = &stmts[0] {
            assert_eq!(decl.attrs.len(), 2);
            assert!(matches!(decl.attrs[0].kind, crate::token::AttrKind::Pure));
            assert!(matches!(decl.attrs[1].kind, crate::token::AttrKind::Inline));
        } else {
            panic!("expected FnDecl");
        }
    }

    // ── Part A: Parser gap tests ────────────────────────────────

    #[test]
    fn test_parse_proof_block_forall() {
        let src = "proof commutativity {\n  forall x: int, x > 0 => x + 1 > 0\n}";
        let stmts = parse_source(src);
        if let Stmt::ProofBlock(name, proof_stmts) = &stmts[0] {
            assert_eq!(name, "commutativity");
            assert_eq!(proof_stmts.len(), 1);
            assert!(matches!(&proof_stmts[0], ProofBlockStmt::ForAll { var, typ, .. } if var == "x" && typ == "int"));
        } else {
            panic!("expected ProofBlock, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_proof_block_exists() {
        let src = "proof existence {\n  exists x: int, x > 0\n}";
        let stmts = parse_source(src);
        if let Stmt::ProofBlock(name, proof_stmts) = &stmts[0] {
            assert_eq!(name, "existence");
            assert_eq!(proof_stmts.len(), 1);
            assert!(matches!(&proof_stmts[0], ProofBlockStmt::Exists { var, typ, .. } if var == "x" && typ == "int"));
        } else {
            panic!("expected ProofBlock, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_proof_block_check_law() {
        let src = "proof laws {\n  check law(6)\n}";
        let stmts = parse_source(src);
        if let Stmt::ProofBlock(name, proof_stmts) = &stmts[0] {
            assert_eq!(name, "laws");
            assert!(matches!(&proof_stmts[0], ProofBlockStmt::CheckLaw(6)));
        } else {
            panic!("expected ProofBlock, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_proof_block_invariant() {
        let src = "proof inv_test {\n  invariant x > 0\n}";
        let stmts = parse_source(src);
        if let Stmt::ProofBlock(name, proof_stmts) = &stmts[0] {
            assert_eq!(name, "inv_test");
            assert!(matches!(&proof_stmts[0], ProofBlockStmt::Invariant(_)));
        } else {
            panic!("expected ProofBlock, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_proof_block_mixed() {
        let src = "proof mixed {\n  forall x: int, x > 0 => x > 0\n  exists y: int, y == 1\n  check law(3)\n}";
        let stmts = parse_source(src);
        if let Stmt::ProofBlock(name, proof_stmts) = &stmts[0] {
            assert_eq!(name, "mixed");
            assert_eq!(proof_stmts.len(), 3);
        } else {
            panic!("expected ProofBlock");
        }
    }

    #[test]
    fn test_parse_simple_proof_with_assert_still_works() {
        // assert inside proof should still parse as simple Proof (not ProofBlock)
        let src = "proof my_proof {\n  assert 1 == 1\n}";
        let stmts = parse_source(src);
        assert!(matches!(&stmts[0], Stmt::Proof(..)));
    }

    #[test]
    fn test_parse_map_literal() {
        let src = r#"#{ "a": 1, "b": 2 }"#;
        let stmts = parse_source(src);
        if let Stmt::Expr(Expr::MapLit(pairs)) = &stmts[0] {
            assert_eq!(pairs.len(), 2);
        } else {
            panic!("expected MapLit, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_map_literal_empty() {
        let src = "#{}";
        let stmts = parse_source(src);
        if let Stmt::Expr(Expr::MapLit(pairs)) = &stmts[0] {
            assert_eq!(pairs.len(), 0);
        } else {
            panic!("expected empty MapLit");
        }
    }

    // ── Part B: Token-only keyword tests ────────────────────────

    #[test]
    fn test_parse_type_alias() {
        let src = "type MyInt = int";
        let stmts = parse_source(src);
        if let Stmt::TypeAlias(name, target, vis) = &stmts[0] {
            assert_eq!(name, "MyInt");
            assert_eq!(target, "int");
            assert_eq!(*vis, Visibility::Private);
        } else {
            panic!("expected TypeAlias, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_pub_type_alias() {
        let src = "pub type Alias = string";
        let stmts = parse_source(src);
        if let Stmt::TypeAlias(name, target, vis) = &stmts[0] {
            assert_eq!(name, "Alias");
            assert_eq!(target, "string");
            assert_eq!(*vis, Visibility::Public);
        } else {
            panic!("expected TypeAlias with pub vis");
        }
    }

    #[test]
    fn test_parse_yield() {
        let src = "yield 42";
        let stmts = parse_source(src);
        if let Stmt::Expr(Expr::Yield(inner)) = &stmts[0] {
            assert!(matches!(inner.as_ref(), Expr::IntLit(42)));
        } else {
            panic!("expected Yield expr, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_crate_visibility() {
        let src = "crate let x = 10";
        let stmts = parse_source(src);
        if let Stmt::Let(name, _, _, vis) = &stmts[0] {
            assert_eq!(name, "x");
            assert_eq!(*vis, Visibility::Crate);
        } else {
            panic!("expected Let with Crate visibility, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_crate_fn() {
        let src = "crate fn helper() {\n  42\n}";
        let stmts = parse_source(src);
        if let Stmt::FnDecl(decl) = &stmts[0] {
            assert_eq!(decl.name, "helper");
            assert_eq!(decl.vis, Visibility::Crate);
        } else {
            panic!("expected FnDecl with Crate visibility");
        }
    }

    #[test]
    fn test_parse_atomic_let() {
        let src = "atomic let counter = 0";
        let stmts = parse_source(src);
        if let Stmt::AtomicLet(name, _, expr, _) = &stmts[0] {
            assert_eq!(name, "counter");
            assert!(expr.is_some());
        } else {
            panic!("expected AtomicLet, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_atomic_let_typed() {
        let src = "atomic let x: int = 42";
        let stmts = parse_source(src);
        if let Stmt::AtomicLet(name, typ, _, _) = &stmts[0] {
            assert_eq!(name, "x");
            assert_eq!(typ.as_deref(), Some("int"));
        } else {
            panic!("expected typed AtomicLet");
        }
    }

    #[test]
    fn test_parse_theorem() {
        let src = "theorem commutativity {\n  forall a: int, a > 0 => a > 0\n}";
        let stmts = parse_source(src);
        if let Stmt::Theorem(name, proof_stmts) = &stmts[0] {
            assert_eq!(name, "commutativity");
            assert_eq!(proof_stmts.len(), 1);
        } else {
            panic!("expected Theorem, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_panic_string() {
        let src = r#"panic "something went wrong""#;
        let stmts = parse_source(src);
        if let Stmt::Panic(expr) = &stmts[0] {
            assert!(matches!(expr, Expr::StringLit(s) if s == "something went wrong"));
        } else {
            panic!("expected Panic stmt, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_parse_panic_expr() {
        let src = "panic 42";
        let stmts = parse_source(src);
        assert!(matches!(&stmts[0], Stmt::Panic(Expr::IntLit(42))));
    }

    // ── Operator category coverage tests ────────────────────────

    #[test]
    fn test_arithmetic_operators() {
        // + - * / % **
        let stmts = parse_source("1 + 2 - 3 * 4 / 5 % 6 ** 7");
        assert!(matches!(&stmts[0], Stmt::Expr(Expr::Binary(..))));
    }

    #[test]
    fn test_bitwise_operators() {
        // & | ^ ~
        let stmts = parse_source("~x");
        assert!(matches!(&stmts[0], Stmt::Expr(Expr::Unary(UnaryOp::BitNot, _))));
    }

    #[test]
    fn test_logical_operators() {
        // && || ! ^^
        let stmts = parse_source("a && b || c ^^ d");
        assert!(matches!(&stmts[0], Stmt::Expr(Expr::Binary(..))));
        let stmts2 = parse_source("!x");
        assert!(matches!(&stmts2[0], Stmt::Expr(Expr::Unary(UnaryOp::Not, _))));
    }

    #[test]
    fn test_comparison_operators() {
        let stmts = parse_source("a == b");
        assert!(matches!(&stmts[0], Stmt::Expr(Expr::Binary(_, BinOp::Eq, _))));
        let stmts2 = parse_source("a != b");
        assert!(matches!(&stmts2[0], Stmt::Expr(Expr::Binary(_, BinOp::Ne, _))));
        let stmts3 = parse_source("a <= b");
        assert!(matches!(&stmts3[0], Stmt::Expr(Expr::Binary(_, BinOp::Le, _))));
        let stmts4 = parse_source("a >= b");
        assert!(matches!(&stmts4[0], Stmt::Expr(Expr::Binary(_, BinOp::Ge, _))));
    }

    #[test]
    fn test_special_operators() {
        // .. ..= ->
        let stmts = parse_source("1..10");
        assert!(matches!(&stmts[0], Stmt::Expr(Expr::Range(_, _, false))));
        let stmts2 = parse_source("1..=10");
        assert!(matches!(&stmts2[0], Stmt::Expr(Expr::Range(_, _, true))));
    }

    // ── G5: if/while/for/match condition struct-literal ambiguity ────
    #[test]
    fn test_g5_if_with_following_struct_literal_block() {
        // The struct literal is INSIDE the then-block, not part of the condition.
        let stmts = parse_source("if x > 0 { Point { x: 1, y: 2 } }");
        if let Stmt::Expr(Expr::If(cond, then_block, _)) = &stmts[0] {
            // Condition must be `x > 0`, not a struct init.
            assert!(matches!(cond.as_ref(), Expr::Binary(_, BinOp::Gt, _)), "cond was {:?}", cond);
            // Then-block must contain a StructInit expression.
            assert_eq!(then_block.len(), 1);
            assert!(matches!(&then_block[0], Stmt::Expr(Expr::StructInit(n, _)) if n == "Point"));
        } else {
            panic!("expected If expression, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_g5_if_no_struct_literal_in_cond() {
        // `if Y { ... }` — Y is an uppercase ident; must be parsed as the condition
        // (plain ident), and `{ ... }` must be the then-block.
        let stmts = parse_source("if Y {\n  1\n}");
        if let Stmt::Expr(Expr::If(cond, then_block, _)) = &stmts[0] {
            assert!(matches!(cond.as_ref(), Expr::Ident(n) if n == "Y"),
                "expected Ident(Y) as cond, got {:?}", cond);
            assert_eq!(then_block.len(), 1);
        } else {
            panic!("expected If expression, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_g5_struct_literal_allowed_in_parens_cond() {
        // Parenthesized cond re-enables struct literals.
        let stmts = parse_source("if (Point { x: 1, y: 2 } == p) { 1 }");
        if let Stmt::Expr(Expr::If(cond, _, _)) = &stmts[0] {
            // cond should be a binary eq whose LHS is a StructInit.
            if let Expr::Binary(lhs, BinOp::Eq, _) = cond.as_ref() {
                assert!(matches!(lhs.as_ref(), Expr::StructInit(n, _) if n == "Point"),
                    "expected StructInit LHS, got {:?}", lhs);
            } else {
                panic!("expected Binary Eq cond, got {:?}", cond);
            }
        } else {
            panic!("expected If expression");
        }
    }

    #[test]
    fn test_g5_while_with_following_struct_literal_block() {
        let stmts = parse_source("while x < 10 { Point { x: 1, y: 2 } }");
        if let Stmt::While(cond, body) = &stmts[0] {
            assert!(matches!(cond, Expr::Binary(_, BinOp::Lt, _)));
            assert_eq!(body.len(), 1);
            assert!(matches!(&body[0], Stmt::Expr(Expr::StructInit(n, _)) if n == "Point"));
        } else {
            panic!("expected While, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_trait_name_in_impl() {
        let src = "impl Display for Point {\n  fn show(self) {\n    42\n  }\n}";
        let stmts = parse_source(src);
        if let Stmt::ImplBlock(impl_block) = &stmts[0] {
            assert_eq!(impl_block.trait_name.as_deref(), Some("Display"));
            assert_eq!(impl_block.target, "Point");
        } else {
            panic!("expected ImplBlock");
        }
    }

    // ── G6: ; (semicolons) regression tests ─────────────────────
    // fix commit: f84f950 — src/parser.rs:parse_return + macro literal/invocation path
    #[test]
    fn test_g6_return_with_semicolon() {
        // `return 1;` and bare `return;` must both parse inside a function body.
        let stmts = parse_source("fn f() -> int {\n  return 1;\n}");
        if let Stmt::FnDecl(f) = &stmts[0] {
            assert_eq!(f.name, "f");
            // Body should contain exactly one Return(Some(1)) stmt.
            assert!(f.body.iter().any(|s| matches!(s, Stmt::Return(Some(_)))));
        } else {
            panic!("expected FnDecl");
        }
        let stmts = parse_source("fn g() {\n  return;\n}");
        if let Stmt::FnDecl(f) = &stmts[0] {
            assert!(f.body.iter().any(|s| matches!(s, Stmt::Return(None))));
        } else {
            panic!("expected FnDecl");
        }
    }

    #[test]
    fn test_g6_macro_with_semicolons() {
        // Semicolons inside a macro invocation body are accepted as separators
        // (parse_macro_invocation: Token::Newline | Token::Semicolon branch).
        // Also: multiple top-level statements separated by ';'.
        let stmts = parse_source("let a = 1; let b = 2; let c = a + b");
        assert_eq!(stmts.len(), 3);
        assert!(matches!(&stmts[0], Stmt::Let(n, _, _, _) if n == "a"));
        assert!(matches!(&stmts[1], Stmt::Let(n, _, _, _) if n == "b"));
        assert!(matches!(&stmts[2], Stmt::Let(n, _, _, _) if n == "c"));
    }

    // ── G8: multiline binop continuation ────────────────────────
    // Operator at end of line must allow the RHS to appear on the next line.
    #[test]
    fn test_g8_multiline_binop_plus() {
        let stmts = parse_source("let s = \"a\" +\n  \"b\"");
        if let Stmt::Let(n, _, Some(rhs), _) = &stmts[0] {
            assert_eq!(n, "s");
            assert!(matches!(rhs, Expr::Binary(_, BinOp::Add, _)),
                "expected Binary(Add), got {:?}", rhs);
        } else {
            panic!("expected Let, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_g8_multiline_binop_chain() {
        // Chain of binops across multiple lines must collapse into a single expression.
        let stmts = parse_source("let x = 1 +\n  2 +\n  3");
        assert_eq!(stmts.len(), 1);
        if let Stmt::Let(n, _, Some(rhs), _) = &stmts[0] {
            assert_eq!(n, "x");
            assert!(matches!(rhs, Expr::Binary(_, BinOp::Add, _)));
        } else {
            panic!("expected Let, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_g8_multiline_logical_and() {
        let stmts = parse_source("let b = a &&\n  c");
        assert_eq!(stmts.len(), 1);
    }

    // ── G10: tuple field access via .0 / .1 ─────────────────────
    // `tup.0` must parse as Expr::Index(tup, IntLit(0)).
    #[test]
    fn test_g10_tuple_field_access_zero() {
        let stmts = parse_source("let t = (1, 2, 3)\nt.0");
        // Second stmt must be an Index expression with an IntLit index.
        if let Stmt::Expr(Expr::Index(_, idx)) = &stmts[1] {
            assert!(matches!(idx.as_ref(), Expr::IntLit(0)),
                "expected IntLit(0), got {:?}", idx);
        } else {
            panic!("expected Index expression, got {:?}", &stmts[1]);
        }
    }

    #[test]
    fn test_g10_tuple_field_access_higher() {
        let stmts = parse_source("t.7");
        if let Stmt::Expr(Expr::Index(_, idx)) = &stmts[0] {
            assert!(matches!(idx.as_ref(), Expr::IntLit(7)));
        } else {
            panic!("expected Index expression, got {:?}", &stmts[0]);
        }
    }

    // ── G11: paren/bracket newline suppression ──────────────────
    // Newlines inside () or [] must not terminate expressions.
    #[test]
    fn test_g11_paren_newline_suppressed() {
        let stmts = parse_source("let x = (\n  1 + 2\n)");
        assert_eq!(stmts.len(), 1);
        if let Stmt::Let(n, _, Some(rhs), _) = &stmts[0] {
            assert_eq!(n, "x");
            assert!(matches!(rhs, Expr::Binary(_, BinOp::Add, _)));
        } else {
            panic!("expected Let");
        }
    }

    #[test]
    fn test_g11_bracket_newline_suppressed() {
        // Array literal across multiple lines must parse as a single Array expr.
        let stmts = parse_source("let xs = [\n  1,\n  2,\n  3\n]");
        assert_eq!(stmts.len(), 1);
        if let Stmt::Let(n, _, Some(rhs), _) = &stmts[0] {
            assert_eq!(n, "xs");
            assert!(matches!(rhs, Expr::Array(v) if v.len() == 3),
                "expected 3-element Array, got {:?}", rhs);
        } else {
            panic!("expected Let");
        }
    }

    // ── G12: multiline function call arguments ──────────────────
    #[test]
    fn test_g12_multiline_call_args() {
        let stmts = parse_source("f(\n  1,\n  2,\n  3\n)");
        if let Stmt::Expr(Expr::Call(_, args)) = &stmts[0] {
            assert_eq!(args.len(), 3);
        } else {
            panic!("expected Call expression, got {:?}", &stmts[0]);
        }
    }

    #[test]
    fn test_g12_multiline_call_nested() {
        // Multiline args containing a nested expression.
        let stmts = parse_source("f(\n  1 + 2,\n  g(3)\n)");
        if let Stmt::Expr(Expr::Call(_, args)) = &stmts[0] {
            assert_eq!(args.len(), 2);
            assert!(matches!(&args[0], Expr::Binary(_, BinOp::Add, _)));
            assert!(matches!(&args[1], Expr::Call(..)));
        } else {
            panic!("expected Call expression, got {:?}", &stmts[0]);
        }
    }

}
