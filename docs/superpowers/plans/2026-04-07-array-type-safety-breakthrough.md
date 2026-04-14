# Array Type Safety Breakthrough Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hexa 배열 시스템의 타입 안전성을 정적(타입 체커) + 동적(인터프리터) 양면으로 돌파하여, 컴파일 타임에 이질 배열/잘못된 연산을 잡고, 런타임에 누락된 배열 기능(메서드, 패턴 매칭, 음수 인덱싱 등)을 완성한다.

**Architecture:** 6개 Task로 분할. Task 1-2는 타입 체커(정적), Task 3-5는 인터프리터(동적), Task 6은 통합 테스트. 각 Task는 독립적으로 빌드/테스트 가능.

**Tech Stack:** Rust (src/type_checker.rs, src/interpreter.rs, src/parser.rs, src/ast.rs)

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/type_checker.rs` | Modify | Task 1-2: 배열 원소 동질성 검사, BinOp 타입 추론, for-loop 비배열 경고 |
| `src/interpreter.rs` | Modify | Task 3-5: 누락 메서드, 음수 인덱싱, 배열 비교, 배열 패턴 매칭, 배열 반복 |
| `src/parser.rs` | Modify | Task 4: 배열 패턴 `[a, b, ...rest]` 파싱 |
| `src/ast.rs` | Modify | Task 4: `Expr::ArrayPattern` variant 추가 |

---

### Task 1: 타입 체커 — 배열 원소 동질성 검사

**Files:**
- Modify: `src/type_checker.rs:634-638` (check_expr Array branch)
- Modify: `src/type_checker.rs:711-717` (infer_expr Array branch)
- Modify: `src/type_checker.rs:674-697` (infer_expr BinOp branch)
- Test: `src/type_checker.rs` (하단 tests 모듈)

- [ ] **Step 1: 배열 원소 동질성 경고 테스트 작성**

`src/type_checker.rs` 하단 `mod tests` 안에 추가:

```rust
#[test]
fn test_heterogeneous_array_error() {
    // [1, "two", 3.0] should fail — mixed element types
    let result = check_source("let x = [1, \"two\", 3.0]");
    assert!(result.is_err());
    let err = result.unwrap_err();
    assert!(err.message.contains("array element type mismatch"));
}

#[test]
fn test_homogeneous_array_ok() {
    assert!(check_source("let x = [1, 2, 3]").is_ok());
    assert!(check_source("let x = [\"a\", \"b\"]").is_ok());
}

#[test]
fn test_empty_array_ok() {
    assert!(check_source("let x = []").is_ok());
}

#[test]
fn test_int_float_coercion_array() {
    // [1, 2.0] — int->float coercion is allowed
    assert!(check_source("let x = [1, 2.0]").is_ok());
}
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd $HEXA_LANG && cargo test test_heterogeneous_array_error -- --nocapture 2>&1 | tail -5`
Expected: FAIL — 현재는 이질 배열이 에러 없이 통과

- [ ] **Step 3: check_expr에서 배열 원소 타입 검증 구현**

`src/type_checker.rs`의 `check_expr` 메서드, `Expr::Array(items)` 분기를 수정:

```rust
Expr::Array(items) => {
    if items.len() > 1 {
        let first_type = self.infer_expr(&items[0]);
        for (i, item) in items.iter().enumerate().skip(1) {
            self.check_expr(item, line, col);
            let item_type = self.infer_expr(item);
            if !first_type.accepts(&item_type) {
                self.emit_error(line, col, format!(
                    "array element type mismatch: element 0 is {} but element {} is {}",
                    first_type.display_name(), i, item_type.display_name()
                ));
                return;
            }
        }
    }
    for item in items {
        self.check_expr(item, line, col);
    }
}
```

- [ ] **Step 4: infer_expr에서 float 승격 반영**

`src/type_checker.rs`의 `infer_expr`, `Expr::Array(items)` 분기를 수정:

```rust
Expr::Array(items) => {
    if items.is_empty() {
        CheckType::Array(Box::new(CheckType::Unknown))
    } else {
        let mut elem = self.infer_expr(&items[0]);
        // Promote to float if any element is float (int->float coercion)
        for item in items.iter().skip(1) {
            let t = self.infer_expr(item);
            if matches!((&elem, &t), (CheckType::Int, CheckType::Float)) {
                elem = CheckType::Float;
            }
        }
        CheckType::Array(Box::new(elem))
    }
}
```

- [ ] **Step 5: 테스트 통과 확인**

Run: `cd $HEXA_LANG && cargo test test_heterogeneous_array -- --nocapture 2>&1 | tail -10`
Expected: 4개 테스트 모두 PASS

- [ ] **Step 6: 커밋**

```bash
cd $HEXA_LANG
git add src/type_checker.rs
git commit -m "feat(type): array element homogeneity check — mixed types now caught at compile time"
```

---

### Task 2: 타입 체커 — Array BinOp 타입 추론 + for-loop 비배열 경고

**Files:**
- Modify: `src/type_checker.rs:674-697` (infer_expr BinOp)
- Modify: `src/type_checker.rs:384-397` (check_stmt For)
- Test: `src/type_checker.rs` (하단 tests 모듈)

- [ ] **Step 1: BinOp + for-loop 테스트 작성**

```rust
#[test]
fn test_array_concat_type() {
    // array + array should be valid
    assert!(check_source("let a = [1, 2]\nlet b = [3, 4]\nlet c = a + b").is_ok());
}

