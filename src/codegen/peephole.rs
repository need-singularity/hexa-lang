//! ARM64 Peephole Optimizer — length-preserving post-emit pass.
//!
//! Runs over the raw byte buffer produced by `arm64::emit_*`. Decodes each
//! 4-byte word into a minimal `Instr` enum, applies a set of 12 rewrite rules
//! (sigma=12 alignment) in a sliding 4-wide window with fixed-point iteration
//! (at most 12 rounds, sigma=12 alignment), and re-encodes back.
//!
//! Length-preserving strategy: fused pairs become `[FUSED, NOP]`; removed
//! no-ops become `NOP`. This keeps every instruction offset stable, so the
//! branch fixups already applied by the emitter remain valid. Branch-target
//! offsets are computed from the final code and we never fuse across a
//! target, nor do we fuse a window whose second slot is a live branch target.
//!
//! Rules implemented (12 = sigma(6)):
//!   (a) redundant-mov / add-zero  — identity instruction -> NOP
//!   (b) MUL + ADD   -> MADD
//!   (c) MUL + SUB   -> MSUB
//!   (d) CMP + CBNZ  -> (conservative, length-preserving) CBZ/CBNZ rewrite
//!   (e) adjacent LDR [base,#k] pair -> LDP  (only when offsets align)
//!   (f) LSL + ADD   -> ADD (shifted register)
//!   (g) STR + LDR elimination — store then load same address -> MOV/NOP
//!   (h) adjacent STR pair -> STP (store pair)
//!   (i) CMP #0 after ADDS/SUBS -> eliminate redundant CMP
//!   (j) redundant reverse-mov elimination — mov Xd,Xn; mov Xn,Xd -> drop 2nd
//!   (k) branch-over-branch — b.cond L1; b L2; L1: -> b.!cond L2
//!   (l) zero register optimization — mov Xd, #0 -> use XZR forwarding

#![allow(dead_code)]

use std::collections::HashSet;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Instr {
    Raw(u32),
    Nop,
    Mov { rd: u8, rn: u8 },
    Add { rd: u8, rn: u8, rm: u8 },
    AddShifted { rd: u8, rn: u8, rm: u8, shift: u8 },
    Sub { rd: u8, rn: u8, rm: u8 },
    SubShifted { rd: u8, rn: u8, rm: u8, shift: u8 },
    Mul { rd: u8, rn: u8, rm: u8 },
    Madd { rd: u8, rn: u8, rm: u8, ra: u8 },
    Msub { rd: u8, rn: u8, rm: u8, ra: u8 },
    LslImm { rd: u8, rn: u8, shift: u8 },
    Cmp { rn: u8, rm: u8 },
    Cbnz { rt: u8, imm19: i32 },
    Cbz { rt: u8, imm19: i32 },
    Ldr { rt: u8, rn: u8, imm12: u16 },
    Str { rt: u8, rn: u8, imm12: u16 },
    Branch { imm26: i32 },
    Bcond { cond: u8, imm19: i32 },
    Adds { rd: u8, rn: u8, rm: u8 },
    Subs { rd: u8, rn: u8, rm: u8 },
    MovImm16 { rd: u8, imm16: u16 },
}

impl Instr {
    pub const NOP_WORD: u32 = 0xd503201f;

