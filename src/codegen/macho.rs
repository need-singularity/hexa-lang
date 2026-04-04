//! Mach-O binary format wrapper — macOS object files.
//!
//! Wraps raw machine code in Mach-O 64-bit relocatable object format (.o)
//! suitable for linking with `cc -lSystem`.

use crate::ir::IrModule;

/// Mach-O header constants.
const MH_MAGIC_64: u32 = 0xFEEDFACF;
const MH_OBJECT: u32 = 1; // Relocatable object file
const CPU_TYPE_ARM64: u32 = 0x0100000C;
const CPU_TYPE_X86_64: u32 = 0x01000007;
const CPU_SUBTYPE_ALL: u32 = 0x00000000;
const CPU_SUBTYPE_X86_ALL: u32 = 0x00000003;

// Load command types
const LC_SEGMENT_64: u32 = 0x19;
const LC_SYMTAB: u32 = 0x02;
const LC_BUILD_VERSION: u32 = 0x32;

// Section types and attributes
const S_REGULAR: u32 = 0x00;
const S_ATTR_PURE_INSTRUCTIONS: u32 = 0x80000000;
const S_ATTR_SOME_INSTRUCTIONS: u32 = 0x00000400;

// nlist_64 symbol type bits
const N_EXT: u8 = 0x01;
const N_SECT: u8 = 0x0e;