#[test]
fn test_array_eq_type() {
    // array == array should return bool
    assert!(check_source("let a = [1]\nlet b = [1]\nlet c: bool = a == b").is_ok());
}
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd $HEXA_LANG && cargo test test_array_concat_type -- --nocapture 2>&1 | tail -5`
Expected: FAIL 또는 Unknown 타입 반환

- [ ] **Step 3: BinOp에 Array 연산 추가**

`src/type_checker.rs`의 `infer_expr`, `BinOp::Add` 분기 수정. 기존 arithmetic 블록의 **앞에** 배열 체크를 추가:

```rust
BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div
| BinOp::Rem | BinOp::Pow => {
    // Array + Array = Array (concatenation)
    if let (CheckType::Array(inner), BinOp::Add) = (&l, op) {
        if matches!(&r, CheckType::Array(_)) {
            return CheckType::Array(inner.clone());
        }
    }
    // Array * Int = Array (repetition)
    if matches!((&l, op, &r), (CheckType::Array(_), BinOp::Mul, CheckType::Int)) {
        return l;
    }
    // String + String = String
    if matches!((&l, op), (CheckType::Str, BinOp::Add)) {
        return CheckType::Str;
    }
    if matches!((&l, &r), (CheckType::Float, _) | (_, CheckType::Float)) {
        CheckType::Float
    } else if matches!((&l, &r), (CheckType::Int, CheckType::Int)) {
        CheckType::Int
    } else {
        CheckType::Unknown
    }
}
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd $HEXA_LANG && cargo test test_array_concat_type test_array_eq_type -- --nocapture 2>&1 | tail -10`
Expected: PASS

- [ ] **Step 5: 커밋**

```bash
cd $HEXA_LANG
git add src/type_checker.rs
git commit -m "feat(type): array BinOp type inference — concat, repeat, comparison"
```

---

### Task 3: 인터프리터 — 누락 배열 메서드 + 음수 인덱싱 + 배열 비교/반복

**Files:**
- Modify: `src/interpreter.rs:2524-2668` (call_array_method)
- Modify: `src/interpreter.rs:1409-1418` (Index 음수 인덱싱)
- Modify: `src/interpreter.rs:400-418` (Assign Index 음수 인덱싱)
- Modify: `src/interpreter.rs:1842-1847` (BinOp 배열 연산 추가)
- Modify: `src/interpreter.rs:4564-4583` (values_equal 배열 지원)
- Test: `src/interpreter.rs` (하단 tests 모듈)

- [ ] **Step 1: 배열 메서드 + 음수 인덱싱 테스트 작성**

`src/interpreter.rs` 하단 tests 모듈에 추가:

```rust
#[test]
fn test_array_pop() {
    let src = "let a = [1, 2, 3].pop()\na";
    // pop returns new array without last element
    assert_eq!(eval(src).to_string(), "[1, 2]");
}

#[test]
fn test_array_find() {
    let src = "let a = [10, 20, 30].find(|x| x > 15)\na";
    assert!(matches!(eval(src), Value::Int(20)));
}

