#![allow(dead_code)]

//! NaN-Boxing: encodes all primitive values in a single 64-bit f64.
//!
//! IEEE 754 double-precision NaN has 52 mantissa bits. By using quiet NaN
//! bit patterns with tag bits, we can store ints, bools, pointers, and void
//! in 8 bytes instead of the ~64+ bytes of a full Value enum.
//!
//! Layout:
//!   Float:  any bit pattern where (bits & QNAN) != QNAN  (normal f64)
//!   Int:    QNAN | TAG_INT  | (lower 48 bits of i64)
//!   Bool:   QNAN | TAG_BOOL | (0 or 1)
//!   Void:   QNAN | TAG_VOID
//!   Ptr:    QNAN | TAG_PTR  | (48-bit pointer to heap Value)

use crate::env::Value;

/// Quiet NaN signature (bits 50-63 set to signal quiet NaN).
const QNAN: u64 = 0x7FFC_000000000000;

/// Tag bits (bits 48-49) to distinguish boxed types.
const TAG_INT: u64 = 0x0001_000000000000;
const TAG_BOOL: u64 = 0x0002_000000000000;
const TAG_VOID: u64 = 0x0003_000000000000;
const TAG_PTR: u64 = 0x0000_000000000000; // pointer has no extra tag, just QNAN + low bits

/// Mask for the 48-bit payload.
const PAYLOAD_MASK: u64 = 0x0000_FFFFFFFFFFFF;

/// A NaN-boxed value: all primitive types in 8 bytes.
#[derive(Clone, Copy)]
pub struct NanBoxed(u64);

impl std::fmt::Debug for NanBoxed {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.is_void() {
            write!(f, "NanBoxed(void)")
        } else if self.is_bool() {
            write!(f, "NanBoxed({})", self.as_bool())
        } else if self.is_int() {
            write!(f, "NanBoxed({}i)", self.as_int())
        } else if self.is_float() {
            write!(f, "NanBoxed({}f)", self.as_float())
        } else if self.is_ptr() {
            write!(f, "NanBoxed(ptr)")
        } else {
            write!(f, "NanBoxed(0x{:016X})", self.0)
        }
    }
}

impl NanBoxed {
    // ---- Constructors ----

    /// Box a float value. Regular f64 values are stored as-is.
    #[inline(always)]
    pub fn from_float(f: f64) -> Self {
        Self(f.to_bits())
    }

    /// Box an integer value. Uses lower 48 bits (supports -2^47..2^47-1).
    #[inline(always)]
    pub fn from_int(i: i64) -> Self {
        // Sign-extend: store as u64 masked to 48 bits
        Self(QNAN | TAG_INT | (i as u64 & PAYLOAD_MASK))
    }

    /// Box a boolean.
    #[inline(always)]
    pub fn from_bool(b: bool) -> Self {
        Self(QNAN | TAG_BOOL | b as u64)
    }

    /// Box void (unit value).
    #[inline(always)]
    pub fn from_void() -> Self {
        Self(QNAN | TAG_VOID)
    }

    /// Box a heap-allocated Value (for strings, arrays, etc.).
    /// The Value is leaked onto the heap and its pointer stored in 48 bits.
    pub fn from_heap(val: Value) -> Self {
        let boxed = Box::new(val);
        let ptr = Box::into_raw(boxed) as u64;
        debug_assert!(ptr & !PAYLOAD_MASK == 0, "pointer exceeds 48 bits");
        Self(QNAN | (ptr & PAYLOAD_MASK))
    }

    // ---- Type checks ----

    /// True if this holds a regular float (not a NaN-tagged value).
    #[inline(always)]
    pub fn is_float(&self) -> bool {
        (self.0 & QNAN) != QNAN
    }

    /// True if this holds a tagged integer.
    #[inline(always)]
    pub fn is_int(&self) -> bool {
        (self.0 & (QNAN | TAG_INT | TAG_BOOL | TAG_VOID)) == (QNAN | TAG_INT)
    }

    /// True if this holds a tagged boolean.
    #[inline(always)]
    pub fn is_bool(&self) -> bool {
        (self.0 & (QNAN | TAG_INT | TAG_BOOL | TAG_VOID)) == (QNAN | TAG_BOOL)
    }

    /// True if this holds void.
    #[inline(always)]
    pub fn is_void(&self) -> bool {
        self.0 == (QNAN | TAG_VOID)
    }

