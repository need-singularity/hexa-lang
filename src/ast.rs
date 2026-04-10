// ── AI-native Attribute ─────────────────────────────────
#[derive(Debug, Clone)]
pub struct Attribute {
    pub kind: crate::token::AttrKind,
    /// key:expr pairs from @attr(key1: expr1, key2: expr2) form (e.g. @contract(requires: x>0))
    pub args: Vec<(String, Expr)>,
}

impl Attribute {
    pub fn new(kind: crate::token::AttrKind) -> Self { Self { kind, args: Vec::new() } }
    pub fn with_args(kind: crate::token::AttrKind, args: Vec<(String, Expr)>) -> Self { Self { kind, args } }
}

pub fn has_attr(attrs: &[Attribute], check: fn(&crate::token::AttrKind) -> bool) -> bool {
    attrs.iter().any(|a| check(&a.kind))
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum Expr {
    IntLit(i64),
    FloatLit(f64),
    BoolLit(bool),
    StringLit(String),
    CharLit(char),
    Ident(String),
    Binary(Box<Expr>, BinOp, Box<Expr>),
    Unary(UnaryOp, Box<Expr>),
    Call(Box<Expr>, Vec<Expr>),
    Lambda(Vec<Param>, Box<Expr>),
    Array(Vec<Expr>),
    Tuple(Vec<Expr>),
    Field(Box<Expr>, String),
    Index(Box<Expr>, Box<Expr>),
    If(Box<Expr>, Block, Option<Block>),
    Match(Box<Expr>, Vec<MatchArm>),
    Block(Block),
    Range(Box<Expr>, Box<Expr>, bool), // start..end or start..=end
    StructInit(String, Vec<(String, Expr)>),  // Name { field: val, ... }
    MapLit(Vec<(Expr, Expr)>),  // { key: val, ... }
    EnumPath(String, String, Option<Box<Expr>>),  // EnumName::Variant(data)
    Wildcard,  // _ pattern (catch-all)
    ArrayPattern(Vec<Expr>, Option<String>),  // [a, b, ...rest] pattern for match
    Own(Box<Expr>),     // own expr — create owned value
    MoveExpr(Box<Expr>),  // move x — transfer ownership
    Borrow(Box<Expr>),  // borrow x — read-only reference
    Await(Box<Expr>),   // await expr — await a future value
    MacroInvoc(MacroInvocation),  // name!(args) or name![args]
    Comptime(Box<Expr>),           // comptime { ... } — compile-time evaluated block
    // Algebraic effects
    HandleWith(Box<Expr>, Vec<EffectHandler>),  // handle { body } with { handlers }
    EffectCall(String, String, Vec<Expr>),       // Effect.op(args)
    Resume(Box<Expr>),                           // resume(val)
    // Trait objects
    DynCast(String, Box<Expr>),                  // dyn TraitName(expr) — wrap value as trait object
    Yield(Box<Expr>),                            // yield expr — generator yield
    /// template { ... } — HTML template block
    Template(Vec<TemplateNode>),
    /// try { expr } catch e { fallback } — try as expression (returns last value)
    TryCatch(Block, String, Block),
}

/// A node in a template tree — represents an HTML element, text, or control flow.
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum TemplateNode {
    /// HTML element: tag { attrs..., children... }
    Element {
        tag: String,
        attrs: Vec<TemplateAttr>,
        children: Vec<TemplateNode>,
    },
    /// Text content — expression that evaluates to a string
    Text(Expr),
    /// Control flow: for name in expr { children }
    For(String, Expr, Vec<TemplateNode>),
    /// Conditional: if expr { children } else { children }
    If(Expr, Vec<TemplateNode>, Option<Vec<TemplateNode>>),
    /// Match expression
    Match(Expr, Vec<(Expr, Vec<TemplateNode>)>),
    /// Verify assertion inside template
    Verify(Expr),
    /// Invariant assertion inside template
    Invariant(Expr),
}

/// An attribute on a template element: key: expr
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct TemplateAttr {
    pub key: String,
    pub value: Expr,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct SelectArm {
    pub receiver: Expr,  // rx.recv() expression or channel ident
    pub binding: String, // as val or from binding
    pub body: Block,
}

/// A timeout arm in a select statement: `timeout(ms) => { body }`
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct TimeoutArm {
    pub duration_ms: Expr,  // timeout duration in milliseconds
    pub body: Block,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum Stmt {
    Let(String, Option<String>, Option<Expr>, Visibility),   // let name: type = expr, visibility
    LetTuple(Vec<String>, Expr),                              // let (a, b) = expr — tuple destructure
    Const(String, Option<String>, Expr, Visibility),         // const name: type = expr (immutable)
    Static(String, Option<String>, Expr, Visibility),        // static name: type = expr (module-level global)
    Assign(Expr, Expr),
    Expr(Expr),
    Return(Option<Expr>),
    FnDecl(FnDecl),
    StructDecl(StructDecl),
    EnumDecl(EnumDecl),
    TraitDecl(TraitDecl),
    ImplBlock(ImplBlock),
    For(String, Expr, Block),
    While(Expr, Block),
    Loop(Block),
    Proof(String, Block),
    ProofBlock(String, Vec<ProofBlockStmt>),  // formal verification with SAT/SMT solver
    Assert(Expr),
    Intent(String, Vec<(String, Expr)>),  // description, key-value fields
    Verify(String, Block),                // name, assertion block
    Mod(String, Vec<Stmt>),
    Use(Vec<String>),
    TryCatch(Block, String, Block),  // try { ... } catch e { ... }
    Throw(Expr),
    Spawn(Block),  // spawn { ... } — concurrent execution
    SpawnNamed(String, Block),  // spawn "name" { ... } — named concurrent execution
    Scope(Block),  // scope { ... } — structured concurrency: all spawns within must complete before scope exits
    DropStmt(String),  // drop x — explicit deallocation
    AsyncFnDecl(FnDecl),  // async fn name(...) { ... }
    Select(Vec<SelectArm>, Option<TimeoutArm>),  // select { rx.recv() as val => body, ... timeout(ms) => body }
    MacroDef(MacroDef),      // macro! name { pattern => body }
    DeriveDecl(String, Vec<String>),  // derive(Trait1, Trait2) for TypeName
    Generate(GenerateTarget),  // generate fn/expr with LLM
    Optimize(FnDecl),          // optimize fn with LLM rewrite
    ComptimeFn(FnDecl),        // comptime fn — function evaluated at compile time
    // Algebraic effects
    EffectDecl(EffectDecl),    // effect Name { fn op(...) -> ret }
    // Phase 15: Consciousness v2
    ConsciousnessBlock(String, Block),  // consciousness "name" { ... }
    EvolveFn(FnDecl),                   // @evolve fn — self-modifying
    // Part B: token-only keywords
    TypeAlias(String, String, Visibility),        // type Name = ExistingType
    AtomicLet(String, Option<String>, Option<Expr>, Visibility),  // atomic let name: type = expr
    Panic(Expr),                                  // panic expr
    Theorem(String, Vec<ProofBlockStmt>),         // theorem name { ... }
    Break,                                        // break — exit loop
    Continue,                                     // continue — skip to next iteration
    // Group 13: FFI
    Extern(ExternFnDecl),                         // extern fn name(params) -> RetType
    // @contract desugar: runtime pre/postcondition checks with descriptive error messages
    ContractAssert(ContractKind, String, Expr),    // kind, fn_name, condition expr
}

/// Contract assertion kind — distinguishes requires (precondition) from ensures (postcondition).
#[derive(Debug, Clone, PartialEq)]
pub enum ContractKind {
    Requires,  // precondition checked at function entry
    Ensures,   // postcondition checked at function exit (or before return)
}

/// External function declaration — calls into C libraries via FFI.
#[derive(Debug, Clone)]
pub struct ExternFnDecl {
    pub name: String,
    pub params: Vec<ExternParam>,
    pub ret_type: Option<String>,     // "Int", "*Void", etc.
    pub link_lib: Option<String>,     // from @link("libname")
}

#[derive(Debug, Clone)]
pub struct ExternParam {
    pub name: String,
    pub typ: String,                  // "Int", "*Void", "*Byte", etc.
}

/// Target for AI code generation.
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum GenerateTarget {
    /// generate fn name(params) -> ret { "description" }
    Fn {
        name: String,
        params: Vec<Param>,
        ret_type: Option<String>,
        description: String,
    },
    /// generate type { "description" } — inline expression
    Expr {
        type_hint: Option<String>,
        description: String,
    },
}

// ── Macro types ──────────────────────────────────────────

/// A fragment specifier in macro patterns: $name:kind
#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
pub enum MacroFragSpec {
    Expr,   // :expr
    Ident,  // :ident
    Ty,     // :ty
    Literal,// :literal
    Block,  // :block
}

/// A token in a macro pattern
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum MacroPatternToken {
    /// $name:spec — captures a fragment
    Capture(String, MacroFragSpec),
    /// $( ... ),* — repetition with separator and kleene op
    Repetition(Vec<MacroPatternToken>, Option<MacroBodyToken>, RepeatKind),
    /// Literal token that must match exactly
    Literal(MacroLitToken),
}

/// A token in a macro body (template)
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum MacroBodyToken {
    /// $name — substitution
    Var(String),
    /// $( ... )* — repeated expansion
    Repetition(Vec<MacroBodyToken>, RepeatKind),
    /// Literal token in output
    Literal(MacroLitToken),
}

/// Kleene operators for repetition
#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
pub enum RepeatKind {
    ZeroOrMore,  // *
    OneOrMore,   // +
}

/// Literal tokens that appear in macro patterns/bodies
#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
pub enum MacroLitToken {
    Ident(String),
    IntLit(i64),
    FloatLit(f64),
    StringLit(String),
    BoolLit(bool),
    // Punctuation
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Colon, ColonColon, Semicolon, Dot,
    Plus, Minus, Star, Slash, Percent, Power,
    Eq, EqEq, NotEq, Lt, Gt, LtEq, GtEq,
    And, Or, Not, Arrow, DotDot, DotDotEq,
    BitAnd, BitOr, BitXor, BitNot,
    // Keywords
    Let, Mut, Fn, Return, If, Else, For, While, Loop,
    Struct, Enum, Trait, Impl, Match, True, False,
    Newline,
}

/// A single macro rule: pattern => body
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct MacroRule {
    pub pattern: Vec<MacroPatternToken>,
    pub body: Vec<MacroBodyToken>,
}

/// A macro definition
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct MacroDef {
    pub name: String,
    pub rules: Vec<MacroRule>,
}

/// A macro invocation (expression-level): name!(args) or name![args]
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct MacroInvocation {
    pub name: String,
    pub tokens: Vec<MacroLitToken>,
}

// BinOp: 22 binary operators (24 total - 2 unary-only: ! ~)
#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
pub enum BinOp {
    Add, Sub, Mul, Div, Rem, Pow,           // arithmetic (n=6)
    Eq, Ne, Lt, Gt, Le, Ge,                 // comparison (n=6)
    And, Or, Xor,                            // logical (3 of τ=4, ! is unary)
    BitAnd, BitOr, BitXor,                   // bitwise (3 of τ=4, ~ is unary)
    Assign, DeclAssign,                      // assignment (φ=2)
    Range, Arrow,                            // special (φ=2)
}

#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
pub enum UnaryOp { Neg, Not, BitNot }

// Supporting types
pub type Block = Vec<Stmt>;

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct Param {
    pub name: String,
    pub typ: Option<String>,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct FnDecl {
    pub name: String,
    pub type_params: Vec<TypeParam>,  // <T>, <T: Display>, <T, U>
    pub params: Vec<Param>,
    pub ret_type: Option<String>,
    pub where_clauses: Vec<WhereClause>,  // where T: Display, U: Clone
    pub precondition: Option<Expr>,       // where expr — contract precondition
    pub postcondition: Option<Expr>,      // ensures expr — contract postcondition
    pub body: Block,
    pub vis: Visibility,
    pub is_pure: bool,  // pure fn — no effects allowed
    pub attrs: Vec<Attribute>,  // AI-native: @inline @hot @memoize @parallel etc.
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct WhereClause {
    pub type_name: String,
    pub bound: String,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct TypeParam {
    pub name: String,
    pub bound: Option<String>,  // trait bound: T: Display
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct StructDecl {
    pub name: String,
    pub type_params: Vec<TypeParam>,  // <T>, <T, U>, etc. for generic structs
    pub fields: Vec<(String, String, Visibility)>,
    pub vis: Visibility,
    pub attrs: Vec<Attribute>,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct EnumDecl {
    pub name: String,
    pub variants: Vec<(String, Option<Vec<String>>)>,
    pub vis: Visibility,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct TraitDecl {
    pub name: String,
    pub methods: Vec<FnDecl>,
    pub vis: Visibility,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct ImplBlock {
    pub trait_name: Option<String>,
    pub target: String,
    pub methods: Vec<FnDecl>,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct MatchArm {
    pub pattern: Expr,
    pub guard: Option<Expr>,
    pub body: Block,
}

// ── Algebraic effects ────────────────────────────────────

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct EffectDecl {
    pub name: String,
    pub type_params: Vec<String>,
    pub operations: Vec<EffectOp>,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct EffectOp {
    pub name: String,
    pub params: Vec<Param>,
    pub return_type: Option<String>,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct EffectHandler {
    pub effect: String,
    pub op: String,
    pub params: Vec<String>,
    pub body: Block,
}

// ── Formal proof block statements ────────────────────────────

/// A statement inside a `proof` formal verification block.
#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum ProofBlockStmt {
    /// `forall x: type, condition => conclusion`
    ForAll {
        var: String,
        typ: String,
        condition: Expr,
        conclusion: Expr,
    },
    /// `exists x: type, condition`
    Exists {
        var: String,
        typ: String,
        condition: Expr,
    },
    /// `assert condition`
    Assert(Expr),
    /// `check law(N)` -- verify a consciousness law by number
    CheckLaw(i64),
    /// `invariant expr` — loop/proof invariant
    Invariant(Expr),
}

// τ=4 visibility levels
#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
pub enum Visibility {
    Private,   // default
    Module,    // mod
    Crate,     // crate
    Public,    // pub
}