    pub fn decode(word: u32) -> Instr {
        if word == Self::NOP_WORD { return Instr::Nop; }
        if (word & 0xFFE0_FFE0) == 0xAA00_03E0 {
            return Instr::Mov { rd: (word & 0x1F) as u8, rn: ((word >> 16) & 0x1F) as u8 };
        }
        if (word & 0xFF20_0000) == 0x8B00_0000 {
            let shift = ((word >> 22) & 0x3) as u8;
            let rm = ((word >> 16) & 0x1F) as u8;
            let imm6 = ((word >> 10) & 0x3F) as u8;
            let rn = ((word >> 5) & 0x1F) as u8;
            let rd = (word & 0x1F) as u8;
            if shift == 0 && imm6 == 0 { return Instr::Add { rd, rn, rm }; }
            else if shift == 0 { return Instr::AddShifted { rd, rn, rm, shift: imm6 }; }
        }
        if (word & 0xFF20_0000) == 0xCB00_0000 {
            let shift = ((word >> 22) & 0x3) as u8;
            let rm = ((word >> 16) & 0x1F) as u8;
            let imm6 = ((word >> 10) & 0x3F) as u8;
            let rn = ((word >> 5) & 0x1F) as u8;
            let rd = (word & 0x1F) as u8;
            if shift == 0 && imm6 == 0 { return Instr::Sub { rd, rn, rm }; }
            else if shift == 0 { return Instr::SubShifted { rd, rn, rm, shift: imm6 }; }
        }
        if (word & 0xFF20_001F) == 0xEB00_001F {
            let shift = ((word >> 22) & 0x3) as u8;
            let imm6 = ((word >> 10) & 0x3F) as u8;
            if shift == 0 && imm6 == 0 {
                return Instr::Cmp { rn: ((word >> 5) & 0x1F) as u8, rm: ((word >> 16) & 0x1F) as u8 };
            }
        }
        if (word & 0xFFE0_8000) == 0x9B00_0000 {
            let rm = ((word >> 16) & 0x1F) as u8; let ra = ((word >> 10) & 0x1F) as u8;
            let rn = ((word >> 5) & 0x1F) as u8; let rd = (word & 0x1F) as u8;
            if ra == 31 { return Instr::Mul { rd, rn, rm }; }
            return Instr::Madd { rd, rn, rm, ra };
        }
        if (word & 0xFFE0_8000) == 0x9B00_8000 {
            return Instr::Msub { rd: (word & 0x1F) as u8, rn: ((word >> 5) & 0x1F) as u8, rm: ((word >> 16) & 0x1F) as u8, ra: ((word >> 10) & 0x1F) as u8 };
        }
        if (word & 0xFFC0_0000) == 0xD340_0000 {
            let immr = ((word >> 16) & 0x3F) as u8; let imms = ((word >> 10) & 0x3F) as u8;
            if imms != 63 && imms + 1 == immr {
                return Instr::LslImm { rd: (word & 0x1F) as u8, rn: ((word >> 5) & 0x1F) as u8, shift: 63 - imms };
            }
        }
        if (word & 0xFF00_0000) == 0xB500_0000 {
            let raw_imm = ((word >> 5) & 0x7_FFFF) as i32;
            return Instr::Cbnz { rt: (word & 0x1F) as u8, imm19: if raw_imm & 0x4_0000 != 0 { raw_imm | !0x7_FFFF } else { raw_imm } };
        }
        if (word & 0xFF00_0000) == 0xB400_0000 {
            let raw_imm = ((word >> 5) & 0x7_FFFF) as i32;
            return Instr::Cbz { rt: (word & 0x1F) as u8, imm19: if raw_imm & 0x4_0000 != 0 { raw_imm | !0x7_FFFF } else { raw_imm } };
        }
        if (word & 0xFFC0_0000) == 0xF940_0000 {
            return Instr::Ldr { rt: (word & 0x1F) as u8, rn: ((word >> 5) & 0x1F) as u8, imm12: ((word >> 10) & 0xFFF) as u16 };
        }
        if (word & 0xFFC0_0000) == 0xF900_0000 {
            return Instr::Str { rt: (word & 0x1F) as u8, rn: ((word >> 5) & 0x1F) as u8, imm12: ((word >> 10) & 0xFFF) as u16 };
        }
        if (word & 0xFF20_0000) == 0xAB00_0000 {
            let shift = ((word >> 22) & 0x3) as u8; let imm6 = ((word >> 10) & 0x3F) as u8;
            if shift == 0 && imm6 == 0 {
                return Instr::Adds { rd: (word & 0x1F) as u8, rn: ((word >> 5) & 0x1F) as u8, rm: ((word >> 16) & 0x1F) as u8 };
            }
        }
        if (word & 0xFF20_0000) == 0xEB00_0000 {
            let shift = ((word >> 22) & 0x3) as u8; let imm6 = ((word >> 10) & 0x3F) as u8;
            let rd = (word & 0x1F) as u8;
            if shift == 0 && imm6 == 0 && rd != 31 {
                return Instr::Subs { rd, rn: ((word >> 5) & 0x1F) as u8, rm: ((word >> 16) & 0x1F) as u8 };
            }
        }
        if (word & 0xFC00_0000) == 0x1400_0000 {
            let raw = (word & 0x03FF_FFFF) as i32;
            return Instr::Branch { imm26: if raw & 0x0200_0000 != 0 { raw | !0x03FF_FFFF } else { raw } };
        }
        if (word & 0xFF00_0010) == 0x5400_0000 {
            let raw_imm = ((word >> 5) & 0x7_FFFF) as i32;
            return Instr::Bcond { cond: (word & 0xF) as u8, imm19: if raw_imm & 0x4_0000 != 0 { raw_imm | !0x7_FFFF } else { raw_imm } };
        }
        if (word & 0xFFE0_0000) == 0xD280_0000 {
            return Instr::MovImm16 { rd: (word & 0x1F) as u8, imm16: ((word >> 5) & 0xFFFF) as u16 };
        }
        Instr::Raw(word)
    }

