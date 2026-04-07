// ═══════════════════════════════════════════════════════════
//  HEXA C Runtime Library — Phase 5 self-hosting support
//  Provides: tagged values, strings, arrays, maps, GC
// ═══════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// ── Tagged Value ─────────────────────────────────────────
// All HEXA values are represented as tagged unions (NaN-boxing alternative)

typedef enum {
    TAG_INT = 0, TAG_FLOAT, TAG_BOOL, TAG_STR, TAG_VOID,
    TAG_ARRAY, TAG_MAP, TAG_FN, TAG_CHAR
} HexaTag;

typedef struct HexaVal {
    HexaTag tag;
    union {
        int64_t i;
        double f;
        int b;
        char* s;         // owned string (malloc'd)
        struct {
            struct HexaVal* items;
            int len;
            int cap;
        } arr;
        struct {
            char** keys;
            struct HexaVal* vals;
            int len;
            int cap;
        } map;
        struct {
            void* fn_ptr;
            int arity;
        } fn;
    };
} HexaVal;

// ── Constructors ─────────────────────────────────────────

HexaVal hexa_int(int64_t n) { return (HexaVal){.tag=TAG_INT, .i=n}; }
HexaVal hexa_float(double f) { return (HexaVal){.tag=TAG_FLOAT, .f=f}; }
HexaVal hexa_bool(int b) { return (HexaVal){.tag=TAG_BOOL, .b=b}; }
HexaVal hexa_void() { return (HexaVal){.tag=TAG_VOID}; }

HexaVal hexa_str(const char* s) {
    HexaVal v = {.tag=TAG_STR};
    v.s = strdup(s);
    return v;
}

HexaVal hexa_str_own(char* s) {
    return (HexaVal){.tag=TAG_STR, .s=s};
}

// ── Array operations ─────────────────────────────────────

HexaVal hexa_array_new() {
    HexaVal v = {.tag=TAG_ARRAY};
    v.arr.items = NULL; v.arr.len = 0; v.arr.cap = 0;
    return v;
}

HexaVal hexa_array_push(HexaVal arr, HexaVal item) {
    HexaVal result = {.tag=TAG_ARRAY};
    int new_len = arr.arr.len + 1;
    int new_cap = new_len > arr.arr.cap ? (new_len * 2) : arr.arr.cap;
    result.arr.items = malloc(sizeof(HexaVal) * new_cap);
    if (arr.arr.len > 0) memcpy(result.arr.items, arr.arr.items, sizeof(HexaVal) * arr.arr.len);
    result.arr.items[arr.arr.len] = item;
    result.arr.len = new_len;
    result.arr.cap = new_cap;
    return result;
}

HexaVal hexa_array_get(HexaVal arr, int64_t idx) {
    if (idx < 0) idx += arr.arr.len;
    if (idx < 0 || idx >= arr.arr.len) {
        fprintf(stderr, "index %lld out of bounds (len %d)\n", (long long)idx, arr.arr.len);
        exit(1);
    }
    return arr.arr.items[idx];
}

int hexa_len(HexaVal v) {
    if (v.tag == TAG_STR) return strlen(v.s);
    if (v.tag == TAG_ARRAY) return v.arr.len;
    if (v.tag == TAG_MAP) return v.map.len;
    return 0;
}

// ── Map operations ───────────────────────────────────────

HexaVal hexa_map_new() {
    HexaVal v = {.tag=TAG_MAP};
    v.map.keys = NULL; v.map.vals = NULL; v.map.len = 0; v.map.cap = 0;
    return v;
}

HexaVal hexa_map_set(HexaVal m, const char* key, HexaVal val) {
    // Check if key exists
    for (int i = 0; i < m.map.len; i++) {
        if (strcmp(m.map.keys[i], key) == 0) {
            m.map.vals[i] = val;
            return m;
        }
    }
    // Add new key
    int new_len = m.map.len + 1;
    if (new_len > m.map.cap) {
        int new_cap = new_len * 2;
        m.map.keys = realloc(m.map.keys, sizeof(char*) * new_cap);
        m.map.vals = realloc(m.map.vals, sizeof(HexaVal) * new_cap);
        m.map.cap = new_cap;
    }
    m.map.keys[m.map.len] = strdup(key);
    m.map.vals[m.map.len] = val;
    m.map.len = new_len;
    return m;
}

HexaVal hexa_map_get(HexaVal m, const char* key) {
    for (int i = 0; i < m.map.len; i++) {
        if (strcmp(m.map.keys[i], key) == 0) return m.map.vals[i];
    }
    fprintf(stderr, "map key '%s' not found\n", key);
    return hexa_void();
}

// ── String operations ────────────────────────────────────

HexaVal hexa_str_concat(HexaVal a, HexaVal b) {
    char* sa = a.tag == TAG_STR ? a.s : "";
    char* sb = b.tag == TAG_STR ? b.s : "";
    char* result = malloc(strlen(sa) + strlen(sb) + 1);
    strcpy(result, sa); strcat(result, sb);
    return hexa_str_own(result);
}

