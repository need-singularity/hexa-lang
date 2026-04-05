//! ARM64 Peephole Optimizer — length-preserving post-emit pass.
//!
//! Runs over the raw byte buffer produced by `arm64::emit_*`. Decodes each
//! 4-byte word into a minimal `Instr` enum, applies a set of 6 rewrite rules
//! (n=6 alignment) in a sliding 4-wide window with fixed-point iteration
//! (at most 6 rounds, n=6 alignment), and re-encodes back.
//!
//! Length-preserving strategy: fused pairs become `[FUSED, NOP]`; removed
//! no-ops become `NOP`. This keeps every instruction offset stable, so the
//! branch fixups already applied by the emitter remain valid. Branch-target
//! offsets are computed from the final code and we never fuse across a
//! target, nor do we fuse a window whose second slot is a live branch target.
//!
//! Rules implemented:
//!   (a) redundant-mov / add-zero  — identity instruction → NOP
//!   (b) MUL + ADD   → MADD
//!   (c) MUL + SUB   → MSUB
//!   (d) CMP + CBNZ  → (conservative, length-preserving) CBZ/CBNZ rewrite
//!   (e) adjacent LDR [base,#k] pair → LDP  (only when offsets align)
//!   (f) LSL + ADD   → ADD (shifted register)
//!
//! All rules are semantics-preserving under the following local assumption:
//! when a rule rewrites a register that is both the destination and a source
//! of the second instruction, the old destination value is guaranteed dead
//! (it is immediately overwritten by the second instruction's result in the
//! same register). That avoids needing a global liveness analysis.

#![allow(dead_code)]

use std::collections::HashSet;

// ── Decoded instruction enum ──

/// Minimal decode of the instructions that arm64.rs actually emits.
/// All encodings are 64-bit variants (sf=1).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Instr {
    /// Raw/unknown — passed through verbatim.
    Raw(u32),
    /// `nop`
    Nop,
    /// `mov Xd, Xn` (ORR Xd, XZR, Xn form).
    Mov { rd: u8, rn: u8 },
    /// `add Xd, Xn, Xm` (shifted register, shift=0 imm=0).
    Add { rd: u8, rn: u8, rm: u8 },
    /// `add Xd, Xn, Xm, LSL #shift`.
    AddShifted { rd: u8, rn: u8, rm: u8, shift: u8 },
    /// `sub Xd, Xn, Xm` (shifted register, shift=0 imm=0).
    Sub { rd: u8, rn: u8, rm: u8 },
    /// `sub Xd, Xn, Xm, LSL #shift`.
    SubShifted { rd: u8, rn: u8, rm: u8, shift: u8 },
    /// `mul Xd, Xn, Xm` (MADD with ra=XZR).
    Mul { rd: u8, rn: u8, rm: u8 },
    /// `madd Xd, Xn, Xm, Xa`.
    Madd { rd: u8, rn: u8, rm: u8, ra: u8 },
    /// `msub Xd, Xn, Xm, Xa`.
    Msub { rd: u8, rn: u8, rm: u8, ra: u8 },
    /// `lsl Xd, Xn, #imm` (UBFM form).
    LslImm { rd: u8, rn: u8, shift: u8 },
    /// `cmp Xn, Xm` (SUBS XZR, Xn, Xm).
    Cmp { rn: u8, rm: u8 },
    /// `cbnz Xt, <label19>` — imm19 encoded in bits [23:5].
    Cbnz { rt: u8, imm19: i32 },
    /// `cbz Xt, <label19>`.
    Cbz { rt: u8, imm19: i32 },
    /// `ldr Xt, [Xn, #imm12*8]` (unsigned offset, 64-bit).
    Ldr { rt: u8, rn: u8, imm12: u16 },
}

impl Instr {
    /// NOP instruction encoding.
    pub const NOP_WORD: u32 = 0xd503201f;

