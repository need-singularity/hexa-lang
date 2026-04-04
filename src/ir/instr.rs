//! HEXA-IR SSA Instructions — every value has exactly one definition.
//!
//! Instructions are organized in basic blocks within functions.
//! Each instruction produces a Value (SSA register) identified by ValueId.

use std::collections::HashMap;
use super::opcode::OpCode;
use super::types::IrType;

/// SSA value identifier — unique per function.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ValueId(pub u32);

/// Basic block identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct BlockId(pub u32);

/// Function identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct FuncId(pub u32);

impl std::fmt::Display for ValueId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "%{}", self.0)
    }
}

impl std::fmt::Display for BlockId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "bb{}", self.0)
    }
}

impl std::fmt::Display for FuncId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "@{}", self.0)
    }
}

/// A single SSA instruction.
#[derive(Debug, Clone)]
pub struct Instruction {
    /// The value this instruction defines (SSA output).
    pub result: ValueId,
    /// The opcode.
    pub op: OpCode,
    /// Operands (SSA inputs).
    pub operands: Vec<Operand>,
    /// Result type.
    pub ty: IrType,
}

/// Comparison kind — encodes which comparison a Sub-based cmp performs.
/// Keeps J₂=24 opcodes intact; comparisons reuse `Sub` with a CmpKind tag.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CmpKind {
    Eq,  // ==
    Ne,  // !=
    Lt,  // <
    Le,  // <=
    Gt,  // >
    Ge,  // >=
}

impl std::fmt::Display for CmpKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", match self {
            CmpKind::Eq => "eq",
            CmpKind::Ne => "ne",
            CmpKind::Lt => "lt",
            CmpKind::Le => "le",
            CmpKind::Gt => "gt",
            CmpKind::Ge => "ge",
        })
    }
}

/// An operand to an instruction.
#[derive(Debug, Clone)]
pub enum Operand {
    /// SSA value reference.
    Value(ValueId),
    /// Immediate integer constant.
    ImmI64(i64),
    /// Immediate float constant.
    ImmF64(f64),
    /// Immediate boolean.
    ImmBool(bool),
    /// Block target (for jumps/branches).
    Block(BlockId),
    /// Function reference.
    Func(FuncId),
    /// Field index (for struct access).
    FieldIdx(u32),
    /// String literal reference (index into string pool).
    StringRef(u32),
    /// Phi incoming: (block, value) pairs stored as flat vec.
    PhiEntry(BlockId, ValueId),
    /// Switch case: (value, block) for dispatch.
    SwitchCase(i64, BlockId),
    /// Comparison kind tag — attached to Sub instructions that represent comparisons.
    Cmp(CmpKind),
}

/// A basic block — sequence of instructions ending with a terminator.
#[derive(Debug, Clone)]
pub struct BasicBlock {
    pub id: BlockId,
    pub name: String,
    pub instructions: Vec<Instruction>,
    /// Predecessor blocks (filled during CFG construction).
    pub preds: Vec<BlockId>,
    /// Successor blocks (derived from terminator).
    pub succs: Vec<BlockId>,
}

impl BasicBlock {
    pub fn new(id: BlockId, name: String) -> Self {
        Self {
            id,
            name,
            instructions: Vec::new(),
            preds: Vec::new(),
            succs: Vec::new(),
        }
    }

    /// The last instruction (should be a terminator).
    pub fn terminator(&self) -> Option<&Instruction> {
        self.instructions.last().filter(|i| i.op.is_terminator())
    }

    /// Whether the block is properly terminated.
    pub fn is_terminated(&self) -> bool {
        self.terminator().is_some()
    }
}

/// An IR function.
#[derive(Debug, Clone)]
pub struct IrFunction {
    pub id: FuncId,
    pub name: String,
    pub params: Vec<(String, IrType)>,
    pub ret_type: IrType,
    pub blocks: Vec<BasicBlock>,
    pub value_types: HashMap<ValueId, IrType>,
    next_value: u32,
    next_block: u32,
}

impl IrFunction {
    pub fn new(id: FuncId, name: String, params: Vec<(String, IrType)>, ret_type: IrType) -> Self {
        Self {
            id,
            name,
            params,
            ret_type,
            blocks: Vec::new(),
            value_types: HashMap::new(),
            next_value: 0,
            next_block: 0,
        }
    }

    /// Allocate a fresh ValueId.
    pub fn fresh_value(&mut self, ty: IrType) -> ValueId {
        let id = ValueId(self.next_value);
        self.next_value += 1;
        self.value_types.insert(id, ty);
        id
    }

    /// Allocate a fresh BasicBlock.
    pub fn fresh_block(&mut self, name: impl Into<String>) -> BlockId {
        let id = BlockId(self.next_block);
        self.next_block += 1;
        self.blocks.push(BasicBlock::new(id, name.into()));
        id
    }

    /// Get a block by ID.
    pub fn block(&self, id: BlockId) -> Option<&BasicBlock> {
        self.blocks.iter().find(|b| b.id == id)
    }