/// Wrap machine code in a Mach-O relocatable object (.o) format.
/// This produces a proper .o file that `cc` / `ld` can link.
pub fn wrap(code: Vec<u8>, module: &IrModule) -> Vec<u8> {
    let mut out = Vec::new();

    let code_size = code.len();

    // Symbol: _hexa_main (C wrapper provides _main)
    let symbol_name = b"_hexa_main\0";

    // Layout calculation:
    // Mach-O header: 32 bytes
    // LC_SEGMENT_64 (with 1 section): 72 + 80 = 152 bytes
    // LC_SYMTAB: 24 bytes
    // LC_BUILD_VERSION: 24 bytes
    // Total headers+cmds = 32 + 152 + 24 + 24 = 232 bytes
    // Align to 8 bytes for code start

    let header_size: usize = 32;
    let segment_cmd_size: usize = 72 + 80; // LC_SEGMENT_64 + 1 section header
    let symtab_cmd_size: usize = 24;
    let build_version_cmd_size: usize = 24;
    let ncmds: u32 = 3;
    let sizeofcmds = (segment_cmd_size + symtab_cmd_size + build_version_cmd_size) as u32;

    let code_offset = align_to(header_size + sizeofcmds as usize, 8);
    let symtab_offset = align_to(code_offset + code_size, 4);
    // nlist_64 entry is 16 bytes
    let nlist_size: usize = 16;
    let strtab_offset = symtab_offset + nlist_size;
    // String table: \0 + _main\0
    let strtab_size = 1 + symbol_name.len();

    // ── Mach-O Header (32 bytes) ──
    out.extend_from_slice(&MH_MAGIC_64.to_le_bytes());

    #[cfg(target_arch = "aarch64")]
    {
        out.extend_from_slice(&CPU_TYPE_ARM64.to_le_bytes());
        out.extend_from_slice(&CPU_SUBTYPE_ALL.to_le_bytes());
    }
    #[cfg(not(target_arch = "aarch64"))]
    {
        out.extend_from_slice(&CPU_TYPE_X86_64.to_le_bytes());
        out.extend_from_slice(&CPU_SUBTYPE_X86_ALL.to_le_bytes());
    }

    out.extend_from_slice(&MH_OBJECT.to_le_bytes());         // filetype = MH_OBJECT
    out.extend_from_slice(&ncmds.to_le_bytes());              // ncmds
    out.extend_from_slice(&sizeofcmds.to_le_bytes());         // sizeofcmds
    out.extend_from_slice(&0u32.to_le_bytes());               // flags
    out.extend_from_slice(&0u32.to_le_bytes());               // reserved (64-bit padding)

    // ── LC_SEGMENT_64 (72 bytes base + 80 bytes per section) ──
    out.extend_from_slice(&LC_SEGMENT_64.to_le_bytes());
    out.extend_from_slice(&(segment_cmd_size as u32).to_le_bytes());
    // segname: empty for object files (16 bytes)
    out.extend_from_slice(&[0u8; 16]);
    // vmaddr, vmsize
    out.extend_from_slice(&0u64.to_le_bytes());                       // vmaddr
    let segment_vmsize = align_to(strtab_offset + strtab_size, 8) as u64;
    out.extend_from_slice(&segment_vmsize.to_le_bytes());              // vmsize
    // fileoff, filesize
    out.extend_from_slice(&(code_offset as u64).to_le_bytes());       // fileoff
    let segment_filesize = (strtab_offset + strtab_size - code_offset) as u64;
    out.extend_from_slice(&segment_filesize.to_le_bytes());            // filesize
    out.extend_from_slice(&7u32.to_le_bytes());                        // maxprot (rwx)
    out.extend_from_slice(&7u32.to_le_bytes());                        // initprot (rwx)
    out.extend_from_slice(&1u32.to_le_bytes());                        // nsects
    out.extend_from_slice(&0u32.to_le_bytes());                        // flags

    // ── Section header: __text in __TEXT (80 bytes) ──
    let mut sectname = [0u8; 16];
    sectname[..6].copy_from_slice(b"__text");
    out.extend_from_slice(&sectname);
    let mut segname = [0u8; 16];
    segname[..6].copy_from_slice(b"__TEXT");
    out.extend_from_slice(&segname);
    out.extend_from_slice(&0u64.to_le_bytes());                        // addr (0 for .o)
    out.extend_from_slice(&(code_size as u64).to_le_bytes());          // size
    out.extend_from_slice(&(code_offset as u32).to_le_bytes());        // offset
    out.extend_from_slice(&2u32.to_le_bytes());                        // align (2^2 = 4 byte)
    out.extend_from_slice(&0u32.to_le_bytes());                        // reloff
    out.extend_from_slice(&0u32.to_le_bytes());                        // nreloc
    let sect_flags = S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
    out.extend_from_slice(&sect_flags.to_le_bytes());                   // flags
    out.extend_from_slice(&0u32.to_le_bytes());                        // reserved1
    out.extend_from_slice(&0u32.to_le_bytes());                        // reserved2
    out.extend_from_slice(&0u32.to_le_bytes());                        // reserved3 (64-bit pad)

    // ── LC_SYMTAB (24 bytes) ──
    out.extend_from_slice(&LC_SYMTAB.to_le_bytes());
    out.extend_from_slice(&(symtab_cmd_size as u32).to_le_bytes());
    out.extend_from_slice(&(symtab_offset as u32).to_le_bytes());     // symoff
    out.extend_from_slice(&1u32.to_le_bytes());                        // nsyms
    out.extend_from_slice(&(strtab_offset as u32).to_le_bytes());     // stroff
    out.extend_from_slice(&(strtab_size as u32).to_le_bytes());       // strsize

    // ── LC_BUILD_VERSION (24 bytes) ──
    out.extend_from_slice(&LC_BUILD_VERSION.to_le_bytes());
    out.extend_from_slice(&(build_version_cmd_size as u32).to_le_bytes());
    out.extend_from_slice(&1u32.to_le_bytes());     // platform = MACOS
    out.extend_from_slice(&0x000E0000u32.to_le_bytes()); // minos = 14.0
    out.extend_from_slice(&0x000E0000u32.to_le_bytes()); // sdk = 14.0
    out.extend_from_slice(&0u32.to_le_bytes());     // ntools

    // ── Pad to code_offset ──
    while out.len() < code_offset {
        out.push(0);
    }

    // ── Code section ──
    out.extend_from_slice(&code);

    // ── Pad to symtab_offset ──
    while out.len() < symtab_offset {
        out.push(0);
    }

    // ── Symbol table: nlist_64 for _main (16 bytes) ──
    // n_strx: offset into string table (1, after the leading \0)
    out.extend_from_slice(&1u32.to_le_bytes());
    // n_type: N_SECT | N_EXT (defined, external)
    out.push(N_SECT | N_EXT);
    // n_sect: 1 (first section, __text)
    out.push(1);
    // n_desc: 0
    out.extend_from_slice(&0u16.to_le_bytes());
    // n_value: 0 (offset within section)
    out.extend_from_slice(&0u64.to_le_bytes());

    // ── String table ──
    assert_eq!(out.len(), strtab_offset);
    out.push(0); // mandatory leading null byte
    out.extend_from_slice(symbol_name);

    out
}