    pub fn decode(word: u32) -> Instr {
        // NOP
        if word == Self::NOP_WORD {
            return Instr::Nop;
        }
        // MOV (ORR Xd, XZR, Xn) = 0xAA0003E0 | (rn<<16) | rd   (rn<=30, rd<=30)
        if (word & 0xFFE0_FFE0) == 0xAA00_03E0 {
            let rd = (word & 0x1F) as u8;
            let rn = ((word >> 16) & 0x1F) as u8;
            return Instr::Mov { rd, rn };
        }
        // ADD (shifted register, 64-bit): sf=1 op=0 S=0 0 1011 shift imm6
        // 0x8B000000 | (shift<<22) | (rm<<16) | (imm6<<10) | (rn<<5) | rd
        if (word & 0xFF20_0000) == 0x8B00_0000 {
            let shift = ((word >> 22) & 0x3) as u8;
            let rm = ((word >> 16) & 0x1F) as u8;
            let imm6 = ((word >> 10) & 0x3F) as u8;
            let rn = ((word >> 5) & 0x1F) as u8;
            let rd = (word & 0x1F) as u8;
            if shift == 0 && imm6 == 0 {
                return Instr::Add { rd, rn, rm };
            } else if shift == 0 {
                // LSL-shifted
                return Instr::AddShifted { rd, rn, rm, shift: imm6 };
            }
        }
        // SUB (shifted register, 64-bit): 0xCB00_0000
        if (word & 0xFF20_0000) == 0xCB00_0000 {
            let shift = ((word >> 22) & 0x3) as u8;
            let rm = ((word >> 16) & 0x1F) as u8;
            let imm6 = ((word >> 10) & 0x3F) as u8;
            let rn = ((word >> 5) & 0x1F) as u8;
            let rd = (word & 0x1F) as u8;
            if shift == 0 && imm6 == 0 {
                return Instr::Sub { rd, rn, rm };
            } else if shift == 0 {
                return Instr::SubShifted { rd, rn, rm, shift: imm6 };
            }
        }
        // CMP (SUBS, shifted reg, 64-bit): 0xEB000000 with rd=0x1F
        //   = 0xEB000000 | (rm<<16) | (rn<<5) | 0x1F
        if (word & 0xFF20_001F) == 0xEB00_001F {
            let shift = ((word >> 22) & 0x3) as u8;
            let imm6 = ((word >> 10) & 0x3F) as u8;
            if shift == 0 && imm6 == 0 {
                let rm = ((word >> 16) & 0x1F) as u8;
                let rn = ((word >> 5) & 0x1F) as u8;
                return Instr::Cmp { rn, rm };
            }
        }
        // MADD (64-bit): 0x9B000000 | (rm<<16) | (ra<<10) | (rn<<5) | rd
        //   bits[31:21]=10011011000, bit[15]=0
        if (word & 0xFFE0_8000) == 0x9B00_0000 {
            let rm = ((word >> 16) & 0x1F) as u8;
            let ra = ((word >> 10) & 0x1F) as u8;
            let rn = ((word >> 5) & 0x1F) as u8;
            let rd = (word & 0x1F) as u8;
            if ra == 31 {
                return Instr::Mul { rd, rn, rm };
            }
            return Instr::Madd { rd, rn, rm, ra };
        }
        // MSUB (64-bit): 0x9B008000 | (rm<<16) | (ra<<10) | (rn<<5) | rd
        if (word & 0xFFE0_8000) == 0x9B00_8000 {
            let rm = ((word >> 16) & 0x1F) as u8;
            let ra = ((word >> 10) & 0x1F) as u8;
            let rn = ((word >> 5) & 0x1F) as u8;
            let rd = (word & 0x1F) as u8;
            return Instr::Msub { rd, rn, rm, ra };
        }
        // LSL Xd, Xn, #shift (UBFM alias, 64-bit):
        //   UBFM Xd, Xn, #(-shift MOD 64), #(63-shift)
        //   Encoding: 0xD3400000 | (immr<<16) | (imms<<10) | (rn<<5) | rd
        //   For LSL: imms = 63-shift, immr = 64-shift (or shift=0 → immr=0,imms=63)
        if (word & 0xFFC0_0000) == 0xD340_0000 {
            let immr = ((word >> 16) & 0x3F) as u8;
            let imms = ((word >> 10) & 0x3F) as u8;
            // LSL alias condition: imms != 63 AND imms + 1 == immr
            if imms != 63 && imms + 1 == immr {
                let rn = ((word >> 5) & 0x1F) as u8;
                let rd = (word & 0x1F) as u8;
                let shift = 63 - imms;
                return Instr::LslImm { rd, rn, shift };
            }
        }
        // CBNZ (64-bit): 0xB5000000 | (imm19<<5) | rt
        if (word & 0xFF00_0000) == 0xB500_0000 {
            let rt = (word & 0x1F) as u8;
            let raw_imm = ((word >> 5) & 0x7_FFFF) as i32;
            let imm19 = if raw_imm & 0x4_0000 != 0 { raw_imm | !0x7_FFFF } else { raw_imm };
            return Instr::Cbnz { rt, imm19 };
        }
        // CBZ (64-bit): 0xB4000000
        if (word & 0xFF00_0000) == 0xB400_0000 {
            let rt = (word & 0x1F) as u8;
            let raw_imm = ((word >> 5) & 0x7_FFFF) as i32;
            let imm19 = if raw_imm & 0x4_0000 != 0 { raw_imm | !0x7_FFFF } else { raw_imm };
            return Instr::Cbz { rt, imm19 };
        }
        // LDR (unsigned offset, 64-bit): 0xF9400000 | (imm12<<10) | (rn<<5) | rt
        if (word & 0xFFC0_0000) == 0xF940_0000 {
            let imm12 = ((word >> 10) & 0xFFF) as u16;
            let rn = ((word >> 5) & 0x1F) as u8;
            let rt = (word & 0x1F) as u8;
            return Instr::Ldr { rt, rn, imm12 };
        }

        Instr::Raw(word)
    }