    /// True if this holds a heap pointer (QNAN set, no type tag, not void).
    #[inline(always)]
    pub fn is_ptr(&self) -> bool {
        (self.0 & QNAN) == QNAN
            && !self.is_int()
            && !self.is_bool()
            && !self.is_void()
    }

    // ---- Extractors ----

    /// Extract float value.
    #[inline(always)]
    pub fn as_float(&self) -> f64 {
        f64::from_bits(self.0)
    }

    /// Extract integer value (sign-extended from 48 bits).
    #[inline(always)]
    pub fn as_int(&self) -> i64 {
        let raw = self.0 & PAYLOAD_MASK;
        // Sign-extend from 48 bits
        if raw & (1 << 47) != 0 {
            (raw | !PAYLOAD_MASK) as i64
        } else {
            raw as i64
        }
    }

    /// Extract boolean value.
    #[inline(always)]
    pub fn as_bool(&self) -> bool {
        (self.0 & 1) != 0
    }

    /// Extract heap pointer as a reference to Value.
    /// # Safety
    /// Only call if `is_ptr()` returns true.
    pub unsafe fn as_ptr_ref(&self) -> &Value {
        let ptr = (self.0 & PAYLOAD_MASK) as *const Value;
        &*ptr
    }

    // ---- Conversion to/from Value ----

    /// Convert a Value into a NanBoxed representation.
    pub fn from_value(val: &Value) -> Self {
        match val {
            Value::Int(i) => Self::from_int(*i),
            Value::Float(f) => Self::from_float(*f),
            Value::Bool(b) => Self::from_bool(*b),
            Value::Void => Self::from_void(),
            // Complex types go to heap
            _ => Self::from_heap(val.clone()),
        }
    }

    /// Convert back to a Value.
    pub fn to_value(&self) -> Value {
        if self.is_void() {
            Value::Void
        } else if self.is_bool() {
            Value::Bool(self.as_bool())
        } else if self.is_int() {
            Value::Int(self.as_int())
        } else if self.is_float() {
            Value::Float(self.as_float())
        } else if self.is_ptr() {
            unsafe { self.as_ptr_ref().clone() }
        } else {
            Value::Void
        }
    }

    /// Check if this value is truthy (for control flow).
    #[inline(always)]
    pub fn is_truthy(&self) -> bool {
        if self.is_void() {
            false
        } else if self.is_bool() {
            self.as_bool()
        } else if self.is_int() {
            self.as_int() != 0
        } else if self.is_float() {
            self.as_float() != 0.0
        } else {
            true // heap values are truthy
        }
    }

    /// Raw bits access (for debugging).
    #[inline(always)]
    pub fn bits(&self) -> u64 {
        self.0
    }

    // ---- Arithmetic (fast path for primitives) ----

    /// Add two NaN-boxed values. Returns None if types are incompatible
    /// (caller should fall back to Value-based arithmetic).
    #[inline(always)]
    pub fn try_add(&self, other: &NanBoxed) -> Option<NanBoxed> {
        if self.is_int() && other.is_int() {
            Some(NanBoxed::from_int(self.as_int().wrapping_add(other.as_int())))
        } else if self.is_float() && other.is_float() {
            Some(NanBoxed::from_float(self.as_float() + other.as_float()))
        } else if self.is_int() && other.is_float() {
            Some(NanBoxed::from_float(self.as_int() as f64 + other.as_float()))
        } else if self.is_float() && other.is_int() {
            Some(NanBoxed::from_float(self.as_float() + other.as_int() as f64))
        } else {
            None
        }
    }

    /// Subtract two NaN-boxed values.
    #[inline(always)]
    pub fn try_sub(&self, other: &NanBoxed) -> Option<NanBoxed> {
        if self.is_int() && other.is_int() {
            Some(NanBoxed::from_int(self.as_int().wrapping_sub(other.as_int())))
        } else if self.is_float() && other.is_float() {
            Some(NanBoxed::from_float(self.as_float() - other.as_float()))
        } else if self.is_int() && other.is_float() {
            Some(NanBoxed::from_float(self.as_int() as f64 - other.as_float()))
        } else if self.is_float() && other.is_int() {
            Some(NanBoxed::from_float(self.as_float() - other.as_int() as f64))
        } else {
            None
        }
    }

