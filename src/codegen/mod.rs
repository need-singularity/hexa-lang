//! Native Code Generation — φ=2 targets (ARM64 + x86-64).
//!
//! No LLVM. No Cranelift. Direct machine code emission.

pub mod arm64;
pub mod x86_64;
pub mod regalloc;
pub mod regalloc_linear;
pub mod peephole;
pub mod elf;
pub mod macho;

use crate::ir::IrModule;

/// Target architecture.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Target {
    Arm64,
    X86_64,
}

/// Object file format.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ObjFormat {
    MachO,
    Elf,
}

/// Code generation result.
pub struct CodegenResult {
    pub code: Vec<u8>,
    pub target: Target,
    pub format: ObjFormat,
}

/// Detect the current host target.
pub fn host_target() -> (Target, ObjFormat) {
    #[cfg(target_arch = "aarch64")]
    let target = Target::Arm64;
    #[cfg(target_arch = "x86_64")]
    let target = Target::X86_64;
    #[cfg(not(any(target_arch = "aarch64", target_arch = "x86_64")))]
    let target = Target::X86_64; // fallback

    #[cfg(target_os = "macos")]
    let format = ObjFormat::MachO;
    #[cfg(target_os = "linux")]
    let format = ObjFormat::Elf;
    #[cfg(not(any(target_os = "macos", target_os = "linux")))]
    let format = ObjFormat::Elf; // fallback

    (target, format)
}

/// Generate native code from an IR module.
pub fn generate(module: &IrModule, target: Target, format: ObjFormat) -> CodegenResult {
    // Register allocation
    let alloc_result = regalloc::allocate(module, target);

    // Generate machine code
    let (code, main_offset) = match target {
        Target::Arm64 => arm64::emit_with_main_offset(module, &alloc_result),
        Target::X86_64 => (x86_64::emit(module, &alloc_result), 0),
    };

    // Wrap in object format
    let obj = match format {
        ObjFormat::MachO => macho::wrap(code, module, main_offset),
        ObjFormat::Elf => elf::wrap(code, module),
    };

    CodegenResult {
        code: obj,
        target,
        format,
    }
}