#[test]
fn test_array_find_index() {
    let src = "let a = [10, 20, 30].find_index(|x| x > 15)\na";
    assert!(matches!(eval(src), Value::Int(1)));
}

#[test]
fn test_array_any() {
    let src = "[1, 2, 3].any(|x| x > 2)";
    assert!(matches!(eval(src), Value::Bool(true)));
}

#[test]
fn test_array_all() {
    let src = "[1, 2, 3].all(|x| x > 0)";
    assert!(matches!(eval(src), Value::Bool(true)));
}

#[test]
fn test_array_swap() {
    let src = "[1, 2, 3].swap(0, 2)";
    assert_eq!(eval(src).to_string(), "[3, 2, 1]");
}

#[test]
fn test_array_fill() {
    let src = "[0, 0, 0].fill(7)";
    assert_eq!(eval(src).to_string(), "[7, 7, 7]");
}

#[test]
fn test_negative_index() {
    let src = "let a = [10, 20, 30]\na[-1]";
    assert!(matches!(eval(src), Value::Int(30)));
}

#[test]
fn test_negative_index_assign() {
    let src = "let mut a = [10, 20, 30]\na[-1] = 99\na[-1]";
    assert!(matches!(eval(src), Value::Int(99)));
}

#[test]
fn test_array_repeat() {
    let src = "[1, 2] * 3";
    assert_eq!(eval(src).to_string(), "[1, 2, 1, 2, 1, 2]");
}