    /// Multiply two NaN-boxed values.
    #[inline(always)]
    pub fn try_mul(&self, other: &NanBoxed) -> Option<NanBoxed> {
        if self.is_int() && other.is_int() {
            Some(NanBoxed::from_int(self.as_int().wrapping_mul(other.as_int())))
        } else if self.is_float() && other.is_float() {
            Some(NanBoxed::from_float(self.as_float() * other.as_float()))
        } else if self.is_int() && other.is_float() {
            Some(NanBoxed::from_float(self.as_int() as f64 * other.as_float()))
        } else if self.is_float() && other.is_int() {
            Some(NanBoxed::from_float(self.as_float() * other.as_int() as f64))
        } else {
            None
        }
    }

    /// Compare less-than for NaN-boxed values.
    #[inline(always)]
    pub fn try_lt(&self, other: &NanBoxed) -> Option<bool> {
        if self.is_int() && other.is_int() {
            Some(self.as_int() < other.as_int())
        } else if self.is_float() && other.is_float() {
            Some(self.as_float() < other.as_float())
        } else if self.is_int() && other.is_float() {
            Some((self.as_int() as f64) < other.as_float())
        } else if self.is_float() && other.is_int() {
            Some(self.as_float() < (other.as_int() as f64))
        } else {
            None
        }
    }

    /// Check equality for NaN-boxed values.
    #[inline(always)]
    pub fn try_eq(&self, other: &NanBoxed) -> Option<bool> {
        if self.is_int() && other.is_int() {
            Some(self.as_int() == other.as_int())
        } else if self.is_float() && other.is_float() {
            Some(self.as_float() == other.as_float())
        } else if self.is_bool() && other.is_bool() {
            Some(self.as_bool() == other.as_bool())
        } else if self.is_void() && other.is_void() {
            Some(true)
        } else if self.is_int() && other.is_float() {
            Some((self.as_int() as f64) == other.as_float())
        } else if self.is_float() && other.is_int() {
            Some(self.as_float() == (other.as_int() as f64))
        } else {
            None
        }
    }
}

// ---- NanBoxed stack for optimized VM ----

/// A stack using NaN-boxed values for cache-friendly operation.
/// Each element is 8 bytes instead of ~64+ bytes.
pub struct NanStack {
    data: Vec<NanBoxed>,
}

impl NanStack {
    pub fn with_capacity(cap: usize) -> Self {
        Self {
            data: Vec::with_capacity(cap),
        }
    }

    #[inline(always)]
    pub fn push(&mut self, val: NanBoxed) {
        self.data.push(val);
    }

    #[inline(always)]
    pub fn pop(&mut self) -> Option<NanBoxed> {
        self.data.pop()
    }

    #[inline(always)]
    pub fn last(&self) -> Option<&NanBoxed> {
        self.data.last()
    }

    #[inline(always)]
    pub fn len(&self) -> usize {
        self.data.len()
    }

    #[inline(always)]
    pub fn truncate(&mut self, len: usize) {
        self.data.truncate(len);
    }

    #[inline(always)]
    pub fn drain_from(&mut self, start: usize) -> Vec<NanBoxed> {
        self.data.drain(start..).collect()
    }

