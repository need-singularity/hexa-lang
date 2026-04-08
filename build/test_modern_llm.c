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

long bench_rms_norm_1k();
long bench_rope_1k();
long bench_swa_100();
long test_rms_norm_scale();
long test_rope_preserves_norm();
long test_swa_shape();
long test_swa_window_1_equals_self();

static hexa_arr x;
static long normed;
static hexa_arr x2;
static long normed2;
static hexa_arr q;
static long q_rope;
static long seq = 6;
static long dim = 4;
static long Q;
static long K;
static long V;
static long swa_out;
static long full_out;
static long s = 4;
static long d = 4;
static long x_block;
static long Wq;
static long Wk;
static long Wv;
static long Wff;
static long x_rms;
static long q_proj;
static long k_proj;
static long v_proj;
static long q_rotated;
static long k_rotated;
static long attn;
static long x_res;
static long x_rms2;
static long ff;
static long x_final;

long bench_rms_norm_1k() {
    long x = randn(64, 0.5);
    long i = 0;
    while ((i < 1000)) {
        rms_norm(x, 8);
        i = (i + 1);
    }
}

long bench_rope_1k() {
    long x = randn(64, 0.3);
    long i = 0;
    while ((i < 1000)) {
        rope(x, 8, 8);
        i = (i + 1);
    }
}

long bench_swa_100() {
    long q = randn(128, 0.1);
    long k = randn(128, 0.1);
    long v = randn(128, 0.1);
    long i = 0;
    while ((i < 100)) {
        sliding_window_attention(q, k, v, 16, 8, 4);
        i = (i + 1);
    }
}

long test_rms_norm_scale() {
    long n = rms_norm(hexa_arr_lit((long[]){(long)(1.0), (long)(1.0), (long)(1.0), (long)(1.0)}, 4), 4);
    if (!((n[0] > 0.99))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "unit vector should be ~unchanged";
    return 0;
}

long test_rope_preserves_norm() {
    hexa_arr x = hexa_arr_lit((long[]){1.0, 0.0, 0.0, 1.0}, 4);
    long r = rope(x, 1, 4);
    long n_before = dot(x, x);
    long n_after = dot(r, r);
    if (!((hexa_abs_i((long)((n_before - n_after))) < 0.01))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "RoPE should preserve norm";
    return 0;
}

long test_swa_shape() {
    long o = sliding_window_attention(randn(12, 0.1), randn(12, 0.1), randn(12, 0.1), 3, 4, 1);
    if (!((hexa_str_len(o) == 12))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "SWA output should be seq×dim";
    return 0;
}

long test_swa_window_1_equals_self() {
    hexa_arr v = hexa_arr_lit((long[]){1.0, 2.0, 3.0, 4.0, 5.0, 6.0}, 6);
    hexa_arr q = hexa_arr_lit((long[]){1.0, 0.0, 0.0, 0.0, 0.0, 1.0}, 6);
    hexa_arr k = q;
    long o = sliding_window_attention(q, k, v, 2, 3, 0);
    if (!((o[0] == 1.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "window=0 pos0 should see only self";
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    x = hexa_arr_lit((long[]){(long)(3.0), (long)(4.0), (long)(0.0), (long)(0.0)}, 4);
    normed = rms_norm(x, 4);
    printf("%s %ld\n", "[1] RMSNorm([3,4,0,0]):", (long)(normed));
    x2 = hexa_arr_lit((long[]){(long)(1.0), (long)(1.0), (long)(1.0), (long)(1.0), (long)(2.0), (long)(2.0), (long)(2.0), (long)(2.0)}, 8);
    normed2 = rms_norm(x2, 4);
    printf("%s %ld\n", "    batch RMSNorm:", (long)(normed2));
    q = hexa_arr_lit((long[]){(long)(1.0), (long)(0.0), (long)(1.0), (long)(0.0), (long)(1.0), (long)(0.0), (long)(1.0), (long)(0.0)}, 8);
    q_rope = rope(q, 2, 4);
    printf("%s\n", "[2] RoPE (2 pos × 4 dim):");
    printf("%s %ld\n", "    pos 0:", (long)(slice(q_rope, 0, 4)));
    printf("%s %ld\n", "    pos 1:", (long)(slice(q_rope, 4, 8)));
    printf("%s %g\n", "    pos0 unchanged:", (double)(((hexa_round((double)((q_rope[0] * 1000))) / 1000) == 1.0)));
    Q = randn(24, 0.3);
    K = randn(24, 0.3);
    V = randn(24, 0.3);
    swa_out = sliding_window_attention(Q, K, V, seq, dim, 2);
    printf("%s\n", "[3] Sliding Window Attention (seq=6, win=2):");
    printf("%s %ld\n", "    pos0 (sees 1 tok):", (long)(slice(swa_out, 0, 4)));
    printf("%s %ld\n", "    pos5 (sees 3 tok):", (long)(slice(swa_out, 20, 24)));
    full_out = attention(Q, K, V, seq, dim, 1);
    printf("%s %ld\n", "    Full causal pos5:", (long)(slice(full_out, 20, 24)));
    printf("%s\n", "    (different — window limits context)");
    printf("%s\n", "\n[4] Mistral Block (RMSNorm + RoPE + SWA):");
    x_block = randn((s * d), 0.3);
    Wq = randn((d * d), 0.25);
    Wk = randn((d * d), 0.25);
    Wv = randn((d * d), 0.25);
    Wff = randn((d * d), 0.25);
    x_rms = rms_norm(x_block, d);
    q_proj = batch_matvec(Wq, x_rms, d, d, s);
    k_proj = batch_matvec(Wk, x_rms, d, d, s);
    v_proj = batch_matvec(Wv, x_rms, d, d, s);
    q_rotated = rope(q_proj, s, d);
    k_rotated = rope(k_proj, s, d);
    attn = sliding_window_attention(q_rotated, k_rotated, v_proj, s, d, 2);
    x_res = mat_add(x_block, attn);
    x_rms2 = rms_norm(x_res, d);
    ff = batch_matvec(Wff, silu(x_rms2), d, d, s);
    x_final = mat_add(x_res, ff);
    printf("%s %ld\n", "    input:", (long)(slice(x_block, 0, 4)));
    printf("%s %ld\n", "    after RoPE+SWA:", (long)(slice(x_res, 0, 4)));
    printf("%s %ld\n", "    after SiLU FFN:", (long)(slice(x_final, 0, 4)));
    run_benchmarks();
    run_tests();
    printf("%s\n", "\n--- modern LLM PASS ---");
    return 0;
}
