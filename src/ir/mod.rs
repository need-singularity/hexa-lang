//! HEXA-IR — Self-designed Intermediate Representation.
//!
//! n=6 derived constants:
//!   - J₂=24 opcodes (τ=4 categories × n=6 each)
//!   - σ=12 types (8 primitive + 4 compound)
//!   - σ=12 optimization passes (3 waves × 4)
//!   - Egyptian memory: 1/2 + 1/3 + 1/6 = 1
//!
//! No LLVM. No Cranelift. Pure self-built.

pub mod opcode;
pub mod types;
pub mod instr;
pub mod builder;
pub mod print;

pub use opcode::OpCode;
pub use types::IrType;
pub use instr::{
    ValueId, BlockId, FuncId, Instruction, Operand,
    BasicBlock, IrFunction, IrModule, Global,
};
pub use builder::IrBuilder;
pub use print::print_module;
