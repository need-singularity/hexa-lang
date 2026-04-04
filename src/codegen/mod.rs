//! Native Code Generation — φ=2 targets (ARM64 + x86-64).
//!
//! No LLVM. No Cranelift. Direct machine code emission.

pub mod arm64;
pub mod x86_64;
pub mod regalloc;
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
    let code = match target {
        Target::Arm64 => arm64::emit(module, &alloc_result),
        Target::X86_64 => x86_64::emit(module, &alloc_result),
    };

    // Wrap in object format
    let obj = match format {
        ObjFormat::MachO => macho::wrap(code, module),
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

    // Write object file
    let obj_path = format!("{}.o", output_path);
    std::fs::write(&obj_path, &result.code)
        .map_err(|e| format!("failed to write object file: {}", e))?;

    // Link with system linker
    let status = std::process::Command::new("cc")
        .arg(&obj_path)
        .arg("-o")
        .arg(output_path)
        .arg("-lSystem")
        .status()
        .map_err(|e| format!("linker failed: {}", e))?;

    // Clean up object file
    let _ = std::fs::remove_file(&obj_path);

    if status.success() {
        Ok(())
    } else {
        Err("linker returned non-zero exit code".into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_host_target() {
        let (target, format) = host_target();
        // Should detect current platform
        #[cfg(target_arch = "aarch64")]
        assert_eq!(target, Target::Arm64);
        #[cfg(target_arch = "x86_64")]
        assert_eq!(target, Target::X86_64);
    }
}
