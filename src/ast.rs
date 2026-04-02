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
    Own(Box<Expr>),     // own expr — create owned value
    MoveExpr(Box<Expr>),  // move x — transfer ownership
    Borrow(Box<Expr>),  // borrow x — read-only reference
    Await(Box<Expr>),   // await expr — await a future value
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct SelectArm {
    pub receiver: Expr,  // rx.recv() expression
    pub binding: String, // as val
    pub body: Block,
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub enum Stmt {
    Let(String, Option<String>, Option<Expr>, Visibility),   // let name: type = expr, visibility
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
    Intent(String, Vec<(String, Expr)>),  // description, key-value fields
    Verify(String, Block),                // name, assertion block
    Mod(String, Vec<Stmt>),
    Use(Vec<String>),
    TryCatch(Block, String, Block),  // try { ... } catch e { ... }
    Throw(Expr),
    Spawn(Block),  // spawn { ... } — concurrent execution
    SpawnNamed(String, Block),  // spawn "name" { ... } — named concurrent execution
    DropStmt(String),  // drop x — explicit deallocation
    AsyncFnDecl(FnDecl),  // async fn name(...) { ... }
    Select(Vec<SelectArm>),  // select { rx.recv() as val => body, ... }
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
    pub body: Block,
    pub vis: Visibility,
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
    pub guard: Option<Expr>,
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
