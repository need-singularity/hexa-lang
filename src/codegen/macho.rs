//! Mach-O binary format wrapper — macOS executables.
//!
//! Wraps raw machine code in Mach-O 64-bit format.

use crate::ir::IrModule;

/// Mach-O header constants.
const MH_MAGIC_64: u32 = 0xFEEDFACF;
const MH_EXECUTE: u32 = 2;
const CPU_TYPE_ARM64: u32 = 0x0100000C;
const CPU_TYPE_X86_64: u32 = 0x01000007;
const CPU_SUBTYPE_ALL: u32 = 0x00000003;

/// Wrap machine code in Mach-O format.
pub fn wrap(code: Vec<u8>, module: &IrModule) -> Vec<u8> {
    let mut out = Vec::new();

    // For now, emit a minimal Mach-O with __TEXT,__text section.
    // A full implementation would emit:
    // 1. Mach-O header
    // 2. LC_SEGMENT_64 for __TEXT
    // 3. Section header for __text
    // 4. LC_MAIN (entry point)
    // 5. LC_SYMTAB (symbol table)

    let code_size = code.len();
    let page_size: usize = 0x4000; // 16KB on ARM64
    let header_size: usize = 0x100; // approximate

    // Mach-O 64 header (32 bytes)
    out.extend_from_slice(&MH_MAGIC_64.to_le_bytes());

    #[cfg(target_arch = "aarch64")]
    out.extend_from_slice(&CPU_TYPE_ARM64.to_le_bytes());
    #[cfg(not(target_arch = "aarch64"))]
    out.extend_from_slice(&CPU_TYPE_X86_64.to_le_bytes());

    out.extend_from_slice(&CPU_SUBTYPE_ALL.to_le_bytes());
    out.extend_from_slice(&MH_EXECUTE.to_le_bytes());
    out.extend_from_slice(&2u32.to_le_bytes());     // ncmds
    out.extend_from_slice(&0u32.to_le_bytes());     // sizeofcmds (patched later)
    out.extend_from_slice(&0x00200085u32.to_le_bytes()); // flags
    out.extend_from_slice(&0u32.to_le_bytes());     // reserved

    let cmds_start = out.len();

    // LC_SEGMENT_64 for __TEXT
    let seg_cmd: u32 = 0x19; // LC_SEGMENT_64
    let seg_size: u32 = 72 + 80; // segment cmd + 1 section header
    out.extend_from_slice(&seg_cmd.to_le_bytes());
    out.extend_from_slice(&seg_size.to_le_bytes());
    // segname: __TEXT (16 bytes)
    let mut segname = [0u8; 16];
    segname[..6].copy_from_slice(b"__TEXT");
    out.extend_from_slice(&segname);
    // vmaddr, vmsize, fileoff, filesize
    out.extend_from_slice(&(page_size as u64).to_le_bytes()); // vmaddr
    out.extend_from_slice(&(code_size as u64).to_le_bytes()); // vmsize
    out.extend_from_slice(&(page_size as u64).to_le_bytes()); // fileoff
    out.extend_from_slice(&(code_size as u64).to_le_bytes()); // filesize
    out.extend_from_slice(&5u32.to_le_bytes());  // maxprot (r-x)
    out.extend_from_slice(&5u32.to_le_bytes());  // initprot (r-x)
    out.extend_from_slice(&1u32.to_le_bytes());  // nsects
    out.extend_from_slice(&0u32.to_le_bytes());  // flags

    // Section header: __text
    let mut sectname = [0u8; 16];
    sectname[..6].copy_from_slice(b"__text");
    out.extend_from_slice(&sectname);
    out.extend_from_slice(&segname); // segname
    out.extend_from_slice(&(page_size as u64).to_le_bytes()); // addr
    out.extend_from_slice(&(code_size as u64).to_le_bytes()); // size
    out.extend_from_slice(&(page_size as u32).to_le_bytes()); // offset
    out.extend_from_slice(&4u32.to_le_bytes());  // align (2^4 = 16)
    out.extend_from_slice(&0u32.to_le_bytes());  // reloff
    out.extend_from_slice(&0u32.to_le_bytes());  // nreloc
    out.extend_from_slice(&0x80000400u32.to_le_bytes()); // flags: S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
    out.extend_from_slice(&0u32.to_le_bytes());  // reserved1
    out.extend_from_slice(&0u32.to_le_bytes());  // reserved2
    out.extend_from_slice(&0u32.to_le_bytes());  // reserved3 (padding for 64-bit)

    // LC_MAIN
    let main_cmd: u32 = 0x80000028; // LC_MAIN
    let main_size: u32 = 24;
    out.extend_from_slice(&main_cmd.to_le_bytes());
    out.extend_from_slice(&main_size.to_le_bytes());
    out.extend_from_slice(&0u64.to_le_bytes());  // entryoff (start of __text)
    out.extend_from_slice(&0u64.to_le_bytes());  // stacksize (default)

    // Patch sizeofcmds
    let cmds_size = (out.len() - cmds_start) as u32;
    out[20..24].copy_from_slice(&cmds_size.to_le_bytes());

    // Pad to page boundary
    while out.len() < page_size {
        out.push(0);
    }

    // Append code
    out.extend_from_slice(&code);

    out
}