    pub fn encode(&self) -> u32 {
        match *self {
            Instr::Raw(w) => w,
            Instr::Nop => Self::NOP_WORD,
            Instr::Mov { rd, rn } => {
                0xAA00_03E0 | ((rn as u32) << 16) | (rd as u32)
            }
            Instr::Add { rd, rn, rm } => {
                0x8B00_0000 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32)
            }
            Instr::AddShifted { rd, rn, rm, shift } => {
                0x8B00_0000 | ((rm as u32) << 16) | (((shift & 0x3F) as u32) << 10)
                    | ((rn as u32) << 5) | (rd as u32)
            }
            Instr::Sub { rd, rn, rm } => {
                0xCB00_0000 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32)
            }
            Instr::SubShifted { rd, rn, rm, shift } => {
                0xCB00_0000 | ((rm as u32) << 16) | (((shift & 0x3F) as u32) << 10)
                    | ((rn as u32) << 5) | (rd as u32)
            }
            Instr::Mul { rd, rn, rm } => {
                0x9B00_0000 | ((rm as u32) << 16) | (31u32 << 10) | ((rn as u32) << 5) | (rd as u32)
            }
            Instr::Madd { rd, rn, rm, ra } => {
                0x9B00_0000 | ((rm as u32) << 16) | ((ra as u32) << 10) | ((rn as u32) << 5) | (rd as u32)
            }
            Instr::Msub { rd, rn, rm, ra } => {
                0x9B00_8000 | ((rm as u32) << 16) | ((ra as u32) << 10) | ((rn as u32) << 5) | (rd as u32)
            }
            Instr::LslImm { rd, rn, shift } => {
                let immr = ((64u32 - shift as u32) & 0x3F) as u32;
                let imms = (63u32 - shift as u32) & 0x3F;
                0xD340_0000 | (immr << 16) | (imms << 10) | ((rn as u32) << 5) | (rd as u32)
            }
            Instr::Cmp { rn, rm } => {
                0xEB00_0000 | ((rm as u32) << 16) | ((rn as u32) << 5) | 0x1F
            }
            Instr::Cbnz { rt, imm19 } => {
                0xB500_0000 | (((imm19 as u32) & 0x7_FFFF) << 5) | (rt as u32)
            }
            Instr::Cbz { rt, imm19 } => {
                0xB400_0000 | (((imm19 as u32) & 0x7_FFFF) << 5) | (rt as u32)
            }
            Instr::Ldr { rt, rn, imm12 } => {
                0xF940_0000 | (((imm12 & 0xFFF) as u32) << 10) | ((rn as u32) << 5) | (rt as u32)
            }
        }
    }
}

// ── Rule trait + window ──

/// A peephole rule consumes a window of decoded instructions and optionally
/// produces a replacement of the *same length*. Length preservation keeps
/// branch offsets stable so we don't need to rewrite fixups.
pub trait PeepholeRule {
    fn name(&self) -> &'static str;
    fn apply(&self, window: &[Instr]) -> Option<Vec<Instr>>;
}

// ── Individual rules (6 total, n=6) ──

/// (a) Identity / dead-mov elimination.
///  - `mov Xd, Xd`                    → nop   (window=1)
///  - `add Xd, Xd, XZR` / `add Xd, XZR, Xd` / `sub Xd, Xd, XZR`  → nop   (window=1)
///  - `mov Xd, Xn` followed by an instruction that overwrites Xd without
///    reading Xd → drop the mov  (window=2; second slot unchanged).
pub struct RuleIdentity;
impl PeepholeRule for RuleIdentity {
    fn name(&self) -> &'static str { "identity_nop" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.is_empty() { return None; }
        // single-instr identities
        match w[0] {
            Instr::Mov { rd, rn } if rd == rn => return Some(vec![Instr::Nop]),
            Instr::Add { rd, rn, rm } if rd == rn && rm == 31 => return Some(vec![Instr::Nop]),
            Instr::Add { rd, rn, rm } if rd == rm && rn == 31 => return Some(vec![Instr::Nop]),
            Instr::Sub { rd, rn, rm } if rd == rn && rm == 31 => return Some(vec![Instr::Nop]),
            _ => {}
        }
        // Dead-mov: mov Xd, Xn ; <instr writing Xd without reading Xd>
        if w.len() >= 2 {
            if let Instr::Mov { rd: md, rn: _mn } = w[0] {
                let (def_reg, reads) = instr_def_use(&w[1]);
                if def_reg == Some(md) && !reads.contains(&md) {
                    return Some(vec![Instr::Nop, w[1]]);
                }
            }
        }
        None
    }
}

