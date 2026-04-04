//! ELF binary format wrapper — Linux executables.
//!
//! Wraps raw machine code in ELF64 format.

use crate::ir::IrModule;

/// ELF magic and constants.
const ELF_MAGIC: [u8; 4] = [0x7f, b'E', b'L', b'F'];
const ET_EXEC: u16 = 2;
const EM_X86_64: u16 = 62;
const EM_AARCH64: u16 = 183;

/// Wrap machine code in ELF64 format.
pub fn wrap(code: Vec<u8>, module: &IrModule) -> Vec<u8> {
    let mut out = Vec::new();

    let ehdr_size: u64 = 64;
    let phdr_size: u64 = 56;
    let header_total = ehdr_size + phdr_size;
    let code_offset = 0x1000u64; // page-aligned
    let load_addr = 0x400000u64; // standard Linux load address
    let entry = load_addr + code_offset;

    // ── ELF Header (64 bytes) ──
    out.extend_from_slice(&ELF_MAGIC);
    out.push(2); // EI_CLASS: ELFCLASS64
    out.push(1); // EI_DATA: ELFDATA2LSB
    out.push(1); // EI_VERSION: EV_CURRENT
    out.push(0); // EI_OSABI: ELFOSABI_NONE
    out.extend_from_slice(&[0; 8]); // EI_ABIVERSION + padding

    out.extend_from_slice(&ET_EXEC.to_le_bytes()); // e_type

    #[cfg(target_arch = "aarch64")]
    out.extend_from_slice(&EM_AARCH64.to_le_bytes());
    #[cfg(not(target_arch = "aarch64"))]
    out.extend_from_slice(&EM_X86_64.to_le_bytes());

    out.extend_from_slice(&1u32.to_le_bytes());           // e_version
    out.extend_from_slice(&entry.to_le_bytes());           // e_entry
    out.extend_from_slice(&ehdr_size.to_le_bytes());       // e_phoff
    out.extend_from_slice(&0u64.to_le_bytes());            // e_shoff (no sections)
    out.extend_from_slice(&0u32.to_le_bytes());            // e_flags
    out.extend_from_slice(&(ehdr_size as u16).to_le_bytes()); // e_ehsize
    out.extend_from_slice(&(phdr_size as u16).to_le_bytes()); // e_phentsize
    out.extend_from_slice(&1u16.to_le_bytes());            // e_phnum
    out.extend_from_slice(&0u16.to_le_bytes());            // e_shentsize
    out.extend_from_slice(&0u16.to_le_bytes());            // e_shnum
    out.extend_from_slice(&0u16.to_le_bytes());            // e_shstrndx

    // ── Program Header (56 bytes) ──
    let pt_load: u32 = 1;
    let pf_rx: u32 = 5; // PF_R | PF_X
    let file_size = code_offset + code.len() as u64;
    let mem_size = file_size;

    out.extend_from_slice(&pt_load.to_le_bytes());         // p_type
    out.extend_from_slice(&pf_rx.to_le_bytes());           // p_flags
    out.extend_from_slice(&0u64.to_le_bytes());            // p_offset
    out.extend_from_slice(&load_addr.to_le_bytes());       // p_vaddr
    out.extend_from_slice(&load_addr.to_le_bytes());       // p_paddr
    out.extend_from_slice(&file_size.to_le_bytes());       // p_filesz
    out.extend_from_slice(&mem_size.to_le_bytes());        // p_memsz
    out.extend_from_slice(&0x1000u64.to_le_bytes());       // p_align

    // Pad to code offset
    while out.len() < code_offset as usize {
        out.push(0);
    }

    // Append code
    out.extend_from_slice(&code);

    out
}
