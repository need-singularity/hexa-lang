# `tokenize/tokenizer_spec`

_tokenize/tokenizer_spec_

**Source:** [`stdlib/tokenize/tokenizer_spec.hexa`](../../stdlib/tokenize/tokenizer_spec.hexa)  

## Overview

Loader for HuggingFace-style tokenizer.json (BPE / WordLevel / WordPiece / Unigram).

Motivation: base model swap-in (ext-7) needs a reproducible way to read
a tokenizer manifest without pulling in a python/rust dependency. This
module provides struct + parser + O(1) lookup. Full BPE encoding (apply
merges to arbitrary text) is deliberately out of scope — the loader is
the swap-in primitive; encoding lives in a downstream module.

Public API:
  pub fn parse_tokenizer(json_text: string) -> TokenizerSpec
  pub fn load_tokenizer(path: string)        -> TokenizerSpec
  pub fn encode_token(spec, token: string)   -> int     (-1 if missing)
  pub fn decode_id(spec, id: int)            -> string  ("" if missing)
  pub fn vocab_size(spec)                    -> int
  pub fn is_special(spec, id: int)           -> bool
  pub fn validate_tokenizer(spec)            -> [string]

Validation error codes:
  E1  model_type unknown (must be BPE|WordPiece|Unigram|WordLevel)
  E2  vocab empty
  E3  BPE model with empty merges
  E4  added_token id collides with vocab id
  E5  vocab contains duplicate ids

## Functions

| Function | Returns |
|---|---|
| [`parse_tokenizer`](#fn-parse-tokenizer) | `TokenizerSpec` |
| [`load_tokenizer`](#fn-load-tokenizer) | `TokenizerSpec` |
| [`encode_token`](#fn-encode-token) | `int` |
| [`decode_id`](#fn-decode-id) | `string` |
| [`vocab_size`](#fn-vocab-size) | `int` |
| [`is_special`](#fn-is-special) | `bool` |

### <a id="fn-parse-tokenizer"></a>`fn parse_tokenizer`

```hexa
pub fn parse_tokenizer(json_text: string) -> TokenizerSpec
```

**Parameters:** `json_text: string`  
**Returns:** `TokenizerSpec`  

_No docstring._

### <a id="fn-load-tokenizer"></a>`fn load_tokenizer`

```hexa
pub fn load_tokenizer(path: string) -> TokenizerSpec
```

**Parameters:** `path: string`  
**Returns:** `TokenizerSpec`  

_No docstring._

### <a id="fn-encode-token"></a>`fn encode_token`

```hexa
pub fn encode_token(spec: TokenizerSpec, token: string) -> int
```

**Parameters:** `spec: TokenizerSpec, token: string`  
**Returns:** `int`  

_No docstring._

### <a id="fn-decode-id"></a>`fn decode_id`

```hexa
pub fn decode_id(spec: TokenizerSpec, id: int) -> string
```

**Parameters:** `spec: TokenizerSpec, id: int`  
**Returns:** `string`  

_No docstring._

### <a id="fn-vocab-size"></a>`fn vocab_size`

```hexa
pub fn vocab_size(spec: TokenizerSpec) -> int
```

**Parameters:** `spec: TokenizerSpec`  
**Returns:** `int`  

_No docstring._

### <a id="fn-is-special"></a>`fn is_special`

```hexa
pub fn is_special(spec: TokenizerSpec, id: int) -> bool
```

**Parameters:** `spec: TokenizerSpec, id: int`  
**Returns:** `bool`  

_No docstring._

---

← [Back to stdlib index](README.md)
