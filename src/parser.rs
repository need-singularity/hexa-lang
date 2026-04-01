#![allow(dead_code)]

use crate::token::Token;
use crate::ast::*;
use crate::error::{HexaError, ErrorClass};

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Parser { tokens, pos: 0 }
    }

    // ── Helpers ──────────────────────────────────────────────

    fn peek(&self) -> &Token {
        self.tokens.get(self.pos).unwrap_or(&Token::Eof)
    }

    fn peek_ahead(&self, offset: usize) -> &Token {
        self.tokens.get(self.pos + offset).unwrap_or(&Token::Eof)
    }

    fn at_end(&self) -> bool {
        matches!(self.peek(), Token::Eof)
    }

    fn advance(&mut self) -> Token {
        let tok = self.tokens.get(self.pos).cloned().unwrap_or(Token::Eof);
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
            Err(self.error(format!("expected {:?}, got {:?}", expected, self.peek())))
        }
    }

    fn skip_newlines(&mut self) {
        while matches!(self.peek(), Token::Newline) {
            self.advance();
        }
    }

    fn error(&self, message: String) -> HexaError {
        HexaError {
            class: ErrorClass::Syntax,
            message,
            line: 0,
            col: self.pos,
        }
    }

    fn expect_ident(&mut self) -> Result<String, HexaError> {
        match self.advance() {
            Token::Ident(name) => Ok(name),
            other => Err(self.error(format!("expected identifier, got {:?}", other))),
        }
    }

    // ── Level 1: Program ────────────────────────────────────

    pub fn parse(&mut self) -> Result<Vec<Stmt>, HexaError> {
        let mut stmts = Vec::new();
        self.skip_newlines();
        while !self.at_end() {
            stmts.push(self.parse_stmt()?);
            self.skip_newlines();
        }
        Ok(stmts)
    }

    // ── Level 2: Statement ──────────────────────────────────

    fn parse_stmt(&mut self) -> Result<Stmt, HexaError> {
        self.skip_newlines();
        // Check for visibility prefix
        let vis = self.parse_visibility();

        match self.peek().clone() {
            Token::Let => self.parse_let(),
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

    fn parse_let(&mut self) -> Result<Stmt, HexaError> {
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
        Ok(Stmt::Let(name, typ, Some(expr)))
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
        let name = self.expect_ident()?;
        self.expect(&Token::LBrace)?;
        self.skip_newlines();
        let mut strings = Vec::new();
        while !matches!(self.peek(), Token::RBrace | Token::Eof) {
            match self.advance() {
                Token::StringLit(s) => strings.push(s),
                Token::Newline | Token::Comma => {}
                other => return Err(self.error(format!("expected string in intent, got {:?}", other))),
            }
        }
        self.expect(&Token::RBrace)?;
        Ok(Stmt::Intent(name, strings))
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
            let pattern = self.parse_expr()?;
            self.expect(&Token::Arrow)?;
            let body = if matches!(self.peek(), Token::LBrace) {
                self.parse_block()?
            } else {
                let expr = self.parse_expr()?;
                vec![Stmt::Expr(expr)]
            };
            arms.push(MatchArm { pattern, body });
            if matches!(self.peek(), Token::Comma) {
                self.advance();
            }
            self.skip_newlines();
        }
        self.expect(&Token::RBrace)?;
        Ok(Expr::Match(Box::new(scrutinee), arms))
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
                _ => break,
            }
        }
        Ok(expr)
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
                Ok(Expr::Ident(name))
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
