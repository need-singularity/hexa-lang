//! HEXA-IR Builder — ergonomic API for constructing IR.
//!
//! Wraps IrFunction to provide convenient instruction emission methods.

use super::instr::*;
use super::opcode::OpCode;
use super::types::IrType;

/// Builder for constructing IR instructions within a function.
pub struct IrBuilder<'a> {
    pub func: &'a mut IrFunction,
    pub current_block: BlockId,
}

impl<'a> IrBuilder<'a> {
    pub fn new(func: &'a mut IrFunction) -> Self {
        let entry = if func.blocks.is_empty() {
            func.fresh_block("entry")
        } else {
            func.blocks[0].id
        };
        Self {
            func,
            current_block: entry,
        }
    }

    /// Switch to a different basic block.
    pub fn switch_to(&mut self, block: BlockId) {
        self.current_block = block;
    }

    /// Check if the last block is terminated (avoids borrow conflict on func.blocks).
    pub fn is_last_block_terminated(&self) -> bool {
        self.func.blocks.last().map_or(false, |b| b.is_terminated())
    }

    /// Check if the CURRENT block is terminated.
    pub fn is_current_block_terminated(&self) -> bool {
        self.func.block(self.current_block).map_or(false, |b| b.is_terminated())
    }

    /// Create a new basic block and return its ID.
    pub fn create_block(&mut self, name: impl Into<String>) -> BlockId {
        self.func.fresh_block(name)
    }

    /// Emit a raw instruction into the current block.
    fn emit(&mut self, op: OpCode, operands: Vec<Operand>, ty: IrType) -> ValueId {
        let result = self.func.fresh_value(ty.clone());
        let instr = Instruction {
            result,
            op,
            operands,
            ty,
        };
        if let Some(block) = self.func.block_mut(self.current_block) {
            block.instructions.push(instr);
        }
        result
    }

    // ── Arithmetic (Category 1) ──