HexaVal hexa_str_chars(HexaVal s) {
    HexaVal arr = hexa_array_new();
    if (s.tag != TAG_STR) return arr;
    for (int i = 0; s.s[i]; i++) {
        char buf[2] = {s.s[i], 0};
        arr = hexa_array_push(arr, hexa_str(buf));
    }
    return arr;
}

int hexa_str_contains(HexaVal s, HexaVal sub) {
    return strstr(s.s, sub.s) != NULL;
}

int hexa_str_eq(HexaVal a, HexaVal b) {
    if (a.tag != TAG_STR || b.tag != TAG_STR) return 0;
    return strcmp(a.s, b.s) == 0;
}

// ── Printing ─────────────────────────────────────────────

void hexa_print_val(HexaVal v) {
    switch (v.tag) {
        case TAG_INT: printf("%lld", (long long)v.i); break;
        case TAG_FLOAT: printf("%g", v.f); break;
        case TAG_BOOL: printf("%s", v.b ? "true" : "false"); break;
        case TAG_STR: printf("%s", v.s); break;
        case TAG_VOID: printf("void"); break;
        case TAG_ARRAY:
            printf("[");
            for (int i = 0; i < v.arr.len; i++) {
                if (i > 0) printf(", ");
                hexa_print_val(v.arr.items[i]);
            }
            printf("]");
            break;
        default: printf("<value>"); break;
    }
}

void hexa_println(HexaVal v) { hexa_print_val(v); printf("\n"); }

// ── to_string ────────────────────────────────────────────

HexaVal hexa_to_string(HexaVal v) {
    char buf[64];
    switch (v.tag) {
        case TAG_INT: snprintf(buf, 64, "%lld", (long long)v.i); return hexa_str(buf);
        case TAG_FLOAT: snprintf(buf, 64, "%g", v.f); return hexa_str(buf);
        case TAG_BOOL: return hexa_str(v.b ? "true" : "false");
        case TAG_STR: return v;
        case TAG_VOID: return hexa_str("void");
        default: return hexa_str("<value>");
    }
}

// ── Truthy check ─────────────────────────────────────────

int hexa_truthy(HexaVal v) {
    switch (v.tag) {
        case TAG_BOOL: return v.b;
        case TAG_INT: return v.i != 0;
        case TAG_STR: return v.s[0] != 0;
        case TAG_VOID: return 0;
        default: return 1;
    }
}

// ── Type checking ────────────────────────────────────────

HexaVal hexa_type_of(HexaVal v) {
    switch (v.tag) {
        case TAG_INT: return hexa_str("int");
        case TAG_FLOAT: return hexa_str("float");
        case TAG_BOOL: return hexa_str("bool");
        case TAG_STR: return hexa_str("string");
        case TAG_VOID: return hexa_str("void");
        case TAG_ARRAY: return hexa_str("array");
        case TAG_MAP: return hexa_str("map");
        default: return hexa_str("unknown");
    }
}

// ── Polymorphic add (int + or string concat) ─────────────

HexaVal hexa_add(HexaVal a, HexaVal b) {
    if (a.tag == TAG_INT && b.tag == TAG_INT) return hexa_int(a.i + b.i);
    if (a.tag == TAG_FLOAT || b.tag == TAG_FLOAT) {
        double fa = a.tag == TAG_FLOAT ? a.f : (double)a.i;
        double fb = b.tag == TAG_FLOAT ? b.f : (double)b.i;
        return hexa_float(fa + fb);
    }
    HexaVal sa = hexa_to_string(a);
    HexaVal sb = hexa_to_string(b);
    return hexa_str_concat(sa, sb);
}

// ── Polymorphic equality ─────────────────────────────────

HexaVal hexa_eq(HexaVal a, HexaVal b) {
    if (a.tag != b.tag) return hexa_bool(0);
    switch (a.tag) {
        case TAG_INT: return hexa_bool(a.i == b.i);
        case TAG_FLOAT: return hexa_bool(a.f == b.f);
        case TAG_BOOL: return hexa_bool(a.b == b.b);
        case TAG_STR: return hexa_bool(strcmp(a.s, b.s) == 0);
        case TAG_VOID: return hexa_bool(1);
        default: return hexa_bool(0);
    }
}

// ── File I/O ─────────────────────────────────────────────

HexaVal hexa_read_file(HexaVal path) {
    FILE* f = fopen(path.s, "r");
    if (!f) return hexa_str("");
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);
    return hexa_str_own(buf);
}

HexaVal hexa_write_file(HexaVal path, HexaVal content) {
    FILE* f = fopen(path.s, "w");
    if (!f) return hexa_bool(0);
    fputs(content.s, f);
    fclose(f);
    return hexa_bool(1);
}