    /// Get a mutable block by ID.
    pub fn block_mut(&mut self, id: BlockId) -> Option<&mut BasicBlock> {
        self.blocks.iter_mut().find(|b| b.id == id)
    }

    /// Entry block (always the first).
    pub fn entry_block(&self) -> Option<&BasicBlock> {
        self.blocks.first()
    }

    /// Total instruction count.
    pub fn instr_count(&self) -> usize {
        self.blocks.iter().map(|b| b.instructions.len()).sum()
    }

    /// Rebuild successor/predecessor edges from terminators.
    pub fn rebuild_cfg(&mut self) {
        // Clear existing edges
        for block in &mut self.blocks {
            block.preds.clear();
            block.succs.clear();
        }

        // Collect successor info
        let mut edges: Vec<(BlockId, BlockId)> = Vec::new();
        for block in &self.blocks {
            if let Some(term) = block.terminator() {
                for op in &term.operands {
                    if let Operand::Block(target) = op {
                        edges.push((block.id, *target));
                    }
                    if let Operand::SwitchCase(_, target) = op {
                        edges.push((block.id, *target));
                    }
                }
            }
        }

        // Apply edges
        for (from, to) in edges {
            if let Some(b) = self.block_mut(from) {
                if !b.succs.contains(&to) {
                    b.succs.push(to);
                }
            }
            if let Some(b) = self.block_mut(to) {
                if !b.preds.contains(&from) {
                    b.preds.push(from);
                }
            }
        }
    }
}

/// Top-level IR module — collection of functions + globals.
#[derive(Debug, Clone)]
pub struct IrModule {
    pub name: String,
    pub functions: Vec<IrFunction>,
    pub globals: Vec<Global>,
    pub string_pool: Vec<String>,
    next_func: u32,
}

/// A global variable or constant.
#[derive(Debug, Clone)]
pub struct Global {
    pub name: String,
    pub ty: IrType,
    pub init: Option<Operand>,
    pub is_const: bool,
}

impl IrModule {
    pub fn new(name: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            functions: Vec::new(),
            globals: Vec::new(),
            string_pool: Vec::new(),
            next_func: 0,
        }
    }

    /// Add a function.
    pub fn add_function(
        &mut self,
        name: String,
        params: Vec<(String, IrType)>,
        ret_type: IrType,
    ) -> FuncId {
        let id = FuncId(self.next_func);
        self.next_func += 1;
        self.functions.push(IrFunction::new(id, name, params, ret_type));
        id
    }

    /// Get a function by ID.
    pub fn function(&self, id: FuncId) -> Option<&IrFunction> {
        self.functions.iter().find(|f| f.id == id)
    }

    /// Get a mutable function by ID.
    pub fn function_mut(&mut self, id: FuncId) -> Option<&mut IrFunction> {
        self.functions.iter_mut().find(|f| f.id == id)
    }

    /// Find function by name.
    pub fn find_function(&self, name: &str) -> Option<&IrFunction> {
        self.functions.iter().find(|f| f.name == name)
    }

    /// Intern a string, returning its index.
    pub fn intern_string(&mut self, s: &str) -> u32 {
        if let Some(idx) = self.string_pool.iter().position(|x| x == s) {
            idx as u32
        } else {
            let idx = self.string_pool.len() as u32;
            self.string_pool.push(s.to_string());
            idx
        }
    }

    /// Add a global variable.
    pub fn add_global(&mut self, name: String, ty: IrType, init: Option<Operand>, is_const: bool) {
        self.globals.push(Global { name, ty, init, is_const });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fresh_values() {
        let mut func = IrFunction::new(
            FuncId(0),
            "test".into(),
            vec![],
            IrType::Void,
        );
        let v0 = func.fresh_value(IrType::I64);
        let v1 = func.fresh_value(IrType::I64);
        assert_eq!(v0, ValueId(0));
        assert_eq!(v1, ValueId(1));
    }

    #[test]
    fn test_basic_block_creation() {
        let mut func = IrFunction::new(
            FuncId(0),
            "test".into(),
            vec![],
            IrType::Void,
        );
        let bb0 = func.fresh_block("entry");
        let bb1 = func.fresh_block("then");
        assert_eq!(bb0, BlockId(0));
        assert_eq!(bb1, BlockId(1));
        assert_eq!(func.blocks.len(), 2);
    }

    #[test]
    fn test_module_functions() {
        let mut module = IrModule::new("test");
        let f0 = module.add_function("main".into(), vec![], IrType::I64);
        let f1 = module.add_function("add".into(), vec![
            ("a".into(), IrType::I64),
            ("b".into(), IrType::I64),
        ], IrType::I64);
        assert_eq!(f0, FuncId(0));
        assert_eq!(f1, FuncId(1));
        assert_eq!(module.find_function("main").unwrap().name, "main");
    }

    #[test]
    fn test_string_interning() {
        let mut module = IrModule::new("test");
        let a = module.intern_string("hello");
        let b = module.intern_string("world");
        let c = module.intern_string("hello"); // duplicate
        assert_eq!(a, 0);
        assert_eq!(b, 1);
        assert_eq!(c, 0); // reused
    }
}
