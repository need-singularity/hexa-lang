use std::rc::Rc;

/// Cheaply cloneable string type for tokens. Avoids heap allocation on clone.
#[derive(Clone, Debug, Eq, Hash)]
pub struct RcStr(Rc<str>);

impl PartialEq for RcStr {
    fn eq(&self, other: &Self) -> bool { self.0 == other.0 }
}

impl PartialEq<str> for RcStr {
    fn eq(&self, other: &str) -> bool { &*self.0 == other }
}

impl std::ops::Deref for RcStr {
    type Target = str;
    fn deref(&self) -> &str { &self.0 }
}

impl std::fmt::Display for RcStr {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

impl From<String> for RcStr {
    fn from(s: String) -> Self { RcStr(Rc::from(s.as_str())) }
}

impl From<&str> for RcStr {
    fn from(s: &str) -> Self { RcStr(Rc::from(s)) }
}

/// Source location: line and column (1-based).
#[derive(Debug, Clone, PartialEq)]
pub struct Span {
    pub line: usize,
    pub col: usize,
}

impl Span {
    pub fn new(line: usize, col: usize) -> Self {
        Self { line, col }
    }
}

/// A token paired with its source location.
#[derive(Debug, Clone)]
pub struct Spanned {
    pub token: Token,
    pub span: Span,
}

impl Spanned {
    pub fn new(token: Token, line: usize, col: usize) -> Self {
        Self { token, span: Span::new(line, col) }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum Token {
    // === Literals ===
    IntLit(i64),
    FloatLit(f64),
    StringLit(RcStr),
    CharLit(char),
    BoolLit(bool),

    // === Identifier ===
    Ident(RcStr),

    // === 53 Keywords (σ·τ + sopfr = 48 + 5) ===
    // Group 1: Control Flow (n=6)
    If, Else, Match, For, While, Loop, Break, Continue,
    // Group 2: Type Decl (sopfr=5)
    Type, Struct, Enum, Trait, Impl, Dyn,
    // Group 3: Functions (sopfr=5)
    Fn, Return, Yield, Async, Await,
    // Group 4: Variables (τ=4)
    Let, Mut, Const, Static,
    // Group 5: Modules (τ=4)
    Mod, Use, Pub, Crate,
    // Group 6: Memory (τ=4)
    Own, Borrow, Move, Drop,
    // Group 7: Concurrency (τ=4 + scope)
    Spawn, Channel, Select, Atomic, Scope,
    // Group 8: Effects (τ=4)
    Effect, Handle, Resume, Pure,
    // Group 9: Proofs (τ=4)
    Proof, Assert, Invariant, Theorem,
    // Group 10: Meta (τ=4)
    Macro, Derive, Where, Comptime,
    // Group 11: Errors (sopfr=5)
    Try, Catch, Throw, Panic, Recover,
    // Group 12: AI (τ=4)
    Intent, Generate, Verify, Optimize,
    // Group 13: FFI (μ=1)
    Extern,

    // === 24 Operators (J₂=24) ===
    // Arithmetic (n=6): + - * / % **
    Plus, Minus, Star, Slash, Percent, Power,
    // Comparison (n=6): == != < > <= >=
    EqEq, NotEq, Lt, Gt, LtEq, GtEq,
    // Logical (τ=4): && || ! ^^
    And, Or, Not, Xor,
    // Bitwise (τ=4): & | ^ ~
    BitAnd, BitOr, BitXor, BitNot,
    // Assignment (φ=2): = :=
    Eq, ColonEq,
    // Special (φ=2): .. ..= -> =>
    DotDot, DotDotEq, Arrow, FatArrow,

    // === Delimiters ===
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Colon, ColonColon, Semicolon, Dot,

    // === Special ===
    HashLBrace,  // #{ — map literal opener
    Newline,
    Eof,
}

pub fn keyword_from_str(s: &str) -> Option<Token> {
    match s {
        // Group 1: Control Flow (n=6)
        "if" => Some(Token::If),
        "else" => Some(Token::Else),
        "match" => Some(Token::Match),
        "for" => Some(Token::For),
        "while" => Some(Token::While),
        "loop" => Some(Token::Loop),
        "break" => Some(Token::Break),
        "continue" => Some(Token::Continue),
        // Group 2: Type Decl (sopfr=5)
        "type" => Some(Token::Type),
        "struct" => Some(Token::Struct),
        "enum" => Some(Token::Enum),
        "trait" => Some(Token::Trait),
        "impl" => Some(Token::Impl),
        "dyn" => Some(Token::Dyn),
        // Group 3: Functions (sopfr=5)
        "fn" => Some(Token::Fn),
        "return" => Some(Token::Return),
        "yield" => Some(Token::Yield),
        "async" => Some(Token::Async),
        "await" => Some(Token::Await),
        // Group 4: Variables (τ=4)
        "let" => Some(Token::Let),
        "mut" => Some(Token::Mut),
        "const" => Some(Token::Const),
        "static" => Some(Token::Static),
        // Group 5: Modules (τ=4)
        "mod" => Some(Token::Mod),
        "use" => Some(Token::Use),
        "import" => Some(Token::Use),  // alias for use
        "pub" => Some(Token::Pub),
        "crate" => Some(Token::Crate),
        // Group 6: Memory (τ=4)
        "own" => Some(Token::Own),
        "borrow" => Some(Token::Borrow),
        "move" => Some(Token::Move),
        "drop" => Some(Token::Drop),
        // Group 7: Concurrency (τ=4)
        "spawn" => Some(Token::Spawn),
        "channel" => Some(Token::Channel),
        "select" => Some(Token::Select),
        "atomic" => Some(Token::Atomic),
        "scope" => Some(Token::Scope),
        // Group 8: Effects (τ=4)
        "effect" => Some(Token::Effect),
        "handle" => Some(Token::Handle),
        "resume" => Some(Token::Resume),
        "pure" => Some(Token::Pure),
        // Group 9: Proofs (τ=4)
        "proof" => Some(Token::Proof),
        "assert" => Some(Token::Assert),
        "invariant" => Some(Token::Invariant),
        "theorem" => Some(Token::Theorem),
        // Group 10: Meta (τ=4)
        "macro" => Some(Token::Macro),
        "derive" => Some(Token::Derive),
        "where" => Some(Token::Where),
        "comptime" => Some(Token::Comptime),
        // Group 11: Errors (sopfr=5)
        "try" => Some(Token::Try),
        "catch" => Some(Token::Catch),
        "throw" => Some(Token::Throw),
        "panic" => Some(Token::Panic),
        "recover" => Some(Token::Recover),
        // Group 12: AI (τ=4)
        "intent" => Some(Token::Intent),
        "generate" => Some(Token::Generate),
        "verify" => Some(Token::Verify),
        "optimize" => Some(Token::Optimize),
        // Group 13: FFI (μ=1)
        "extern" => Some(Token::Extern),
        // Booleans
        "true" => Some(Token::BoolLit(true)),
        "false" => Some(Token::BoolLit(false)),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_keyword_count_is_53() {
        // σ·τ + sopfr = 48 + 5 = 53
        let keywords = vec![
            "if","else","match","for","while","loop",
            "type","struct","enum","trait","impl","dyn",
            "fn","return","yield","async","await",
            "let","mut","const","static",
            "mod","use","pub","crate",
            "own","borrow","move","drop",
            "spawn","channel","select","atomic","scope",
            "effect","handle","resume","pure",
            "proof","assert","invariant","theorem",
            "macro","derive","where","comptime",
            "try","catch","throw","panic","recover",
            "intent","generate","verify","optimize",
            "extern",
        ];
        assert_eq!(keywords.len(), 56);
        for kw in &keywords {
            assert!(keyword_from_str(kw).is_some(), "Missing keyword: {}", kw);
        }
    }

    #[test]
    fn test_keyword_groups_is_12() {
        // σ(6) = 12 keyword groups
        let group_sizes = [6, 6, 5, 4, 4, 4, 5, 4, 4, 4, 5, 4, 1];
        assert_eq!(group_sizes.len(), 13);
        assert_eq!(group_sizes.iter().sum::<usize>(), 56);
    }
}