    #[inline(always)]
    pub fn get(&self, idx: usize) -> Option<&NanBoxed> {
        self.data.get(idx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_nanbox_int() {
        let v = NanBoxed::from_int(42);
        assert!(v.is_int());
        assert!(!v.is_float());
        assert!(!v.is_bool());
        assert!(!v.is_void());
        assert_eq!(v.as_int(), 42);
    }

    #[test]
    fn test_nanbox_negative_int() {
        let v = NanBoxed::from_int(-100);
        assert!(v.is_int());
        assert_eq!(v.as_int(), -100);
    }

    #[test]
    fn test_nanbox_float() {
        let v = NanBoxed::from_float(3.14);
        assert!(v.is_float());
        assert!(!v.is_int());
        assert!((v.as_float() - 3.14).abs() < 1e-10);
    }

    #[test]
    fn test_nanbox_bool() {
        let t = NanBoxed::from_bool(true);
        let f = NanBoxed::from_bool(false);
        assert!(t.is_bool());
        assert!(f.is_bool());
        assert!(t.as_bool());
        assert!(!f.as_bool());
    }

    #[test]
    fn test_nanbox_void() {
        let v = NanBoxed::from_void();
        assert!(v.is_void());
        assert!(!v.is_int());
        assert!(!v.is_float());
    }

    #[test]
    fn test_nanbox_roundtrip_value() {
        // Int
        let v = NanBoxed::from_value(&Value::Int(12345));
        assert_eq!(v.to_value().to_string(), "12345");

        // Float
        let v = NanBoxed::from_value(&Value::Float(2.718));
        match v.to_value() {
            Value::Float(f) => assert!((f - 2.718).abs() < 1e-10),
            _ => panic!("expected float"),
        }

        // Bool
        let v = NanBoxed::from_value(&Value::Bool(true));
        assert_eq!(v.to_value().to_string(), "true");

        // Void
        let v = NanBoxed::from_value(&Value::Void);
        assert!(matches!(v.to_value(), Value::Void));

        // String (heap)
        let v = NanBoxed::from_value(&Value::Str("hello".into()));
        assert_eq!(v.to_value().to_string(), "hello");
    }

    #[test]
    fn test_nanbox_arithmetic() {
        let a = NanBoxed::from_int(10);
        let b = NanBoxed::from_int(20);
        assert_eq!(a.try_add(&b).unwrap().as_int(), 30);
        assert_eq!(b.try_sub(&a).unwrap().as_int(), 10);
        assert_eq!(a.try_mul(&b).unwrap().as_int(), 200);

        let fa = NanBoxed::from_float(1.5);
        let fb = NanBoxed::from_float(2.5);
        assert!((fa.try_add(&fb).unwrap().as_float() - 4.0).abs() < 1e-10);
    }

    #[test]
    fn test_nanbox_comparison() {
        let a = NanBoxed::from_int(5);
        let b = NanBoxed::from_int(10);
        assert_eq!(a.try_lt(&b), Some(true));
        assert_eq!(b.try_lt(&a), Some(false));
        assert_eq!(a.try_eq(&a), Some(true));
        assert_eq!(a.try_eq(&b), Some(false));
    }

    #[test]
    fn test_nanbox_truthy() {
        assert!(NanBoxed::from_int(1).is_truthy());
        assert!(!NanBoxed::from_int(0).is_truthy());
        assert!(NanBoxed::from_bool(true).is_truthy());
        assert!(!NanBoxed::from_bool(false).is_truthy());
        assert!(!NanBoxed::from_void().is_truthy());
        assert!(NanBoxed::from_float(1.0).is_truthy());
        assert!(!NanBoxed::from_float(0.0).is_truthy());
    }

    #[test]
    fn test_nanbox_size() {
        // NaN-boxed value is exactly 8 bytes
        assert_eq!(std::mem::size_of::<NanBoxed>(), 8);
        // Value enum is much larger
        let value_size = std::mem::size_of::<Value>();
        assert!(value_size > 8, "Value should be larger than NanBoxed (Value={}, NanBoxed=8)", value_size);
    }

    #[test]
    fn test_nanbox_stack() {
        let mut stack = NanStack::with_capacity(16);
        stack.push(NanBoxed::from_int(1));
        stack.push(NanBoxed::from_int(2));
        stack.push(NanBoxed::from_int(3));
        assert_eq!(stack.len(), 3);
        assert_eq!(stack.pop().unwrap().as_int(), 3);
        assert_eq!(stack.len(), 2);
    }

    #[test]
    fn test_nanbox_large_int() {
        // Test near the 48-bit boundary
        let max = (1i64 << 47) - 1;
        let v = NanBoxed::from_int(max);
        assert_eq!(v.as_int(), max);

        let min = -(1i64 << 47);
        let v = NanBoxed::from_int(min);
        assert_eq!(v.as_int(), min);
    }

    #[test]
    fn test_nanbox_zero() {
        let zi = NanBoxed::from_int(0);
        assert!(zi.is_int());
        assert_eq!(zi.as_int(), 0);

        let zf = NanBoxed::from_float(0.0);
        assert!(zf.is_float());
        assert_eq!(zf.as_float(), 0.0);
    }

    #[test]
    fn test_nanbox_cross_type_arithmetic() {
        let i = NanBoxed::from_int(3);
        let f = NanBoxed::from_float(1.5);
        let result = i.try_add(&f).unwrap();
        assert!(result.is_float());
        assert!((result.as_float() - 4.5).abs() < 1e-10);
    }
}
