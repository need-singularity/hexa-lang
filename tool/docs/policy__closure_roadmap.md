# `policy/closure_roadmap`

_policy/closure_roadmap_

**Source:** [`stdlib/policy/closure_roadmap.hexa`](../../stdlib/policy/closure_roadmap.hexa)  

## Overview

Schema + loader + validator for closure_roadmap.json files.

Purpose: declarative spec for multi-track policy (β / α / γ or any
custom tag), so downstream tools can read a single file to know the
active track, fork rules, exit criteria, and gate references without
parsing ad-hoc YAML/shell formats per project.

Schema v1 (id = "hexa-lang/closure_roadmap/1"):

    "schema":       "hexa-lang/closure_roadmap/1",
    "updated_ts":   "2026-04-23T...",
    "active_track": "<name>",
    "tracks": [
        "name":           "β",
        "phase":          "P1",
        "parent":         "",
        "fork_policy":    "permissive" | "strict" | "mirror",
        "exit_criteria":  ["...", ...],
        "gates":          ["gate_id_1", ...]

Public API:
  pub fn parse_closure_roadmap(json_text: string) -> ClosureRoadmap
  pub fn load_closure_roadmap(path: string)      -> ClosureRoadmap
  pub fn validate_closure_roadmap(rm)            -> array<string>
  pub fn find_track(rm, name)                    -> array  [found_bool, TrackSpec]
  pub fn active_track(rm)                        -> array  [found_bool, TrackSpec]

Validation rules:
  E1  schema id mismatch
  E2  updated_ts missing or empty
  E3  active_track empty or not present among tracks
  E4  track with empty name
  E5  duplicate track name
  E6  fork_policy not in {permissive, strict, mirror}
  E7  parent references a non-existent track
  E8  parent cycle (A→B→A)

## Functions

| Function | Returns |
|---|---|
| [`parse_closure_roadmap`](#fn-parse-closure-roadmap) | `ClosureRoadmap` |
| [`load_closure_roadmap`](#fn-load-closure-roadmap) | `ClosureRoadmap` |
| [`validate_closure_roadmap`](#fn-validate-closure-roadmap) | `array` |
| [`find_track`](#fn-find-track) | `array` |
| [`active_track`](#fn-active-track) | `array` |

### <a id="fn-parse-closure-roadmap"></a>`fn parse_closure_roadmap`

```hexa
pub fn parse_closure_roadmap(json_text: string) -> ClosureRoadmap
```

**Parameters:** `json_text: string`  
**Returns:** `ClosureRoadmap`  

_No docstring._

### <a id="fn-load-closure-roadmap"></a>`fn load_closure_roadmap`

```hexa
pub fn load_closure_roadmap(path: string) -> ClosureRoadmap
```

**Parameters:** `path: string`  
**Returns:** `ClosureRoadmap`  

_No docstring._

### <a id="fn-validate-closure-roadmap"></a>`fn validate_closure_roadmap`

```hexa
pub fn validate_closure_roadmap(rm: ClosureRoadmap) -> array
```

**Parameters:** `rm: ClosureRoadmap`  
**Returns:** `array`  

_No docstring._

### <a id="fn-find-track"></a>`fn find_track`

```hexa
pub fn find_track(rm: ClosureRoadmap, name: string) -> array
```

**Parameters:** `rm: ClosureRoadmap, name: string`  
**Returns:** `array`  

_No docstring._

### <a id="fn-active-track"></a>`fn active_track`

```hexa
pub fn active_track(rm: ClosureRoadmap) -> array
```

**Parameters:** `rm: ClosureRoadmap`  
**Returns:** `array`  

_No docstring._

---

← [Back to stdlib index](README.md)
