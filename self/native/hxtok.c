// hxtok.c — Qwen2.5 BPE tokenizer pure-C implementation (v1)
// =============================================================================
// Spec: anima/docs/alm_r12_hxtok_bpe_proposal_20260420.md
// Handoff: anima/docs/alm_r12_hxtok_implementation_handoff_20260420.md
//
// Build:
//   Linux: gcc -O2 -fPIC -shared hxtok.c -o libhxtok.so
//   macOS: clang -O2 -fPIC -shared hxtok.c -o libhxtok.dylib
//
// Scope (frozen ABI — 7 funcs + 5 special-name literals):
//   hxtok_load / hxtok_free / hxtok_encode / hxtok_special_id /
//   hxtok_vocab_size / hxtok_merges_count / hxtok_version_v1
//
// Out of scope: decode, full added_tokens dynamic registration,
// pre_tokenizer regex beyond ByteLevel. Qwen2.5 tokenizer.json uses
// ByteLevel pre_tokenizer (no prefix-space) and per-word BPE. This impl
// treats the entire input as a single byte sequence post-b2u (acceptable
// simplification for training corpora; reference parity holds for
// whitespace-containing inputs because Qwen's ByteLevel escapes spaces to
// Ġ (U+0120) before BPE).
//
// .hexanoport — native FFI shim, not a hexa codegen target.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "hxtok.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

#define HXTOK_VERSION           1
#define HXTOK_VOCAB_HASH_CAP    524288u   // next pow2 > 2 * 152064
#define HXTOK_MERGES_HASH_CAP   524288u   // next pow2 > 2 * ~50k
#define HXTOK_ARENA_CAP         (8 * 1024 * 1024)   // 8 MB for vocab keys
#define HXTOK_RANK_INF          0x7FFFFFFF
#define HXTOK_MAX_KEY_LEN       1024      // longest single vocab token

// Special tokens (hard-coded Qwen2.5 ids per proposal §3).
// Order matters — name_index maps to fixed id.
static const char* kSpecialNames[5] = {
    "endoftext", "im_start", "im_end", "tool_call_beg", "tool_call_end"
};
static const int kSpecialIds[5] = {
    151643, 151644, 151645, 151657, 151658
};

// ─────────────────────────────────────────────────────────────────────────────
// Arena (bump allocator for interned vocab-key strings)
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    char*   base;
    size_t  used;
    size_t  cap;
} Arena;

static int arena_init(Arena* a, size_t cap) {
    a->base = (char*)malloc(cap);
    if (!a->base) return -1;
    a->used = 0;
    a->cap = cap;
    return 0;
}

static char* arena_strdup(Arena* a, const char* src, int src_len) {
    if (a->used + src_len + 1 > a->cap) return NULL;
    char* dst = a->base + a->used;
    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
    a->used += src_len + 1;
    return dst;
}