    pub fn add(&mut self, lhs: ValueId, rhs: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::Add, vec![Operand::Value(lhs), Operand::Value(rhs)], ty)
    }

    pub fn sub(&mut self, lhs: ValueId, rhs: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::Sub, vec![Operand::Value(lhs), Operand::Value(rhs)], ty)
    }

    pub fn mul(&mut self, lhs: ValueId, rhs: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::Mul, vec![Operand::Value(lhs), Operand::Value(rhs)], ty)
    }

    pub fn div(&mut self, lhs: ValueId, rhs: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::Div, vec![Operand::Value(lhs), Operand::Value(rhs)], ty)
    }

    pub fn modulo(&mut self, lhs: ValueId, rhs: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::Mod, vec![Operand::Value(lhs), Operand::Value(rhs)], ty)
    }

    pub fn neg(&mut self, val: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::Neg, vec![Operand::Value(val)], ty)
    }

    // ── Memory (Category 2) ──

    pub fn load(&mut self, ptr: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::Load, vec![Operand::Value(ptr)], ty)
    }

    pub fn store(&mut self, ptr: ValueId, val: ValueId) -> ValueId {
        self.emit(OpCode::Store, vec![Operand::Value(ptr), Operand::Value(val)], IrType::Void)
    }

    pub fn alloc(&mut self, ty: IrType) -> ValueId {
        let ptr_ty = IrType::Ptr(Box::new(ty.clone()));
        self.emit(OpCode::Alloc, vec![], ptr_ty)
    }

    pub fn free(&mut self, ptr: ValueId) -> ValueId {
        self.emit(OpCode::Free, vec![Operand::Value(ptr)], IrType::Void)
    }

    pub fn copy_val(&mut self, val: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::Copy, vec![Operand::Value(val)], ty)
    }

    pub fn move_val(&mut self, val: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::Move, vec![Operand::Value(val)], ty)
    }

    // ── Control (Category 3) ──

    pub fn jump(&mut self, target: BlockId) -> ValueId {
        self.emit(OpCode::Jump, vec![Operand::Block(target)], IrType::Void)
    }

    pub fn branch(&mut self, cond: ValueId, then_bb: BlockId, else_bb: BlockId) -> ValueId {
        self.emit(
            OpCode::Branch,
            vec![
                Operand::Value(cond),
                Operand::Block(then_bb),
                Operand::Block(else_bb),
            ],
            IrType::Void,
        )
    }

    pub fn call(&mut self, func_id: FuncId, args: Vec<ValueId>, ret_ty: IrType) -> ValueId {
        let mut operands = vec![Operand::Func(func_id)];
        for arg in args {
            operands.push(Operand::Value(arg));
        }
        self.emit(OpCode::Call, operands, ret_ty)
    }

    pub fn call_indirect(&mut self, callee: ValueId, args: Vec<ValueId>, ret_ty: IrType) -> ValueId {
        let mut operands = vec![Operand::Value(callee)];
        for arg in args {
            operands.push(Operand::Value(arg));
        }
        self.emit(OpCode::Call, operands, ret_ty)
    }

    pub fn ret(&mut self, val: Option<ValueId>) -> ValueId {
        let operands = match val {
            Some(v) => vec![Operand::Value(v)],
            None => vec![],
        };
        self.emit(OpCode::Return, operands, IrType::Void)
    }

    pub fn phi(&mut self, incoming: Vec<(BlockId, ValueId)>, ty: IrType) -> ValueId {
        let operands = incoming
            .into_iter()
            .map(|(b, v)| Operand::PhiEntry(b, v))
            .collect();
        self.emit(OpCode::Phi, operands, ty)
    }

    pub fn switch(&mut self, val: ValueId, cases: Vec<(i64, BlockId)>, default: BlockId) -> ValueId {
        let mut operands = vec![Operand::Value(val)];
        for (case_val, target) in cases {
            operands.push(Operand::SwitchCase(case_val, target));
        }
        operands.push(Operand::Block(default));
        self.emit(OpCode::Switch, operands, IrType::Void)
    }

    // ── Proof (Category 4) ──

    pub fn ir_assert(&mut self, cond: ValueId) -> ValueId {
        self.emit(OpCode::Assert, vec![Operand::Value(cond)], IrType::Void)
    }

    pub fn assume(&mut self, cond: ValueId) -> ValueId {
        self.emit(OpCode::Assume, vec![Operand::Value(cond)], IrType::Void)
    }

    pub fn invariant(&mut self, cond: ValueId) -> ValueId {
        self.emit(OpCode::Invariant, vec![Operand::Value(cond)], IrType::Void)
    }

    pub fn lifetime_start(&mut self, val: ValueId) -> ValueId {
        self.emit(OpCode::LifetimeStart, vec![Operand::Value(val)], IrType::Void)
    }

    pub fn lifetime_end(&mut self, val: ValueId) -> ValueId {
        self.emit(OpCode::LifetimeEnd, vec![Operand::Value(val)], IrType::Void)
    }

    pub fn ownership_transfer(&mut self, val: ValueId, ty: IrType) -> ValueId {
        self.emit(OpCode::OwnershipTransfer, vec![Operand::Value(val)], ty)
    }

    // ── Convenience: Constants ──

    pub fn const_i64(&mut self, val: i64) -> ValueId {
        self.emit(OpCode::Load, vec![Operand::ImmI64(val)], IrType::I64)
    }

    pub fn const_f64(&mut self, val: f64) -> ValueId {
        self.emit(OpCode::Load, vec![Operand::ImmF64(val)], IrType::F64)
    }

    pub fn const_bool(&mut self, val: bool) -> ValueId {
        self.emit(OpCode::Load, vec![Operand::ImmBool(val)], IrType::Bool)
    }

    pub fn const_string(&mut self, idx: u32) -> ValueId {
        self.emit(OpCode::Load, vec![Operand::StringRef(idx)], IrType::Str)
    }

    // ── Convenience: Struct/Array ──

    pub fn struct_field(&mut self, obj: ValueId, field_idx: u32, ty: IrType) -> ValueId {
        self.emit(OpCode::Load, vec![Operand::Value(obj), Operand::FieldIdx(field_idx)], ty)
    }

    pub fn struct_store_field(&mut self, obj: ValueId, field_idx: u32, val: ValueId) -> ValueId {
        self.emit(
            OpCode::Store,
            vec![Operand::Value(obj), Operand::FieldIdx(field_idx), Operand::Value(val)],
            IrType::Void,
        )
    }

    pub fn array_index(&mut self, arr: ValueId, idx: ValueId, elem_ty: IrType) -> ValueId {
        self.emit(OpCode::Load, vec![Operand::Value(arr), Operand::Value(idx)], elem_ty)
    }

    pub fn array_store(&mut self, arr: ValueId, idx: ValueId, val: ValueId) -> ValueId {
        self.emit(
            OpCode::Store,
            vec![Operand::Value(arr), Operand::Value(idx), Operand::Value(val)],
            IrType::Void,
        )
    }

    // ── Comparison helpers (Sub opcode + CmpKind tag → Bool result) ──
    //
    // Comparisons reuse the Sub opcode to keep J₂=24 intact.
    // Each emits: %r: bool = sub %lhs, %rhs, cmp.<kind>
    // The CmpKind operand tells codegen/opt which comparison semantics to apply.

    pub fn emit_cmp(&mut self, lhs: ValueId, rhs: ValueId, kind: CmpKind) -> ValueId {
        self.emit(
            OpCode::Sub,
            vec![Operand::Value(lhs), Operand::Value(rhs), Operand::Cmp(kind)],
            IrType::Bool,
        )
    }

    pub fn emit_cmp_eq(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        self.emit_cmp(lhs, rhs, CmpKind::Eq)
    }

    pub fn emit_cmp_ne(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        self.emit_cmp(lhs, rhs, CmpKind::Ne)
    }

    pub fn emit_cmp_lt(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        self.emit_cmp(lhs, rhs, CmpKind::Lt)
    }

    pub fn emit_cmp_gt(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        self.emit_cmp(lhs, rhs, CmpKind::Gt)
    }

    pub fn emit_cmp_le(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        self.emit_cmp(lhs, rhs, CmpKind::Le)
    }

    pub fn emit_cmp_ge(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        self.emit_cmp(lhs, rhs, CmpKind::Ge)
    }

    // ── Logical/Bitwise helpers ──

    pub fn emit_and(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        self.mul(lhs, rhs, IrType::Bool) // a & b (for booleans, mul == and)
    }

    pub fn emit_or(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        // a | b = a + b - a*b
        let sum = self.add(lhs, rhs, IrType::I64);
        let prod = self.mul(lhs, rhs, IrType::I64);
        self.sub(sum, prod, IrType::Bool)
    }

    pub fn emit_xor(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        // a ^ b = (a + b) - 2*(a*b) for booleans; use sub for general
        let sum = self.add(lhs, rhs, IrType::I64);
        let prod = self.mul(lhs, rhs, IrType::I64);
        let two_prod = self.add(prod, prod, IrType::I64);
        self.sub(sum, two_prod, IrType::I64)
    }

    pub fn emit_bitand(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        self.mul(lhs, rhs, IrType::I64) // simplified
    }

    pub fn emit_bitor(&mut self, lhs: ValueId, rhs: ValueId) -> ValueId {
        let sum = self.add(lhs, rhs, IrType::I64);
        let prod = self.mul(lhs, rhs, IrType::I64);
        self.sub(sum, prod, IrType::I64)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_builder_arithmetic() {
        let mut module = IrModule::new("test");
        let fid = module.add_function("add_two".into(), vec![
            ("a".into(), IrType::I64),
            ("b".into(), IrType::I64),
        ], IrType::I64);
        let func = module.function_mut(fid).unwrap();
        let mut builder = IrBuilder::new(func);

        let a = builder.const_i64(10);
        let b = builder.const_i64(20);
        let sum = builder.add(a, b, IrType::I64);
        builder.ret(Some(sum));

        assert_eq!(func.blocks.len(), 1);
        assert_eq!(func.blocks[0].instructions.len(), 4); // 2 const + add + ret
        assert!(func.blocks[0].is_terminated());
    }

    #[test]
    fn test_cmp_all_six_produce_bool() {
        let mut module = IrModule::new("test");
        let fid = module.add_function("cmp_test".into(), vec![], IrType::Bool);
        let func = module.function_mut(fid).unwrap();
        let mut builder = IrBuilder::new(func);

        let a = builder.const_i64(10);
        let b = builder.const_i64(20);

        let eq = builder.emit_cmp_eq(a, b);
        let ne = builder.emit_cmp_ne(a, b);
        let lt = builder.emit_cmp_lt(a, b);
        let gt = builder.emit_cmp_gt(a, b);
        let le = builder.emit_cmp_le(a, b);
        let ge = builder.emit_cmp_ge(a, b);

        // All comparisons produce Bool type
        for vid in [eq, ne, lt, gt, le, ge] {
            assert_eq!(
                func.value_types.get(&vid),
                Some(&IrType::Bool),
                "Comparison result {:?} should be Bool",
                vid
            );
        }
    }

    #[test]
    fn test_cmp_uses_sub_opcode_with_cmpkind() {
        let mut module = IrModule::new("test");
        let fid = module.add_function("cmp_opcode".into(), vec![], IrType::Bool);
        let func = module.function_mut(fid).unwrap();
        let mut builder = IrBuilder::new(func);

        let a = builder.const_i64(5);
        let b = builder.const_i64(3);
        let _eq = builder.emit_cmp_eq(a, b);

        // The comparison instruction is the 3rd (index 2) after two consts
        let instr = &func.blocks[0].instructions[2];
        assert_eq!(instr.op, OpCode::Sub, "Comparison must use Sub opcode (J₂=24 preserved)");
        assert_eq!(instr.ty, IrType::Bool);
        // Third operand should be Cmp(Eq)
        assert!(matches!(instr.operands[2], Operand::Cmp(CmpKind::Eq)));
    }

    #[test]
    fn test_cmp_single_instruction_per_comparison() {
        // Each comparison should emit exactly 1 instruction (not multiple subs)
        let mut module = IrModule::new("test");
        let fid = module.add_function("single".into(), vec![], IrType::Bool);
        let func = module.function_mut(fid).unwrap();
        let mut builder = IrBuilder::new(func);

        let a = builder.const_i64(1);
        let b = builder.const_i64(2);
        // 2 consts so far
        let _le = builder.emit_cmp_le(a, b);
        // Should be exactly 3 instructions: const, const, cmp.le
        assert_eq!(func.blocks[0].instructions.len(), 3);
    }

    #[test]
    fn test_cmp_chaining_and() {
        // a < b && b < c  should produce 2 comparisons + 1 and
        let mut module = IrModule::new("test");
        let fid = module.add_function("chain".into(), vec![], IrType::Bool);
        let func = module.function_mut(fid).unwrap();
        let mut builder = IrBuilder::new(func);

        let a = builder.const_i64(1);
        let b = builder.const_i64(2);
        let c = builder.const_i64(3);
        let ab = builder.emit_cmp_lt(a, b);
        let bc = builder.emit_cmp_lt(b, c);
        let result = builder.emit_and(ab, bc);

        assert_eq!(func.value_types.get(&result), Some(&IrType::Bool));
        // 3 consts + 2 cmp + 1 and = 6 instructions
        assert_eq!(func.blocks[0].instructions.len(), 6);
    }

    #[test]
    fn test_builder_branch() {
        let mut module = IrModule::new("test");
        let fid = module.add_function("branch_test".into(), vec![], IrType::I64);
        let func = module.function_mut(fid).unwrap();
        let mut builder = IrBuilder::new(func);

        let then_bb = builder.create_block("then");
        let else_bb = builder.create_block("else");
        let merge_bb = builder.create_block("merge");

        let cond = builder.const_bool(true);
        builder.branch(cond, then_bb, else_bb);

        builder.switch_to(then_bb);
        let v1 = builder.const_i64(1);
        builder.jump(merge_bb);

        builder.switch_to(else_bb);
        let v2 = builder.const_i64(2);
        builder.jump(merge_bb);

        builder.switch_to(merge_bb);
        let result = builder.phi(vec![(then_bb, v1), (else_bb, v2)], IrType::I64);
        builder.ret(Some(result));

        assert_eq!(func.blocks.len(), 4); // entry + then + else + merge
    }
}
