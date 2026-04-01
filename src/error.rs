// sopfr(6) = 5 error classes
#[allow(dead_code)]
#[derive(Debug, Clone, PartialEq)]
pub enum ErrorClass {
    Syntax,   // 1: Lex/parse errors
    Type,     // 2: Type mismatch
    Name,     // 3: Undefined name
    Runtime,  // 4: Runtime errors
    Logic,    // 5: Proof/assert failure
}

#[allow(dead_code)]
#[derive(Debug, Clone)]
pub struct HexaError {
    pub class: ErrorClass,
    pub message: String,
    pub line: usize,
    pub col: usize,
}

impl std::fmt::Display for HexaError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "[{:?}] line {}:{}: {}", self.class, self.line, self.col, self.message)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_classes_count_is_sopfr() {
        // sopfr(6) = 5
        let classes = [ErrorClass::Syntax, ErrorClass::Type, ErrorClass::Name, ErrorClass::Runtime, ErrorClass::Logic];
        assert_eq!(classes.len(), 5);
    }

    #[test]
    fn test_error_display() {
        let err = HexaError {
            class: ErrorClass::Syntax,
            message: "unexpected token".into(),
            line: 1,
            col: 5,
        };
        assert_eq!(format!("{}", err), "[Syntax] line 1:5: unexpected token");
    }
}