/// Compile an IR module to a native executable.
pub fn compile_to_binary(module: &IrModule, output_path: &str) -> Result<(), String> {
    let (target, format) = host_target();
    let result = generate(module, target, format);

    // Write object file (hexa_main symbol)
    let obj_path = format!("{}.o", output_path);
    std::fs::write(&obj_path, &result.code)
        .map_err(|e| format!("failed to write object file: {}", e))?;

    // Write C wrapper that calls hexa_main and prints result
    let wrapper_path = format!("{}_wrapper.c", output_path);
    std::fs::write(&wrapper_path,
        "#include <stdio.h>\nextern long hexa_main(void);\nint main(void) { printf(\"%ld\\n\", hexa_main()); return 0; }\n"
    ).map_err(|e| format!("failed to write wrapper: {}", e))?;

    // Link with system linker
    let status = std::process::Command::new("cc")
        .arg("-O2")
        .arg(&wrapper_path)
        .arg(&obj_path)
        .arg("-o")
        .arg(output_path)
        .arg("-lSystem")
        .status()
        .map_err(|e| format!("linker failed: {}", e))?;

    let _ = std::fs::remove_file(&wrapper_path);
    // Keep .o for debugging: let _ = std::fs::remove_file(&obj_path);

    if status.success() {
        Ok(())
    } else {
        Err("linker returned non-zero exit code".into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrModule, IrType, IrBuilder};

    #[test]
    fn test_host_target() {
        let (target, _format) = host_target();
        // Should detect current platform
        #[cfg(target_arch = "aarch64")]
        assert_eq!(target, Target::Arm64);
        #[cfg(target_arch = "x86_64")]
        assert_eq!(target, Target::X86_64);
    }

    #[test]
    fn test_codegen_simple_main() {
        // Create a simple IR module: main() returns 42
        let mut module = IrModule::new("test");
        let fid = module.add_function("main".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let six = b.const_i64(6);
            let seven = b.const_i64(7);
            let result = b.mul(six, seven, IrType::I64);
            b.ret(Some(result));
        }

        let (target, format) = host_target();
        let result = generate(&module, target, format);

        // Verify output is non-empty
        assert!(!result.code.is_empty(), "generated code should not be empty");

        // Verify Mach-O magic bytes (on macOS)
        #[cfg(target_os = "macos")]
        {
            assert_eq!(result.format, ObjFormat::MachO);
            let magic = u32::from_le_bytes([
                result.code[0], result.code[1], result.code[2], result.code[3],
            ]);
            assert_eq!(magic, 0xFEEDFACF, "output should start with Mach-O 64-bit magic");
        }

        // Verify ELF magic bytes (on Linux)
        #[cfg(target_os = "linux")]
        {
            assert_eq!(result.format, ObjFormat::Elf);
            assert_eq!(&result.code[0..4], &[0x7f, b'E', b'L', b'F']);
        }
    }

    #[test]
    fn test_codegen_produces_arm64_instructions() {
        let mut module = IrModule::new("test");
        let fid = module.add_function("main".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let val = b.const_i64(42);
            b.ret(Some(val));
        }

        // Generate raw ARM64 code (no object wrapper)
        let alloc_result = regalloc::allocate(&module, Target::Arm64);
        let code = arm64::emit(&module, &alloc_result);

        // Should have instructions: prologue (2-3) + load_imm(1) + return_mov(0-1) + epilogue(2-3)
        assert!(code.len() >= 16, "ARM64 code should have at least 4 instructions (16 bytes)");
        // All ARM64 instructions are 4 bytes
        assert_eq!(code.len() % 4, 0, "ARM64 code length should be multiple of 4");
    }

    #[test]
    fn test_compile_to_binary_e2e() {
        // Full E2E: IR -> regalloc -> ARM64 -> Mach-O .o -> link -> run
        let mut module = IrModule::new("test");
        let fid = module.add_function("main".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let val = b.const_i64(42);
            b.ret(Some(val));
        }

        let output_path = "/tmp/hexa_ir_e2e_test";
        let result = compile_to_binary(&module, output_path);
        assert!(result.is_ok(), "compile_to_binary should succeed: {:?}", result);

        // Run the binary and check stdout contains "42"
        let output = std::process::Command::new(output_path)
            .output()
            .expect("should be able to run the binary");

        // Clean up
        let _ = std::fs::remove_file(output_path);

        let stdout = String::from_utf8_lossy(&output.stdout);
        assert!(stdout.trim() == "42", "binary should output 42, got: {}", stdout.trim());
    }

    #[test]
    fn test_e2e_arithmetic() {
        // main returns 6*7 = 42
        let mut module = IrModule::new("test");
        let fid = module.add_function("main".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let a = b.const_i64(6);
            let c = b.const_i64(7);
            let r = b.mul(a, c, IrType::I64);
            b.ret(Some(r));
        }

        let output_path = "/tmp/hexa_ir_e2e_arith";
        let result = compile_to_binary(&module, output_path);
        assert!(result.is_ok(), "compile failed: {:?}", result);

        let output = std::process::Command::new(output_path).output().unwrap();
        let _ = std::fs::remove_file(output_path);
        let stdout = String::from_utf8_lossy(&output.stdout);
        assert_eq!(stdout.trim(), "42", "6*7 should be 42, got: {}", stdout.trim());
    }

    #[test]
    fn test_e2e_direct_call() {
        // fn add_one(x) -> x + 1
        // main: add_one(41) -> 42
        let mut module = IrModule::new("test");

        let add_one_id = module.add_function(
            "add_one".into(),
            vec![("x".into(), IrType::I64)],
            IrType::I64,
        );
        {
            let func = module.function_mut(add_one_id).unwrap();
            let mut b = IrBuilder::new(func);
            let x = b.load_param(0, IrType::I64);
            let one = b.const_i64(1);
            let r = b.add(x, one, IrType::I64);
            b.ret(Some(r));
        }

        let main_id = module.add_function("main".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(main_id).unwrap();
            let mut b = IrBuilder::new(func);
            let arg = b.const_i64(41);
            let r = b.call(add_one_id, vec![arg], IrType::I64);
            b.ret(Some(r));
        }

        let output_path = "/tmp/hexa_ir_e2e_call";
        let result = compile_to_binary(&module, output_path);
        assert!(result.is_ok(), "compile failed: {:?}", result);

        let output = std::process::Command::new(output_path).output().unwrap();
        let _ = std::fs::remove_file(output_path);
        let stdout = String::from_utf8_lossy(&output.stdout);
        assert_eq!(stdout.trim(), "42", "add_one(41) should be 42, got: {}", stdout.trim());
    }

    #[test]
    fn test_e2e_recursive_factorial() {
        // fn fact(n) -> if n <= 1 { 1 } else { n * fact(n-1) }
        // main: fact(5) -> 120
        let mut module = IrModule::new("test");

        let fact_id = module.add_function(
            "fact".into(),
            vec![("n".into(), IrType::I64)],
            IrType::I64,
        );
        {
            let func = module.function_mut(fact_id).unwrap();
            let mut b = IrBuilder::new(func);

            // Save param to stack slot (survives across calls)
            let n_ptr = b.alloc(IrType::I64);
            let n_param = b.load_param(0, IrType::I64);
            b.store(n_ptr, n_param);

            // Load n from stack for the comparison
            let n = b.load(n_ptr, IrType::I64);
            let one = b.const_i64(1);
            let cond = b.emit_cmp_le(n, one);

            let then_bb = b.create_block("then");
            let else_bb = b.create_block("else");
            let merge_bb = b.create_block("merge");

            b.branch(cond, then_bb, else_bb);

            // then: return 1
            b.switch_to(then_bb);
            let then_val = b.const_i64(1);
            b.jump(merge_bb);

            // else: n * fact(n - 1)
            // Reload n from stack slot each time -- registers get clobbered
            // by the recursive call return value in x0.
            b.switch_to(else_bb);
            let n2 = b.load(n_ptr, IrType::I64);
            let one2 = b.const_i64(1);
            let nm1 = b.sub(n2, one2, IrType::I64);
            let sub_result = b.call(fact_id, vec![nm1], IrType::I64);
            let n3 = b.load(n_ptr, IrType::I64); // reload AFTER call
            let else_val = b.mul(n3, sub_result, IrType::I64);
            b.jump(merge_bb);

            // merge: phi
            b.switch_to(merge_bb);
            let result = b.phi(vec![(then_bb, then_val), (else_bb, else_val)], IrType::I64);
            b.ret(Some(result));
        }

        let main_id = module.add_function("main".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(main_id).unwrap();
            let mut b = IrBuilder::new(func);
            let five = b.const_i64(5);
            let r = b.call(fact_id, vec![five], IrType::I64);
            b.ret(Some(r));
        }

        let output_path = "/tmp/hexa_ir_e2e_fact";
        let result = compile_to_binary(&module, output_path);
        assert!(result.is_ok(), "compile failed: {:?}", result);

        let output = std::process::Command::new(output_path).output().unwrap();
        let _ = std::fs::remove_file(output_path);
        let stdout = String::from_utf8_lossy(&output.stdout);
        assert_eq!(stdout.trim(), "120", "fact(5) should be 120, got: {}", stdout.trim());
    }

    #[test]
    fn test_e2e_if_else_expr() {
        // main: if 1 { 42 } else { 99 } -> 42
        let mut module = IrModule::new("test");
        let fid = module.add_function("main".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);

            let cond = b.const_i64(1);
            let then_bb = b.create_block("then");
            let else_bb = b.create_block("else");
            let merge_bb = b.create_block("merge");

            b.branch(cond, then_bb, else_bb);

            b.switch_to(then_bb);
            let v1 = b.const_i64(42);
            b.jump(merge_bb);

            b.switch_to(else_bb);
            let v2 = b.const_i64(99);
            b.jump(merge_bb);

            b.switch_to(merge_bb);
            let result = b.phi(vec![(then_bb, v1), (else_bb, v2)], IrType::I64);
            b.ret(Some(result));
        }

        let output_path = "/tmp/hexa_ir_e2e_ifelse";
        let result = compile_to_binary(&module, output_path);
        assert!(result.is_ok(), "compile failed: {:?}", result);

        let output = std::process::Command::new(output_path).output().unwrap();
        let _ = std::fs::remove_file(output_path);
        let stdout = String::from_utf8_lossy(&output.stdout);
        assert_eq!(stdout.trim(), "42", "if 1 {{42}} else {{99}} should be 42, got: {}", stdout.trim());
    }
}
