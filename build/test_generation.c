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

long test_gelu_zero();
long test_silu_zero();
long test_pe_shape();
long test_mha_shape();
long test_topk_order();
long test_mse_zero();

static long g;
static long s;
static long pe;
static long seq = 3;
static long dim = 4;
static long n_heads = 2;
static long x;
static long Wq;
static long Wk;
static long Wv;
static long Wo;
static long mha_out;
static hexa_arr logits;
static long top3;
static hexa_arr gen_logits;
static long t_high;
static long t_low;
static hexa_arr pred;
static hexa_arr target;
static long mse;
static long vocab = 8;
static long d = 4;
static long W_e;
static long W_q_g;
static long W_k_g;
static long W_v_g;
static long W_o_g;
static long W_out;
static hexa_arr generated;
static long step = 0;

long test_gelu_zero() {
    if (!((gelu(0.0) == 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "gelu(0) = 0";
    return 0;
}

long test_silu_zero() {
    if (!((silu(0.0) == 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "silu(0) = 0";
    return 0;
}

long test_pe_shape() {
    long p = sinusoidal_pe(5, 8);
    if (!((hexa_str_len(p) == 40))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "PE should be seq×dim";
    return 0;
}

long test_mha_shape() {
    long o = multi_head_attention(randn(12, 0.1), randn(16, 0.1), randn(16, 0.1), randn(16, 0.1), randn(16, 0.1), 3, 4, 2, 1);
    if (!((hexa_str_len(o) == 12))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "MHA output = seq×dim";
    return 0;
}

long test_topk_order() {
    long t = topk(hexa_arr_lit((long[]){(long)(1.0), (long)(3.0), (long)(2.0)}, 3), 2);
    if (!((t[0] == 1))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "top1 index should be 1 (value 3.0)";
    return 0;
}

long test_mse_zero() {
    if (!((mse_loss(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0)}, 2), hexa_arr_lit((long[]){(long)(1.0), (long)(2.0)}, 2)) == 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "mse of identical = 0";
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    g = gelu(hexa_arr_lit((long[]){(long)((-2.0)), (long)((-1.0)), (long)(0.0), (long)(1.0), (long)(2.0)}, 5));
    s = silu(hexa_arr_lit((long[]){(long)((-2.0)), (long)((-1.0)), (long)(0.0), (long)(1.0), (long)(2.0)}, 5));
    printf("%s %ld\n", "[1] gelu:", (long)(g));
    printf("%s %ld\n", "    silu:", (long)(s));
    pe = sinusoidal_pe(4, 4);
    printf("%s\n", "[2] PE (4 positions × 4 dim):");
    printf("%s %ld\n", "    pos0:", (long)(slice(pe, 0, 4)));
    printf("%s %ld\n", "    pos1:", (long)(slice(pe, 4, 8)));
    printf("%s %ld\n", "    pos2:", (long)(slice(pe, 8, 12)));
    x = randn(12, 0.3);
    Wq = randn(16, 0.25);
    Wk = randn(16, 0.25);
    Wv = randn(16, 0.25);
    Wo = randn(16, 0.25);
    mha_out = multi_head_attention(x, Wq, Wk, Wv, Wo, seq, dim, n_heads, 1);
    printf("%s\n", "[3] MHA (seq=3, dim=4, heads=2, causal):");
    printf("%s %ld\n", "    pos0:", (long)(slice(mha_out, 0, 4)));
    printf("%s %ld\n", "    pos2:", (long)(slice(mha_out, 8, 12)));
    logits = hexa_arr_lit((long[]){(long)(0.1), (long)(0.8), (long)(0.3), (long)(0.9), (long)(0.2), (long)(0.7), (long)(0.5), (long)(0.4)}, 8);
    top3 = topk(logits, 3);
    printf("%s %ld\n", "[4] topk([0.1,0.8,0.3,0.9,0.2,0.7,0.5,0.4], k=3):", (long)(top3));
    gen_logits = hexa_arr_lit((long[]){(long)(2.0), (long)(1.0), (long)(0.5), (long)((-1.0)), (long)(0.1), (long)(0.3), (long)((-0.5)), (long)(0.8)}, 8);
    t_high = sample_token(gen_logits, 1.0);
    t_low = sample_token(gen_logits, 0.1);
    printf("%s\n", "[5] sample_token(logits):");
    printf("%s %ld %s %ld\n", "    temp=1.0:", (long)(t_high), "temp=0.1:", (long)(t_low));
    printf("%s %ld\n", "    argmax:", (long)(argmax(gen_logits)));
    pred = hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0)}, 3);
    target = hexa_arr_lit((long[]){(long)(1.1), (long)(2.2), (long)(2.8)}, 3);
    mse = mse_loss(pred, target);
    printf("%s %ld\n", "[6] mse_loss:", (long)(mse));
    printf("%s\n", "\n[7] Anima Text Generation:");
    W_e = randn((vocab * d), 0.5);
    W_q_g = randn((d * d), 0.25);
    W_k_g = randn((d * d), 0.25);
    W_v_g = randn((d * d), 0.25);
    W_o_g = randn((d * d), 0.25);
    W_out = randn((vocab * d), 0.25);
    generated = hexa_arr_lit((long[]){(long)(1)}, 1);
    while ((step < 5)) {
        long cur_len = generated.n;
        long emb = embedding(generated, W_e, vocab, d);
        long pe_enc = sinusoidal_pe(cur_len, d);
        long x_gen = mat_add(emb, pe_enc);
        long attn = multi_head_attention(x_gen, W_q_g, W_k_g, W_v_g, W_o_g, cur_len, d, 2, 1);
        long x_out = mat_add(x_gen, attn);
        long last_hidden = slice(x_out, ((cur_len - 1) * d), (cur_len * d));
        long logits_gen = matvec(W_out, last_hidden, vocab, d);
        long next_tok = sample_token(logits_gen, 0.8);
        generated = hexa_arr_concat(generated, hexa_arr_lit((long[]){(long)(next_tok)}, 1));
        if ((step < 3)) {
            printf("%s %ld %s %ld %s %ld\n", "    step", (long)(step), ": generated tok=", (long)(next_tok), "logits top3:", (long)(topk(logits_gen, 3)));
        }
        step = (step + 1);
    }
    printf("%s [arr len=%ld]\n", "    final sequence:", (long)((generated).n));
    run_tests();
    printf("%s\n", "\n--- generation pipeline PASS ---");
    return 0;
}
