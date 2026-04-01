use crate::token::{Token, keyword_from_str};

pub struct Lexer {
    source: Vec<char>,
    pos: usize,
    line: usize,
    col: usize,
}

impl Lexer {
    pub fn new(source: &str) -> Self {
        Lexer {
            source: source.chars().collect(),
            pos: 0,
            line: 1,
            col: 1,
        }
    }

    pub fn tokenize(&mut self) -> Result<Vec<Token>, String> {
        let mut tokens = Vec::new();
        loop {
            let tok = self.next_token()?;
            let is_eof = tok == Token::Eof;
            tokens.push(tok);
            if is_eof {
                break;
            }
        }
        Ok(tokens)
    }

    fn peek(&self) -> Option<char> {
        self.source.get(self.pos).copied()
    }

    fn peek_ahead(&self, offset: usize) -> Option<char> {
        self.source.get(self.pos + offset).copied()
    }

    fn advance(&mut self) -> Option<char> {
        let ch = self.source.get(self.pos).copied();
        if let Some(c) = ch {
            self.pos += 1;
            if c == '\n' {
                self.line += 1;
                self.col = 1;
            } else {
                self.col += 1;
            }
        }
        ch
    }

    fn skip_whitespace(&mut self) {
        while let Some(c) = self.peek() {
            if c == ' ' || c == '\t' || c == '\r' {
                self.advance();
            } else {
                break;
            }
        }
    }

    fn skip_line_comment(&mut self) {
        while let Some(c) = self.peek() {
            if c == '\n' {
                break;
            }
            self.advance();
        }
    }

    fn read_ident(&mut self) -> String {
        let mut s = String::new();
        while let Some(c) = self.peek() {
            if c.is_alphanumeric() || c == '_' {
                s.push(c);
                self.advance();
            } else {
                break;
            }
        }
        s
    }

