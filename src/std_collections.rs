//! std::collections — Advanced collection builtins for HEXA-LANG.
//!
//! Provides:
//!   btree_new()                — create empty BTreeMap
//!   btree_set(bt, key, val)    — insert key-value
//!   btree_get(bt, key)         — get value by key (void if missing)
//!   btree_remove(bt, key)      — remove key
//!   btree_keys(bt)             — sorted keys array
//!   btree_len(bt)              — number of entries
//!   pq_new()                   — create empty priority queue (min-heap)
//!   pq_push(pq, priority, val) — push with priority
//!   pq_pop(pq)                 — pop min-priority item
//!   pq_len(pq)                 — number of items
//!   deque_new()                — create empty deque
//!   deque_push_front(dq, val)  — push to front
//!   deque_push_back(dq, val)   — push to back
//!   deque_pop_front(dq)        — pop from front
//!   deque_pop_back(dq)         — pop from back
//!   deque_len(dq)              — number of items

#![allow(dead_code)]

use std::collections::{BTreeMap, BinaryHeap, VecDeque, HashMap};
use std::cmp::Reverse;
use std::sync::Mutex;

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch a collections builtin by name.
pub fn call_collections_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "btree_new" => builtin_btree_new(interp, args),
        "btree_set" => builtin_btree_set(interp, args),
        "btree_get" => builtin_btree_get(interp, args),
        "btree_remove" => builtin_btree_remove(interp, args),
        "btree_keys" => builtin_btree_keys(interp, args),
        "btree_len" => builtin_btree_len(interp, args),
        "pq_new" => builtin_pq_new(interp, args),
        "pq_push" => builtin_pq_push(interp, args),
        "pq_pop" => builtin_pq_pop(interp, args),
        "pq_len" => builtin_pq_len(interp, args),
        "deque_new" => builtin_deque_new(interp, args),
        "deque_push_front" => builtin_deque_push_front(interp, args),
        "deque_push_back" => builtin_deque_push_back(interp, args),
        "deque_pop_front" => builtin_deque_pop_front(interp, args),
        "deque_pop_back" => builtin_deque_pop_back(interp, args),
        "deque_len" => builtin_deque_len(interp, args),
        _ => Err(coll_err(interp, format!("unknown collections builtin: {}", name))),
    }
}

fn coll_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

fn type_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Type,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

// ── BTreeMap (stored as Map with _type marker) ──
// We use a Map handle with _type = "btree" and store data in a global registry.

type BTreeStore = BTreeMap<String, Value>;

static NEXT_COLL_ID: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);

mod btree_registry {
    use super::*;
    use std::sync::OnceLock;
    static INSTANCE: OnceLock<Mutex<HashMap<u64, BTreeStore>>> = OnceLock::new();
    pub fn get() -> &'static Mutex<HashMap<u64, BTreeStore>> {
        INSTANCE.get_or_init(|| Mutex::new(HashMap::new()))
    }
}

mod pq_registry {
    use super::*;
    use std::sync::OnceLock;
    // Min-heap: Reverse<(priority, insert_order)> for stable ordering
    static INSTANCE: OnceLock<Mutex<HashMap<u64, (BinaryHeap<Reverse<(i64, u64)>>, HashMap<u64, Value>, u64)>>> = OnceLock::new();
    pub fn get() -> &'static Mutex<HashMap<u64, (BinaryHeap<Reverse<(i64, u64)>>, HashMap<u64, Value>, u64)>> {
        INSTANCE.get_or_init(|| Mutex::new(HashMap::new()))
    }
}

mod deque_registry {
    use super::*;
    use std::sync::OnceLock;
    static INSTANCE: OnceLock<Mutex<HashMap<u64, VecDeque<Value>>>> = OnceLock::new();
    pub fn get() -> &'static Mutex<HashMap<u64, VecDeque<Value>>> {
        INSTANCE.get_or_init(|| Mutex::new(HashMap::new()))
    }
}

fn make_handle(type_name: &str, id: u64) -> Value {
    let mut m = HashMap::new();
    m.insert("_type".into(), Value::Str(type_name.into()));
    m.insert("_id".into(), Value::Int(id as i64));
    Value::Map(m)
}