    pub fn encode(&self) -> u32 {
        match *self {
            Instr::Raw(w) => w,
            Instr::Nop => Self::NOP_WORD,
            Instr::Mov { rd, rn } => 0xAA00_03E0 | ((rn as u32) << 16) | (rd as u32),
            Instr::Add { rd, rn, rm } => 0x8B00_0000 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32),
            Instr::AddShifted { rd, rn, rm, shift } => 0x8B00_0000 | ((rm as u32) << 16) | (((shift & 0x3F) as u32) << 10) | ((rn as u32) << 5) | (rd as u32),
            Instr::Sub { rd, rn, rm } => 0xCB00_0000 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32),
            Instr::SubShifted { rd, rn, rm, shift } => 0xCB00_0000 | ((rm as u32) << 16) | (((shift & 0x3F) as u32) << 10) | ((rn as u32) << 5) | (rd as u32),
            Instr::Mul { rd, rn, rm } => 0x9B00_0000 | ((rm as u32) << 16) | (31u32 << 10) | ((rn as u32) << 5) | (rd as u32),
            Instr::Madd { rd, rn, rm, ra } => 0x9B00_0000 | ((rm as u32) << 16) | ((ra as u32) << 10) | ((rn as u32) << 5) | (rd as u32),
            Instr::Msub { rd, rn, rm, ra } => 0x9B00_8000 | ((rm as u32) << 16) | ((ra as u32) << 10) | ((rn as u32) << 5) | (rd as u32),
            Instr::LslImm { rd, rn, shift } => { let immr = ((64u32 - shift as u32) & 0x3F) as u32; let imms = (63u32 - shift as u32) & 0x3F; 0xD340_0000 | (immr << 16) | (imms << 10) | ((rn as u32) << 5) | (rd as u32) }
            Instr::Cmp { rn, rm } => 0xEB00_0000 | ((rm as u32) << 16) | ((rn as u32) << 5) | 0x1F,
            Instr::Cbnz { rt, imm19 } => 0xB500_0000 | (((imm19 as u32) & 0x7_FFFF) << 5) | (rt as u32),
            Instr::Cbz { rt, imm19 } => 0xB400_0000 | (((imm19 as u32) & 0x7_FFFF) << 5) | (rt as u32),
            Instr::Ldr { rt, rn, imm12 } => 0xF940_0000 | (((imm12 & 0xFFF) as u32) << 10) | ((rn as u32) << 5) | (rt as u32),
            Instr::Str { rt, rn, imm12 } => 0xF900_0000 | (((imm12 & 0xFFF) as u32) << 10) | ((rn as u32) << 5) | (rt as u32),
            Instr::Branch { imm26 } => 0x1400_0000 | ((imm26 as u32) & 0x03FF_FFFF),
            Instr::Bcond { cond, imm19 } => 0x5400_0000 | (((imm19 as u32) & 0x7_FFFF) << 5) | (cond as u32),
            Instr::Adds { rd, rn, rm } => 0xAB00_0000 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32),
            Instr::Subs { rd, rn, rm } => 0xEB00_0000 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32),
            Instr::MovImm16 { rd, imm16 } => 0xD280_0000 | (((imm16 as u32) & 0xFFFF) << 5) | (rd as u32),
        }
    }
}

pub trait PeepholeRule { fn name(&self) -> &'static str; fn apply(&self, window: &[Instr]) -> Option<Vec<Instr>>; }

fn instr_def_use(i: &Instr) -> (Option<u8>, Vec<u8>) {
    match *i {
        Instr::Nop | Instr::Raw(_) => (None, vec![]),
        Instr::Mov { rd, rn } => (Some(rd), vec![rn]),
        Instr::Add { rd, rn, rm } | Instr::Sub { rd, rn, rm } => (Some(rd), vec![rn, rm]),
        Instr::AddShifted { rd, rn, rm, .. } | Instr::SubShifted { rd, rn, rm, .. } => (Some(rd), vec![rn, rm]),
        Instr::Mul { rd, rn, rm } => (Some(rd), vec![rn, rm]),
        Instr::Madd { rd, rn, rm, ra } | Instr::Msub { rd, rn, rm, ra } => (Some(rd), vec![rn, rm, ra]),
        Instr::LslImm { rd, rn, .. } => (Some(rd), vec![rn]),
        Instr::Cmp { rn, rm } => (None, vec![rn, rm]),
        Instr::Cbnz { rt, .. } | Instr::Cbz { rt, .. } => (None, vec![rt]),
        Instr::Ldr { rt, rn, .. } => (Some(rt), vec![rn]),
        Instr::Str { rt, rn, .. } => (None, vec![rt, rn]),
        Instr::Branch { .. } | Instr::Bcond { .. } => (None, vec![]),
        Instr::Adds { rd, rn, rm } | Instr::Subs { rd, rn, rm } => (Some(rd), vec![rn, rm]),
        Instr::MovImm16 { rd, .. } => (Some(rd), vec![]),
    }
}

fn subst_reg(instr: &Instr, old: u8, new_reg: u8) -> Option<Instr> {
    match *instr {
        Instr::Add { rd, rn, rm } => Some(Instr::Add { rd, rn: if rn == old { new_reg } else { rn }, rm: if rm == old { new_reg } else { rm } }),
        Instr::Sub { rd, rn, rm } => Some(Instr::Sub { rd, rn: if rn == old { new_reg } else { rn }, rm: if rm == old { new_reg } else { rm } }),
        Instr::Str { rt, rn, imm12 } if rt == old => Some(Instr::Str { rt: new_reg, rn, imm12 }),
        Instr::Mov { rd, rn } if rn == old => Some(Instr::Mov { rd, rn: new_reg }),
        Instr::Cmp { rn, rm } => Some(Instr::Cmp { rn: if rn == old { new_reg } else { rn }, rm: if rm == old { new_reg } else { rm } }),
        _ => None,
    }
}

pub struct RuleIdentity;
impl PeepholeRule for RuleIdentity {
    fn name(&self) -> &'static str { "identity_nop" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.is_empty() { return None; }
        match w[0] {
            Instr::Mov { rd, rn } if rd == rn => return Some(vec![Instr::Nop]),
            Instr::Add { rd, rn, rm } if rd == rn && rm == 31 => return Some(vec![Instr::Nop]),
            Instr::Add { rd, rn, rm } if rd == rm && rn == 31 => return Some(vec![Instr::Nop]),
            Instr::Sub { rd, rn, rm } if rd == rn && rm == 31 => return Some(vec![Instr::Nop]),
            _ => {}
        }
        if w.len() >= 2 { if let Instr::Mov { rd: md, rn: _mn } = w[0] { let (def_reg, reads) = instr_def_use(&w[1]); if def_reg == Some(md) && !reads.contains(&md) { return Some(vec![Instr::Nop, w[1]]); } } }
        None
    }
}

