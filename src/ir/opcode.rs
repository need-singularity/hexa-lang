//! HEXA-IR Opcode definitions — J₂=24 opcodes (τ=4 categories × n=6 each).
//!
//! Every opcode derives from n=6 arithmetic. Zero arbitrary choices.

/// J₂=24 opcodes across τ=4 categories.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum OpCode {
    // ── Category 1: Arithmetic (n=6) ──
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,

    // ── Category 2: Memory (n=6) ──
    Load,
    Store,
    Alloc,
    Free,
    Copy,
    Move,

    // ── Category 3: Control (n=6) ──
    Jump,
    Branch,
    Call,
    Return,
    Phi,
    Switch,

    // ── Category 4: Proof (n=6) ──
    Assert,
    Assume,
    Invariant,
    LifetimeStart,
    LifetimeEnd,
    OwnershipTransfer,
}

impl OpCode {
    /// Total opcode count = J₂(6) = 24.
    pub const COUNT: usize = 24;

    /// Category index (0..τ=4).
    pub fn category(&self) -> usize {
        (*self as usize) / 6
    }

    /// Index within category (0..n=6).
    pub fn index_in_category(&self) -> usize {
        (*self as usize) % 6
    }

    /// Whether this opcode has side effects.
    pub fn has_side_effect(&self) -> bool {
        matches!(
            self,
            OpCode::Store
                | OpCode::Alloc
                | OpCode::Free
                | OpCode::Copy
                | OpCode::Move
                | OpCode::Call
                | OpCode::OwnershipTransfer
        )
    }

    /// Whether this opcode is a terminator (ends a basic block).
    pub fn is_terminator(&self) -> bool {
        matches!(
            self,
            OpCode::Jump | OpCode::Branch | OpCode::Return | OpCode::Switch
        )
    }

    /// Whether this is a proof-related opcode (compile-time only).
    pub fn is_proof(&self) -> bool {
        self.category() == 3
    }

    /// Category name.
    pub fn category_name(&self) -> &'static str {
        match self.category() {
            0 => "arithmetic",
            1 => "memory",
            2 => "control",
            3 => "proof",
            _ => unreachable!(),
        }
    }
}

impl std::fmt::Display for OpCode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", match self {
            OpCode::Add => "add",
            OpCode::Sub => "sub",
            OpCode::Mul => "mul",
            OpCode::Div => "div",
            OpCode::Mod => "mod",
            OpCode::Neg => "neg",
            OpCode::Load => "load",
            OpCode::Store => "store",
            OpCode::Alloc => "alloc",
            OpCode::Free => "free",
            OpCode::Copy => "copy",
            OpCode::Move => "move",
            OpCode::Jump => "jump",
            OpCode::Branch => "branch",
            OpCode::Call => "call",
            OpCode::Return => "ret",
            OpCode::Phi => "phi",
            OpCode::Switch => "switch",
            OpCode::Assert => "assert",
            OpCode::Assume => "assume",
            OpCode::Invariant => "invariant",
            OpCode::LifetimeStart => "lifetime.start",
            OpCode::LifetimeEnd => "lifetime.end",
            OpCode::OwnershipTransfer => "ownership.transfer",
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_j2_24_opcodes() {
        assert_eq!(OpCode::COUNT, 24, "J₂(6) = 24");
    }

    #[test]
    fn test_tau_4_categories() {
        assert_eq!(OpCode::Add.category(), 0);
        assert_eq!(OpCode::Load.category(), 1);
        assert_eq!(OpCode::Jump.category(), 2);
        assert_eq!(OpCode::Assert.category(), 3);
    }

    #[test]
    fn test_n6_per_category() {
        assert_eq!(OpCode::Neg.index_in_category(), 5); // last in arithmetic
        assert_eq!(OpCode::Move.index_in_category(), 5); // last in memory
        assert_eq!(OpCode::Switch.index_in_category(), 5); // last in control
        assert_eq!(OpCode::OwnershipTransfer.index_in_category(), 5); // last in proof
    }

    #[test]
    fn test_terminators() {
        assert!(OpCode::Jump.is_terminator());
        assert!(OpCode::Branch.is_terminator());
        assert!(OpCode::Return.is_terminator());
        assert!(OpCode::Switch.is_terminator());
        assert!(!OpCode::Add.is_terminator());
    }

    #[test]
    fn test_proof_opcodes() {
        assert!(OpCode::Assert.is_proof());
        assert!(OpCode::Assume.is_proof());
        assert!(OpCode::Invariant.is_proof());
        assert!(OpCode::LifetimeStart.is_proof());
        assert!(OpCode::LifetimeEnd.is_proof());
        assert!(OpCode::OwnershipTransfer.is_proof());
        assert!(!OpCode::Add.is_proof());
    }
}