fn extract_id(args: &[Value], idx: usize, type_name: &str, interp: &Interpreter, func: &str) -> Result<u64, HexaError> {
    match args.get(idx) {
        Some(Value::Map(m)) => {
            let is_type = matches!(m.get("_type"), Some(Value::Str(s)) if s == type_name);
            if !is_type {
                return Err(type_err(interp, format!("{}() requires a {} handle", func, type_name)));
            }
            match m.get("_id") {
                Some(Value::Int(n)) => Ok(*n as u64),
                _ => Err(type_err(interp, format!("{}() invalid handle", func))),
            }
        }
        _ => Err(type_err(interp, format!("{}() requires a {} handle", func, type_name))),
    }
}

// ── BTree ──

fn builtin_btree_new(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    let id = NEXT_COLL_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    btree_registry::get().lock().unwrap().insert(id, BTreeMap::new());
    Ok(make_handle("btree", id))
}

fn builtin_btree_set(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "btree", interp, "btree_set")?;
    let key = match args.get(1) {
        Some(Value::Str(s)) => s.clone(),
        Some(v) => format!("{}", v),
        None => return Err(type_err(interp, "btree_set() requires 3 arguments".into())),
    };
    let val = args.get(2).cloned().unwrap_or(Value::Void);
    btree_registry::get().lock().unwrap()
        .get_mut(&id)
        .ok_or_else(|| coll_err(interp, "btree handle expired".into()))?
        .insert(key, val);
    Ok(Value::Void)
}

fn builtin_btree_get(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "btree", interp, "btree_get")?;
    let key = match args.get(1) {
        Some(Value::Str(s)) => s.clone(),
        Some(v) => format!("{}", v),
        None => return Err(type_err(interp, "btree_get() requires 2 arguments".into())),
    };
    let store = btree_registry::get().lock().unwrap();
    let bt = store.get(&id).ok_or_else(|| coll_err(interp, "btree handle expired".into()))?;
    Ok(bt.get(&key).cloned().unwrap_or(Value::Void))
}

fn builtin_btree_remove(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "btree", interp, "btree_remove")?;
    let key = match args.get(1) {
        Some(Value::Str(s)) => s.clone(),
        Some(v) => format!("{}", v),
        None => return Err(type_err(interp, "btree_remove() requires 2 arguments".into())),
    };
    let mut store = btree_registry::get().lock().unwrap();
    let bt = store.get_mut(&id).ok_or_else(|| coll_err(interp, "btree handle expired".into()))?;
    let old = bt.remove(&key).unwrap_or(Value::Void);
    Ok(old)
}

fn builtin_btree_keys(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "btree", interp, "btree_keys")?;
    let store = btree_registry::get().lock().unwrap();
    let bt = store.get(&id).ok_or_else(|| coll_err(interp, "btree handle expired".into()))?;
    let keys: Vec<Value> = bt.keys().map(|k| Value::Str(k.clone())).collect();
    Ok(Value::Array(keys))
}

fn builtin_btree_len(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "btree", interp, "btree_len")?;
    let store = btree_registry::get().lock().unwrap();
    let bt = store.get(&id).ok_or_else(|| coll_err(interp, "btree handle expired".into()))?;
    Ok(Value::Int(bt.len() as i64))
}

// ── PriorityQueue (min-heap) ──

fn builtin_pq_new(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    let id = NEXT_COLL_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    pq_registry::get().lock().unwrap().insert(id, (BinaryHeap::new(), HashMap::new(), 0));
    Ok(make_handle("priority_queue", id))
}

fn builtin_pq_push(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "priority_queue", interp, "pq_push")?;
    let priority = match args.get(1) {
        Some(Value::Int(n)) => *n,
        Some(_) => return Err(type_err(interp, "pq_push() priority must be int".into())),
        None => return Err(type_err(interp, "pq_push() requires 3 arguments".into())),
    };
    let val = args.get(2).cloned().unwrap_or(Value::Void);

    let mut store = pq_registry::get().lock().unwrap();
    let (heap, vals, counter) = store.get_mut(&id)
        .ok_or_else(|| coll_err(interp, "pq handle expired".into()))?;
    let order = *counter;
    *counter += 1;
    heap.push(Reverse((priority, order)));
    vals.insert(order, val);
    Ok(Value::Void)
}

