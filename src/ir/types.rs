//! HEXA-IR Type system — σ=12 types (σ-τ=8 primitive + τ=4 compound).
//!
//! Derived from: σ(6)=12 total, σ(6)-τ(6)=8 primitive, τ(6)=4 compound.

/// σ-τ=8 primitive types + τ=4 compound types = σ=12 total.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum IrType {
    // ── Primitive (σ-τ=8) ──
    I8,
    I16,
    I32,
    I64,
    F32,
    F64,
    Bool,
    Byte,

    // ── Compound (τ=4) ──
    Array(Box<IrType>, Option<usize>),             // [T; N] or [T]
    Struct(StructType),                             // { field: T, ... }
    Enum(EnumType),                                 // tag + payload
    Fn(Vec<IrType>, Box<IrType>),                   // (params) -> ret

    // ── Special (not counted in σ=12) ──
    Void,
    Ptr(Box<IrType>),   // raw pointer for codegen
    Str,                 // string slice (sugar for Ptr(Byte) + len)
}

/// Named struct type with ordered fields.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct StructType {
    pub name: String,
    pub fields: Vec<(String, IrType)>,
}

/// Enum type with tagged variants.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct EnumType {
    pub name: String,
    pub variants: Vec<(String, Option<IrType>)>,
}

impl IrType {
    /// Total type count = σ(6) = 12.
    pub const COUNT: usize = 12;
    /// Primitive count = σ(6) - τ(6) = 8.
    pub const PRIMITIVE_COUNT: usize = 8;
    /// Compound count = τ(6) = 4.
    pub const COMPOUND_COUNT: usize = 4;

    /// Size in bytes for code generation.
    pub fn size_bytes(&self) -> usize {
        match self {
            IrType::I8 | IrType::Byte | IrType::Bool => 1,
            IrType::I16 => 2,
            IrType::I32 | IrType::F32 => 4,
            IrType::I64 | IrType::F64 | IrType::Ptr(_) => 8,
            IrType::Str => 16, // ptr + len
            IrType::Array(elem, Some(n)) => elem.size_bytes() * n,
            IrType::Array(_, None) => 16, // ptr + len (slice)
            IrType::Struct(s) => s.fields.iter().map(|(_, t)| align8(t.size_bytes())).sum(),
            IrType::Enum(e) => {
                let max_payload = e
                    .variants
                    .iter()
                    .map(|(_, t)| t.as_ref().map_or(0, |t| t.size_bytes()))
                    .max()
                    .unwrap_or(0);
                8 + align8(max_payload) // tag + max payload
            }
            IrType::Fn(_, _) => 8, // function pointer
            IrType::Void => 0,
        }
    }

    /// Alignment in bytes.
    pub fn align_bytes(&self) -> usize {
        match self {
            IrType::I8 | IrType::Byte | IrType::Bool => 1,
            IrType::I16 => 2,
            IrType::I32 | IrType::F32 => 4,
            _ => 8,
        }
    }

    pub fn is_integer(&self) -> bool {
        matches!(self, IrType::I8 | IrType::I16 | IrType::I32 | IrType::I64 | IrType::Byte)
    }

    pub fn is_float(&self) -> bool {
        matches!(self, IrType::F32 | IrType::F64)
    }

    pub fn is_numeric(&self) -> bool {
        self.is_integer() || self.is_float()
    }

    pub fn is_primitive(&self) -> bool {
        matches!(
            self,
            IrType::I8
                | IrType::I16
                | IrType::I32
                | IrType::I64
                | IrType::F32
                | IrType::F64
                | IrType::Bool
                | IrType::Byte
        )
    }
}

/// Align to 8 bytes.
fn align8(size: usize) -> usize {
    (size + 7) & !7
}

impl std::fmt::Display for IrType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            IrType::I8 => write!(f, "i8"),
            IrType::I16 => write!(f, "i16"),
            IrType::I32 => write!(f, "i32"),
            IrType::I64 => write!(f, "i64"),
            IrType::F32 => write!(f, "f32"),
            IrType::F64 => write!(f, "f64"),
            IrType::Bool => write!(f, "bool"),
            IrType::Byte => write!(f, "byte"),
            IrType::Array(t, Some(n)) => write!(f, "[{}; {}]", t, n),
            IrType::Array(t, None) => write!(f, "[{}]", t),
            IrType::Struct(s) => write!(f, "{}", s.name),
            IrType::Enum(e) => write!(f, "{}", e.name),
            IrType::Fn(params, ret) => {
                write!(f, "fn(")?;
                for (i, p) in params.iter().enumerate() {
                    if i > 0 { write!(f, ", ")?; }
                    write!(f, "{}", p)?;
                }
                write!(f, ") -> {}", ret)
            }
            IrType::Void => write!(f, "void"),
            IrType::Ptr(t) => write!(f, "*{}", t),
            IrType::Str => write!(f, "str"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sigma_12_types() {
        assert_eq!(IrType::COUNT, 12);
        assert_eq!(IrType::PRIMITIVE_COUNT, 8);
        assert_eq!(IrType::COMPOUND_COUNT, 4);
        assert_eq!(IrType::PRIMITIVE_COUNT + IrType::COMPOUND_COUNT, IrType::COUNT);
    }

    #[test]
    fn test_sizes() {
        assert_eq!(IrType::I8.size_bytes(), 1);
        assert_eq!(IrType::I64.size_bytes(), 8);
        assert_eq!(IrType::F64.size_bytes(), 8);
        assert_eq!(IrType::Bool.size_bytes(), 1);
        assert_eq!(IrType::Void.size_bytes(), 0);
        assert_eq!(IrType::Str.size_bytes(), 16);
    }

    #[test]
    fn test_struct_size() {
        let s = IrType::Struct(StructType {
            name: "Point".into(),
            fields: vec![("x".into(), IrType::I64), ("y".into(), IrType::I64)],
        });
        assert_eq!(s.size_bytes(), 16);
    }

    #[test]
    fn test_enum_size() {
        let e = IrType::Enum(EnumType {
            name: "Option".into(),
            variants: vec![
                ("None".into(), None),
                ("Some".into(), Some(IrType::I64)),
            ],
        });
        assert_eq!(e.size_bytes(), 16); // 8 tag + 8 payload
    }
}
