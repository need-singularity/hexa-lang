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
}

impl Parser {
    pub fn new(tokens: Vec<Spanned>) -> Self {
        Parser { tokens, pos: 0 }
    }

    /// Construct from plain tokens (no span info). Convenience for tests.
    pub fn from_plain(tokens: Vec<Token>) -> Self {
        let spanned = tokens.into_iter()
            .map(|t| Spanned::new(t, 0, 0))
            .collect();
        Parser { tokens: spanned, pos: 0 }
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
        while matches!(self.peek(), Token::Newline) {
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
            Token::Ident(name) => Ok(name),
            other => Err(self.error_at(span, format!("expected identifier, got {:?}", other))),
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
            Token::Pure => self.parse_pure_fn(vis),
            Token::Ident(ref s) if s == "consciousness" && !matches!(self.peek_ahead(1), Token::ColonColon | Token::Dot | Token::Eq | Token::LParen) => {
                self.advance(); // consume 'consciousness'
                let name = match self.peek().clone() {
                    Token::StringLit(s) => { self.advance(); s }
                    Token::Ident(_) if !matches!(self.peek_ahead(1), Token::ColonColon) => self.expect_ident()?,
                    Token::LBrace => "default".to_string(),
                    _ => return Err(self.error(format!(
                        "expected name after 'consciousness', got {:?}", self.peek()
                    ))),
                };
                let body = self.parse_block()?;
                Ok(Stmt::ConsciousnessBlock(name, body))
            }
            Token::Ident(ref s) if s == "@evolve" => {
                self.advance(); // consume '@evolve'
                self.expect(&Token::Fn)?;
                let name = self.expect_ident()?;
                let type_params = if matches!(self.peek(), Token::Lt) {
                    self.parse_type_params()?
                } else { vec![] };
                self.expect(&Token::LParen)?;
                let params = self.parse_params()?;
                self.expect(&Token::RParen)?;
                let ret_type = if matches!(self.peek(), Token::Arrow) {
                    self.advance(); Some(self.expect_ident()?)
                } else { None };
                let where_clauses = if matches!(self.peek(), Token::Where) {
                    self.parse_where_clauses()?
                } else { vec![] };
                let body = self.parse_block()?;
                Ok(Stmt::EvolveFn(FnDecl {
                    name, type_params, params, ret_type, where_clauses, body, vis, is_pure: false,
                }))
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
        let name = self.expect_ident()?;
        // optional type annotation
        let typ = if matches!(self.peek(), Token::Colon) {
            self.advance();
            Some(self.expect_ident()?)
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
        // optional type annotation
        let typ = if matches!(self.peek(), Token::Colon) {
            self.advance();
            Some(self.expect_ident()?)
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
        // optional type annotation
        let typ = if matches!(self.peek(), Token::Colon) {
            self.advance();
            Some(self.expect_ident()?)
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
        if matches!(self.peek(), Token::Newline | Token::Eof | Token::RBrace) {
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
            let is_formal = matches!(self.peek(),
                Token::Assert | Token::Invariant)
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
            Some(self.expect_ident()?)
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
            Token::StringLit(s) => { self.advance(); s }
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
            Token::StringLit(s) => { self.advance(); s }
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
        match self.peek() {
            Token::Catch | Token::Recover => { self.advance(); }
            _ => return Err(self.error(format!("expected 'catch' or 'recover' after try block, got {:?}", self.peek()))),
        }
        let err_name = self.expect_ident()?;
        let catch_block = self.parse_block()?;
        Ok(Stmt::TryCatch(try_block, err_name, catch_block))
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
            return Ok(Stmt::SpawnNamed(name, body));
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
                Some(self.expect_ident()?)
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
                name, type_params, params, ret_type, where_clauses, body,
                vis: Visibility::Private, is_pure: false,
            }));
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
            Some(self.expect_ident()?)
        } else {
            None
        };
        let body = self.parse_block()?;
        Ok(Stmt::AsyncFnDecl(FnDecl { name, type_params, params, ret_type, where_clauses: vec![], body, vis, is_pure: false }))
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
                if s == "timeout" {
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
                matches!(self.peek_ahead(1), Token::Ident(ref s) if s == "from")
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
                Some(self.expect_ident()?)
            } else {
                None
            };
            self.expect(&Token::LBrace)?;
            self.skip_newlines();
            // Body must be a single string literal (the description)
            let description = match self.peek().clone() {
                Token::StringLit(s) => { self.advance(); s }
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
                Token::StringLit(s) => { self.advance(); s }
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
            Some(self.expect_ident()?)
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
            body,
            vis: Visibility::Private,
            is_pure: false,
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
            if s == "$" {
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
            if s == "$" {
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
            Token::Ident(ref s) => MacroLitToken::Ident(s.clone()),
            Token::IntLit(n) => MacroLitToken::IntLit(n),
            Token::FloatLit(n) => MacroLitToken::FloatLit(n),
            Token::StringLit(ref s) => MacroLitToken::StringLit(s.clone()),
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
            if matches!(self.peek(), Token::Newline) {
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
        self.advance(); // consume 'use'
        let mut path = vec![self.expect_ident()?];
        while matches!(self.peek(), Token::ColonColon) {
            self.advance(); // consume ::
            path.push(self.expect_ident()?);
        }
        Ok(Stmt::Use(path))
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
            Some(self.expect_ident()?)
        } else {
            None
        };
        // Parse optional where clause: where T: Display, U: Clone
        let where_clauses = if matches!(self.peek(), Token::Where) {
            self.parse_where_clauses()?
        } else {
            vec![]
        };
        let body = self.parse_block()?;
        Ok(Stmt::FnDecl(FnDecl { name, type_params, params, ret_type, where_clauses, body, vis, is_pure: false }))
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

    fn parse_params(&mut self) -> Result<Vec<Param>, HexaError> {
        let mut params = Vec::new();
        if matches!(self.peek(), Token::RParen) {
            return Ok(params);
        }
        loop {
            let name = self.expect_ident()?;
            let typ = if matches!(self.peek(), Token::Colon) {
                self.advance();
                Some(self.expect_ident()?)
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
            let field_type = self.expect_ident()?;
            fields.push((field_name, field_type, field_vis));
            // consume comma or newline
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            }
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::StructDecl(StructDecl { name, type_params, fields, vis }))
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
                Some(self.expect_ident()?)
            } else {
                None
            };
            // trait methods can have body or not
            let m_body = if matches!(self.peek(), Token::LBrace) {
                self.parse_block()?
            } else {
                vec![]
            };
            methods.push(FnDecl { name: m_name, type_params: vec![], params: m_params, ret_type: m_ret, where_clauses: vec![], body: m_body, vis: m_vis, is_pure: false });
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
                Some(self.expect_ident()?)
            } else {
                None
            };
            let m_body = self.parse_block()?;
            methods.push(FnDecl { name: m_name, type_params: vec![], params: m_params, ret_type: m_ret, where_clauses: vec![], body: m_body, vis: m_vis, is_pure: false });
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
        let iter_expr = self.parse_expr()?;
        let body = self.parse_block()?;
        Ok(Stmt::For(var, iter_expr, body))
    }

    fn parse_while_stmt(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'while'
        let cond = self.parse_expr()?;
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
        let cond = self.parse_expr()?;
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
        let scrutinee = self.parse_expr()?;
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut arms = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            let pattern = self.parse_match_pattern()?;
            // Optional guard: `if cond`
            let guard = if matches!(self.peek(), Token::If) {
                self.advance(); // consume 'if'
                Some(self.parse_expr()?)
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
        self.expect(&Token::RBrace)?;
        Ok(Expr::Match(Box::new(scrutinee), arms))
    }

    /// Parse a match pattern. Handles `_`, `EnumName::Variant(binding)`, and regular expressions.
    fn parse_match_pattern(&mut self) -> Result<Expr, HexaError> {
        // Check for wildcard `_`
        if let Token::Ident(ref name) = self.peek().clone() {
            if name == "_" {
                self.advance();
                return Ok(Expr::Wildcard);
            }
            // Check for EnumPath: `Ident :: Ident` or `Ident :: Ident ( expr )`
            if matches!(self.peek_ahead(1), Token::ColonColon) {
                let enum_name = name.clone();
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
            let right = self.parse_addition()?;
            left = Expr::Binary(Box::new(left), op, Box::new(right));
        }
        Ok(left)
    }

    // Level 5.4: + -
    fn parse_addition(&mut self) -> Result<Expr, HexaError> {
        let mut left = self.parse_multiplication()?;
        loop {
            let op = match self.peek() {
                Token::Plus => BinOp::Add,
                Token::Minus => BinOp::Sub,
                _ => break,
            };
            self.advance();
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
                    let args = self.parse_args()?;
                    self.expect(&Token::RParen)?;
                    expr = Expr::Call(Box::new(expr), args);
                }
                Token::Dot => {
                    self.advance();
                    let field = self.expect_ident()?;
                    expr = Expr::Field(Box::new(expr), field);
                }
                Token::LBracket => {
                    self.advance();
                    let index = self.parse_expr()?;
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
        if matches!(self.peek(), Token::RParen) {
            return Ok(args);
        }
        loop {
            args.push(self.parse_expr()?);
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            } else {
                break;
            }
        }
        Ok(args)
    }

    // ── Level 6: Primary ────────────────────────────────────

    fn parse_primary(&mut self) -> Result<Expr, HexaError> {
        match self.peek().clone() {
            Token::IntLit(n) => { self.advance(); Ok(Expr::IntLit(n)) }
            Token::FloatLit(n) => { self.advance(); Ok(Expr::FloatLit(n)) }
            Token::BoolLit(b) => { self.advance(); Ok(Expr::BoolLit(b)) }
            Token::StringLit(s) => { self.advance(); Ok(Expr::StringLit(s)) }
            Token::CharLit(c) => { self.advance(); Ok(Expr::CharLit(c)) }
            Token::Ident(name) => {
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
                else if matches!(self.peek(), Token::LBrace) && name.chars().next().map_or(false, |c| c.is_uppercase()) {
                    self.advance(); // consume {
                    self.skip_newlines();
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
                    self.expect(&Token::RBrace)?;
                    Ok(Expr::StructInit(name, fields))
                } else {
                    Ok(Expr::Ident(name))
                }
            }
            Token::LParen => {
                self.advance();
                let expr = self.parse_expr()?;
                // Tuple or grouped
                if matches!(self.peek(), Token::Comma) {
                    let mut items = vec![expr];
                    while matches!(self.peek(), Token::Comma) {
                        self.advance();
                        if matches!(self.peek(), Token::RParen) {
                            break;
                        }
                        items.push(self.parse_expr()?);
                    }
                    self.expect(&Token::RParen)?;
                    Ok(Expr::Tuple(items))
                } else {
                    self.expect(&Token::RParen)?;
                    Ok(expr) // grouped expression
                }
            }
            Token::LBracket => {
                self.advance();
                let mut items = Vec::new();
                while !matches!(self.peek(), Token::RBracket | Token::Eof) {
                    items.push(self.parse_expr()?);
                    if matches!(self.peek(), Token::Comma) {
                        self.advance();
                    }
                }
                self.expect(&Token::RBracket)?;
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
            // Channel keyword used as expression resolves to the builtin function
            Token::Channel => {
                self.advance();
                Ok(Expr::Ident("channel".into()))
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
            other => Err(self.error(format!("unexpected token in expression: {:?}", other))),
        }
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
                // Accept both identifier and () for void return
                if matches!(self.peek(), Token::LParen) {
                    self.advance();
                    self.expect(&Token::RParen)?;
                    None // () means void
                } else {
                    Some(self.expect_ident()?)
                }
            } else {
                None
            };
            operations.push(EffectOp { name: op_name, params, return_type: ret_type });
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::EffectDecl(EffectDecl { name, type_params, operations }))
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
            Some(self.expect_ident()?)
        } else {
            None
        };
        let where_clauses = if matches!(self.peek(), Token::Where) {
            self.parse_where_clauses()?
        } else {
            vec![]
        };
        let body = self.parse_block()?;
        Ok(Stmt::FnDecl(FnDecl { name, type_params, params, ret_type, where_clauses, body, vis, is_pure: true }))
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

}