/// Wrap machine code in a Mach-O executable format (direct execution, no linker).
pub fn wrap_executable(code: Vec<u8>, _module: &IrModule) -> Vec<u8> {
    let mut out = Vec::new();
    let code_size = code.len();
    let page_size: usize = 0x4000; // 16KB on ARM64

    // Mach-O 64 header (32 bytes)
    out.extend_from_slice(&MH_MAGIC_64.to_le_bytes());

    #[cfg(target_arch = "aarch64")]
    {
        out.extend_from_slice(&CPU_TYPE_ARM64.to_le_bytes());
        out.extend_from_slice(&CPU_SUBTYPE_ALL.to_le_bytes());
    }
    #[cfg(not(target_arch = "aarch64"))]
    {
        out.extend_from_slice(&CPU_TYPE_X86_64.to_le_bytes());
        out.extend_from_slice(&CPU_SUBTYPE_X86_ALL.to_le_bytes());
    }

    let mh_execute: u32 = 2;
    out.extend_from_slice(&mh_execute.to_le_bytes());
    out.extend_from_slice(&2u32.to_le_bytes());     // ncmds
    out.extend_from_slice(&0u32.to_le_bytes());     // sizeofcmds (patched later)
    out.extend_from_slice(&0x00200085u32.to_le_bytes()); // flags
    out.extend_from_slice(&0u32.to_le_bytes());     // reserved

    let cmds_start = out.len();

    // LC_SEGMENT_64 for __TEXT
    let seg_size: u32 = 72 + 80; // segment cmd + 1 section header
    out.extend_from_slice(&LC_SEGMENT_64.to_le_bytes());
    out.extend_from_slice(&seg_size.to_le_bytes());
    let mut seg_name = [0u8; 16];
    seg_name[..6].copy_from_slice(b"__TEXT");
    out.extend_from_slice(&seg_name);
    out.extend_from_slice(&0u64.to_le_bytes());                         // vmaddr
    out.extend_from_slice(&((page_size + code_size) as u64).to_le_bytes()); // vmsize
    out.extend_from_slice(&0u64.to_le_bytes());                         // fileoff
    out.extend_from_slice(&((page_size + code_size) as u64).to_le_bytes()); // filesize
    out.extend_from_slice(&5u32.to_le_bytes());  // maxprot (r-x)
    out.extend_from_slice(&5u32.to_le_bytes());  // initprot (r-x)
    out.extend_from_slice(&1u32.to_le_bytes());  // nsects
    out.extend_from_slice(&0u32.to_le_bytes());  // flags

    // Section header: __text
    let mut sectname = [0u8; 16];
    sectname[..6].copy_from_slice(b"__text");
    out.extend_from_slice(&sectname);
    out.extend_from_slice(&seg_name); // segname
    out.extend_from_slice(&(page_size as u64).to_le_bytes()); // addr
    out.extend_from_slice(&(code_size as u64).to_le_bytes()); // size
    out.extend_from_slice(&(page_size as u32).to_le_bytes()); // offset
    out.extend_from_slice(&2u32.to_le_bytes());  // align (2^2 = 4)
    out.extend_from_slice(&0u32.to_le_bytes());  // reloff
    out.extend_from_slice(&0u32.to_le_bytes());  // nreloc
    let sect_flags = S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
    out.extend_from_slice(&sect_flags.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());  // reserved1
    out.extend_from_slice(&0u32.to_le_bytes());  // reserved2
    out.extend_from_slice(&0u32.to_le_bytes());  // reserved3

    // LC_MAIN
    let lc_main: u32 = 0x80000028;
    let main_size: u32 = 24;
    out.extend_from_slice(&lc_main.to_le_bytes());
    out.extend_from_slice(&main_size.to_le_bytes());
    out.extend_from_slice(&(page_size as u64).to_le_bytes());  // entryoff
    out.extend_from_slice(&0u64.to_le_bytes());                // stacksize

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

fn align_to(val: usize, align: usize) -> usize {
    (val + align - 1) & !(align - 1)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_macho_magic() {
        let module = crate::ir::IrModule::new("test");
        let code = vec![0xd6, 0x5f, 0x03, 0xc0]; // ret
        let obj = wrap(code, &module);
        // Check Mach-O magic
        assert_eq!(&obj[0..4], &MH_MAGIC_64.to_le_bytes());
        // Check it's an object file
        let filetype = u32::from_le_bytes([obj[12], obj[13], obj[14], obj[15]]);
        assert_eq!(filetype, MH_OBJECT);
    }

    #[test]
    fn test_macho_executable_magic() {
        let module = crate::ir::IrModule::new("test");
        let code = vec![0xd6, 0x5f, 0x03, 0xc0]; // ret
        let exe = wrap_executable(code, &module);
        assert_eq!(&exe[0..4], &MH_MAGIC_64.to_le_bytes());
        let filetype = u32::from_le_bytes([exe[12], exe[13], exe[14], exe[15]]);
        assert_eq!(filetype, 2); // MH_EXECUTE
    }
}