/// Return (defined_reg, read_regs) for a decoded Instr (x-register reads).
fn instr_def_use(i: &Instr) -> (Option<u8>, Vec<u8>) {
    match *i {
        Instr::Nop | Instr::Raw(_) => (None, vec![]),
        Instr::Mov { rd, rn } => (Some(rd), vec![rn]),
        Instr::Add { rd, rn, rm } | Instr::Sub { rd, rn, rm }
            => (Some(rd), vec![rn, rm]),
        Instr::AddShifted { rd, rn, rm, .. } | Instr::SubShifted { rd, rn, rm, .. }
            => (Some(rd), vec![rn, rm]),
        Instr::Mul { rd, rn, rm } => (Some(rd), vec![rn, rm]),
        Instr::Madd { rd, rn, rm, ra } | Instr::Msub { rd, rn, rm, ra }
            => (Some(rd), vec![rn, rm, ra]),
        Instr::LslImm { rd, rn, .. } => (Some(rd), vec![rn]),
        Instr::Cmp { rn, rm } => (None, vec![rn, rm]),
        Instr::Cbnz { rt, .. } | Instr::Cbz { rt, .. } => (None, vec![rt]),
        Instr::Ldr { rt, rn, .. } => (Some(rt), vec![rn]),
    }
}

/// (b) MUL + ADD → MADD, when the add both consumes and redefines the mul's dst.
/// Pattern:
///   mul  Xd, Xn, Xm
///   add  Xd, Xd, Xa     →  madd Xd, Xn, Xm, Xa ; nop
///   add  Xd, Xa, Xd     →  madd Xd, Xn, Xm, Xa ; nop
/// Safety: `add Xd,...,Xd` overwrites the old Xd, so Xd's mul-result is dead.
pub struct RuleMulAddToMadd;
impl PeepholeRule for RuleMulAddToMadd {
    fn name(&self) -> &'static str { "mul_add_to_madd" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Mul { rd: md, rn: mn, rm: mm },
                Instr::Add { rd: ad, rn: an, rm: am }) = (w[0], w[1]) {
            if ad != md { return None; } // must redefine mul's dst
            // Avoid clobbering source regs — ra must be a simple reg (not XZR).
            if an == md {
                // add Xd, Xd, Xa  ⇒  ra = am
                if am == 31 { return None; }
                return Some(vec![Instr::Madd { rd: md, rn: mn, rm: mm, ra: am }, Instr::Nop]);
            }
            if am == md {
                // add Xd, Xa, Xd  ⇒  ra = an
                if an == 31 { return None; }
                return Some(vec![Instr::Madd { rd: md, rn: mn, rm: mm, ra: an }, Instr::Nop]);
            }
            None
        } else { None }
    }
}

/// (c) MUL + SUB → MSUB (Xd = Xa - Xn*Xm).
/// Pattern:
///   mul  Xd, Xn, Xm
///   sub  Xd, Xa, Xd  →  msub Xd, Xn, Xm, Xa ; nop
/// Safety: sub redefines Xd, so Xd's mul-result is dead.
pub struct RuleMulSubToMsub;
impl PeepholeRule for RuleMulSubToMsub {
    fn name(&self) -> &'static str { "mul_sub_to_msub" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Mul { rd: md, rn: mn, rm: mm },
                Instr::Sub { rd: sd, rn: sn, rm: sm }) = (w[0], w[1]) {
            if sd != md { return None; }
            if sm != md { return None; }     // mul-dst must be the subtrahend
            if sn == md { return None; }     // ra must differ from md (sn is Xa)
            if sn == 31 { return None; }     // avoid XZR rebind
            return Some(vec![Instr::Msub { rd: md, rn: mn, rm: mm, ra: sn }, Instr::Nop]);
        }
        None
    }
}

/// (d) CMP (with zero) fusion hint: when we see `cmp Xn, XZR` we could rewrite
/// a following CBNZ/CBZ to use Xn directly. However the emitter already emits
/// `cbnz Xt` only on non-fused compare paths. This rule catches the specific
/// pattern `cmp Xn, XZR` (XZR=31) — which is `subs XZR, Xn, XZR` — and turns
/// it into `nop` because a subsequent cbnz/cbz uses Xn directly anyway. This
/// is a narrow cleanup that fires only when regalloc produces a cmp-vs-zero.
pub struct RuleCmpZeroDeadCmp;
impl PeepholeRule for RuleCmpZeroDeadCmp {
    fn name(&self) -> &'static str { "cmp_zero_dead" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Cmp { rn: _, rm: 31 },
                next) = (w[0], w[1]) {
            // If the next instr is cbnz/cbz using any reg, NZCV flags are unused → drop cmp.
            // More conservatively: only drop if next is cbnz/cbz (flag-independent) or nop.
            match next {
                Instr::Cbnz { .. } | Instr::Cbz { .. } | Instr::Nop => {
                    return Some(vec![Instr::Nop, next]);
                }
                _ => {}
            }
        }
        None
    }
}