#[test]
fn test_array_equality() {
    assert!(matches!(eval("[1, 2] == [1, 2]"), Value::Bool(true)));
    assert!(matches!(eval("[1, 2] == [1, 3]"), Value::Bool(false)));
    assert!(matches!(eval("[1, 2] != [1, 3]"), Value::Bool(true)));
}
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd $HEXA_LANG && cargo test test_array_pop test_negative_index test_array_repeat test_array_equality -- --nocapture 2>&1 | tail -10`
Expected: FAIL

- [ ] **Step 3: 누락 메서드 6종 구현 (pop, find, find_index, any, all, swap, fill)**

`src/interpreter.rs`의 `call_array_method` 함수에서, `"flatten"` 분기 뒤, `_ =>` 분기 앞에 추가:

```rust
"pop" => {
    if arr.is_empty() { return Err(self.runtime_err("pop() on empty array".into())); }
    Ok(Value::Array(arr[..arr.len()-1].to_vec()))
}
"find" => {
    if args.is_empty() { return Err(self.type_err("find() requires 1 argument (function)".into())); }
    let func = args.into_iter().next().unwrap();
    for item in arr {
        let result = self.call_function(func.clone(), vec![item.clone()])?;
        if Self::is_truthy(&result) {
            return Ok(item.clone());
        }
    }
    Ok(Value::Void)
}
"find_index" => {
    if args.is_empty() { return Err(self.type_err("find_index() requires 1 argument (function)".into())); }
    let func = args.into_iter().next().unwrap();
    for (i, item) in arr.iter().enumerate() {
        let result = self.call_function(func.clone(), vec![item.clone()])?;
        if Self::is_truthy(&result) {
            return Ok(Value::Int(i as i64));
        }
    }
    Ok(Value::Int(-1))
}
"any" => {
    if args.is_empty() { return Err(self.type_err("any() requires 1 argument (function)".into())); }
    let func = args.into_iter().next().unwrap();
    for item in arr {
        let result = self.call_function(func.clone(), vec![item.clone()])?;
        if Self::is_truthy(&result) {
            return Ok(Value::Bool(true));
        }
    }
    Ok(Value::Bool(false))
}
"all" => {
    if args.is_empty() { return Err(self.type_err("all() requires 1 argument (function)".into())); }
    let func = args.into_iter().next().unwrap();
    for item in arr {
        let result = self.call_function(func.clone(), vec![item.clone()])?;
        if !Self::is_truthy(&result) {
            return Ok(Value::Bool(false));
        }
    }
    Ok(Value::Bool(true))
}
"swap" => {
    if args.len() < 2 { return Err(self.type_err("swap() requires 2 arguments (i, j)".into())); }
    match (&args[0], &args[1]) {
        (Value::Int(i), Value::Int(j)) => {
            let mut new_arr = arr.to_vec();
            let i = *i as usize;
            let j = *j as usize;
            if i >= new_arr.len() || j >= new_arr.len() {
                return Err(self.runtime_err(format!("swap index out of bounds")));
            }
            new_arr.swap(i, j);
            Ok(Value::Array(new_arr))
        }
        _ => Err(self.type_err("swap() requires int arguments".into())),
    }
}
"fill" => {
    if args.is_empty() { return Err(self.type_err("fill() requires 1 argument".into())); }
    let fill_val = args.into_iter().next().unwrap();
    Ok(Value::Array(vec![fill_val; arr.len()]))
}
```

- [ ] **Step 4: 음수 인덱싱 구현 (읽기)**

`src/interpreter.rs`의 `Expr::Index` 분기에서 `(Value::Array(a), Value::Int(i))` 매치를 수정:

```rust
(Value::Array(a), Value::Int(i)) => {
    let idx = if *i < 0 {
        let pos = a.len() as i64 + *i;
        if pos < 0 {
            return Err(self.runtime_err(format!(
                "negative index {} out of bounds (len {})", i, a.len()
            )));
        }
        pos as usize
    } else {
        *i as usize
    };
    if idx >= a.len() {
        Err(self.runtime_err(format!(
            "index {} out of bounds (len {})", i, a.len()
        )))
    } else {
        Ok(a[idx].clone())
    }
}
```

- [ ] **Step 5: 음수 인덱싱 구현 (쓰기)**

`src/interpreter.rs`의 `Stmt::Assign` → `Expr::Index` 분기에서 인덱스 계산 수정:

```rust
Expr::Index(arr_expr, idx_expr) => {
    let idx_val = self.eval_expr(idx_expr)?;
    let raw_idx = match idx_val {
        Value::Int(i) => i,
        _ => return Err(self.type_err("index must be an integer".into())),
    };
    if let Expr::Ident(name) = arr_expr.as_ref() {
        if let Some(Value::Array(mut arr)) = self.env.get(name) {
            let idx = if raw_idx < 0 {
                let pos = arr.len() as i64 + raw_idx;
                if pos < 0 {
                    return Err(self.runtime_err(format!(
                        "negative index {} out of bounds (len {})", raw_idx, arr.len()
                    )));
                }
                pos as usize
            } else {
                raw_idx as usize
            };
            if idx >= arr.len() {
                return Err(self.runtime_err(format!(
                    "index {} out of bounds (len {})", raw_idx, arr.len()
                )));
            }
            arr[idx] = val;
            self.env.set(name, Value::Array(arr));
        } else {
            return Err(self.type_err("index assignment requires array".into()));
        }
    } else {
        return Err(self.runtime_err("complex index assignment not supported".into()));
    }
}
```

- [ ] **Step 6: 배열 반복 (Array * Int) 구현**

`src/interpreter.rs`의 `eval_binary`에서, 배열 concat 분기 뒤에 추가:

```rust
// Array repetition
(Value::Array(a), BinOp::Mul, Value::Int(n)) => {
    let n = *n as usize;
    let mut result = Vec::with_capacity(a.len() * n);
    for _ in 0..n {
        result.extend(a.iter().cloned());
    }
    Ok(Value::Array(result))
}
```

- [ ] **Step 7: 배열 동등성 비교 구현**

`src/interpreter.rs`의 `eval_binary`에서, 배열 반복 분기 뒤에 추가:

```rust
// Array equality
(Value::Array(a), BinOp::Eq, Value::Array(b)) => {
    Ok(Value::Bool(a.len() == b.len() && a.iter().zip(b.iter()).all(|(x, y)| Self::values_equal(x, y))))
}
(Value::Array(a), BinOp::Ne, Value::Array(b)) => {
    Ok(Value::Bool(a.len() != b.len() || !a.iter().zip(b.iter()).all(|(x, y)| Self::values_equal(x, y))))
}
```

- [ ] **Step 8: values_equal에 배열 지원 추가**

`src/interpreter.rs`의 `values_equal` 함수, `_ => false` 앞에 추가:

```rust
(Value::Array(a), Value::Array(b)) => {
    a.len() == b.len() && a.iter().zip(b.iter()).all(|(x, y)| Self::values_equal(x, y))
}
(Value::Tuple(a), Value::Tuple(b)) => {
    a.len() == b.len() && a.iter().zip(b.iter()).all(|(x, y)| Self::values_equal(x, y))
}
```

- [ ] **Step 9: 테스트 통과 확인**

Run: `cd $HEXA_LANG && cargo test test_array_pop test_array_find test_array_find_index test_array_any test_array_all test_array_swap test_array_fill test_negative_index test_negative_index_assign test_array_repeat test_array_equality -- --nocapture 2>&1 | tail -20`
Expected: 모두 PASS

- [ ] **Step 10: 커밋**

```bash
cd $HEXA_LANG
git add src/interpreter.rs
git commit -m "feat(array): 7 new methods + negative indexing + repeat + equality comparison"
```

---

### Task 4: 배열 패턴 매칭 — `[a, b, ...rest]` 구조분해

**Files:**
- Modify: `src/ast.rs:14` (Expr enum에 ArrayPattern 추가)
- Modify: `src/parser.rs:1478-1504` (parse_match_pattern에 배열 패턴 추가)
- Modify: `src/interpreter.rs:4191-4315` (match_pattern에 ArrayPattern 분기)
- Test: `src/interpreter.rs` (하단 tests 모듈)

- [ ] **Step 1: 배열 패턴 매칭 테스트 작성**

```rust
#[test]
fn test_match_array_literal() {
    let src = r#"
let x = [1, 2, 3]
match x {
    [1, 2, 3] => "exact"
    _ => "no"
}
"#;
    assert_eq!(eval(src).to_string(), "exact");
}

