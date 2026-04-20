// hxtok.h — Qwen2.5 BPE tokenizer C-FFI surface (v1)
//
// Frozen ABI per anima/docs/alm_r12_hxtok_bpe_proposal_20260420.md §3.
// 7 공개 함수 + 5 special-token name literals.
//
// Build:
//   Linux: gcc -O2 -fPIC -shared hxtok.c -o libhxtok.so
//   macOS: clang -O2 -fPIC -shared hxtok.c -o libhxtok.dylib
//
// Call pattern: hexa FFI passes pointers as int64; strings as const char*
// (null-terminated).
//
// .hexanoport — native shim, not a hexa codegen target.

#ifndef HXTOK_H
#define HXTOK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HxTok HxTok;

// Load tokenizer.json. Returns non-NULL handle on success, NULL on any
// failure (file missing, JSON malformed, vocab_size mismatch).
HxTok* hxtok_load(const char* path, int expected_vocab_size);

// Free handle + all owned memory. Idempotent on NULL.
void hxtok_free(HxTok* h);

// Encode UTF-8 text to token ids. Writes up to max_out ids; returns the
// full count (may exceed max_out — caller re-calls with larger buffer).
int hxtok_encode(HxTok* h, const char* text, int text_len,
                 int64_t* out_ids, int max_out);

// Special-token id by name ("endoftext", "im_start", "im_end",
// "tool_call_beg", "tool_call_end"). Returns -1 on unknown name.
int hxtok_special_id(HxTok* h, const char* name);

int hxtok_vocab_size(HxTok* h);
int hxtok_merges_count(HxTok* h);
int hxtok_version_v1(void);

#ifdef __cplusplus
}
#endif
#endif /* HXTOK_H */
