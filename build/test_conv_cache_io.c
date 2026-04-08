#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/wait.h>

static const double PI = 3.14159265358979323846;
static const double E = 2.71828182845904523536;
static long hexa_time_unix(void) { return (long)time(NULL); }
static double hexa_clock(void) { return (double)clock() / CLOCKS_PER_SEC; }
static double hexa_sqrt(double x) { return sqrt(x); }
static double hexa_pow(double b, double e) { return pow(b, e); }
static double hexa_abs_f(double x) { return x < 0 ? -x : x; }
static long hexa_abs_i(long x) { return x < 0 ? -x : x; }
static double hexa_sin(double x) { return sin(x); }
static double hexa_cos(double x) { return cos(x); }
static double hexa_tan(double x) { return tan(x); }
static double hexa_log(double x) { return log(x); }
static double hexa_log10(double x) { return log10(x); }
static double hexa_exp(double x) { return exp(x); }
static double hexa_floor(double x) { return floor(x); }
static double hexa_ceil(double x) { return ceil(x); }
static double hexa_round(double x) { return (x >= 0) ? (long)(x + 0.5) : (long)(x - 0.5); }
// hexa_alloc: 단순 malloc wrapper (무제한)
static char* hexa_alloc(size_t n) {
    char* p = (char*)malloc(n);
    if (!p) { fprintf(stderr, "hexa_alloc oom (%zu)\n", n); exit(1); }
    return p;
}
static const char* hexa_concat(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* p = hexa_alloc(la + lb + 1);
    memcpy(p, a, la); memcpy(p + la, b, lb); p[la + lb] = 0;
    return p;
}
static const char* hexa_int_to_str(long v) {
    char* p = hexa_alloc(24);
    snprintf(p, 24, "%ld", v);
    return p;
}
static const char* hexa_float_to_str(double v) {
    char* p = hexa_alloc(32);
    snprintf(p, 32, "%g", v);
    return p;
}
static const char* hexa_substr(const char* s, long a, long b) {
    long sl = (long)strlen(s);
    if (a < 0) a = 0; if (b > sl) b = sl; if (a > b) a = b;
    long n = b - a;
    char* p = hexa_alloc(n + 1);
    memcpy(p, s + a, n); p[n] = 0;
    return p;
}
static long hexa_str_len(const char* s) { return (long)strlen(s); }
static long hexa_contains(const char* h, const char* n) { return strstr(h, n) ? 1 : 0; }
static long hexa_index_of(const char* h, const char* n) {
    const char* p = strstr(h, n); if (!p) return -1;
    return (long)(p - h);
}
static long hexa_starts_with(const char* h, const char* n) {
    size_t ln = strlen(n); return strncmp(h, n, ln) == 0 ? 1 : 0;
}
static long hexa_ends_with(const char* h, const char* n) {
    size_t lh = strlen(h), ln = strlen(n);
    if (ln > lh) return 0; return strcmp(h + lh - ln, n) == 0 ? 1 : 0;
}
static const char* hexa_trim(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    long n = (long)strlen(s);
    while (n > 0 && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\n'||s[n-1]=='\r')) n--;
    char* p = hexa_alloc(n + 1);
    memcpy(p, s, n); p[n] = 0;
    return p;
}
static const char* hexa_to_upper(const char* s) {
    long n = (long)strlen(s); char* p = hexa_alloc(n + 1);
    for (long i = 0; i < n; i++) { char c = s[i]; p[i] = (c>='a'&&c<='z') ? c - 32 : c; }
    p[n] = 0; return p;
}
static const char* hexa_to_lower(const char* s) {
    long n = (long)strlen(s); char* p = hexa_alloc(n + 1);
    for (long i = 0; i < n; i++) { char c = s[i]; p[i] = (c>='A'&&c<='Z') ? c + 32 : c; }
    p[n] = 0; return p;
}
static long hexa_parse_int(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    long sign = 1; if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    long v = 0; while (*s >= '0' && *s <= '9') { v = v*10 + (*s - '0'); s++; }
    return sign * v;
}
#include <stdlib.h>
static double hexa_to_float(const char* s) { return strtod(s, NULL); }
static long* hexa_struct_alloc(const long* items, long n) {
    long* p = (long*)hexa_alloc(n * sizeof(long));
    memcpy(p, items, n * sizeof(long));
    return p;
}
static const char* hexa_env(const char* name) {
    const char* v = getenv(name); return v ? v : "";
}
static const char* hexa_replace(const char* s, const char* old_, const char* new_) {
    long oln = (long)strlen(old_); if (oln == 0) return s;
    long sl = (long)strlen(s), nl = (long)strlen(new_);
    // 결과 최대 길이 = s 길이 * (new/old 비율)
    long cap = sl + 1; if (nl > oln) cap = sl * (nl / oln + 1) + 1;
    char* out = hexa_alloc(cap + 64);
    long oi = 0; const char* cur = s;
    while (*cur) {
        if (strncmp(cur, old_, oln) == 0) {
            memcpy(out + oi, new_, nl); oi += nl; cur += oln;
        } else { out[oi++] = *cur++; }
    }
    out[oi] = 0; return out;
}
static const char* hexa_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return "";
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* p = hexa_alloc(sz + 1);
    fread(p, 1, sz, f); p[sz] = 0; fclose(f);
    return p;
}
static long hexa_write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = strlen(content);
    fwrite(content, 1, n, f); fclose(f);
    return (long)n;
}
static long hexa_file_exists(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return 0; fclose(f); return 1;
}
static long hexa_to_int_str(const char* s) { return strtol(s, NULL, 10); }
static long hexa_append_file(const char* path, const char* content) {
    FILE* f = fopen(path, "ab");
    if (!f) return 0;
    size_t n = strlen(content);
    fwrite(content, 1, n, f); fclose(f);
    return (long)n;
}
static const char* hexa_read_stdin(void) {
    char buf[8192]; size_t total = 0;
    char* out = hexa_alloc(65536);
    size_t r; while ((r = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        if (total + r >= 65535) break;
        memcpy(out + total, buf, r); total += r;
    }
    out[total] = 0; return out;
}
static const char* hexa_exec_with_status(const char* cmd) {
    FILE* f = popen(cmd, "r");
    if (!f) return "-1|";
    char buf[8192]; size_t total = 0;
    char* out = hexa_alloc(16384);
    size_t rd; while ((rd = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (total + rd >= 16000) break;
        memcpy(out + total, buf, rd); total += rd;
    }
    out[total] = 0; int rc = pclose(f);
    while (total > 0 && (out[total-1] == '\n' || out[total-1] == '\r')) { out[--total] = 0; }
    char* formatted = hexa_alloc(total + 32);
    snprintf(formatted, total + 32, "%d|%s", WEXITSTATUS(rc), out);
    return formatted;
}
static const char* hexa_exec(const char* cmd) {
    FILE* f = popen(cmd, "r");
    if (!f) return "";
    char buf[8192]; size_t total = 0;
    char* out = hexa_alloc(8192);
    size_t r; while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (total + r >= 8191) break;
        memcpy(out + total, buf, r); total += r;
    }
    out[total] = 0; pclose(f);
    while (total > 0 && (out[total-1] == '\n' || out[total-1] == '\r')) { out[--total] = 0; }
    return out;
}
typedef struct { long* d; long n; long cap; } hexa_arr;
static hexa_arr hexa_arr_new(void) { hexa_arr a = {NULL, 0, 0}; return a; }
static hexa_arr hexa_arr_lit(const long* items, long n) {
    hexa_arr a; a.d = (long*)malloc((n>0?n:1)*sizeof(long)); a.n = n; a.cap = (n>0?n:1);
    if (n > 0) memcpy(a.d, items, n*sizeof(long));
    return a;
}
static hexa_arr hexa_arr_push(hexa_arr a, long x) {
    if (a.n >= a.cap) {
        a.cap = a.cap ? a.cap * 2 : 4;
        a.d = (long*)realloc(a.d, a.cap * sizeof(long));
    }
    a.d[a.n++] = x;
    return a;
}
static long hexa_arr_len(hexa_arr a) { return a.n; }
static long hexa_arr_get(hexa_arr a, long i) { return a.d[i]; }
static hexa_arr hexa_arr_concat(hexa_arr a, hexa_arr b) {
    hexa_arr r; r.n = a.n + b.n; r.cap = r.n > 0 ? r.n : 1;
    r.d = (long*)malloc(r.cap * sizeof(long));
    if (a.n > 0) memcpy(r.d, a.d, a.n * sizeof(long));
    if (b.n > 0) memcpy(r.d + a.n, b.d, b.n * sizeof(long));
    return r;
}
static hexa_arr hexa_chars(const char* s) {
    long n = (long)strlen(s);
    hexa_arr a; a.d = (long*)malloc((n>0?n:1)*sizeof(long)); a.n = n; a.cap = (n>0?n:1);
    for (long i = 0; i < n; i++) a.d[i] = (long)(unsigned char)s[i];
    return a;
}
#include <ctype.h>
static long hexa_is_alpha(long c) { return (long)(isalpha((int)c) ? 1 : 0); }
static long hexa_is_alnum(long c) { return (long)(isalnum((int)c) ? 1 : 0); }
static int hexa_main_argc = 0;
static char** hexa_main_argv = NULL;
static hexa_arr hexa_args(void) {
    hexa_arr a; a.n = hexa_main_argc; a.cap = hexa_main_argc;
    a.d = (long*)malloc((hexa_main_argc>0?hexa_main_argc:1)*sizeof(long));
    for (int i = 0; i < hexa_main_argc; i++) a.d[i] = (long)hexa_main_argv[i];
    return a;
}
static const char* hexa_arg(long i) {
    if (i < 0 || i >= hexa_main_argc) return "";
    return hexa_main_argv[i];
}
static long hexa_argc(void) { return (long)hexa_main_argc; }
static hexa_arr hexa_split(const char* s, const char* sep) {
    hexa_arr a = hexa_arr_new();
    long sl = (long)strlen(sep); if (sl == 0) { return hexa_arr_push(a, (long)s); }
    const char* cur = s;
    while (1) {
        const char* hit = strstr(cur, sep);
        if (!hit) {
            long ln = (long)strlen(cur);
            char* p = hexa_alloc(ln + 1);
            memcpy(p, cur, ln); p[ln] = 0;
            a = hexa_arr_push(a, (long)p);
            break;
        }
        long ln = hit - cur;
        char* p = hexa_alloc(ln + 1);
        memcpy(p, cur, ln); p[ln] = 0;
        a = hexa_arr_push(a, (long)p);
        cur = hit + sl;
    }
    return a;
}

long test_conv1d_identity();
long test_pool_reduces();
long test_kv_cache_grows();
long test_save_load_roundtrip();

static hexa_arr signal;
static hexa_arr kernel;
static long conv_out;
static long conv_s2;
static long pooled;
static long dim = 4;
static hexa_arr k_cache;
static hexa_arr v_cache;
static long k1;
static long v1;
static long k2;
static long v2;
static long q_new;
static long attn_out;
static long step = 0;
static long attn5;
static long weights;
static long loaded;
static long vocab = 8;
static long d = 4;
static long W_e;
static long W_q;
static long W_k;
static long W_v;
static long W_out;
static hexa_arr kc;
static hexa_arr vc;
static hexa_arr gen;
static long t = 0;
static hexa_arr phi_history;
static hexa_arr trend_kernel;
static long trend;

long test_conv1d_identity() {
    long out = conv1d(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0)}, 3), hexa_arr_lit((long[]){(long)(1.0)}, 1), 3, 1, 1);
    if (!((out[0] == 1.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "identity kernel";
    return 0;
}

long test_pool_reduces() {
    long p = max_pool1d(hexa_arr_lit((long[]){(long)(1.0), (long)(5.0), (long)(3.0), (long)(2.0)}, 4), 2, 2);
    if (!((p[0] == 5.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "max of [1,5] = 5";
    return 0;
}

long test_kv_cache_grows() {
    long c = kv_cache_append(hexa_arr_new(), hexa_arr_lit((long[]){(long)(1.0), (long)(2.0)}, 2), 2);
    long c2 = kv_cache_append(c, hexa_arr_lit((long[]){(long)(3.0), (long)(4.0)}, 2), 2);
    if (!((hexa_str_len(c2) == 4))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "cache should grow";
    return 0;
}

long test_save_load_roundtrip() {
    save_array(hexa_arr_lit((long[]){(long)(42.0), (long)((-1.5))}, 2), "/tmp/hexa_test_rt.json");
    long loaded = load_array("/tmp/hexa_test_rt.json");
    if (!((loaded[0] == 42.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "roundtrip should preserve values";
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    signal = hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0), (long)(5.0), (long)(6.0), (long)(7.0), (long)(8.0)}, 8);
    kernel = hexa_arr_lit((long[]){(long)(1.0), (long)(0.0), (long)((-1.0))}, 3);
    conv_out = conv1d(signal, kernel, 8, 3, 1);
    printf("%s %ld\n", "[1] conv1d(signal, edge_kernel):", (long)(conv_out));
    conv_s2 = conv1d(signal, kernel, 8, 3, 2);
    printf("%s %ld\n", "    stride=2:", (long)(conv_s2));
    pooled = max_pool1d(hexa_arr_lit((long[]){(long)(1.0), (long)(3.0), (long)(2.0), (long)(5.0), (long)(4.0), (long)(6.0)}, 6), 2, 2);
    printf("%s %ld\n", "[2] max_pool1d([1,3,2,5,4,6], pool=2, stride=2):", (long)(pooled));
    k_cache = hexa_arr_new();
    v_cache = hexa_arr_new();
    k1 = randn(dim, 0.3);
    v1 = randn(dim, 0.3);
    k_cache = kv_cache_append(k_cache, k1, dim);
    v_cache = kv_cache_append(v_cache, v1, dim);
    printf("%s %ld\n", "[3] KV cache after token 1: len=", (long)((k_cache.n / dim)));
    k2 = randn(dim, 0.3);
    v2 = randn(dim, 0.3);
    k_cache = kv_cache_append(k_cache, k2, dim);
    v_cache = kv_cache_append(v_cache, v2, dim);
    printf("%s %ld\n", "    KV cache after token 2: len=", (long)((k_cache.n / dim)));
    q_new = randn(dim, 0.3);
    attn_out = attention_cached(q_new, k_cache, v_cache, 2, dim);
    printf("%s %ld\n", "    Cached attention output:", (long)(attn_out));
    while ((step < 3)) {
        k_cache = kv_cache_append(k_cache, randn(dim, 0.3), dim);
        v_cache = kv_cache_append(v_cache, randn(dim, 0.3), dim);
        step = (step + 1);
    }
    attn5 = attention_cached(q_new, k_cache, v_cache, 5, dim);
    printf("%s %ld\n", "    Cached attention (5 tokens):", (long)(attn5));
    weights = randn(16, 0.5);
    printf("%s\n", "[4] Saving weights (16 params)...");
    save_array(weights, "/tmp/hexa_test_weights.json");
    loaded = load_array("/tmp/hexa_test_weights.json");
    printf("%s %ld %s\n", "    Loaded:", (long)(hexa_str_len(loaded)), "params");
    printf("%s %ld %ld\n", "    Match:", (long)((weights[0] == loaded[0])), (long)((weights[15] == loaded[15])));
    printf("%s\n", "\n[5] Anima KV-cached generation (5 tokens):");
    W_e = randn((vocab * d), 0.5);
    W_q = randn((d * d), 0.25);
    W_k = randn((d * d), 0.25);
    W_v = randn((d * d), 0.25);
    W_out = randn((vocab * d), 0.25);
    kc = hexa_arr_new();
    vc = hexa_arr_new();
    gen = hexa_arr_lit((long[]){(long)(1)}, 1);
    while ((t < 5)) {
        long tok = gen.d[(gen.n - 1)];
        long emb = slice(W_e, (tok * d), ((tok * d) + d));
        long q = matvec(W_q, emb, d, d);
        long k = matvec(W_k, emb, d, d);
        long v = matvec(W_v, emb, d, d);
        kc = kv_cache_append(kc, k, d);
        vc = kv_cache_append(vc, v, d);
        long attn = attention_cached(q, kc, vc, gen.n, d);
        long hidden = mat_add(emb, attn);
        long logits = matvec(W_out, hidden, vocab, d);
        long next = sample_token(logits, 0.7);
        gen = hexa_arr_concat(gen, hexa_arr_lit((long[]){(long)(next)}, 1));
        printf("%s %ld %s %ld %s %ld\n", "    t=", (long)(t), "tok=", (long)(next), "cache_len=", (long)((kc.n / d)));
        t = (t + 1);
    }
    printf("%s [arr len=%ld]\n", "    generated:", (long)((gen).n));
    phi_history = hexa_arr_lit((long[]){(long)(0.1), (long)(0.15), (long)(0.2), (long)(0.18), (long)(0.25), (long)(0.3), (long)(0.28), (long)(0.35), (long)(0.4), (long)(0.38)}, 10);
    trend_kernel = hexa_arr_lit((long[]){(long)(0.33), (long)(0.33), (long)(0.34)}, 3);
    trend = conv1d(phi_history, trend_kernel, 10, 3, 1);
    printf("%s %ld\n", "[6] Φ trend (MA-3):", (long)(trend));
    run_tests();
    printf("%s\n", "\n--- conv+cache+io PASS ---");
    return 0;
}