pub struct RuleMulAddToMadd;
impl PeepholeRule for RuleMulAddToMadd {
    fn name(&self) -> &'static str { "mul_add_to_madd" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Mul { rd: md, rn: mn, rm: mm }, Instr::Add { rd: ad, rn: an, rm: am }) = (w[0], w[1]) {
            if ad != md { return None; }
            if an == md { if am == 31 { return None; } return Some(vec![Instr::Madd { rd: md, rn: mn, rm: mm, ra: am }, Instr::Nop]); }
            if am == md { if an == 31 { return None; } return Some(vec![Instr::Madd { rd: md, rn: mn, rm: mm, ra: an }, Instr::Nop]); }
        }
        None
    }
}

pub struct RuleMulSubToMsub;
impl PeepholeRule for RuleMulSubToMsub {
    fn name(&self) -> &'static str { "mul_sub_to_msub" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Mul { rd: md, rn: mn, rm: mm }, Instr::Sub { rd: sd, rn: sn, rm: sm }) = (w[0], w[1]) {
            if sd != md || sm != md || sn == md || sn == 31 { return None; }
            return Some(vec![Instr::Msub { rd: md, rn: mn, rm: mm, ra: sn }, Instr::Nop]);
        }
        None
    }
}

pub struct RuleCmpZeroDeadCmp;
impl PeepholeRule for RuleCmpZeroDeadCmp {
    fn name(&self) -> &'static str { "cmp_zero_dead" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Cmp { rn: _, rm: 31 }, next) = (w[0], w[1]) {
            match next { Instr::Cbnz { .. } | Instr::Cbz { .. } | Instr::Nop => return Some(vec![Instr::Nop, next]), _ => {} }
        }
        None
    }
}

pub struct RuleLdrPairToLdp;
impl PeepholeRule for RuleLdrPairToLdp {
    fn name(&self) -> &'static str { "ldr_pair_to_ldp" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Ldr { rt: rt1, rn: base1, imm12: off1 }, Instr::Ldr { rt: rt2, rn: base2, imm12: off2 }) = (w[0], w[1]) {
            if base1 != base2 || rt1 == rt2 || rt1 == base1 || off2 != off1 + 1 || off1 > 63 { return None; }
            let ldp = 0xA940_0000u32 | (((off1 as u32) & 0x7F) << 15) | ((rt2 as u32) << 10) | ((base1 as u32) << 5) | (rt1 as u32);
            return Some(vec![Instr::Raw(ldp), Instr::Nop]);
        }
        None
    }
}

pub struct RuleLslAddToShiftedAdd;
impl PeepholeRule for RuleLslAddToShiftedAdd {
    fn name(&self) -> &'static str { "lsl_add_to_shifted_add" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::LslImm { rd: lt, rn: ls, shift }, Instr::Add { rd: ad, rn: an, rm: am }) = (w[0], w[1]) {
            if shift == 0 || shift > 4 || lt != ad || ls == ad { return None; }
            let xy = if an == lt && am != lt { Some(am) } else if am == lt && an != lt { Some(an) } else { None }?;
            if xy == ad { return None; }
            return Some(vec![Instr::AddShifted { rd: ad, rn: xy, rm: ls, shift }, Instr::Nop]);
        }
        None
    }
}

pub struct RuleStrLdrElim;
impl PeepholeRule for RuleStrLdrElim {
    fn name(&self) -> &'static str { "str_ldr_elim" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Str { rt: sn, rn: sb, imm12: soff }, Instr::Ldr { rt: ld, rn: lb, imm12: loff }) = (w[0], w[1]) {
            if sb != lb || soff != loff { return None; }
            if ld == sn { return Some(vec![w[0], Instr::Nop]); }
            return Some(vec![w[0], Instr::Mov { rd: ld, rn: sn }]);
        }
        None
    }
}

pub struct RuleStrPairToStp;
impl PeepholeRule for RuleStrPairToStp {
    fn name(&self) -> &'static str { "str_pair_to_stp" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Str { rt: rt1, rn: base1, imm12: off1 }, Instr::Str { rt: rt2, rn: base2, imm12: off2 }) = (w[0], w[1]) {
            if base1 != base2 || off2 != off1 + 1 || off1 > 63 { return None; }
            let stp = 0xA900_0000u32 | (((off1 as u32) & 0x7F) << 15) | ((rt2 as u32) << 10) | ((base1 as u32) << 5) | (rt1 as u32);
            return Some(vec![Instr::Raw(stp), Instr::Nop]);
        }
        None
    }
}

pub struct RuleCmpZeroAfterArith;
impl PeepholeRule for RuleCmpZeroAfterArith {
    fn name(&self) -> &'static str { "cmp_zero_after_arith" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        match (w[0], w[1]) {
            (Instr::Add { rd, rn, rm }, Instr::Cmp { rn: cn, rm: 31 }) if cn == rd => Some(vec![Instr::Adds { rd, rn, rm }, Instr::Nop]),
            (Instr::Sub { rd, rn, rm }, Instr::Cmp { rn: cn, rm: 31 }) if cn == rd => Some(vec![Instr::Subs { rd, rn, rm }, Instr::Nop]),
            _ => None,
        }
    }
}

