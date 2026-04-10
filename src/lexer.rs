// ⛔ CORE — L0 불변식 (렉서 토큰화. 수정 전 유저 승인 필수)
use crate::token::{Token, Spanned, keyword_from_str};

/// Byte-based lexer: operates on &[u8] directly instead of Vec<char>.
/// ASCII-only hot paths (operators, digits, whitespace) avoid char conversion.
/// ~4x less memory, better L1d cache utilization.
pub struct Lexer {
    source: Vec<u8>,
    pos: usize,
    line: usize,
    col: usize,
}

impl Lexer {
    pub fn new(source: &str) -> Self {
        Lexer {
            source: source.as_bytes().to_vec(),
            pos: 0,
            line: 1,
            col: 1,
        }
    }

    pub fn tokenize(&mut self) -> Result<Vec<Spanned>, String> {
        let mut tokens: Vec<Spanned> = Vec::new();
        // Paren/bracket depth: suppress Newline tokens while depth > 0
        // (not brace — blocks still need statement separators).
        // Enables multi-line fn params, multi-line call args, multi-line collection
        // literals, and leading-operator line continuation inside (...) / [...].
        let mut paren_depth: i32 = 0;
        loop {
            let (tok, line, col) = self.next_token_spanned()?;
            let is_eof = tok == Token::Eof;
            match &tok {
                Token::LParen | Token::LBracket => paren_depth += 1,
                Token::RParen | Token::RBracket => {
                    if paren_depth > 0 { paren_depth -= 1; }
                }
                Token::Newline if paren_depth > 0 => {
                    // Drop newline inside grouping — treat as whitespace.
                    continue;
                }
                _ => {}
            }
            tokens.push(Spanned::new(tok, line, col));
            if is_eof {
                break;
            }
        }
        Ok(tokens)
    }

    /// Tokenize into plain tokens (no span info). Used by existing tests.
    pub fn tokenize_plain(&mut self) -> Result<Vec<Token>, String> {
        self.tokenize().map(|v| v.into_iter().map(|s| s.token).collect())
    }

    #[inline(always)]
    fn peek_byte(&self) -> Option<u8> {
        self.source.get(self.pos).copied()
    }

    #[inline(always)]
    fn peek_byte_ahead(&self, offset: usize) -> Option<u8> {
        self.source.get(self.pos + offset).copied()
    }

    /// Peek as char (for non-ASCII compatibility).
    fn peek(&self) -> Option<char> {
        if self.pos >= self.source.len() { return None; }
        let b = self.source[self.pos];
        if b < 128 {
            Some(b as char)
        } else {
            // Multi-byte UTF-8: decode from current position
            let rest = &self.source[self.pos..];
            std::str::from_utf8(rest).ok().and_then(|s| s.chars().next())
        }
    }

    fn peek_ahead(&self, offset: usize) -> Option<char> {
        // For ASCII fast path (used only in number/operator lookahead)
        if let Some(b) = self.source.get(self.pos + offset) {
            if *b < 128 { return Some(*b as char); }
        }
        None
    }

    #[inline(always)]
    fn advance_byte(&mut self) -> Option<u8> {
        if self.pos >= self.source.len() { return None; }
        let b = self.source[self.pos];
        self.pos += 1;
        if b == b'\n' {
            self.line += 1;
            self.col = 1;
        } else {
            self.col += 1;
        }
        Some(b)
    }

    /// Advance one logical character (handles multi-byte UTF-8).
    fn advance(&mut self) -> Option<char> {
        if self.pos >= self.source.len() { return None; }
        let b = self.source[self.pos];
        if b < 128 {
            self.pos += 1;
            if b == b'\n' {
                self.line += 1;
                self.col = 1;
            } else {
                self.col += 1;
            }
            Some(b as char)
        } else {
            let rest = &self.source[self.pos..];
            if let Ok(s) = std::str::from_utf8(rest) {
                if let Some(c) = s.chars().next() {
                    self.pos += c.len_utf8();
                    self.col += 1;
                    return Some(c);
                }
            }
            // Invalid UTF-8: skip byte
            self.pos += 1;
            self.col += 1;
            None
        }
    }

    #[inline(always)]
    fn skip_whitespace(&mut self) {
        while self.pos < self.source.len() {
            let b = self.source[self.pos];
            if b == b' ' || b == b'\t' || b == b'\r' {
                self.pos += 1;
                self.col += 1;
            } else {
                break;
            }
        }
    }

    fn skip_line_comment(&mut self) {
        while self.pos < self.source.len() {
            if self.source[self.pos] == b'\n' {
                break;
            }
            self.pos += 1;
            self.col += 1;
        }
    }

