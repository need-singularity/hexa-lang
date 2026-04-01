#[derive(Debug, Clone, PartialEq)]
pub enum Token {
    // === Literals ===
    IntLit(i64),
    FloatLit(f64),
    StringLit(String),
    CharLit(char),
    BoolLit(bool),

    // === Identifier ===
    Ident(String),

    // === 53 Keywords (σ·τ + sopfr = 48 + 5) ===
    // Group 1: Control Flow (n=6)
    If, Else, Match, For, While, Loop,
    // Group 2: Type Decl (sopfr=5)
    Type, Struct, Enum, Trait, Impl,
    // Group 3: Functions (sopfr=5)
    Fn, Return, Yield, Async, Await,
    // Group 4: Variables (τ=4)
    Let, Mut, Const, Static,
    // Group 5: Modules (τ=4)
    Mod, Use, Pub, Crate,
    // Group 6: Memory (τ=4)
    Own, Borrow, Move, Drop,
    // Group 7: Concurrency (τ=4)
    Spawn, Channel, Select, Atomic,
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
    // Special (φ=2): .. ->
    DotDot, Arrow,

    // === Delimiters ===
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Colon, Semicolon, Dot,

    // === Special ===
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
        // Group 2: Type Decl (sopfr=5)
        "type" => Some(Token::Type),
        "struct" => Some(Token::Struct),
        "enum" => Some(Token::Enum),
        "trait" => Some(Token::Trait),
        "impl" => Some(Token::Impl),
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
            "type","struct","enum","trait","impl",
            "fn","return","yield","async","await",
            "let","mut","const","static",
            "mod","use","pub","crate",
            "own","borrow","move","drop",
            "spawn","channel","select","atomic",
            "effect","handle","resume","pure",
            "proof","assert","invariant","theorem",
            "macro","derive","where","comptime",
            "try","catch","throw","panic","recover",
            "intent","generate","verify","optimize",
        ];
        assert_eq!(keywords.len(), 53);
        for kw in &keywords {
            assert!(keyword_from_str(kw).is_some(), "Missing keyword: {}", kw);
        }
    }

    #[test]
    fn test_keyword_groups_is_12() {
        // σ(6) = 12 keyword groups
        let group_sizes = [6, 5, 5, 4, 4, 4, 4, 4, 4, 4, 5, 4];
        assert_eq!(group_sizes.len(), 12); // σ(6)
        assert_eq!(group_sizes.iter().sum::<usize>(), 53); // σ·τ + sopfr
    }
}