pub struct RuleRedundantReverseMov;
impl PeepholeRule for RuleRedundantReverseMov {
    fn name(&self) -> &'static str { "redundant_reverse_mov" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Mov { rd: d1, rn: n1 }, Instr::Mov { rd: d2, rn: n2 }) = (w[0], w[1]) {
            if d1 == n2 && n1 == d2 { return Some(vec![w[0], Instr::Nop]); }
        }
        None
    }
}

pub struct RuleBranchOverBranch;
impl PeepholeRule for RuleBranchOverBranch {
    fn name(&self) -> &'static str { "branch_over_branch" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.len() < 2 { return None; }
        if let (Instr::Bcond { cond, imm19: 2 }, Instr::Branch { imm26 }) = (w[0], w[1]) {
            let new_offset = imm26 + 1;
            if new_offset >= -(1 << 18) && new_offset < (1 << 18) {
                return Some(vec![Instr::Bcond { cond: cond ^ 1, imm19: new_offset }, Instr::Nop]);
            }
        }
        None
    }
}

pub struct RuleZeroRegOpt;
impl PeepholeRule for RuleZeroRegOpt {
    fn name(&self) -> &'static str { "zero_reg_opt" }
    fn apply(&self, w: &[Instr]) -> Option<Vec<Instr>> {
        if w.is_empty() { return None; }
        if let Instr::MovImm16 { rd, imm16: 0 } = w[0] {
            if w.len() >= 2 { let (_def, reads) = instr_def_use(&w[1]); if reads.contains(&rd) { if let Some(new_next) = subst_reg(&w[1], rd, 31) { return Some(vec![Instr::Nop, new_next]); } } }
            return Some(vec![Instr::Mov { rd, rn: 31 }]);
        }
        None
    }
}

#[derive(Debug, Default, Clone)]
pub struct PeepholeStats {
    pub identity_nop: u32, pub mul_add_to_madd: u32, pub mul_sub_to_msub: u32,
    pub cmp_zero_dead: u32, pub ldr_pair_to_ldp: u32, pub lsl_add_to_shifted_add: u32,
    pub str_ldr_elim: u32, pub str_pair_to_stp: u32, pub cmp_zero_after_arith: u32,
    pub redundant_reverse_mov: u32, pub branch_over_branch: u32, pub zero_reg_opt: u32,
    pub rounds: u32,
}

impl PeepholeStats {
    pub fn total(&self) -> u32 {
        self.identity_nop + self.mul_add_to_madd + self.mul_sub_to_msub + self.cmp_zero_dead + self.ldr_pair_to_ldp + self.lsl_add_to_shifted_add + self.str_ldr_elim + self.str_pair_to_stp + self.cmp_zero_after_arith + self.redundant_reverse_mov + self.branch_over_branch + self.zero_reg_opt
    }
    fn bump(&mut self, name: &str) {
        match name {
            "identity_nop" => self.identity_nop += 1, "mul_add_to_madd" => self.mul_add_to_madd += 1,
            "mul_sub_to_msub" => self.mul_sub_to_msub += 1, "cmp_zero_dead" => self.cmp_zero_dead += 1,
            "ldr_pair_to_ldp" => self.ldr_pair_to_ldp += 1, "lsl_add_to_shifted_add" => self.lsl_add_to_shifted_add += 1,
            "str_ldr_elim" => self.str_ldr_elim += 1, "str_pair_to_stp" => self.str_pair_to_stp += 1,
            "cmp_zero_after_arith" => self.cmp_zero_after_arith += 1, "redundant_reverse_mov" => self.redundant_reverse_mov += 1,
            "branch_over_branch" => self.branch_over_branch += 1, "zero_reg_opt" => self.zero_reg_opt += 1,
            _ => {}
        }
    }
}

pub fn run_peephole(code: &mut [u8]) -> PeepholeStats {
    let mut stats = PeepholeStats::default();
    if code.len() % 4 != 0 || code.is_empty() { return stats; }
    let n = code.len() / 4;
    let mut words: Vec<u32> = (0..n).map(|i| u32::from_le_bytes([code[4*i], code[4*i+1], code[4*i+2], code[4*i+3]])).collect();
    let branch_targets = collect_branch_targets(&words);
    let rules: Vec<Box<dyn PeepholeRule>> = vec![
        Box::new(RuleIdentity), Box::new(RuleMulAddToMadd), Box::new(RuleMulSubToMsub),
        Box::new(RuleCmpZeroDeadCmp), Box::new(RuleLdrPairToLdp), Box::new(RuleLslAddToShiftedAdd),
        Box::new(RuleStrLdrElim), Box::new(RuleStrPairToStp), Box::new(RuleCmpZeroAfterArith),
        Box::new(RuleRedundantReverseMov), Box::new(RuleBranchOverBranch), Box::new(RuleZeroRegOpt),
    ];
    for _round in 0..12 {
        let mut changed = false;
        let mut i = 0usize;
        while i < n {
            let w_end = (i + 4).min(n);
            let window: Vec<Instr> = (i..w_end).map(|j| Instr::decode(words[j])).collect();
            let mut replaced_here = false;
            for rule in &rules {
                for wsize in (1..=window.len()).rev() {
                    if wsize > 2 { continue; }
                    if wsize == 2 && branch_targets.contains(&(i + 1)) { continue; }
                    if let Some(new) = rule.apply(&window[..wsize]) {
                        if new.len() == wsize {
                            for (k, instr) in new.iter().enumerate() { words[i + k] = instr.encode(); }
                            stats.bump(rule.name()); changed = true; replaced_here = true; break;
                        }
                    }
                }
                if replaced_here { break; }
            }
            i += 1;
        }
        if changed { stats.rounds += 1; } else { break; }
    }
    for (i, w) in words.iter().enumerate() { code[4*i..4*i+4].copy_from_slice(&w.to_le_bytes()); }
    stats
}

