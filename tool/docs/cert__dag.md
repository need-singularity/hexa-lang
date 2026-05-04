# `cert/dag`

_cert/dag_

**Source:** [`stdlib/cert/dag.hexa`](../../stdlib/cert/dag.hexa)  

## Overview

Generic cert-chain DAG: roots, orphans, ancestry, descendants, cycles.

Motivation: anima's .meta2-cert/ directory grew to 29+ certs each
chained via `prev_cert_sha`. Orphan detection (G39 #33 audit) and
ancestry walks were hand-rolled per call site. This module lifts
the graph primitives into stdlib so any consumer (roadmap watcher,
cert_gate, audit tools) gets the same bounded traversal semantics.

CertNode schema (intentionally minimal):
  slug         — unique identifier (filename, sha, or any string)
  parent_slug  — "" or "GENESIS" means root; otherwise must match
                 the slug of another node or the node is an orphan.
  ts           — optional timestamp (used by topological tie-break)

Public API:
  pub fn make_dag(nodes: [CertNode])        -> CertDag
  pub fn find_roots(dag)                    -> [string]
  pub fn find_orphans(dag)                  -> [string]
  pub fn ancestors(dag, slug)               -> [string]
  pub fn descendants(dag, slug)             -> [string]
  pub fn detect_cycles(dag)                 -> [[string]]
  pub fn topological_order(dag)             -> [string]

Meta2-cert loader (companion):
  pub fn load_meta2_dag(dir_path: string)   -> CertDag
    Reads {dir}/*.json files, pulls `sha_v2` as slug and
    `prev_cert_sha` as parent_slug, `ts_utc` as ts. Non-.json
    entries are skipped; malformed JSON contributes an orphan
    with empty parent_slug so callers can surface the failure.

All traversals are bounded by node count to defend against
malformed chains — infinite loops surface as a detected cycle.

## Functions

| Function | Returns |
|---|---|
| [`make_dag`](#fn-make-dag) | `CertDag` |
| [`find_roots`](#fn-find-roots) | `array` |
| [`find_orphans`](#fn-find-orphans) | `array` |
| [`ancestors`](#fn-ancestors) | `array` |
| [`descendants`](#fn-descendants) | `array` |
| [`detect_cycles`](#fn-detect-cycles) | `array` |
| [`topological_order`](#fn-topological-order) | `array` |
| [`load_meta2_dag`](#fn-load-meta2-dag) | `CertDag` |

### <a id="fn-make-dag"></a>`fn make_dag`

```hexa
pub fn make_dag(nodes: [CertNode]) -> CertDag
```

**Parameters:** `nodes: [CertNode]`  
**Returns:** `CertDag`  

_No docstring._

### <a id="fn-find-roots"></a>`fn find_roots`

```hexa
pub fn find_roots(dag: CertDag) -> array
```

**Parameters:** `dag: CertDag`  
**Returns:** `array`  

_No docstring._

### <a id="fn-find-orphans"></a>`fn find_orphans`

```hexa
pub fn find_orphans(dag: CertDag) -> array
```

**Parameters:** `dag: CertDag`  
**Returns:** `array`  

_No docstring._

### <a id="fn-ancestors"></a>`fn ancestors`

```hexa
pub fn ancestors(dag: CertDag, slug: string) -> array
```

**Parameters:** `dag: CertDag, slug: string`  
**Returns:** `array`  

Walk parent chain upward from slug. Terminates on root or orphan
or when visited-set reaches len(nodes) (cycle breaker).

### <a id="fn-descendants"></a>`fn descendants`

```hexa
pub fn descendants(dag: CertDag, slug: string) -> array
```

**Parameters:** `dag: CertDag, slug: string`  
**Returns:** `array`  

BFS-walk descendants. Bounded queue drain.

### <a id="fn-detect-cycles"></a>`fn detect_cycles`

```hexa
pub fn detect_cycles(dag: CertDag) -> array
```

**Parameters:** `dag: CertDag`  
**Returns:** `array`  

Detect cycles as lists of slugs. A cycle surfaces when walking
ancestors from a node revisits that node's slug. Each distinct
cycle is reported once keyed by its lexicographically smallest
slug. Empty list means acyclic.

### <a id="fn-topological-order"></a>`fn topological_order`

```hexa
pub fn topological_order(dag: CertDag) -> array
```

**Parameters:** `dag: CertDag`  
**Returns:** `array`  

Topological order: roots first, then children. Stable with respect
to node insertion order (not ts) so regen is byte-identical.
Nodes inside cycles are appended at the end after acyclic drain.

### <a id="fn-load-meta2-dag"></a>`fn load_meta2_dag`

```hexa
pub fn load_meta2_dag(dir_path: string) -> CertDag
```

**Parameters:** `dir_path: string`  
**Returns:** `CertDag`  

_No docstring._

---

← [Back to stdlib index](README.md)
