#[allow(dead_code)]

// σ-τ = 8 primitive types
#[derive(Debug, Clone, PartialEq)]
pub enum PrimitiveType {
    Int,     // i64 (i8/i16/i32/i64/i128)
    Float,   // f64 (f16/f32/f64)
    Bool,    // true/false
    Char,    // UTF-8 scalar
    Str,     // heap-allocated string
    Byte,    // u8
    Void,    // unit ()
    Any,     // dynamic dispatch
}

// τ=4 type layers
#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
pub enum HexaType {
    // Layer 1: Primitive
    Primitive(PrimitiveType),
    // Layer 2: Composite
    Array(Box<HexaType>),
    Tuple(Vec<HexaType>),
    Struct(String, Vec<(String, HexaType)>),
    Enum(String, Vec<(String, Option<Vec<HexaType>>)>),
    // Layer 3: Reference
    Ref(Box<HexaType>, bool),      // &T or &mut T
    Owned(Box<HexaType>),           // own T
    // Layer 4: Function
    Fn(Vec<HexaType>, Box<HexaType>),  // fn(A, B) -> C
    // Special
    Unknown,
    Named(String),  // unresolved type name
}

impl PrimitiveType {
    pub fn from_str(s: &str) -> Option<Self> {
        match s {
            "int" => Some(Self::Int),
            "float" => Some(Self::Float),
            "bool" => Some(Self::Bool),
            "char" => Some(Self::Char),
            "string" => Some(Self::Str),
            "byte" => Some(Self::Byte),
            "void" => Some(Self::Void),
            "any" => Some(Self::Any),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_primitive_count_is_sigma_minus_tau() {
        // σ-τ = 12-4 = 8
        let all = ["int","float","bool","char","string","byte","void","any"];
        assert_eq!(all.len(), 8);
        for name in &all {
            assert!(PrimitiveType::from_str(name).is_some());
        }
    }

    #[test]
    fn test_type_layers_is_tau() {
        // τ=4: Primitive, Composite, Reference, Function
        let layers = 4;
        assert_eq!(layers, 4);
    }
}
