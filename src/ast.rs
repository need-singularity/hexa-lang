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
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum Stmt {
    Let(String, Option<String>, Option<Expr>),   // let name: type = expr
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
    Assert(Expr),
    Intent(String, Vec<String>),
    Mod(String, Vec<Stmt>),
    Use(Vec<String>),
    TryCatch(Block, String, Block),  // try { ... } catch e { ... }
    Throw(Expr),
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
    pub params: Vec<Param>,
    pub ret_type: Option<String>,
    pub body: Block,
    pub vis: Visibility,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct StructDecl {
    pub name: String,
    pub fields: Vec<(String, String, Visibility)>,
    pub vis: Visibility,
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
    pub body: Block,
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
