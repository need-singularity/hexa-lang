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

long test_bytes_roundtrip();
long test_moe_gate_topk();
long test_moe_gate_sums_to_one();
long test_ewc_zero_when_same();
long test_fisher_mean();

static const char* text = "Hello Anima";
static long encoded;
static long decoded;
static const char* kr = "의식";
static long kr_enc;
static hexa_arr input;
static long gate_W;
static long routing;
static long expert_idx;
static long gate_vals;
static hexa_arr expert_out_0;
static hexa_arr expert_out_1;
static long combined_out;
static hexa_arr old_weights;
static hexa_arr new_weights;
static hexa_arr fisher;
static long penalty;
static hexa_arr grads_sq;
static long fisher_diag;
static long n_experts = 3;
static long dim = 4;
static hexa_arr W_experts;
static long i = 0;
static long W_gate;
static long x_moe;
static long route;
static long sel;
static long gates;
static hexa_arr expert_results;
static long e = 0;
static long moe_out;
static long task1_weights;
static long task1_fisher;
static long fi = 0;
static hexa_arr pos_fisher;
static long current;
static long ewc;

long test_bytes_roundtrip() {
    const char* s = "test123";
    if (!((bytes_decode(bytes_encode(s)) == s))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "roundtrip should preserve";
    return 0;
}

long test_moe_gate_topk() {
    long r = moe_gate(hexa_arr_lit((long[]){(long)(1.0), (long)(0.0)}, 2), randn(8, 0.5), 4, 2);
    if (!((hexa_str_len(r[0]) == 2))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "top-2 should return 2 experts";
    return 0;
}

long test_moe_gate_sums_to_one() {
    long r = moe_gate(hexa_arr_lit((long[]){(long)(1.0), (long)(0.0)}, 2), randn(8, 0.5), 4, 2);
    long s = (r[1][0] + r[1][1]);
    if (!((s > 0.99))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "gate values should sum to ~1";
    return 0;
}

long test_ewc_zero_when_same() {
    if (!((ewc_penalty(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0)}, 2), hexa_arr_lit((long[]){(long)(1.0), (long)(2.0)}, 2), hexa_arr_lit((long[]){(long)(1.0), (long)(1.0)}, 2), 1.0) == 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "no penalty when unchanged";
    return 0;
}

long test_fisher_mean() {
    long f = fisher_diagonal(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0)}, 4), 2, 2);
    if (!((f[0] == 2.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "mean of [1,3] = 2";
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    encoded = bytes_encode(text);
    printf("%s %ld\n", "[1] bytes_encode(\"Hello Anima\"):", (long)(encoded));
    decoded = bytes_decode(encoded);
    printf("%s %ld\n", "    bytes_decode:", (long)(decoded));
    printf("%s %ld\n", "    roundtrip:", (long)((text == decoded)));
    kr_enc = bytes_encode(kr);
    printf("%s %ld %s %ld\n", "    korean bytes:", (long)(kr_enc), "len:", (long)(hexa_str_len(kr_enc)));
    printf("%s %ld\n", "    decoded:", (long)(bytes_decode(kr_enc)));
    printf("%s\n", "\n[2] MoE Routing (4 experts, top-2):");
    input = hexa_arr_lit((long[]){(long)(0.5), (long)((-0.3)), (long)(0.8), (long)(0.1)}, 4);
    gate_W = randn(16, 0.5);
    routing = moe_gate(input, gate_W, 4, 2);
    expert_idx = routing[0];
    gate_vals = routing[1];
    printf("%s %ld\n", "    selected experts:", (long)(expert_idx));
    printf("%s %ld\n", "    gate weights:", (long)(gate_vals));
    printf("%s %ld %s\n", "    sum:", (long)((gate_vals[0] + gate_vals[1])), "(should be ~1.0)");
    expert_out_0 = hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0)}, 4);
    expert_out_1 = hexa_arr_lit((long[]){(long)(5.0), (long)(6.0), (long)(7.0), (long)(8.0)}, 4);
    combined_out = moe_combine(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0), (long)(5.0), (long)(6.0), (long)(7.0), (long)(8.0)}, 8), gate_vals);
    printf("%s %ld\n", "    combined output:", (long)(combined_out));
    printf("%s\n", "\n[3] Elastic Weight Consolidation:");
    old_weights = hexa_arr_lit((long[]){(long)(0.5), (long)((-0.3)), (long)(0.8), (long)(0.1)}, 4);
    new_weights = hexa_arr_lit((long[]){(long)(0.6), (long)((-0.2)), (long)(0.7), (long)(0.2)}, 4);
    fisher = hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(0.5), (long)(1.5)}, 4);
    penalty = ewc_penalty(new_weights, old_weights, fisher, 100.0);
    printf("%s [arr len=%ld]\n", "    old:", (long)((old_weights).n));
    printf("%s [arr len=%ld]\n", "    new:", (long)((new_weights).n));
    printf("%s [arr len=%ld]\n", "    fisher:", (long)((fisher).n));
    printf("%s %ld\n", "    EWC penalty (λ=100):", (long)(penalty));
    grads_sq = hexa_arr_lit((long[]){(long)(0.1), (long)(0.2), (long)(0.05), (long)(0.3), (long)(0.2), (long)(0.1), (long)(0.15), (long)(0.25), (long)(0.15), (long)(0.3), (long)(0.1), (long)(0.2)}, 12);
    fisher_diag = fisher_diagonal(grads_sq, 3, 4);
    printf("%s %ld\n", "[4] Fisher diagonal:", (long)(fisher_diag));
    printf("%s\n", "\n[5] Anima MoE ConsciousBlock:");
    W_experts = hexa_arr_new();
    while ((i < n_experts)) {
        W_experts = hexa_arr_concat(W_experts, randn((dim * dim), 0.25));
        i = (i + 1);
    }
    W_gate = randn((n_experts * dim), 0.5);
    x_moe = randn(dim, 0.3);
    route = moe_gate(x_moe, W_gate, n_experts, 2);
    sel = route[0];
    gates = route[1];
    expert_results = hexa_arr_new();
    while ((e < 2)) {
        long eidx = sel[e];
        long W_e = slice(W_experts, ((eidx * dim) * dim), (((eidx + 1) * dim) * dim));
        long out = gelu(matvec(W_e, x_moe, dim, dim));
        expert_results = hexa_arr_concat(expert_results, out);
        e = (e + 1);
    }
    moe_out = moe_combine(expert_results, gates);
    printf("%s %ld %s %ld\n", "    experts:", (long)(sel), "gates:", (long)(gates));
    printf("%s %ld\n", "    MoE output:", (long)(moe_out));
    printf("%s\n", "\n[6] Continual Learning:");
    task1_weights = randn(8, 0.5);
    task1_fisher = randn(8, 0.1);
    pos_fisher = hexa_arr_new();
    while ((fi < hexa_str_len(task1_fisher))) {
        pos_fisher = hexa_arr_concat(pos_fisher, hexa_arr_lit((long[]){(long)(hexa_abs_i((long)(task1_fisher[fi])))}, 1));
        fi = (fi + 1);
    }
    current = randn(8, 0.5);
    ewc = ewc_penalty(current, task1_weights, pos_fisher, 50.0);
    printf("%s %g\n", "    Task 1 → Task 2 EWC penalty:", (double)((hexa_round((double)((ewc * 1000))) / 1000)));
    printf("%s\n", "    (higher = more forgetting risk)");
    run_tests();
    printf("%s\n", "\n--- tokenizer+moe+ewc PASS ---");
    return 0;
}