    /// Read an identifier as bytes, convert to String at the end (one allocation).
    fn read_ident(&mut self) -> String {
        let start = self.pos;
        while self.pos < self.source.len() {
            let b = self.source[self.pos];
            if b.is_ascii_alphanumeric() || b == b'_' {
                self.pos += 1;
                self.col += 1;
            } else if b >= 128 {
                // Multi-byte UTF-8 char in identifier
                let rest = &self.source[self.pos..];
                if let Ok(s) = std::str::from_utf8(rest) {
                    if let Some(c) = s.chars().next() {
                        if c.is_alphanumeric() || c == '_' {
                            self.pos += c.len_utf8();
                            self.col += 1;
                            continue;
                        }
                    }
                }
                break;
            } else {
                break;
            }
        }
        // SAFETY: source was valid UTF-8 originally, and we only advanced by valid char boundaries
        String::from_utf8(self.source[start..self.pos].to_vec()).unwrap_or_default()
    }

    fn read_number(&mut self) -> Token {
        let mut s = String::new();
        let mut is_float = false;

        while self.pos < self.source.len() {
            let b = self.source[self.pos];
            if b.is_ascii_digit() {
                s.push(b as char);
                self.pos += 1;
                self.col += 1;
            } else if b == b'.' && !is_float {
                // Check next char: if digit, it's a float; if '.', it's range operator
                if let Some(&next) = self.source.get(self.pos + 1) {
                    if next.is_ascii_digit() {
                        is_float = true;
                        s.push('.');
                        self.pos += 1;
                        self.col += 1;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else if b == b'_' {
                // Allow underscores in numbers (visual separator)
                self.pos += 1;
                self.col += 1;
            } else if (b == b'e' || b == b'E') && !s.is_empty() {
                // Scientific notation: 1e10, 1.5e-3, 2E+8
                is_float = true;
                s.push(b as char);
                self.pos += 1;
                self.col += 1;
                // optional +/- after e
                if let Some(&sign) = self.source.get(self.pos) {
                    if sign == b'+' || sign == b'-' {
                        s.push(sign as char);
                        self.pos += 1;
                        self.col += 1;
                    }
                }
                // consume exponent digits
                while self.pos < self.source.len() && self.source[self.pos].is_ascii_digit() {
                    s.push(self.source[self.pos] as char);
                    self.pos += 1;
                    self.col += 1;
                }
                break;
            } else {
                break;
            }
        }

        if is_float {
            Token::FloatLit(s.parse::<f64>().unwrap())
        } else {
            // Try i64 first; on overflow fall back to f64 representation
            // so the value survives lexing. At runtime, arithmetic on large
            // values promotes to BigInt via checked_* ops.
            match s.parse::<i64>() {
                Ok(n) => Token::IntLit(n),
                Err(_) => Token::FloatLit(s.parse::<f64>().unwrap_or(f64::INFINITY)),
            }
        }
    }

    fn read_string(&mut self) -> Result<String, String> {
        // Skip opening quote
        self.advance_byte();
        let mut s = String::new();
        loop {
            match self.advance() {
                Some('"') => return Ok(s),
                Some('\\') => {
                    match self.advance() {
                        Some('n') => s.push('\n'),
                        Some('t') => s.push('\t'),
                        Some('r') => s.push('\r'),
                        Some('\\') => s.push('\\'),
                        Some('"') => s.push('"'),
                        Some('0') => s.push('\0'),
                        Some('x') => {
                            let h1 = self.advance().ok_or_else(|| format!("line {}:{}: incomplete \\x escape", self.line, self.col))?;
                            let h2 = self.advance().ok_or_else(|| format!("line {}:{}: incomplete \\x escape", self.line, self.col))?;
                            let hex_str = format!("{}{}", h1, h2);
                            let code = u8::from_str_radix(&hex_str, 16).map_err(|_| format!("line {}:{}: invalid hex escape \\x{}", self.line, self.col, hex_str))?;
                            s.push(code as char);
                        }
                        Some(c) => return Err(format!("line {}:{}: unknown escape \\{}", self.line, self.col, c)),
                        None => return Err(format!("line {}:{}: unterminated string escape", self.line, self.col)),
                    }
                }
                Some(c) => s.push(c),
                None => return Err(format!("line {}:{}: unterminated string literal", self.line, self.col)),
            }
        }
    }

    fn read_triple_string(&mut self) -> Result<String, String> {
        // Skip opening """
        self.advance_byte(); // "
        self.advance_byte(); // "
        self.advance_byte(); // "
        // Skip optional leading newline
        if self.peek_byte() == Some(b'\n') {
            self.advance_byte();
        } else if self.peek_byte() == Some(b'\r') && self.peek_byte_ahead(1) == Some(b'\n') {
            self.advance_byte();
            self.advance_byte();
        }
        let mut s = String::new();
        loop {
            if self.peek_byte() == Some(b'"')
                && self.peek_byte_ahead(1) == Some(b'"')
                && self.peek_byte_ahead(2) == Some(b'"')
            {
                self.advance_byte(); // "
                self.advance_byte(); // "
                self.advance_byte(); // "
                return Ok(s);
            }
            match self.advance() {
                Some(c) => s.push(c),
                None => return Err(format!("line {}:{}: unterminated triple-quoted string", self.line, self.col)),
            }
        }
    }

    fn read_char_lit(&mut self) -> Result<char, String> {
        // Skip opening quote
        self.advance_byte();
        let ch = match self.advance() {
            Some('\\') => {
                match self.advance() {
                    Some('n') => '\n',
                    Some('t') => '\t',
                    Some('r') => '\r',
                    Some('\\') => '\\',
                    Some('\'') => '\'',
                    Some('0') => '\0',
                    Some('x') => {
                        let h1 = self.advance().ok_or_else(|| format!("line {}:{}: incomplete \\x escape", self.line, self.col))?;
                        let h2 = self.advance().ok_or_else(|| format!("line {}:{}: incomplete \\x escape", self.line, self.col))?;
                        let hex_str = format!("{}{}", h1, h2);
                        u8::from_str_radix(&hex_str, 16).map_err(|_| format!("line {}:{}: invalid hex escape \\x{}", self.line, self.col, hex_str))? as char
                    }
                    Some(c) => return Err(format!("line {}:{}: unknown char escape \\{}", self.line, self.col, c)),
                    None => return Err(format!("line {}:{}: unterminated char literal", self.line, self.col)),
                }
            }
            Some(c) => c,
            None => return Err(format!("line {}:{}: unterminated char literal", self.line, self.col)),
        };
        match self.advance() {
            Some('\'') => Ok(ch),
            _ => Err(format!("line {}:{}: unterminated char literal", self.line, self.col)),
        }
    }

    fn next_token_spanned(&mut self) -> Result<(Token, usize, usize), String> {
        self.skip_whitespace();
        let line = self.line;
        let col = self.col;
        let tok = self.next_token_inner()?;
        Ok((tok, line, col))
    }

    fn next_token(&mut self) -> Result<Token, String> {
        self.skip_whitespace();
        self.next_token_inner()
    }

    fn next_token_inner(&mut self) -> Result<Token, String> {

        let b = match self.peek_byte() {
            Some(b) => b,
            None => return Ok(Token::Eof),
        };

        // Comments (ASCII fast path)
        if b == b'/' {
            if self.peek_byte_ahead(1) == Some(b'/') {
                self.skip_line_comment();
                return self.next_token();
            }
            if self.peek_byte_ahead(1) == Some(b'*') {
                // Block comment (supports nesting)
                self.pos += 2;
                self.col += 2;
                let mut depth: u32 = 1;
                while depth > 0 {
                    match self.peek_byte() {
                        None => return Err("unterminated block comment".into()),
                        Some(b'/') if self.peek_byte_ahead(1) == Some(b'*') => {
                            depth += 1;
                            self.pos += 2;
                            self.col += 2;
                        }
                        Some(b'*') if self.peek_byte_ahead(1) == Some(b'/') => {
                            depth -= 1;
                            self.pos += 2;
                            self.col += 2;
                        }
                        Some(b'\n') => {
                            self.pos += 1;
                            self.line += 1;
                            self.col = 1;
                        }
                        Some(_) => {
                            self.advance();
                        }
                    }
                }
                return self.next_token();
            }
        }

        // Newline
        if b == b'\n' {
            self.pos += 1;
            self.line += 1;
            self.col = 1;
            return Ok(Token::Newline);
        }

        // Dollar sign
        if b == b'$' {
            self.pos += 1;
            self.col += 1;
            return Ok(Token::Ident("$".into()));
        }

        // Hash-brace: #{ for map literals
        if b == b'#' {
            if self.peek_byte_ahead(1) == Some(b'{') {
                self.pos += 2;
                self.col += 2;
                return Ok(Token::HashLBrace);
            }
        }

        // At-sign → first-class Attribute token (AI-native)
        if b == b'@' {
            self.pos += 1;
            self.col += 1;
            if self.peek_byte().map_or(false, |b| b.is_ascii_alphabetic() || b == b'_' || b >= 128) {
                let ident = self.read_ident();
                return Ok(Token::Attribute(ident.into()));
            }
            return Ok(Token::Attribute("".into()));
        }

        // Identifiers and keywords (ASCII fast path)
        if b.is_ascii_alphabetic() || b == b'_' {
            let ident = self.read_ident();
            if let Some(kw) = keyword_from_str(&ident) {
                return Ok(kw);
            }
            return Ok(Token::Ident(ident.into()));
        }
        // Non-ASCII identifier start
        if b >= 128 {
            if let Some(c) = self.peek() {
                if c.is_alphabetic() {
                    let ident = self.read_ident();
                    if let Some(kw) = keyword_from_str(&ident) {
                        return Ok(kw);
                    }
                    return Ok(Token::Ident(ident.into()));
                }
            }
        }

        // Numbers
        if b.is_ascii_digit() {
            return Ok(self.read_number());
        }

        // String literal (triple-quote """ for multiline, or regular "")
        if b == b'"' {
            if self.peek_byte_ahead(1) == Some(b'"') && self.peek_byte_ahead(2) == Some(b'"') {
                let s = self.read_triple_string()?;
                return Ok(Token::StringLit(s.into()));
            }
            let s = self.read_string()?;
            return Ok(Token::StringLit(s.into()));
        }

        // Char literal
        if b == b'\'' {
            let c = self.read_char_lit()?;
            return Ok(Token::CharLit(c));
        }

        // Operators and delimiters (pure byte matching — no char conversion)
        self.pos += 1;
        self.col += 1;
        match b {
            b'+' => Ok(Token::Plus),
            b'*' => {
                if self.peek_byte() == Some(b'*') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::Power)
                } else {
                    Ok(Token::Star)
                }
            }
            b'%' => Ok(Token::Percent),
            b'-' => {
                if self.peek_byte() == Some(b'>') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::Arrow)
                } else {
                    Ok(Token::Minus)
                }
            }
            b'/' => Ok(Token::Slash),
            b'=' => {
                if self.peek_byte() == Some(b'=') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::EqEq)
                } else if self.peek_byte() == Some(b'>') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::FatArrow)
                } else {
                    Ok(Token::Eq)
                }
            }
            b'!' => {
                if self.peek_byte() == Some(b'=') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::NotEq)
                } else {
                    Ok(Token::Not)
                }
            }
            b'<' => {
                if self.peek_byte() == Some(b'=') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::LtEq)
                } else {
                    Ok(Token::Lt)
                }
            }
            b'>' => {
                if self.peek_byte() == Some(b'=') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::GtEq)
                } else {
                    Ok(Token::Gt)
                }
            }
            b'&' => {
                if self.peek_byte() == Some(b'&') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::And)
                } else {
                    Ok(Token::BitAnd)
                }
            }
            b'|' => {
                if self.peek_byte() == Some(b'|') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::Or)
                } else {
                    Ok(Token::BitOr)
                }
            }
            b'^' => {
                if self.peek_byte() == Some(b'^') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::Xor)
                } else {
                    Ok(Token::BitXor)
                }
            }
            b'~' => Ok(Token::BitNot),
            b':' => {
                if self.peek_byte() == Some(b'=') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::ColonEq)
                } else if self.peek_byte() == Some(b':') {
                    self.pos += 1; self.col += 1;
                    Ok(Token::ColonColon)
                } else {
                    Ok(Token::Colon)
                }
            }
            b'.' => {
                if self.peek_byte() == Some(b'.') {
                    self.pos += 1; self.col += 1;
                    if self.peek_byte() == Some(b'=') {
                        self.pos += 1; self.col += 1;
                        Ok(Token::DotDotEq)
                    } else {
                        Ok(Token::DotDot)
                    }
                } else {
                    Ok(Token::Dot)
                }
            }
            b'(' => Ok(Token::LParen),
            b')' => Ok(Token::RParen),
            b'{' => Ok(Token::LBrace),
            b'}' => Ok(Token::RBrace),
            b'[' => Ok(Token::LBracket),
            b']' => Ok(Token::RBracket),
            b',' => Ok(Token::Comma),
            b';' => Ok(Token::Semicolon),
            _ => Err(format!("line {}:{}: unexpected character '{}'", self.line, self.col, b as char)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::token::Token;

    #[test]
    fn test_lex_let_binding() {
        let mut lexer = Lexer::new("let x = 42");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens, vec![
            Token::Let, Token::Ident("x".into()), Token::Eq, Token::IntLit(42), Token::Eof
        ]);
    }

    #[test]
    fn test_lex_fn_decl() {
        let mut lexer = Lexer::new("fn main() {}");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens, vec![
            Token::Fn, Token::Ident("main".into()), Token::LParen, Token::RParen,
            Token::LBrace, Token::RBrace, Token::Eof
        ]);
    }

    #[test]
    fn test_lex_all_operators() {
        let mut lexer = Lexer::new("+ - * / % ** == != < > <= >= && || ! ^^ & | ^ ~ = := .. ->");
        let tokens = lexer.tokenize_plain().unwrap();
        // Should have 24 operator tokens + Eof = 25
        assert_eq!(tokens.len(), 25);
    }

    #[test]
    fn test_lex_string_literal() {
        let mut lexer = Lexer::new("\"hello world\"");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::StringLit("hello world".into()));
    }

    #[test]
    fn test_lex_egyptian_fraction() {
        let mut lexer = Lexer::new("1.0/2.0 + 1.0/3.0 + 1.0/6.0");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::FloatLit(1.0));
    }

    // ── G3: block comments (with whitespace after */) ──────────
    #[test]
    fn test_g3_block_comment_basic() {
        let mut lexer = Lexer::new("let x = /* skip me */ 5");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens, vec![
            Token::Let, Token::Ident("x".into()), Token::Eq, Token::IntLit(5), Token::Eof
        ]);
    }

    #[test]
    fn test_g3_block_comment_multiline() {
        let mut lexer = Lexer::new("let x = /* line1\nline2\nline3 */ 7");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens, vec![
            Token::Let, Token::Ident("x".into()), Token::Eq, Token::IntLit(7), Token::Eof
        ]);
    }

    #[test]
    fn test_g3_block_comment_nested() {
        let mut lexer = Lexer::new("let x = /* outer /* inner */ outer */ 9");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens, vec![
            Token::Let, Token::Ident("x".into()), Token::Eq, Token::IntLit(9), Token::Eof
        ]);
    }

    #[test]
    fn test_lex_comments_skipped() {
        let mut lexer = Lexer::new("let x = 1 // comment\nlet y = 2");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens, vec![
            Token::Let, Token::Ident("x".into()), Token::Eq, Token::IntLit(1),
            Token::Newline,
            Token::Let, Token::Ident("y".into()), Token::Eq, Token::IntLit(2),
            Token::Eof
        ]);
    }

    #[test]
    fn test_lex_string_escapes() {
        let mut lexer = Lexer::new("\"hello\\nworld\"");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::StringLit("hello\nworld".into()));
    }

    #[test]
    fn test_lex_char_literal() {
        let mut lexer = Lexer::new("'a'");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::CharLit('a'));
    }

    #[test]
    fn test_lex_booleans() {
        let mut lexer = Lexer::new("true false");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::BoolLit(true));
        assert_eq!(tokens[1], Token::BoolLit(false));
    }

    #[test]
    fn test_lex_at_evolve_token() {
        let mut lexer = Lexer::new("@evolve fn");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::Attribute("evolve".into()));
        assert_eq!(tokens[1], Token::Fn);
    }

    #[test]
    fn test_lex_ai_native_attrs() {
        let mut lexer = Lexer::new("@pure @inline @parallel @hot @cold @memoize @simd @bounded");
        let tokens = lexer.tokenize_plain().unwrap();
        let names = ["pure","inline","parallel","hot","cold","memoize","simd","bounded"];
        for (i, name) in names.iter().enumerate() {
            assert_eq!(tokens[i], Token::Attribute((*name).into()));
        }
    }

    #[test]
    fn test_lex_consciousness_as_ident() {
        let mut lexer = Lexer::new("consciousness \"test\"");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::Ident("consciousness".into()));
        assert_eq!(tokens[1], Token::StringLit("test".into()));
    }

    // ── G9: triple-quoted multiline string ─────────────────────
    #[test]
    fn test_g9_triple_quoted_single_line() {
        let mut lexer = Lexer::new("\"\"\"hello world\"\"\"");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::StringLit("hello world".into()));
    }

    #[test]
    fn test_g9_triple_quoted_multiline() {
        // Opening """ followed by newline → leading newline skipped.
        let mut lexer = Lexer::new("\"\"\"\nline1\nline2\n\"\"\"");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::StringLit("line1\nline2\n".into()));
    }

    #[test]
    fn test_g9_triple_quoted_preserves_quotes() {
        // Single " and "" inside a """..."""  block are preserved literally.
        let mut lexer = Lexer::new("\"\"\"a \"b\" c\"\"\"");
        let tokens = lexer.tokenize_plain().unwrap();
        assert_eq!(tokens[0], Token::StringLit("a \"b\" c".into()));
    }
}
