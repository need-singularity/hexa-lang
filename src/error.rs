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
    /// Optional hint for the user (e.g. "did you mean 'print'?")
    pub hint: Option<String>,
}

impl std::fmt::Display for HexaError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        if self.line > 0 {
            write!(f, "[{:?}] line {}:{}: {}", self.class, self.line, self.col, self.message)?;
        } else {
            write!(f, "[{:?}] {}", self.class, self.message)?;
        }
        if let Some(hint) = &self.hint {
            write!(f, "\n  = help: {}", hint)?;
        }
        Ok(())
    }
}

/// Diagnostic formatter that produces Rust-style error messages with source context.
#[allow(dead_code)]
pub struct Diagnostic<'a> {
    pub error: &'a HexaError,
    pub source_lines: &'a [String],
    pub file_name: &'a str,
}

impl<'a> Diagnostic<'a> {
    /// Format an error with source context, producing Rust-like output.
    pub fn format_with_source(&self) -> String {
        let mut out = String::new();

        // Header: error[Class]: message
        out.push_str(&format!("error[{:?}]: {}\n", self.error.class, self.error.message));

        if self.error.line > 0 && !self.source_lines.is_empty() {
            let line_idx = self.error.line.saturating_sub(1);
            if line_idx < self.source_lines.len() {
                let source_line = &self.source_lines[line_idx];
                let line_num = self.error.line;
                let line_num_width = format!("{}", line_num).len();

                // Location
                out.push_str(&format!("{:>width$}--> {}:{}:{}\n",
                    " ", self.file_name, line_num, self.error.col, width = line_num_width));
                // Blank line with pipe
                out.push_str(&format!("{:>width$} |\n", " ", width = line_num_width));
                // Source line
                out.push_str(&format!("{} | {}\n", line_num, source_line));
                // Underline caret
                let col = self.error.col.saturating_sub(1);
                let underline_len = estimate_span_len(source_line, col);
                out.push_str(&format!("{:>width$} | {:>col$}{}\n",
                    " ",
                    "",
                    "^".repeat(underline_len.max(1)),
                    width = line_num_width,
                    col = col));
            }
        }

        // Hint
        if let Some(hint) = &self.error.hint {
            out.push_str(&format!("  = help: {}\n", hint));
        }

        out
    }
}

/// Estimate how many characters to underline starting at `col` in the source line.
fn estimate_span_len(line: &str, col: usize) -> usize {
    let chars: Vec<char> = line.chars().collect();
    if col >= chars.len() {
        return 1;
    }
    // Try to find a "token" starting at col
    let start_char = chars[col];
    if start_char == '"' {
        // String literal: find matching quote
        let mut i = col + 1;
        while i < chars.len() {
            if chars[i] == '"' && (i == 0 || chars[i-1] != '\\') {
                return i - col + 1;
            }
            i += 1;
        }
        return chars.len() - col;
    }
    if start_char.is_alphanumeric() || start_char == '_' {
        let mut i = col;
        while i < chars.len() && (chars[i].is_alphanumeric() || chars[i] == '_') {
            i += 1;
        }
        return i - col;
    }
    1
}

/// Compute Levenshtein edit distance between two strings.
pub fn levenshtein(a: &str, b: &str) -> usize {
    let a_chars: Vec<char> = a.chars().collect();
    let b_chars: Vec<char> = b.chars().collect();
    let m = a_chars.len();
    let n = b_chars.len();
    let mut dp = vec![vec![0usize; n + 1]; m + 1];
    for i in 0..=m { dp[i][0] = i; }
    for j in 0..=n { dp[0][j] = j; }
    for i in 1..=m {
        for j in 1..=n {
            let cost = if a_chars[i-1] == b_chars[j-1] { 0 } else { 1 };
            dp[i][j] = (dp[i-1][j] + 1)
                .min(dp[i][j-1] + 1)
                .min(dp[i-1][j-1] + cost);
        }
    }
    dp[m][n]
}

/// Find the closest name from a list of known names (edit distance <= 2).
pub fn suggest_name(unknown: &str, known: &[&str]) -> Option<String> {
    let mut best: Option<(usize, &str)> = None;
    for name in known {
        let dist = levenshtein(unknown, name);
        if dist <= 2 && dist > 0 {
            if best.is_none() || dist < best.unwrap().0 {
                best = Some((dist, name));
            }
        }
    }
    best.map(|(_, name)| name.to_string())
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
            hint: None,
        };
        assert_eq!(format!("{}", err), "[Syntax] line 1:5: unexpected token");
    }

    #[test]
    fn test_levenshtein_distance() {
        assert_eq!(levenshtein("print", "print"), 0);
        assert_eq!(levenshtein("prnt", "print"), 1);
        assert_eq!(levenshtein("pirnt", "print"), 2);
        assert_eq!(levenshtein("xyz", "print"), 5);
    }

    #[test]
    fn test_suggest_name() {
        let known = vec!["print", "println", "len", "type_of", "sigma"];
        assert_eq!(suggest_name("prnt", &known), Some("print".to_string()));
        assert_eq!(suggest_name("pirnt", &known), Some("print".to_string()));
        assert_eq!(suggest_name("xyz", &known), None);
    }

    #[test]
    fn test_diagnostic_format() {
        let err = HexaError {
            class: ErrorClass::Type,
            message: "expected int, found string".into(),
            line: 4,
            col: 17,
            hint: Some("expected int".into()),
        };
        let lines = vec![
            "fn main() {".into(),
            "    let a = 5".into(),
            "    let b = 10".into(),
            "    let x: int = \"hello\"".into(),
            "}".into(),
        ];
        let diag = Diagnostic {
            error: &err,
            source_lines: &lines,
            file_name: "test.hexa",
        };
        let output = diag.format_with_source();
        assert!(output.contains("error[Type]: expected int, found string"));
        assert!(output.contains("--> test.hexa:4:17"));
        assert!(output.contains("let x: int = \"hello\""));
        assert!(output.contains("^"));
        assert!(output.contains("= help: expected int"));
    }
}