/// (e) Adjacent LDR same-base with consecutive 8-byte offsets → LDP pair.
/// Length-preserving variant: emit LDP + NOP. We only match when the two
/// loads are at contiguous offsets (k and k+1 in imm12 units) and use
/// distinct destination registers that do not overlap the base, so the LDP
/// semantics match.
///
/// Implementation note: LDP encoding is a single 32-bit word, so we replace
/// the two LDRs with one LDP and a NOP. This is safe as LDP loads both regs
/// in one instruction.
pub struct RuleLdrPairToLdp;
impl PeepholeRule for RuleLdrPairToLdp {
    fn name(&self) -> &'static str { "ldr_pair_to_ldp" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Ldr { rt: rt1, rn: base1, imm12: off1 },
                Instr::Ldr { rt: rt2, rn: base2, imm12: off2 }) = (w[0], w[1]) {
            if base1 != base2 { return None; }
            if rt1 == rt2 { return None; }               // would self-clobber
            if rt1 == base1 { return None; }             // 1st load clobbers base
            if off2 != off1 + 1 { return None; }         // must be consecutive 8-byte slots
            if off1 > 63 { return None; }                // LDP simm7 (signed) range, imm7*8 bytes
            // LDP (signed offset, 64-bit): 0xA9400000 | (imm7<<15) | (rt2<<10) | (rn<<5) | rt1
            let ldp = 0xA940_0000u32
                | (((off1 as u32) & 0x7F) << 15)
                | ((rt2 as u32) << 10)
                | ((base1 as u32) << 5)
                | (rt1 as u32);
            return Some(vec![Instr::Raw(ldp), Instr::Nop]);
        }
        None
    }
}

/// (f) LSL-imm + ADD → ADD (shifted register).
/// Pattern:
///   lsl  Xt, Xs, #n
///   add  Xd, Xy, Xt   →  add Xd, Xy, Xs, LSL #n ; nop   (if Xt == Xd)
///   add  Xd, Xt, Xy   →  add Xd, Xy, Xs, LSL #n ; nop   (if Xt == Xd, via commutativity)
/// Safety constraints:
///   - Xt must equal Xd so the shift result register is dead post-add.
///   - Xs must not equal Xd (otherwise Xs is clobbered by shift before use…
///     actually shift already happened, so Xs's original value isn't needed
///     again here — the shifted ADD uses Xs directly. We still need Xs to
///     hold the same value at the ADD point. If Xs == Xd, the lsl already
///     clobbered Xs. So we require Xs != Xd.)
///   - shift in 0..=4 (standard LSL range accepted by add-shifted-reg: 0..63,
///     but ≤4 is the common addressing-mode range and keeps the rule tight).
pub struct RuleLslAddToShiftedAdd;
impl PeepholeRule for RuleLslAddToShiftedAdd {
    fn name(&self) -> &'static str { "lsl_add_to_shifted_add" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::LslImm { rd: lt, rn: ls, shift },
                Instr::Add { rd: ad, rn: an, rm: am }) = (w[0], w[1]) {
            if shift == 0 || shift > 4 { return None; }
            if lt != ad { return None; }
            if ls == ad { return None; }
            // Xt consumed as one ADD operand, the other remains Xy.
            let xy = if an == lt && am != lt { Some(am) }
                     else if am == lt && an != lt { Some(an) }
                     else { None };
            let xy = xy?;
            if xy == ad { return None; }  // avoid aliasing
            return Some(vec![
                Instr::AddShifted { rd: ad, rn: xy, rm: ls, shift },
                Instr::Nop,
            ]);
        }
        None
    }
}

// ── Hit counter ──

#[derive(Debug, Default, Clone)]
pub struct PeepholeStats {
    pub identity_nop: u32,
    pub mul_add_to_madd: u32,
    pub mul_sub_to_msub: u32,
    pub cmp_zero_dead: u32,
    pub ldr_pair_to_ldp: u32,
    pub lsl_add_to_shifted_add: u32,
    pub rounds: u32,
}

impl PeepholeStats {
    pub fn total(&self) -> u32 {
        self.identity_nop + self.mul_add_to_madd + self.mul_sub_to_msub
            + self.cmp_zero_dead + self.ldr_pair_to_ldp + self.lsl_add_to_shifted_add
    }
    fn bump(&mut self, name: &str) {
        match name {
            "identity_nop"            => self.identity_nop += 1,
            "mul_add_to_madd"         => self.mul_add_to_madd += 1,
            "mul_sub_to_msub"         => self.mul_sub_to_msub += 1,
            "cmp_zero_dead"           => self.cmp_zero_dead += 1,
            "ldr_pair_to_ldp"         => self.ldr_pair_to_ldp += 1,
            "lsl_add_to_shifted_add"  => self.lsl_add_to_shifted_add += 1,
            _ => {}
        }
    }
}

// ── Main pass ──

