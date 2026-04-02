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
            Token::Fn => self.parse_fn_decl(vis),
            Token::Struct => self.parse_struct_decl(vis),
            Token::Enum => self.parse_enum_decl(vis),
            Token::Trait => self.parse_trait_decl(vis),
            Token::Impl => self.parse_impl_block(),
            Token::For => self.parse_for_stmt(),
            Token::While => self.parse_while_stmt(),
            Token::Loop => self.parse_loop_stmt(),
            Token::Return => self.parse_return(),
            Token::Proof => self.parse_proof(),
            Token::Assert => self.parse_assert(),
            Token::Intent => self.parse_intent(),
            Token::Verify => self.parse_verify(),
            Token::Mod => self.parse_mod_decl(),
            Token::Use => self.parse_use_decl(),
            Token::Try => self.parse_try_catch(),
            Token::Throw => self.parse_throw(),
            Token::Spawn => self.parse_spawn(),
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

    fn parse_return(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'return'
        if matches!(self.peek(), Token::Newline | Token::Eof | Token::RBrace) {
            Ok(Stmt::Return(None))
        } else {
            let expr = self.parse_expr()?;
            Ok(Stmt::Return(Some(expr)))
        }
    }

    fn parse_proof(&mut self) -> Result<Stmt, HexaError> {
        self.advance(); // consume 'proof'
        let name = self.expect_ident()?;
        let body = self.parse_block()?;
        Ok(Stmt::Proof(name, body))
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
        let body = self.parse_block()?;
        Ok(Stmt::Spawn(body))
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
        Ok(Stmt::FnDecl(FnDecl { name, params, ret_type, body, vis }))
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
        Ok(Stmt::StructDecl(StructDecl { name, fields, vis }))
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
            methods.push(FnDecl { name: m_name, params: m_params, ret_type: m_ret, body: m_body, vis: m_vis });
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
            methods.push(FnDecl { name: m_name, params: m_params, ret_type: m_ret, body: m_body, vis: m_vis });
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
        let else_block = if matches!(self.peek(), Token::Else) {
            self.advance();
            Some(self.parse_block()?)
        } else {
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
            Token::If => self.parse_if_expr(),
            Token::Match => self.parse_match_expr(),
            Token::BitOr => self.parse_lambda(),
            // Channel keyword used as expression resolves to the builtin function
            Token::Channel => {
                self.advance();
                Ok(Expr::Ident("channel".into()))
            }
            other => Err(self.error(format!("unexpected token in expression: {:?}", other))),
        }
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
}
