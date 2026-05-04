# `cert/loader`

_cert / state file loaders + sha256 digest_

**Source:** [`stdlib/cert/loader.hexa`](../../stdlib/cert/loader.hexa)  

## Overview

 stdlib/cert/loader.hexa — cert / state file loaders + sha256 digest

 Mirrors anima cert_gate.hexa's load_cert / load_state fallback cascade.
 File-absent returns CertScore { present: false, sat: 0.0 } — never
 raises. Callers are expected to skip `present=false` entries in
 aggregates to avoid biasing arithmetic means.

## Functions

| Function | Returns |
|---|---|
| [`load_cert`](#fn-load-cert) | `_inferred_` |
| [`load_state`](#fn-load-state) | `_inferred_` |
| [`digest`](#fn-digest) | `_inferred_` |

### <a id="fn-load-cert"></a>`fn load_cert`

```hexa
pub fn load_cert(cert_dir, slug)
```

**Parameters:** `cert_dir, slug`  
**Returns:** _inferred_  

Load <cert_dir>/<slug>.json and extract verdict via json_str_field_last.
present = file-exists-on-disk (missing → sat=0.0, verdict="MISSING").

### <a id="fn-load-state"></a>`fn load_state`

```hexa
pub fn load_state(state_dir, fname)
```

**Parameters:** `state_dir, fname`  
**Returns:** _inferred_  

Load <state_dir>/<fname> with the anima cascade:
  verdict → closure_verdict → h_star_verdict → verdict_empirical
   → overall_pass (bool) → pass (bool) → smoke_result → NO_VERDICT

### <a id="fn-digest"></a>`fn digest`

```hexa
pub fn digest(path)
```

**Parameters:** `path`  
**Returns:** _inferred_  

sha256 of a file via shell. macOS → `shasum -a 256`; linux falls back to
`sha256sum`. Returns "" if the file is absent (or if neither tool is
available, in which case the first exec also returns "").

---

← [Back to stdlib index](README.md)