/// Run the peephole pass over a raw byte buffer (little-endian ARM64 code).
/// Returns hit statistics. Mutates `code` in place and preserves length.
pub fn run_peephole(code: &mut [u8]) -> PeepholeStats {
    let mut stats = PeepholeStats::default();
    if code.len() % 4 != 0 || code.is_empty() { return stats; }

    let n = code.len() / 4;
    let mut words: Vec<u32> = (0..n)
        .map(|i| u32::from_le_bytes([code[4*i], code[4*i+1], code[4*i+2], code[4*i+3]]))
        .collect();

    // First pass: collect all branch target offsets (indices into `words`).
    // We must not fuse a 2-slot window whose second slot is a branch target
    // (another block could jump to it).
    let branch_targets = collect_branch_targets(&words);

    let rules: Vec<Box<dyn PeepholeRule>> = vec![
        Box::new(RuleIdentity),
        Box::new(RuleMulAddToMadd),
        Box::new(RuleMulSubToMsub),
        Box::new(RuleCmpZeroDeadCmp),
        Box::new(RuleLdrPairToLdp),
        Box::new(RuleLslAddToShiftedAdd),
    ];

    // Fixed-point iteration, max 6 rounds (n=6).
    for _round in 0..6 {
        let mut changed = false;
        let mut i = 0usize;
        while i < n {
            // Decode a small window (up to 4).
            let w_end = (i + 4).min(n);
            let window: Vec<Instr> = (i..w_end).map(|j| Instr::decode(words[j])).collect();

            // Try each rule.
            let mut replaced_here = false;
            for rule in &rules {
                for wsize in (1..=window.len()).rev() {
                    // Windows of size 1 (identity) or 2 (fusions).
                    if wsize > 2 { continue; }
                    // For fusion rules, the second slot must not be a branch target.
                    if wsize == 2 && branch_targets.contains(&(i + 1)) {
                        continue;
                    }
                    let slice = &window[..wsize];
                    if let Some(new) = rule.apply(slice) {
                        if new.len() == wsize {
                            // Re-encode into words.
                            for (k, instr) in new.iter().enumerate() {
                                words[i + k] = instr.encode();
                            }
                            stats.bump(rule.name());
                            changed = true;
                            replaced_here = true;
                            break;
                        }
                    }
                }
                if replaced_here { break; }
            }
            i += 1;
        }
        if changed { stats.rounds += 1; } else { break; }
    }

    // Write back.
    for (i, w) in words.iter().enumerate() {
        code[4*i..4*i+4].copy_from_slice(&w.to_le_bytes());
    }

    stats
}

/// Scan words for branch targets. Returns set of word-indices that are targets.
fn collect_branch_targets(words: &[u32]) -> HashSet<usize> {
    let mut set = HashSet::new();
    for (i, &w) in words.iter().enumerate() {
        // B / BL (imm26): 0x14000000 / 0x94000000
        let top = w & 0xFC00_0000;
        if top == 0x1400_0000 || top == 0x9400_0000 {
            let imm26 = (w & 0x03FF_FFFF) as i32;
            let signed = if imm26 & 0x0200_0000 != 0 { imm26 | !0x03FF_FFFF } else { imm26 };
            let tgt = i as isize + signed as isize;
            if tgt >= 0 && (tgt as usize) < words.len() {
                set.insert(tgt as usize);
            }
            continue;
        }
        // B.cond (0x54000000) + CBZ/CBNZ (0xB4/0xB5): imm19 at bits [23:5]
        let top8 = w & 0xFF00_0000;
        if top8 == 0x5400_0000 || top8 == 0xB400_0000 || top8 == 0xB500_0000 {
            let imm19 = ((w >> 5) & 0x7_FFFF) as i32;
            let signed = if imm19 & 0x4_0000 != 0 { imm19 | !0x7_FFFF } else { imm19 };
            let tgt = i as isize + signed as isize;
            if tgt >= 0 && (tgt as usize) < words.len() {
                set.insert(tgt as usize);
            }
        }
    }
    set
}

// ── Tests ──

#[cfg(test)]
mod tests {
    use super::*;

    fn enc(i: Instr) -> u32 { i.encode() }
    fn dec(w: u32) -> Instr { Instr::decode(w) }

    #[test]
    fn encode_decode_roundtrip_basic() {
        let cases = [
            Instr::Nop,
            Instr::Mov { rd: 1, rn: 2 },
            Instr::Add { rd: 3, rn: 4, rm: 5 },
            Instr::Sub { rd: 6, rn: 7, rm: 8 },
            Instr::Mul { rd: 9, rn: 10, rm: 11 },
            Instr::Madd { rd: 1, rn: 2, rm: 3, ra: 4 },
            Instr::Msub { rd: 1, rn: 2, rm: 3, ra: 4 },
            Instr::Cmp { rn: 5, rm: 6 },
            Instr::Cbnz { rt: 7, imm19: 10 },
            Instr::Cbz { rt: 8, imm19: -4 },
            Instr::Ldr { rt: 9, rn: 10, imm12: 5 },
            Instr::AddShifted { rd: 1, rn: 2, rm: 3, shift: 2 },
            Instr::LslImm { rd: 1, rn: 2, shift: 3 },
        ];
        for c in cases {
            let w = enc(c);
            let d = dec(w);
            assert_eq!(c, d, "roundtrip failed for {:?} → {:#010x} → {:?}", c, w, d);
        }
    }