#[test]
fn test_match_array_destructure() {
    let src = r#"
let x = [10, 20, 30]
match x {
    [a, b, c] => a + b + c
    _ => 0
}
"#;
    assert!(matches!(eval(src), Value::Int(60)));
}

#[test]
fn test_match_array_rest() {
    let src = r#"
let x = [1, 2, 3, 4, 5]
match x {
    [head, ...tail] => len(tail)
    _ => 0
}
"#;
    assert!(matches!(eval(src), Value::Int(4)));
}

#[test]
fn test_match_array_empty() {
    let src = r#"
let x = []
match x {
    [] => "empty"
    _ => "not"
}
"#;
    assert_eq!(eval(src).to_string(), "empty");
}
```

- [ ] **Step 2: 테스트 실패 확인**

Run: `cd $HEXA_LANG && cargo test test_match_array_literal -- --nocapture 2>&1 | tail -5`
Expected: FAIL

- [ ] **Step 3: AST에 ArrayPattern 추가**

`src/ast.rs`에서 `Expr` enum에 새 variant 추가. 기존 `Wildcard` 뒤에:

```rust
/// Array pattern for match: [a, b, ...rest]
/// Elements are patterns, rest_name is the optional ...ident capture
ArrayPattern(Vec<Expr>, Option<String>),  // (element_patterns, rest_binding)
```

- [ ] **Step 4: 파서에 배열 패턴 파싱 추가**

`src/parser.rs`의 `parse_match_pattern`에서, wildcard 체크 바로 앞에 배열 패턴 체크를 추가:

```rust
fn parse_match_pattern(&mut self) -> Result<Expr, HexaError> {
    // Check for array pattern `[a, b, ...rest]`
    if matches!(self.peek(), Token::LBracket) {
        self.advance(); // consume [
        self.skip_newlines();
        let mut patterns = Vec::new();
        let mut rest_name: Option<String> = None;
        while !matches!(self.peek(), Token::RBracket | Token::Eof) {
            // Check for ...rest spread pattern
            if matches!(self.peek(), Token::DotDot) {
                self.advance(); // consume ..
                // Check for optional trailing dot (... = DotDot + Dot) or ident
                if matches!(self.peek(), Token::Dot) {
                    self.advance(); // consume the third dot
                }
                if let Token::Ident(ref name) = self.peek().clone() {
                    rest_name = Some(name.to_string());
                    self.advance();
                }
                // Skip trailing comma if present
                if matches!(self.peek(), Token::Comma) {
                    self.advance();
                }
                self.skip_newlines();
                break;
            }
            let pat = self.parse_match_pattern()?;
            patterns.push(pat);
            if matches!(self.peek(), Token::Comma) {
                self.advance();
                self.skip_newlines();
            } else {
                break;
            }
        }
        self.skip_newlines();
        self.expect(&Token::RBracket)?;
        return Ok(Expr::ArrayPattern(patterns, rest_name));
    }
    // Check for wildcard `_`
    // ... (existing code continues unchanged)
```

- [ ] **Step 5: 인터프리터에 ArrayPattern 매칭 구현**

`src/interpreter.rs`의 `match_pattern`에서, `Expr::Wildcard` 분기 뒤에 추가:

```rust
// Array pattern: [a, b, ...rest]
Expr::ArrayPattern(patterns, rest_name) => {
    match val {
        Value::Array(arr) => {
            // Check minimum length
            if rest_name.is_some() {
                if arr.len() < patterns.len() {
                    return Ok(None);
                }
            } else {
                if arr.len() != patterns.len() {
                    return Ok(None);
                }
            }
            let mut bindings = Vec::new();
            // Match each element pattern
            for (i, pat) in patterns.iter().enumerate() {
                match self.match_pattern(&arr[i], pat)? {
                    Some(sub_bindings) => bindings.extend(sub_bindings),
                    None => return Ok(None),
                }
            }
            // Bind rest if present
            if let Some(rest) = rest_name {
                let rest_arr = arr[patterns.len()..].to_vec();
                bindings.push((rest.clone(), Value::Array(rest_arr)));
            }
            Ok(Some(bindings))
        }
        _ => Ok(None),
    }
}
```

- [ ] **Step 6: match_pattern에서 Ident를 변수 바인딩으로 사용하도록 수정**

현재 `Expr::Ident`는 먼저 env에서 찾고 같은 값인지 비교한다. 배열 패턴 안의 식별자는 바인딩으로 사용되어야 하므로, match_pattern의 Ident 분기를 수정:

```rust
Expr::Ident(name) => {
    // First check if it's an enum constant (None, etc.)
    if let Some(const_val) = self.env.get(name) {
        if matches!(&const_val, Value::EnumVariant(_)) {
            if Self::values_equal(val, &const_val) {
                return Ok(Some(vec![]));
            } else {
                return Ok(None);
            }
        }
    }
    // If not a known constant, treat as variable binding
    Ok(Some(vec![(name.clone(), val.clone())]))
}
```

**주의:** 이 변경은 기존 match 동작에 영향을 줄 수 있음. 기존에 `match x { y => ... }` 에서 y가 변수에 있으면 비교, 없으면 에러였던 것이 이제 항상 바인딩으로 동작. 이것은 Rust/Hexa의 표준 패턴 매칭 시맨틱과 일치.

- [ ] **Step 7: 테스트 통과 확인**

Run: `cd $HEXA_LANG && cargo test test_match_array -- --nocapture 2>&1 | tail -15`
Expected: 4개 모두 PASS

- [ ] **Step 8: 기존 테스트 전체 통과 확인**

Run: `cd $HEXA_LANG && cargo test 2>&1 | tail -5`
Expected: 전체 PASS (기존 match 테스트 포함)

- [ ] **Step 9: 커밋**

```bash
cd $HEXA_LANG
git add src/ast.rs src/parser.rs src/interpreter.rs
git commit -m "feat(match): array pattern matching — [a, b, ...rest] destructuring in match"
```

---

### Task 5: 추가 코드 경로 — JIT/VM/ownership에 ArrayPattern 전파

**Files:**
- Modify: `src/jit.rs` (can_jit_expr, collect_free_vars_inner에 ArrayPattern 추가)
- Modify: `src/ownership.rs` (check_expr_use에 ArrayPattern 추가)
- Modify: `src/compiler.rs` (ArrayPattern은 VM 미지원 — fallback 확인)

- [ ] **Step 1: JIT에 ArrayPattern exhaustive match 추가**

`src/jit.rs`에서 `can_jit_expr`의 `Expr::Match` 분기가 arm.pattern을 재귀적으로 체크. `Expr::ArrayPattern`이 처리 안 되면 컴파일 에러 발생. `can_jit_expr`에 추가:

```rust
Expr::ArrayPattern(pats, _) => pats.iter().all(|p| can_jit_expr(p)),
```

`collect_free_vars_inner`에도 추가:

```rust
Expr::ArrayPattern(pats, _) => {
    for p in pats {
        collect_free_vars_inner(p, params, bound, free);
    }
}
```

- [ ] **Step 2: ownership에 ArrayPattern 추가**

`src/ownership.rs`에서 `Expr::Match(scrutinee, arms)` 분기가 arm.pattern을 재귀 체크. `check_expr_use`에 ArrayPattern 추가:

```rust
Expr::ArrayPattern(pats, _) => {
    for p in pats {
        self.check_expr_use(p, line, col);
    }
}
```

- [ ] **Step 3: 컴파일 확인**

Run: `cd $HEXA_LANG && cargo build 2>&1 | tail -10`
Expected: 컴파일 성공 (exhaustive match 경고 없음)

- [ ] **Step 4: 전체 테스트**

Run: `cd $HEXA_LANG && cargo test 2>&1 | tail -5`
Expected: 전체 PASS

- [ ] **Step 5: 커밋**

```bash
cd $HEXA_LANG
git add src/jit.rs src/ownership.rs
git commit -m "fix: propagate ArrayPattern to JIT/ownership for exhaustive match coverage"
```

---

### Task 6: 통합 테스트 + examples + 빌드 검증

**Files:**
- Test: `src/interpreter.rs` (통합 시나리오 테스트)
- Verify: `bash build.sh test`

- [ ] **Step 1: 통합 시나리오 테스트**

```rust
#[test]
fn test_array_breakthrough_integration() {
    // Full pipeline: create → manipulate → pattern match → compare
    let src = r#"
let data = [1, 2, 3, 4, 5]
let doubled = data.map(|x| x * 2)
let big = doubled.filter(|x| x > 4)
let found = big.find(|x| x == 6)
found
"#;
    assert!(matches!(eval(src), Value::Int(6)));
}

#[test]
fn test_array_negative_index_chain() {
    let src = r#"
let a = [10, 20, 30, 40, 50]
a[-1] + a[-2]
"#;
    assert!(matches!(eval(src), Value::Int(90)));
}

#[test]
fn test_array_all_methods() {
    // Exercise every new method
    assert!(matches!(eval("[1,2,3].pop().len()"), Value::Int(2)));
    assert!(matches!(eval("[1,2,3].any(|x| x == 2)"), Value::Bool(true)));
    assert!(matches!(eval("[1,2,3].all(|x| x > 0)"), Value::Bool(true)));
    assert!(matches!(eval("[1,2,3].find_index(|x| x == 3)"), Value::Int(2)));
    assert_eq!(eval("[0,0,0].fill(5)").to_string(), "[5, 5, 5]");
    assert_eq!(eval("[3,1,2].swap(0, 2)").to_string(), "[2, 1, 3]");
}
```

- [ ] **Step 2: 전체 빌드 + 테스트**

Run: `cd $HEXA_LANG && bash build.sh test 2>&1 | tail -20`
Expected: 전체 PASS

- [ ] **Step 3: 커밋**

```bash
cd $HEXA_LANG
git add src/interpreter.rs
git commit -m "test: array breakthrough integration tests — all scenarios PASS"
```

---

## Summary: 이 돌파에 포함된 것

| 영역 | 변경 | 갭 해소 |
|------|------|---------|
| 타입 체커 | 배열 원소 동질성 검사 | `[1, "two"]` 컴파일 에러 |
| 타입 체커 | Array BinOp 추론 | `arr + arr` → `array<T>`, `arr * int` → `array<T>` |
| 인터프리터 | 7개 메서드 추가 | pop, find, find_index, any, all, swap, fill |
| 인터프리터 | 음수 인덱싱 | `arr[-1]` 읽기/쓰기 |
| 인터프리터 | 배열 반복 | `[1,2] * 3` |
| 인터프리터 | 배열 동등성 | `[1,2] == [1,2]` → true |
| 인터프리터 | values_equal 배열/튜플 | 재귀적 구조 비교 |
| 파서+AST | ArrayPattern | `[a, b, ...rest]` 구문 |
| 인터프리터 | 배열 패턴 매칭 | match에서 배열 구조분해 |
| JIT/ownership | ArrayPattern 전파 | exhaustive match 보장 |