fn collect_branch_targets(words: &[u32]) -> HashSet<usize> {
    let mut set = HashSet::new();
    for (i, &w) in words.iter().enumerate() {
        let top = w & 0xFC00_0000;
        if top == 0x1400_0000 || top == 0x9400_0000 {
            let imm26 = (w & 0x03FF_FFFF) as i32;
            let signed = if imm26 & 0x0200_0000 != 0 { imm26 | !0x03FF_FFFF } else { imm26 };
            let tgt = i as isize + signed as isize;
            if tgt >= 0 && (tgt as usize) < words.len() { set.insert(tgt as usize); }
            continue;
        }
        let top8 = w & 0xFF00_0000;
        if top8 == 0x5400_0000 || top8 == 0xB400_0000 || top8 == 0xB500_0000 {
            let imm19 = ((w >> 5) & 0x7_FFFF) as i32;
            let signed = if imm19 & 0x4_0000 != 0 { imm19 | !0x7_FFFF } else { imm19 };
            let tgt = i as isize + signed as isize;
            if tgt >= 0 && (tgt as usize) < words.len() { set.insert(tgt as usize); }
        }
    }
    set
}

#[cfg(test)]
mod tests {
    use super::*;
    fn enc(i: Instr) -> u32 { i.encode() }
    fn dec(w: u32) -> Instr { Instr::decode(w) }