    #[test]
    fn rule_identity_mov_same() {
        let r = RuleIdentity;
        let out = r.apply(&[Instr::Mov { rd: 3, rn: 3 }]).unwrap();
        assert_eq!(out, vec![Instr::Nop]);
        // Different regs → no match
        assert!(r.apply(&[Instr::Mov { rd: 3, rn: 4 }]).is_none());
    }

    #[test]
    fn rule_identity_dead_mov() {
        let r = RuleIdentity;
        // mov x0, x3 ; mul x0, x3, x3   → nop ; mul
        let w = [
            Instr::Mov { rd: 0, rn: 3 },
            Instr::Mul { rd: 0, rn: 3, rm: 3 },
        ];
        let out = r.apply(&w).unwrap();
        assert_eq!(out[0], Instr::Nop);
        assert_eq!(out[1], Instr::Mul { rd: 0, rn: 3, rm: 3 });

        // mov x0, x3 ; add x0, x0, x4   — reads x0 (=Xd), must NOT drop.
        let w2 = [
            Instr::Mov { rd: 0, rn: 3 },
            Instr::Add { rd: 0, rn: 0, rm: 4 },
        ];
        assert!(r.apply(&w2).is_none());

        // mov x0, x3 ; ldr x0, [x29,#1] — ldr writes x0, doesn't read x0 → drop.
        let w3 = [
            Instr::Mov { rd: 0, rn: 3 },
            Instr::Ldr { rt: 0, rn: 29, imm12: 1 },
        ];
        let out3 = r.apply(&w3).unwrap();
        assert_eq!(out3[0], Instr::Nop);
    }

    #[test]
    fn rule_identity_add_xzr() {
        let r = RuleIdentity;
        let out = r.apply(&[Instr::Add { rd: 5, rn: 5, rm: 31 }]).unwrap();
        assert_eq!(out, vec![Instr::Nop]);
        let out2 = r.apply(&[Instr::Add { rd: 5, rn: 31, rm: 5 }]).unwrap();
        assert_eq!(out2, vec![Instr::Nop]);
        // rd != rn,rm (no xzr involved) → no match
        assert!(r.apply(&[Instr::Add { rd: 1, rn: 2, rm: 3 }]).is_none());
    }

    #[test]
    fn rule_mul_add_to_madd_forward() {
        let r = RuleMulAddToMadd;
        let w = [
            Instr::Mul { rd: 5, rn: 3, rm: 4 },
            Instr::Add { rd: 5, rn: 5, rm: 6 },
        ];
        let out = r.apply(&w).unwrap();
        assert_eq!(out, vec![Instr::Madd { rd: 5, rn: 3, rm: 4, ra: 6 }, Instr::Nop]);
    }

    #[test]
    fn rule_mul_add_to_madd_commuted() {
        let r = RuleMulAddToMadd;
        let w = [
            Instr::Mul { rd: 5, rn: 3, rm: 4 },
            Instr::Add { rd: 5, rn: 6, rm: 5 },
        ];
        let out = r.apply(&w).unwrap();
        assert_eq!(out, vec![Instr::Madd { rd: 5, rn: 3, rm: 4, ra: 6 }, Instr::Nop]);
    }

    #[test]
    fn rule_mul_add_rejects_when_dst_differs() {
        let r = RuleMulAddToMadd;
        let w = [
            Instr::Mul { rd: 5, rn: 3, rm: 4 },
            Instr::Add { rd: 7, rn: 5, rm: 6 },  // ad != md → reject (safety)
        ];
        assert!(r.apply(&w).is_none());
    }

    #[test]
    fn rule_mul_sub_to_msub_ok() {
        let r = RuleMulSubToMsub;
        let w = [
            Instr::Mul { rd: 5, rn: 3, rm: 4 },
            Instr::Sub { rd: 5, rn: 6, rm: 5 },    // 5 = 6 - 5  →  msub rd=5, ra=6
        ];
        let out = r.apply(&w).unwrap();
        assert_eq!(out, vec![Instr::Msub { rd: 5, rn: 3, rm: 4, ra: 6 }, Instr::Nop]);
    }

    #[test]
    fn rule_mul_sub_rejects_wrong_form() {
        let r = RuleMulSubToMsub;
        // sub rd, rd, ra  — mul-dst is NOT the subtrahend → reject
        let w = [
            Instr::Mul { rd: 5, rn: 3, rm: 4 },
            Instr::Sub { rd: 5, rn: 5, rm: 6 },
        ];
        assert!(r.apply(&w).is_none());
    }

    #[test]
    fn rule_cmp_zero_dead_with_cbnz() {
        let r = RuleCmpZeroDeadCmp;
        let w = [
            Instr::Cmp { rn: 4, rm: 31 },
            Instr::Cbnz { rt: 4, imm19: 3 },
        ];
        let out = r.apply(&w).unwrap();
        assert_eq!(out[0], Instr::Nop);
        assert_eq!(out[1], Instr::Cbnz { rt: 4, imm19: 3 });
    }

