//! HEXA-IR Pretty Printer — human-readable textual IR format.

use super::instr::*;

/// Print an entire module to string.
pub fn print_module(module: &IrModule) -> String {
    let mut out = String::new();
    out.push_str(&format!("; module {}\n", module.name));
    out.push_str(&format!("; strings: {}\n\n", module.string_pool.len()));

    // String pool
    for (i, s) in module.string_pool.iter().enumerate() {
        out.push_str(&format!("@str.{} = \"{}\"\n", i, escape_str(s)));
    }
    if !module.string_pool.is_empty() {
        out.push('\n');
    }

    // Globals
    for g in &module.globals {
        let kw = if g.is_const { "const" } else { "global" };
        out.push_str(&format!("{} @{}: {}", kw, g.name, g.ty));
        if let Some(init) = &g.init {
            out.push_str(&format!(" = {}", print_operand(init)));
        }
        out.push('\n');
    }
    if !module.globals.is_empty() {
        out.push('\n');
    }

    // Functions
    for func in &module.functions {
        out.push_str(&print_function(func));
        out.push('\n');
    }

    out
}

/// Print a single function.
pub fn print_function(func: &IrFunction) -> String {
    let mut out = String::new();

    // Signature
    out.push_str(&format!("fn @{}(", func.name));
    for (i, (name, ty)) in func.params.iter().enumerate() {
        if i > 0 { out.push_str(", "); }
        out.push_str(&format!("{}: {}", name, ty));
    }
    out.push_str(&format!(") -> {} {{\n", func.ret_type));

    // Blocks
    for block in &func.blocks {
        out.push_str(&print_block(block));
    }

    out.push_str("}\n");
    out
}

/// Print a basic block.
fn print_block(block: &BasicBlock) -> String {
    let mut out = String::new();

    // Block header with predecessors
    out.push_str(&format!("  {}:", block.name));
    if !block.preds.is_empty() {
        out.push_str(&format!(
            "  ; preds: {}",
            block.preds.iter().map(|p| p.to_string()).collect::<Vec<_>>().join(", ")
        ));
    }
    out.push('\n');

    // Instructions
    for instr in &block.instructions {
        out.push_str(&format!("    {}\n", print_instruction(instr)));
    }

    out
}

/// Print a single instruction.
fn print_instruction(instr: &Instruction) -> String {
    let mut out = String::new();

    // Result assignment (skip for void)
    if instr.ty != super::types::IrType::Void {
        out.push_str(&format!("{}: {} = ", instr.result, instr.ty));
    }

    // Opcode
    out.push_str(&format!("{}", instr.op));

    // Operands
    if !instr.operands.is_empty() {
        out.push(' ');
        let ops: Vec<String> = instr.operands.iter().map(print_operand).collect();
        out.push_str(&ops.join(", "));
    }

    out
}

fn print_operand(op: &Operand) -> String {
    match op {
        Operand::Value(v) => format!("{}", v),
        Operand::ImmI64(n) => format!("{}", n),
        Operand::ImmF64(f) => format!("{:.6}", f),
        Operand::ImmBool(b) => format!("{}", b),
        Operand::Block(b) => format!("{}", b),
        Operand::Func(f) => format!("{}", f),
        Operand::FieldIdx(i) => format!("field.{}", i),
        Operand::StringRef(i) => format!("@str.{}", i),
        Operand::PhiEntry(b, v) => format!("[{}: {}]", b, v),
        Operand::SwitchCase(val, b) => format!("case {} => {}", val, b),
    }
}

fn escape_str(s: &str) -> String {
    s.replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\n', "\\n")
        .replace('\t', "\\t")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::types::IrType;
    use crate::ir::builder::IrBuilder;

    #[test]
    fn test_print_simple_function() {
        let mut module = IrModule::new("test");
        let fid = module.add_function("add".into(), vec![
            ("a".into(), IrType::I64),
            ("b".into(), IrType::I64),
        ], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let a = b.const_i64(10);
            let bv = b.const_i64(20);
            let sum = b.add(a, bv, IrType::I64);
            b.ret(Some(sum));
        }

        let output = print_module(&module);
        assert!(output.contains("fn @add(a: i64, b: i64) -> i64"));
        assert!(output.contains("add"));
        assert!(output.contains("ret"));
    }

    #[test]
    fn test_print_string_pool() {
        let mut module = IrModule::new("test");
        module.intern_string("hello");
        module.intern_string("world");
        let output = print_module(&module);
        assert!(output.contains("@str.0 = \"hello\""));
        assert!(output.contains("@str.1 = \"world\""));
    }
}