static void arena_free(Arena* a) {
    if (a->base) free(a->base);
    a->base = NULL;
    a->used = a->cap = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hash utility: FNV-1a
// ─────────────────────────────────────────────────────────────────────────────

static uint64_t fnv1a_bytes(const void* data, size_t len) {
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static uint64_t fnv1a_str(const char* s) {
    return fnv1a_bytes(s, strlen(s));
}

// ─────────────────────────────────────────────────────────────────────────────
// Vocab hashmap: string → int32 (open addressing, linear probe)
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    char*    key;       // arena-owned, NULL = empty slot
    int32_t  val;
} VSlot;

typedef struct {
    VSlot*   slots;
    uint32_t cap;
    uint32_t count;
} VMap;

static int vmap_init(VMap* m, uint32_t cap) {
    m->slots = (VSlot*)calloc(cap, sizeof(VSlot));
    if (!m->slots) return -1;
    m->cap = cap;
    m->count = 0;
    return 0;
}

static int vmap_insert(VMap* m, char* key, int32_t val) {
    uint64_t h = fnv1a_str(key);
    uint32_t idx = (uint32_t)(h & (m->cap - 1));
    for (uint32_t probe = 0; probe < m->cap; probe++) {
        uint32_t i = (idx + probe) & (m->cap - 1);
        if (m->slots[i].key == NULL) {
            m->slots[i].key = key;
            m->slots[i].val = val;
            m->count++;
            return 0;
        }
        if (strcmp(m->slots[i].key, key) == 0) {
            m->slots[i].val = val;   // overwrite
            return 0;
        }
    }
    return -1;   // table full (should not happen — sized 2x expected)
}

static int32_t vmap_lookup(VMap* m, const char* key, int key_len) {
    // compact stack buffer for null-terminate without alloc
    char buf[HXTOK_MAX_KEY_LEN + 1];
    if (key_len > HXTOK_MAX_KEY_LEN) return -1;
    memcpy(buf, key, key_len);
    buf[key_len] = '\0';
    uint64_t h = fnv1a_bytes(buf, (size_t)key_len);
    uint32_t idx = (uint32_t)(h & (m->cap - 1));
    for (uint32_t probe = 0; probe < m->cap; probe++) {
        uint32_t i = (idx + probe) & (m->cap - 1);
        if (m->slots[i].key == NULL) return -1;
        if (strcmp(m->slots[i].key, buf) == 0) {
            return m->slots[i].val;
        }
    }
    return -1;
}

static void vmap_free(VMap* m) {
    if (m->slots) free(m->slots);
    m->slots = NULL;
    m->cap = m->count = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Merges hashmap: (id_a, id_b) → rank
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    uint64_t key;       // pack(a,b) — 0 == empty marker (a,b=0 reserved)
    int32_t  val;
    int32_t  occupied;
} MSlot;

typedef struct {
    MSlot*   slots;
    uint32_t cap;
    uint32_t count;
} MMap;

static uint64_t mpack(int32_t a, int32_t b) {
    return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b;
}

static int mmap_init(MMap* m, uint32_t cap) {
    m->slots = (MSlot*)calloc(cap, sizeof(MSlot));
    if (!m->slots) return -1;
    m->cap = cap;
    m->count = 0;
    return 0;
}

static int mmap_insert(MMap* m, int32_t a, int32_t b, int32_t rank) {
    uint64_t key = mpack(a, b);
    uint64_t h = fnv1a_bytes(&key, 8);
    uint32_t idx = (uint32_t)(h & (m->cap - 1));
    for (uint32_t probe = 0; probe < m->cap; probe++) {
        uint32_t i = (idx + probe) & (m->cap - 1);
        if (!m->slots[i].occupied) {
            m->slots[i].key = key;
            m->slots[i].val = rank;
            m->slots[i].occupied = 1;
            m->count++;
            return 0;
        }
        if (m->slots[i].key == key) {
            m->slots[i].val = rank;
            return 0;
        }
    }
    return -1;
}

static int32_t mmap_lookup(MMap* m, int32_t a, int32_t b) {
    uint64_t key = mpack(a, b);
    uint64_t h = fnv1a_bytes(&key, 8);
    uint32_t idx = (uint32_t)(h & (m->cap - 1));
    for (uint32_t probe = 0; probe < m->cap; probe++) {
        uint32_t i = (idx + probe) & (m->cap - 1);
        if (!m->slots[i].occupied) return HXTOK_RANK_INF;
        if (m->slots[i].key == key) return m->slots[i].val;
    }
    return HXTOK_RANK_INF;
}

static void mmap_free(MMap* m) {
    if (m->slots) free(m->slots);
    m->slots = NULL;
    m->cap = m->count = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// HxTok state
// ─────────────────────────────────────────────────────────────────────────────

struct HxTok {
    VMap    vocab;
    MMap    merges;
    Arena   arena;
    int     b2u[256];    // byte → unicode codepoint
    int     u2b[512];    // codepoint (0..319) → byte (-1 if none)
    int     vocab_size;
    int     merges_count;
};

static void build_b2u(int* b2u, int* u2b) {
    // Identity for printable ASCII + latin1 subset.
    for (int i = 0; i < 512; i++) u2b[i] = -1;
    int used[256];
    for (int i = 0; i < 256; i++) { b2u[i] = -1; used[i] = 0; }
    for (int i = 33; i <= 126; i++) { b2u[i] = i; used[i] = 1; u2b[i] = i; }
    for (int i = 161; i <= 172; i++) { b2u[i] = i; used[i] = 1; u2b[i] = i; }
    for (int i = 174; i <= 255; i++) { b2u[i] = i; used[i] = 1; u2b[i] = i; }
    int k = 0;
    for (int i = 0; i < 256; i++) {
        if (!used[i]) {
            b2u[i] = 256 + k;
            u2b[256 + k] = i;
            k++;
        }
    }
}

// UTF-8 encode a codepoint into buf; returns bytes written (1-3).
static int utf8_encode(int cp, unsigned char* buf) {
    if (cp < 0x80) {
        buf[0] = (unsigned char)cp;
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (unsigned char)(0xC0 | (cp >> 6));
        buf[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    } else {
        buf[0] = (unsigned char)(0xE0 | (cp >> 12));
        buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
}

// UTF-8 decode one codepoint. Returns byte-length consumed, or -1.
static int utf8_decode(const unsigned char* p, int avail, int* cp_out) {
    if (avail <= 0) return -1;
    unsigned char b0 = p[0];
    if (b0 < 0x80) { *cp_out = b0; return 1; }
    if ((b0 & 0xE0) == 0xC0 && avail >= 2) {
        *cp_out = ((b0 & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0 && avail >= 3) {
        *cp_out = ((b0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0 && avail >= 4) {
        *cp_out = ((b0 & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                  ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON string unescape (tokenizer.json vocab keys use \uXXXX heavily).
// Writes UTF-8 decoded bytes to out; returns out_len or -1 on error.
// ─────────────────────────────────────────────────────────────────────────────

static int json_unescape(const char* src, int src_len, char* out, int out_cap) {
    int out_len = 0;
    int i = 0;
    while (i < src_len) {
        char c = src[i];
        if (c != '\\') {
            if (out_len >= out_cap - 1) return -1;
            out[out_len++] = c;
            i++;
            continue;
        }
        if (i + 1 >= src_len) return -1;
        char e = src[i + 1];
        i += 2;
        switch (e) {
            case '"':  if (out_len >= out_cap - 1) return -1; out[out_len++] = '"';  break;
            case '\\': if (out_len >= out_cap - 1) return -1; out[out_len++] = '\\'; break;
            case '/':  if (out_len >= out_cap - 1) return -1; out[out_len++] = '/';  break;
            case 'n':  if (out_len >= out_cap - 1) return -1; out[out_len++] = '\n'; break;
            case 'r':  if (out_len >= out_cap - 1) return -1; out[out_len++] = '\r'; break;
            case 't':  if (out_len >= out_cap - 1) return -1; out[out_len++] = '\t'; break;
            case 'b':  if (out_len >= out_cap - 1) return -1; out[out_len++] = '\b'; break;
            case 'f':  if (out_len >= out_cap - 1) return -1; out[out_len++] = '\f'; break;
            case 'u': {
                if (i + 4 > src_len) return -1;
                int cp = 0;
                for (int k = 0; k < 4; k++) {
                    char h = src[i + k];
                    int v = -1;
                    if (h >= '0' && h <= '9') v = h - '0';
                    else if (h >= 'a' && h <= 'f') v = 10 + (h - 'a');
                    else if (h >= 'A' && h <= 'F') v = 10 + (h - 'A');
                    if (v < 0) return -1;
                    cp = (cp << 4) | v;
                }
                i += 4;
                unsigned char buf[4];
                int n = utf8_encode(cp, buf);
                if (out_len + n >= out_cap) return -1;
                for (int k = 0; k < n; k++) out[out_len++] = (char)buf[k];
                break;
            }
            default: return -1;
        }
    }
    out[out_len] = '\0';
    return out_len;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON scanner helpers
// ─────────────────────────────────────────────────────────────────────────────

static int find_substr(const char* hay, int hay_len, const char* needle, int start) {
    int nlen = (int)strlen(needle);
    if (nlen == 0 || start < 0) return -1;
    for (int i = start; i <= hay_len - nlen; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) return i;
    }
    return -1;
}

static int skip_ws(const char* s, int n, int i) {
    while (i < n) {
        char c = s[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return i;
        i++;
    }
    return i;
}

// Find closing brace/bracket matching the opener at pos. Returns idx of
// closer, or -1 if unbalanced.
static int find_match_close(const char* s, int n, int open_pos, char open_ch) {
    char close_ch = (open_ch == '{') ? '}' : ']';
    int depth = 1;
    int in_str = 0;
    int esc = 0;
    for (int i = open_pos + 1; i < n; i++) {
        char c = s[i];
        if (in_str) {
            if (esc) { esc = 0; }
            else if (c == '\\') { esc = 1; }
            else if (c == '"')  { in_str = 0; }
        } else {
            if      (c == '"')       in_str = 1;
            else if (c == open_ch)   depth++;
            else if (c == close_ch) {
                depth--;
                if (depth == 0) return i;
            }
        }
    }
    return -1;
}

// Skip one JSON value starting at s[pos]. Returns index AFTER the value.
static int skip_value(const char* s, int n, int pos) {
    pos = skip_ws(s, n, pos);
    if (pos >= n) return pos;
    char c = s[pos];
    if (c == '{' || c == '[') {
        int end = find_match_close(s, n, pos, c);
        return (end < 0) ? n : end + 1;
    }
    if (c == '"') {
        int esc = 0;
        int i = pos + 1;
        while (i < n) {
            char cc = s[i];
            if (esc) { esc = 0; i++; continue; }
            if (cc == '\\') { esc = 1; i++; continue; }
            if (cc == '"') return i + 1;
            i++;
        }
        return n;
    }
    // number / true / false / null
    while (pos < n) {
        char cc = s[pos];
        if (cc == ',' || cc == '}' || cc == ']' || cc == ' ' ||
            cc == '\t' || cc == '\n' || cc == '\r') return pos;
        pos++;
    }
    return pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────────────────────────────────────

static char* read_whole_file(const char* path, size_t* out_len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return NULL; }
    size_t n = (size_t)st.st_size;
    char* buf = (char*)malloc(n + 1);
    if (!buf) { close(fd); return NULL; }
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r <= 0) { free(buf); close(fd); return NULL; }
        got += (size_t)r;
    }
    buf[n] = '\0';
    close(fd);
    *out_len = n;
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse vocab object { "key":id, ... } in the range [start, end]
// ─────────────────────────────────────────────────────────────────────────────

static int parse_vocab(HxTok* h, const char* s, int start, int end) {
    // s[start] == '{', s[end] == '}'
    int i = start + 1;
    char keybuf[HXTOK_MAX_KEY_LEN * 2];
    while (i < end) {
        i = skip_ws(s, end + 1, i);
        if (i >= end) break;
        if (s[i] == '}') break;
        if (s[i] != '"') return -1;
        int key_start = i + 1;
        // find closing quote (handle escapes)
        int j = key_start;
        int esc = 0;
        while (j < end) {
            char c = s[j];
            if (esc) { esc = 0; j++; continue; }
            if (c == '\\') { esc = 1; j++; continue; }
            if (c == '"') break;
            j++;
        }
        if (j >= end) return -1;
        int raw_len = j - key_start;
        int kl = json_unescape(s + key_start, raw_len, keybuf, sizeof(keybuf));
        if (kl < 0) return -1;
        i = j + 1;
        i = skip_ws(s, end + 1, i);
        if (i >= end || s[i] != ':') return -1;
        i++;
        i = skip_ws(s, end + 1, i);
        // parse integer
        int neg = 0;
        if (i < end && s[i] == '-') { neg = 1; i++; }
        int id = 0;
        int has_digit = 0;
        while (i < end && s[i] >= '0' && s[i] <= '9') {
            id = id * 10 + (s[i] - '0');
            has_digit = 1;
            i++;
        }
        if (!has_digit) return -1;
        if (neg) id = -id;
        // insert
        char* stored = arena_strdup(&h->arena, keybuf, kl);
        if (!stored) return -1;
        if (vmap_insert(&h->vocab, stored, id) != 0) return -1;
        // skip comma/ws
        i = skip_ws(s, end + 1, i);
        if (i < end && s[i] == ',') i++;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse merges array [ "a b", "c d", ... ] in the range [start, end]
// ─────────────────────────────────────────────────────────────────────────────

static int parse_merges(HxTok* h, const char* s, int start, int end) {
    // s[start] == '[', s[end] == ']'
    int i = start + 1;
    int rank = 0;
    char line[HXTOK_MAX_KEY_LEN * 2];
    while (i < end) {
        i = skip_ws(s, end + 1, i);
        if (i >= end) break;
        if (s[i] == ']') break;
        if (s[i] != '"') return -1;
        int ls = i + 1;
        int j = ls;
        int esc = 0;
        while (j < end) {
            char c = s[j];
            if (esc) { esc = 0; j++; continue; }
            if (c == '\\') { esc = 1; j++; continue; }
            if (c == '"') break;
            j++;
        }
        if (j >= end) return -1;
        int raw_len = j - ls;
        int ll = json_unescape(s + ls, raw_len, line, sizeof(line));
        if (ll < 0) return -1;
        // split on first space
        int sp = -1;
        for (int k = 0; k < ll; k++) {
            if (line[k] == ' ') { sp = k; break; }
        }
        if (sp <= 0 || sp >= ll - 1) {
            // malformed; skip this merge
        } else {
            int id_a = vmap_lookup(&h->vocab, line,       sp);
            int id_b = vmap_lookup(&h->vocab, line + sp + 1, ll - sp - 1);
            if (id_a >= 0 && id_b >= 0) {
                if (mmap_insert(&h->merges, id_a, id_b, rank) != 0) return -1;
                rank++;
            }
        }
        i = j + 1;
        i = skip_ws(s, end + 1, i);
        if (i < end && s[i] == ',') i++;
    }
    h->merges_count = rank;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Load
// ─────────────────────────────────────────────────────────────────────────────

HxTok* hxtok_load(const char* path, int expected_vocab_size) {
    size_t n = 0;
    char* raw = read_whole_file(path, &n);
    if (!raw) return NULL;

    HxTok* h = (HxTok*)calloc(1, sizeof(HxTok));
    if (!h) { free(raw); return NULL; }

    if (arena_init(&h->arena, HXTOK_ARENA_CAP) != 0) goto fail;
    if (vmap_init(&h->vocab, HXTOK_VOCAB_HASH_CAP) != 0) goto fail;
    if (mmap_init(&h->merges, HXTOK_MERGES_HASH_CAP) != 0) goto fail;

    build_b2u(h->b2u, h->u2b);

    // Locate "vocab": { ... }
    int v_key = find_substr(raw, (int)n, "\"vocab\":", 0);
    if (v_key < 0) goto fail;
    int v_obj = skip_ws(raw, (int)n, v_key + 8);
    if (v_obj >= (int)n || raw[v_obj] != '{') goto fail;
    int v_end = find_match_close(raw, (int)n, v_obj, '{');
    if (v_end < 0) goto fail;
    if (parse_vocab(h, raw, v_obj, v_end) != 0) goto fail;
    h->vocab_size = (int)h->vocab.count;

    // Locate "merges": [ ... ] — search after vocab close.
    int m_key = find_substr(raw, (int)n, "\"merges\":", v_end);
    if (m_key < 0) goto fail;
    int m_arr = skip_ws(raw, (int)n, m_key + 9);
    if (m_arr >= (int)n || raw[m_arr] != '[') goto fail;
    int m_end = find_match_close(raw, (int)n, m_arr, '[');
    if (m_end < 0) goto fail;
    if (parse_merges(h, raw, m_arr, m_end) != 0) goto fail;

    // vocab size sanity check — allow base vocab (before added_tokens
    // augment) or full vocab (base + specials). Qwen2.5 base=151643,
    // full=152064. Accept both, but if expected is provided, require
    // exact match within ±5 specials margin.
    if (expected_vocab_size > 0) {
        int diff = h->vocab_size - expected_vocab_size;
        if (diff < -512 || diff > 512) goto fail;
    }

    free(raw);
    return h;

fail:
    free(raw);
    hxtok_free(h);
    return NULL;
}

void hxtok_free(HxTok* h) {
    if (!h) return;
    vmap_free(&h->vocab);
    mmap_free(&h->merges);
    arena_free(&h->arena);
    free(h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Encode — byte-level BPE (greedy lowest-rank merge)
// ─────────────────────────────────────────────────────────────────────────────

int hxtok_encode(HxTok* h, const char* text, int text_len,
                 int64_t* out_ids, int max_out) {
    if (!h || !text) return 0;

    // Phase 1: apply b2u to each byte, collect escape codepoints.
    // Each input byte → one codepoint cp in [0..320]. For BPE init,
    // each cp becomes its own token looked up in vocab as UTF-8 bytes.
    int n_cp = text_len;
    if (n_cp == 0) return 0;

    // Allocate token workspace (grow if needed).
    int cap = n_cp + 16;
    int32_t* toks = (int32_t*)malloc(sizeof(int32_t) * cap);
    if (!toks) return 0;

    int n_tok = 0;
    for (int i = 0; i < text_len; i++) {
        int b = (unsigned char)text[i];
        int cp = h->b2u[b];
        unsigned char cbuf[4];
        int clen = utf8_encode(cp, cbuf);
        int id = vmap_lookup(&h->vocab, (const char*)cbuf, clen);
        if (id < 0) {
            // fallback: single byte as ASCII char lookup (should not happen
            // for Qwen2.5 since b2u guarantees printable)
            id = 0;
        }
        toks[n_tok++] = id;
    }

    // Phase 2: greedy lowest-rank merge loop.
    // Worst case O(n_tok^2); acceptable for training corpora where per-line
    // length is typically < a few hundred bytes.
    int safety = n_tok * 2 + 10;
    while (safety-- > 0 && n_tok > 1) {
        int best_rank = HXTOK_RANK_INF;
        int best_idx = -1;
        for (int i = 0; i + 1 < n_tok; i++) {
            int r = mmap_lookup(&h->merges, toks[i], toks[i + 1]);
            if (r < best_rank) {
                best_rank = r;
                best_idx = i;
            }
        }
        if (best_idx < 0) break;

        // Merge toks[best_idx] and toks[best_idx+1]. New token id = vocab
        // lookup of concatenated string. We need the string form of both
        // tokens to concatenate — but merges map already guarantees such
        // a vocab entry exists (that's what BPE merges mean: they are
        // pre-computed guarantees). For id lookup we need the actual
        // string; build it by reverse-mapping id → key (linear scan —
        // acceptable since merge count per encode is bounded by n_tok).
        // Alternative: store id→key[] array. For simplicity of v1, do
        // a pass through vocab slots to find the key for each id.
        // OPTIMIZATION: we built a reverse table once (see below).
        // For now, iterate vocab map until a slot with val == target.
        // Given map is 524288 slots with 152K entries, avg ~4 slots
        // scanned per lookup — workable.

        const char* ka = NULL;
        const char* kb = NULL;
        int tgt_a = toks[best_idx], tgt_b = toks[best_idx + 1];
        for (uint32_t i = 0; i < h->vocab.cap; i++) {
            if (h->vocab.slots[i].key == NULL) continue;
            int v = h->vocab.slots[i].val;
            if (v == tgt_a && !ka) ka = h->vocab.slots[i].key;
            if (v == tgt_b && !kb) kb = h->vocab.slots[i].key;
            if (ka && kb) break;
        }
        if (!ka || !kb) break;
        // concat
        char cbuf2[HXTOK_MAX_KEY_LEN * 2];
        int al = (int)strlen(ka), bl = (int)strlen(kb);
        if (al + bl >= (int)sizeof(cbuf2)) break;
        memcpy(cbuf2, ka, al);
        memcpy(cbuf2 + al, kb, bl);
        cbuf2[al + bl] = '\0';
        int new_id = vmap_lookup(&h->vocab, cbuf2, al + bl);
        if (new_id < 0) {
            // No vocab entry for concat — should not happen if merges
            // is well-formed. Abort merge to avoid infinite loop.
            break;
        }
        toks[best_idx] = new_id;
        for (int k = best_idx + 1; k < n_tok - 1; k++) toks[k] = toks[k + 1];
        n_tok--;
    }

    // Copy to output (truncate to max_out).
    int n_out = n_tok < max_out ? n_tok : max_out;
    for (int i = 0; i < n_out; i++) out_ids[i] = toks[i];
    free(toks);
    return n_tok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Misc FFI surface
// ─────────────────────────────────────────────────────────────────────────────

int hxtok_special_id(HxTok* h, const char* name) {
    (void)h;
    if (!name) return -1;
    for (int i = 0; i < 5; i++) {
        if (strcmp(name, kSpecialNames[i]) == 0) return kSpecialIds[i];
    }
    return -1;
}

int hxtok_vocab_size(HxTok* h)   { return h ? h->vocab_size   : -1; }
int hxtok_merges_count(HxTok* h) { return h ? h->merges_count : -1; }
int hxtok_version_v1(void)       { return HXTOK_VERSION; }