fn builtin_pq_pop(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "priority_queue", interp, "pq_pop")?;
    let mut store = pq_registry::get().lock().unwrap();
    let (heap, vals, _) = store.get_mut(&id)
        .ok_or_else(|| coll_err(interp, "pq handle expired".into()))?;
    match heap.pop() {
        Some(Reverse((_prio, order))) => {
            Ok(vals.remove(&order).unwrap_or(Value::Void))
        }
        None => Ok(Value::Void),
    }
}

fn builtin_pq_len(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "priority_queue", interp, "pq_len")?;
    let store = pq_registry::get().lock().unwrap();
    let (heap, _, _) = store.get(&id)
        .ok_or_else(|| coll_err(interp, "pq handle expired".into()))?;
    Ok(Value::Int(heap.len() as i64))
}

// ── Deque ──

fn builtin_deque_new(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    let id = NEXT_COLL_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    deque_registry::get().lock().unwrap().insert(id, VecDeque::new());
    Ok(make_handle("deque", id))
}

fn builtin_deque_push_front(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "deque", interp, "deque_push_front")?;
    let val = args.get(1).cloned().unwrap_or(Value::Void);
    deque_registry::get().lock().unwrap()
        .get_mut(&id)
        .ok_or_else(|| coll_err(interp, "deque handle expired".into()))?
        .push_front(val);
    Ok(Value::Void)
}

fn builtin_deque_push_back(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "deque", interp, "deque_push_back")?;
    let val = args.get(1).cloned().unwrap_or(Value::Void);
    deque_registry::get().lock().unwrap()
        .get_mut(&id)
        .ok_or_else(|| coll_err(interp, "deque handle expired".into()))?
        .push_back(val);
    Ok(Value::Void)
}

fn builtin_deque_pop_front(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "deque", interp, "deque_pop_front")?;
    let val = deque_registry::get().lock().unwrap()
        .get_mut(&id)
        .ok_or_else(|| coll_err(interp, "deque handle expired".into()))?
        .pop_front()
        .unwrap_or(Value::Void);
    Ok(val)
}

fn builtin_deque_pop_back(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "deque", interp, "deque_pop_back")?;
    let val = deque_registry::get().lock().unwrap()
        .get_mut(&id)
        .ok_or_else(|| coll_err(interp, "deque handle expired".into()))?
        .pop_back()
        .unwrap_or(Value::Void);
    Ok(val)
}

fn builtin_deque_len(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let id = extract_id(&args, 0, "deque", interp, "deque_len")?;
    let store = deque_registry::get().lock().unwrap();
    let dq = store.get(&id).ok_or_else(|| coll_err(interp, "deque handle expired".into()))?;
    Ok(Value::Int(dq.len() as i64))
}

// ── Tests ──

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_btree_basic() {
        let id = NEXT_COLL_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let mut bt = BTreeMap::new();
        bt.insert("c".to_string(), Value::Int(3));
        bt.insert("a".to_string(), Value::Int(1));
        bt.insert("b".to_string(), Value::Int(2));
        btree_registry::get().lock().unwrap().insert(id, bt);

        let store = btree_registry::get().lock().unwrap();
        let bt = store.get(&id).unwrap();
        let keys: Vec<&String> = bt.keys().collect();
        assert_eq!(keys, vec!["a", "b", "c"]); // sorted
        assert_eq!(bt.len(), 3);
    }

    #[test]
    fn test_pq_min_heap() {
        let mut heap: BinaryHeap<Reverse<(i64, u64)>> = BinaryHeap::new();
        heap.push(Reverse((5, 0)));
        heap.push(Reverse((1, 1)));
        heap.push(Reverse((3, 2)));

        assert_eq!(heap.pop(), Some(Reverse((1, 1)))); // min first
        assert_eq!(heap.pop(), Some(Reverse((3, 2))));
        assert_eq!(heap.pop(), Some(Reverse((5, 0))));
    }

    #[test]
    fn test_deque_basic() {
        let mut dq: VecDeque<i32> = VecDeque::new();
        dq.push_back(1);
        dq.push_back(2);
        dq.push_front(0);
        assert_eq!(dq.pop_front(), Some(0));
        assert_eq!(dq.pop_back(), Some(2));
        assert_eq!(dq.len(), 1);
    }
}