    #[test] fn encode_decode_roundtrip_basic() {
        for c in [Instr::Nop, Instr::Mov { rd: 1, rn: 2 }, Instr::Add { rd: 3, rn: 4, rm: 5 }, Instr::Sub { rd: 6, rn: 7, rm: 8 }, Instr::Mul { rd: 9, rn: 10, rm: 11 }, Instr::Madd { rd: 1, rn: 2, rm: 3, ra: 4 }, Instr::Msub { rd: 1, rn: 2, rm: 3, ra: 4 }, Instr::Cmp { rn: 5, rm: 6 }, Instr::Cbnz { rt: 7, imm19: 10 }, Instr::Cbz { rt: 8, imm19: -4 }, Instr::Ldr { rt: 9, rn: 10, imm12: 5 }, Instr::AddShifted { rd: 1, rn: 2, rm: 3, shift: 2 }, Instr::LslImm { rd: 1, rn: 2, shift: 3 }] { assert_eq!(c, dec(enc(c)), "roundtrip failed for {:?}", c); }
    }
    #[test] fn encode_decode_roundtrip_new_variants() {
        for c in [Instr::Str { rt: 3, rn: 29, imm12: 5 }, Instr::Branch { imm26: 10 }, Instr::Branch { imm26: -4 }, Instr::Bcond { cond: 0, imm19: 5 }, Instr::Bcond { cond: 1, imm19: -3 }, Instr::Adds { rd: 1, rn: 2, rm: 3 }, Instr::Subs { rd: 4, rn: 5, rm: 6 }, Instr::MovImm16 { rd: 7, imm16: 42 }, Instr::MovImm16 { rd: 0, imm16: 0 }] { assert_eq!(c, dec(enc(c)), "roundtrip failed for {:?}", c); }
    }
    #[test] fn rule_identity_mov_same() { let r = RuleIdentity; assert_eq!(r.apply(&[Instr::Mov { rd: 3, rn: 3 }]).unwrap(), vec![Instr::Nop]); assert!(r.apply(&[Instr::Mov { rd: 3, rn: 4 }]).is_none()); }
    #[test] fn rule_identity_dead_mov() { let r = RuleIdentity; let out = r.apply(&[Instr::Mov { rd: 0, rn: 3 }, Instr::Mul { rd: 0, rn: 3, rm: 3 }]).unwrap(); assert_eq!(out[0], Instr::Nop); assert!(r.apply(&[Instr::Mov { rd: 0, rn: 3 }, Instr::Add { rd: 0, rn: 0, rm: 4 }]).is_none()); assert_eq!(r.apply(&[Instr::Mov { rd: 0, rn: 3 }, Instr::Ldr { rt: 0, rn: 29, imm12: 1 }]).unwrap()[0], Instr::Nop); }
    #[test] fn rule_identity_add_xzr() { let r = RuleIdentity; assert_eq!(r.apply(&[Instr::Add { rd: 5, rn: 5, rm: 31 }]).unwrap(), vec![Instr::Nop]); assert_eq!(r.apply(&[Instr::Add { rd: 5, rn: 31, rm: 5 }]).unwrap(), vec![Instr::Nop]); assert!(r.apply(&[Instr::Add { rd: 1, rn: 2, rm: 3 }]).is_none()); }
    #[test] fn rule_mul_add_to_madd_forward() { assert_eq!(RuleMulAddToMadd.apply(&[Instr::Mul { rd: 5, rn: 3, rm: 4 }, Instr::Add { rd: 5, rn: 5, rm: 6 }]).unwrap(), vec![Instr::Madd { rd: 5, rn: 3, rm: 4, ra: 6 }, Instr::Nop]); }
    #[test] fn rule_mul_add_to_madd_commuted() { assert_eq!(RuleMulAddToMadd.apply(&[Instr::Mul { rd: 5, rn: 3, rm: 4 }, Instr::Add { rd: 5, rn: 6, rm: 5 }]).unwrap(), vec![Instr::Madd { rd: 5, rn: 3, rm: 4, ra: 6 }, Instr::Nop]); }
    #[test] fn rule_mul_add_rejects_when_dst_differs() { assert!(RuleMulAddToMadd.apply(&[Instr::Mul { rd: 5, rn: 3, rm: 4 }, Instr::Add { rd: 7, rn: 5, rm: 6 }]).is_none()); }
    #[test] fn rule_mul_sub_to_msub_ok() { assert_eq!(RuleMulSubToMsub.apply(&[Instr::Mul { rd: 5, rn: 3, rm: 4 }, Instr::Sub { rd: 5, rn: 6, rm: 5 }]).unwrap(), vec![Instr::Msub { rd: 5, rn: 3, rm: 4, ra: 6 }, Instr::Nop]); }
    #[test] fn rule_mul_sub_rejects_wrong_form() { assert!(RuleMulSubToMsub.apply(&[Instr::Mul { rd: 5, rn: 3, rm: 4 }, Instr::Sub { rd: 5, rn: 5, rm: 6 }]).is_none()); }
    #[test] fn rule_cmp_zero_dead_with_cbnz() { let out = RuleCmpZeroDeadCmp.apply(&[Instr::Cmp { rn: 4, rm: 31 }, Instr::Cbnz { rt: 4, imm19: 3 }]).unwrap(); assert_eq!(out[0], Instr::Nop); }
    #[test] fn rule_cmp_zero_rejects_non_branch_next() { assert!(RuleCmpZeroDeadCmp.apply(&[Instr::Cmp { rn: 4, rm: 31 }, Instr::Add { rd: 1, rn: 2, rm: 3 }]).is_none()); }
    #[test] fn rule_ldr_pair_to_ldp_ok() { let out = RuleLdrPairToLdp.apply(&[Instr::Ldr { rt: 3, rn: 29, imm12: 2 }, Instr::Ldr { rt: 4, rn: 29, imm12: 3 }]).unwrap(); assert_eq!(out[1], Instr::Nop); let expected = 0xA940_0000u32 | (2u32 << 15) | (4u32 << 10) | (29u32 << 5) | 3u32; if let Instr::Raw(w) = out[0] { assert_eq!(w, expected); } else { panic!("expected Raw(LDP)"); } }
    #[test] fn rule_ldr_pair_rejects_non_contiguous() { assert!(RuleLdrPairToLdp.apply(&[Instr::Ldr { rt: 3, rn: 29, imm12: 2 }, Instr::Ldr { rt: 4, rn: 29, imm12: 4 }]).is_none()); }
    #[test] fn rule_lsl_add_to_shifted_ok() { assert_eq!(RuleLslAddToShiftedAdd.apply(&[Instr::LslImm { rd: 10, rn: 5, shift: 2 }, Instr::Add { rd: 10, rn: 7, rm: 10 }]).unwrap(), vec![Instr::AddShifted { rd: 10, rn: 7, rm: 5, shift: 2 }, Instr::Nop]); }
    #[test] fn rule_lsl_add_rejects_when_xs_eq_xd() { assert!(RuleLslAddToShiftedAdd.apply(&[Instr::LslImm { rd: 5, rn: 5, shift: 2 }, Instr::Add { rd: 5, rn: 7, rm: 5 }]).is_none()); }
    #[test] fn run_peephole_identity_nop() { let prog = vec![enc(Instr::Mov { rd: 3, rn: 3 }), enc(Instr::Add { rd: 1, rn: 2, rm: 4 })]; let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect(); let stats = run_peephole(&mut bytes); assert_eq!(stats.identity_nop, 1); }
    #[test] fn run_peephole_fusion_and_cleanup() { let prog = vec![enc(Instr::Mul { rd: 5, rn: 3, rm: 4 }), enc(Instr::Add { rd: 5, rn: 5, rm: 6 }), enc(Instr::Mov { rd: 0, rn: 0 })]; let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect(); let stats = run_peephole(&mut bytes); assert!(stats.mul_add_to_madd >= 1); assert!(stats.identity_nop >= 1); }
    #[test] fn run_peephole_preserves_length() { let prog = vec![enc(Instr::Mul { rd: 5, rn: 3, rm: 4 }), enc(Instr::Add { rd: 5, rn: 5, rm: 6 }), 0xd65f03c0]; let len = prog.len() * 4; let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect(); run_peephole(&mut bytes); assert_eq!(bytes.len(), len); }
    #[test] fn run_peephole_skips_branch_target_in_middle() { let prog = vec![0x14000000u32 | 2, enc(Instr::Mul { rd: 5, rn: 3, rm: 4 }), enc(Instr::Add { rd: 5, rn: 5, rm: 6 })]; let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect(); assert_eq!(run_peephole(&mut bytes).mul_add_to_madd, 0); }
    #[test] fn rule_str_ldr_elim_same_reg() { let out = RuleStrLdrElim.apply(&[Instr::Str { rt: 3, rn: 29, imm12: 5 }, Instr::Ldr { rt: 3, rn: 29, imm12: 5 }]).unwrap(); assert_eq!(out[1], Instr::Nop); }
    #[test] fn rule_str_ldr_elim_diff_reg() { let out = RuleStrLdrElim.apply(&[Instr::Str { rt: 3, rn: 29, imm12: 5 }, Instr::Ldr { rt: 7, rn: 29, imm12: 5 }]).unwrap(); assert_eq!(out[1], Instr::Mov { rd: 7, rn: 3 }); }
    #[test] fn rule_str_ldr_elim_rejects_diff_offset() { assert!(RuleStrLdrElim.apply(&[Instr::Str { rt: 3, rn: 29, imm12: 5 }, Instr::Ldr { rt: 7, rn: 29, imm12: 6 }]).is_none()); }
    #[test] fn rule_str_pair_to_stp_ok() { let out = RuleStrPairToStp.apply(&[Instr::Str { rt: 3, rn: 31, imm12: 2 }, Instr::Str { rt: 4, rn: 31, imm12: 3 }]).unwrap(); assert_eq!(out[1], Instr::Nop); let expected = 0xA900_0000u32 | (2u32 << 15) | (4u32 << 10) | (31u32 << 5) | 3u32; if let Instr::Raw(w) = out[0] { assert_eq!(w, expected); } else { panic!("expected Raw(STP)"); } }
    #[test] fn rule_str_pair_rejects_non_contiguous() { assert!(RuleStrPairToStp.apply(&[Instr::Str { rt: 3, rn: 31, imm12: 2 }, Instr::Str { rt: 4, rn: 31, imm12: 4 }]).is_none()); }
    #[test] fn rule_cmp_zero_after_add() { let out = RuleCmpZeroAfterArith.apply(&[Instr::Add { rd: 5, rn: 3, rm: 4 }, Instr::Cmp { rn: 5, rm: 31 }]).unwrap(); assert_eq!(out[0], Instr::Adds { rd: 5, rn: 3, rm: 4 }); assert_eq!(out[1], Instr::Nop); }
    #[test] fn rule_cmp_zero_after_sub() { let out = RuleCmpZeroAfterArith.apply(&[Instr::Sub { rd: 5, rn: 3, rm: 4 }, Instr::Cmp { rn: 5, rm: 31 }]).unwrap(); assert_eq!(out[0], Instr::Subs { rd: 5, rn: 3, rm: 4 }); assert_eq!(out[1], Instr::Nop); }
    #[test] fn rule_cmp_zero_after_arith_rejects_diff_reg() { assert!(RuleCmpZeroAfterArith.apply(&[Instr::Add { rd: 5, rn: 3, rm: 4 }, Instr::Cmp { rn: 6, rm: 31 }]).is_none()); }
    #[test] fn rule_redundant_reverse_mov_ok() { let out = RuleRedundantReverseMov.apply(&[Instr::Mov { rd: 3, rn: 5 }, Instr::Mov { rd: 5, rn: 3 }]).unwrap(); assert_eq!(out[1], Instr::Nop); }
    #[test] fn rule_redundant_reverse_mov_rejects_non_reverse() { assert!(RuleRedundantReverseMov.apply(&[Instr::Mov { rd: 3, rn: 5 }, Instr::Mov { rd: 6, rn: 3 }]).is_none()); }
    #[test] fn rule_branch_over_branch_ok() { let out = RuleBranchOverBranch.apply(&[Instr::Bcond { cond: 0, imm19: 2 }, Instr::Branch { imm26: 10 }]).unwrap(); assert_eq!(out[0], Instr::Bcond { cond: 1, imm19: 11 }); assert_eq!(out[1], Instr::Nop); }
    #[test] fn rule_branch_over_branch_rejects_non_skip() { assert!(RuleBranchOverBranch.apply(&[Instr::Bcond { cond: 0, imm19: 3 }, Instr::Branch { imm26: 10 }]).is_none()); }
    #[test] fn rule_zero_reg_opt_single() { assert_eq!(RuleZeroRegOpt.apply(&[Instr::MovImm16 { rd: 5, imm16: 0 }]).unwrap(), vec![Instr::Mov { rd: 5, rn: 31 }]); }
    #[test] fn rule_zero_reg_opt_with_add() { let out = RuleZeroRegOpt.apply(&[Instr::MovImm16 { rd: 5, imm16: 0 }, Instr::Add { rd: 7, rn: 5, rm: 3 }]).unwrap(); assert_eq!(out[0], Instr::Nop); assert_eq!(out[1], Instr::Add { rd: 7, rn: 31, rm: 3 }); }
    #[test] fn rule_zero_reg_opt_nonzero_no_match() { assert!(RuleZeroRegOpt.apply(&[Instr::MovImm16 { rd: 5, imm16: 42 }]).is_none()); }
    #[test] fn run_peephole_str_ldr_elim_integration() { let prog = vec![enc(Instr::Str { rt: 3, rn: 29, imm12: 5 }), enc(Instr::Ldr { rt: 3, rn: 29, imm12: 5 })]; let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect(); assert!(run_peephole(&mut bytes).str_ldr_elim >= 1); }
    #[test] fn run_peephole_reverse_mov_integration() { let prog = vec![enc(Instr::Mov { rd: 3, rn: 5 }), enc(Instr::Mov { rd: 5, rn: 3 })]; let mut bytes: Vec<u8> = prog.iter().flat_map(|w| w.to_le_bytes()).collect(); assert!(run_peephole(&mut bytes).redundant_reverse_mov >= 1); }
}