    fn read_number(&mut self) -> Token {
        let mut s = String::new();
        let mut is_float = false;

        while let Some(c) = self.peek() {
            if c.is_ascii_digit() {
                s.push(c);
                self.advance();
            } else if c == '.' && !is_float {
                // Check next char: if digit, it's a float; if '.', it's range operator
                if let Some(next) = self.peek_ahead(1) {
                    if next.is_ascii_digit() {
                        is_float = true;
                        s.push(c);
                        self.advance();
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else if c == '_' {
                // Allow underscores in numbers (visual separator)
                self.advance();
            } else {
                break;
            }
        }

        if is_float {
            Token::FloatLit(s.parse::<f64>().unwrap())
        } else {
            Token::IntLit(s.parse::<i64>().unwrap())
        }
    }

    fn read_string(&mut self) -> Result<String, String> {
        // Skip opening quote
        self.advance();
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
                        Some(c) => return Err(format!("line {}:{}: unknown escape \\{}", self.line, self.col, c)),
                        None => return Err(format!("line {}:{}: unterminated string escape", self.line, self.col)),
                    }
                }
                Some(c) => s.push(c),
                None => return Err(format!("line {}:{}: unterminated string literal", self.line, self.col)),
            }
        }
    }

    fn read_char_lit(&mut self) -> Result<char, String> {
        // Skip opening quote
        self.advance();
        let ch = match self.advance() {
            Some('\\') => {
                match self.advance() {
                    Some('n') => '\n',
                    Some('t') => '\t',
                    Some('r') => '\r',
                    Some('\\') => '\\',
                    Some('\'') => '\'',
                    Some('0') => '\0',
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

    fn next_token(&mut self) -> Result<Token, String> {
        self.skip_whitespace();

        let ch = match self.peek() {
            Some(c) => c,
            None => return Ok(Token::Eof),
        };

        // Comments
        if ch == '/' {
            if self.peek_ahead(1) == Some('/') {
                self.skip_line_comment();
                return self.next_token();
            }
        }

        // Newline
        if ch == '\n' {
            self.advance();
            return Ok(Token::Newline);
        }

        // Identifiers and keywords
        if ch.is_alphabetic() || ch == '_' {
            let ident = self.read_ident();
            if let Some(kw) = keyword_from_str(&ident) {
                return Ok(kw);
            }
            return Ok(Token::Ident(ident));
        }

        // Numbers
        if ch.is_ascii_digit() {
            return Ok(self.read_number());
        }

        // String literal
        if ch == '"' {
            let s = self.read_string()?;
            return Ok(Token::StringLit(s));
        }

        // Char literal
        if ch == '\'' {
            let c = self.read_char_lit()?;
            return Ok(Token::CharLit(c));
        }

        // Operators and delimiters
        self.advance();
        match ch {
            '+' => Ok(Token::Plus),
            '*' => {
                if self.peek() == Some('*') {
                    self.advance();
                    Ok(Token::Power)
                } else {
                    Ok(Token::Star)
                }
            }
            '%' => Ok(Token::Percent),
            '-' => {
                if self.peek() == Some('>') {
                    self.advance();
                    Ok(Token::Arrow)
                } else {
                    Ok(Token::Minus)
                }
            }
            '/' => Ok(Token::Slash),
            '=' => {
                if self.peek() == Some('=') {
                    self.advance();
                    Ok(Token::EqEq)
                } else {
                    Ok(Token::Eq)
                }
            }
            '!' => {
                if self.peek() == Some('=') {
                    self.advance();
                    Ok(Token::NotEq)
                } else {
                    Ok(Token::Not)
                }
            }
            '<' => {
                if self.peek() == Some('=') {
                    self.advance();
                    Ok(Token::LtEq)
                } else {
                    Ok(Token::Lt)
                }
            }
            '>' => {
                if self.peek() == Some('=') {
                    self.advance();
                    Ok(Token::GtEq)
                } else {
                    Ok(Token::Gt)
                }
            }
            '&' => {
                if self.peek() == Some('&') {
                    self.advance();
                    Ok(Token::And)
                } else {
                    Ok(Token::BitAnd)
                }
            }
            '|' => {
                if self.peek() == Some('|') {
                    self.advance();
                    Ok(Token::Or)
                } else {
                    Ok(Token::BitOr)
                }
            }
            '^' => {
                if self.peek() == Some('^') {
                    self.advance();
                    Ok(Token::Xor)
                } else {
                    Ok(Token::BitXor)
                }
            }
            '~' => Ok(Token::BitNot),
            ':' => {
                if self.peek() == Some('=') {
                    self.advance();
                    Ok(Token::ColonEq)
                } else {
                    Ok(Token::Colon)
                }
            }
            '.' => {
                if self.peek() == Some('.') {
                    self.advance();
                    if self.peek() == Some('=') {
                        self.advance();
                        Ok(Token::DotDotEq)
                    } else {
                        Ok(Token::DotDot)
                    }
                } else {
                    Ok(Token::Dot)
                }
            }
            '(' => Ok(Token::LParen),
            ')' => Ok(Token::RParen),
            '{' => Ok(Token::LBrace),
            '}' => Ok(Token::RBrace),
            '[' => Ok(Token::LBracket),
            ']' => Ok(Token::RBracket),
            ',' => Ok(Token::Comma),
            ';' => Ok(Token::Semicolon),
            _ => Err(format!("line {}:{}: unexpected character '{}'", self.line, self.col, ch)),
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
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens, vec![
            Token::Let, Token::Ident("x".into()), Token::Eq, Token::IntLit(42), Token::Eof
        ]);
    }

    #[test]
    fn test_lex_fn_decl() {
        let mut lexer = Lexer::new("fn main() {}");
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens, vec![
            Token::Fn, Token::Ident("main".into()), Token::LParen, Token::RParen,
            Token::LBrace, Token::RBrace, Token::Eof
        ]);
    }

    #[test]
    fn test_lex_all_operators() {
        let mut lexer = Lexer::new("+ - * / % ** == != < > <= >= && || ! ^^ & | ^ ~ = := .. ->");
        let tokens = lexer.tokenize().unwrap();
        // Should have 24 operator tokens + Eof = 25
        assert_eq!(tokens.len(), 25);
    }

    #[test]
    fn test_lex_string_literal() {
        let mut lexer = Lexer::new("\"hello world\"");
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens[0], Token::StringLit("hello world".into()));
    }

    #[test]
    fn test_lex_egyptian_fraction() {
        let mut lexer = Lexer::new("1.0/2.0 + 1.0/3.0 + 1.0/6.0");
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens[0], Token::FloatLit(1.0));
    }

    #[test]
    fn test_lex_comments_skipped() {
        let mut lexer = Lexer::new("let x = 1 // comment\nlet y = 2");
        let tokens = lexer.tokenize().unwrap();
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
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens[0], Token::StringLit("hello\nworld".into()));
    }

    #[test]
    fn test_lex_char_literal() {
        let mut lexer = Lexer::new("'a'");
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens[0], Token::CharLit('a'));
    }

    #[test]
    fn test_lex_booleans() {
        let mut lexer = Lexer::new("true false");
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens[0], Token::BoolLit(true));
        assert_eq!(tokens[1], Token::BoolLit(false));
    }
}