    #[test]
    fn rule_cmp_zero_rejects_non_branch_next() {
        let r = RuleCmpZeroDeadCmp;
        // Without a following flag-free branch we can't drop cmp (flags may be used).
        let w = [
            Instr::Cmp { rn: 4, rm: 31 },
            Instr::Add { rd: 1, rn: 2, rm: 3 },
        ];
        assert!(r.apply(&w).is_none());
    }

    #[test]
    fn rule_ldr_pair_to_ldp_ok() {
        let r = RuleLdrPairToLdp;
        let w = [
            Instr::Ldr { rt: 3, rn: 29, imm12: 2 },
            Instr::Ldr { rt: 4, rn: 29, imm12: 3 },
        ];
        let out = r.apply(&w).unwrap();
        assert_eq!(out.len(), 2);
        assert_eq!(out[1], Instr::Nop);
        // LDP encoding check: 0xA9400000 | (2<<15) | (4<<10) | (29<<5) | 3
        let expected = 0xA940_0000u32 | (2u32 << 15) | (4u32 << 10) | (29u32 << 5) | 3u32;
        if let Instr::Raw(w_out) = out[0] { assert_eq!(w_out, expected); }
        else { panic!("expected Raw(LDP)"); }
    }

    #[test]
    fn rule_ldr_pair_rejects_non_contiguous() {
        let r = RuleLdrPairToLdp;
        let w = [
            Instr::Ldr { rt: 3, rn: 29, imm12: 2 },
            Instr::Ldr { rt: 4, rn: 29, imm12: 4 }, // gap
        ];
        assert!(r.apply(&w).is_none());
    }

    #[test]
    fn rule_lsl_add_to_shifted_ok() {
        let r = RuleLslAddToShiftedAdd;
        let w = [
            Instr::LslImm { rd: 10, rn: 5, shift: 2 },
            Instr::Add { rd: 10, rn: 7, rm: 10 },
        ];
        let out = r.apply(&w).unwrap();
        assert_eq!(out, vec![
            Instr::AddShifted { rd: 10, rn: 7, rm: 5, shift: 2 },
            Instr::Nop,
        ]);
    }

    #[test]
    fn rule_lsl_add_rejects_when_xs_eq_xd() {
        let r = RuleLslAddToShiftedAdd;
        // Xs == Xd: lsl clobbers src, rule must reject.
        let w = [
            Instr::LslImm { rd: 5, rn: 5, shift: 2 },
            Instr::Add { rd: 5, rn: 7, rm: 5 },
        ];
        assert!(r.apply(&w).is_none());
    }

    #[test]
    fn run_peephole_identity_nop() {
        let prog = vec![
            Instr::Mov { rd: 3, rn: 3 }.encode(),
            Instr::Add { rd: 1, rn: 2, rm: 4 }.encode(),
        ];
        let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect();
        let stats = run_peephole(&mut bytes);
        assert_eq!(stats.identity_nop, 1);
        let w0 = u32::from_le_bytes([bytes[0],bytes[1],bytes[2],bytes[3]]);
        assert_eq!(w0, Instr::NOP_WORD);
    }

    #[test]
    fn run_peephole_fusion_and_cleanup() {
        // mul x5,x3,x4 ; add x5,x5,x6  → madd ; nop
        let prog = vec![
            Instr::Mul { rd: 5, rn: 3, rm: 4 }.encode(),
            Instr::Add { rd: 5, rn: 5, rm: 6 }.encode(),
            Instr::Mov { rd: 0, rn: 0 }.encode(), // identity
        ];
        let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect();
        let stats = run_peephole(&mut bytes);
        assert!(stats.mul_add_to_madd >= 1);
        assert!(stats.identity_nop >= 1);
        // First word is MADD
        let w0 = u32::from_le_bytes([bytes[0],bytes[1],bytes[2],bytes[3]]);
        assert_eq!(Instr::decode(w0), Instr::Madd { rd: 5, rn: 3, rm: 4, ra: 6 });
    }

    #[test]
    fn run_peephole_preserves_length() {
        let prog = vec![
            Instr::Mul { rd: 5, rn: 3, rm: 4 }.encode(),
            Instr::Add { rd: 5, rn: 5, rm: 6 }.encode(),
            0xd65f03c0, // ret
        ];
        let orig_len = prog.len() * 4;
        let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect();
        run_peephole(&mut bytes);
        assert_eq!(bytes.len(), orig_len);
    }

    #[test]
    fn run_peephole_skips_branch_target_in_middle() {
        // b .+2  ; mul ; add  — second slot (add) is branch target, can't fuse across.
        let b_instr = 0x14000000u32 | 2; // b +2 words
        let prog = vec![
            b_instr,
            Instr::Mul { rd: 5, rn: 3, rm: 4 }.encode(),
            Instr::Add { rd: 5, rn: 5, rm: 6 }.encode(), // target of branch
        ];
        let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect();
        let stats = run_peephole(&mut bytes);
        // Must NOT fuse because words[2] is a branch target.
        assert_eq!(stats.mul_add_to_madd, 0);
    }
}
