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

long test_layer_norm_centered();
long test_attention_shape();
long test_gru_preserves_dim();
long test_embedding_shape();
long test_causal_mask();
long bench_attention_16x8();
long bench_gru_cell_100();

static hexa_arr x;
static long ln;
static long vals;
static long dropped;
static long passthru;
static long W_embed;
static hexa_arr tokens;
static long embedded;
static long seq_len = 3;
static long dim = 4;
static long Q;
static long K;
static long V;
static long out_bi;
static long out_causal;
static long input_dim = 3;
static long hidden_dim = 4;
static long gru_input;
static long gru_hidden;
static long combined_dim;
static long gru_Wz;
static long gru_Wr;
static long gru_Wh;
static long new_h;
static long h;
static long step = 0;
static long seq = 4;
static long d_model = 4;
static long W_e;
static hexa_arr toks;
static long x_block;
static long x_ln;
static long W_q;
static long W_k;
static long W_v;
static long q;
static long k;
static long v;
static long attn_out;
static long x_res;
static long x_ln2;
static long W_ff1;
static long ff_out;
static long x_final;

long test_layer_norm_centered() {
    long ln = layer_norm(hexa_arr_lit((long[]){(long)(10.0), (long)(20.0), (long)(30.0)}, 3), 3);
    long m = mean(ln);
    if (!((m < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "layer_norm should center to ~0";
    return 0;
}

long test_attention_shape() {
    long o = attention(randn(8, 0.1), randn(8, 0.1), randn(8, 0.1), 2, 4);
    if (!((hexa_str_len(o) == 8))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "attention output should be seq×dim";
    return 0;
}

long test_gru_preserves_dim() {
    long h = gru_cell(randn(2, 0.1), randn(3, 0.1), randn(15, 0.1), randn(15, 0.1), randn(15, 0.1), 2, 3);
    if (!((hexa_str_len(h) == 3))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "gru output dim should match hidden_dim";
    return 0;
}

long test_embedding_shape() {
    long e = embedding(hexa_arr_lit((long[]){(long)(0), (long)(1)}, 2), randn(8, 0.1), 2, 4);
    if (!((hexa_str_len(e) == 8))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "embedding should be n_tokens × dim";
    return 0;
}

long test_causal_mask() {
    hexa_arr q = hexa_arr_lit((long[]){1.0, 0.0, 0.0, 1.0, 0.0, 1.0}, 6);
    hexa_arr k = hexa_arr_lit((long[]){1.0, 0.0, 0.0, 1.0, 0.0, 1.0}, 6);
    hexa_arr v = hexa_arr_lit((long[]){1.0, 2.0, 3.0, 4.0, 5.0, 6.0}, 6);
    long o = attention(q, k, v, 2, 3, 1);
    if (!((hexa_str_len(o) == 6))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "causal attention output shape";
    return 0;
}

long bench_attention_16x8() {
    long q = randn(128, 0.1);
    long k = randn(128, 0.1);
    long v = randn(128, 0.1);
    long i = 0;
    while ((i < 100)) {
        attention(q, k, v, 16, 8, 1);
        i = (i + 1);
    }
}

long bench_gru_cell_100() {
    long inp = randn(4, 0.1);
    long h = randn(8, 0.1);
    long wz = randn(96, 0.1);
    long wr = randn(96, 0.1);
    long wh = randn(96, 0.1);
    long i = 0;
    while ((i < 100)) {
        gru_cell(inp, h, wz, wr, wh, 4, 8);
        i = (i + 1);
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    x = hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0), (long)(5.0), (long)(6.0)}, 6);
    ln = layer_norm(x, 3);
    printf("%s\n", "[1] layer_norm([1,2,3,4,5,6], dim=3):");
    printf("%s %ld %ld %ld\n", "    sample0:", (long)(ln[0]), (long)(ln[1]), (long)(ln[2]));
    printf("%s %ld %ld %ld\n", "    sample1:", (long)(ln[3]), (long)(ln[4]), (long)(ln[5]));
    vals = ones(8);
    dropped = dropout(vals, 0.5, 1);
    passthru = dropout(vals, 0.5, 0);
    printf("%s\n", "[2] dropout(ones(8), rate=0.5):");
    printf("%s %ld\n", "    training:", (long)(dropped));
    printf("%s %ld\n", "    inference:", (long)(passthru));
    W_embed = randn(24, 0.5);
    tokens = hexa_arr_lit((long[]){(long)(0), (long)(3), (long)(5), (long)(1)}, 4);
    embedded = embedding(tokens, W_embed, 6, 4);
    printf("%s\n", "[3] embedding([0,3,5,1], vocab=6, dim=4):");
    printf("%s %ld\n", "    tok0:", (long)(slice(embedded, 0, 4)));
    printf("%s %ld\n", "    tok1:", (long)(slice(embedded, 4, 8)));
    Q = randn(12, 0.3);
    K = randn(12, 0.3);
    V = randn(12, 0.3);
    out_bi = attention(Q, K, V, seq_len, dim);
    printf("%s\n", "[4] Attention (bidirectional):");
    printf("%s %ld\n", "    pos0:", (long)(slice(out_bi, 0, 4)));
    out_causal = attention(Q, K, V, seq_len, dim, 1);
    printf("%s %ld\n", "    Causal pos0:", (long)(slice(out_causal, 0, 4)));
    printf("%s %ld\n", "    Causal pos2:", (long)(slice(out_causal, 8, 12)));
    gru_input = randn(3, 0.3);
    gru_hidden = randn(4, 0.1);
    combined_dim = (input_dim + hidden_dim);
    gru_Wz = randn((hidden_dim * combined_dim), 0.2);
    gru_Wr = randn((hidden_dim * combined_dim), 0.2);
    gru_Wh = randn((hidden_dim * combined_dim), 0.2);
    new_h = gru_cell(gru_input, gru_hidden, gru_Wz, gru_Wr, gru_Wh, input_dim, hidden_dim);
    printf("%s\n", "[5] GRU cell (input=3, hidden=4):");
    printf("%s %ld\n", "    input:", (long)(gru_input));
    printf("%s %ld\n", "    hidden:", (long)(gru_hidden));
    printf("%s %ld\n", "    new_hidden:", (long)(new_h));
    h = gru_hidden;
    while ((step < 5)) {
        h = gru_cell(randn(3, 0.1), h, gru_Wz, gru_Wr, gru_Wh, input_dim, hidden_dim);
        step = (step + 1);
    }
    printf("%s %ld\n", "    after 5 steps:", (long)(h));
    printf("%s\n", "\n[6] Anima ConsciousBlock:");
    W_e = randn(32, 0.5);
    toks = hexa_arr_lit((long[]){(long)(1), (long)(3), (long)(5), (long)(2)}, 4);
    x_block = embedding(toks, W_e, 8, d_model);
    x_ln = layer_norm(x_block, d_model);
    W_q = randn(16, 0.25);
    W_k = randn(16, 0.25);
    W_v = randn(16, 0.25);
    q = batch_matvec(W_q, x_ln, d_model, d_model, seq);
    k = batch_matvec(W_k, x_ln, d_model, d_model, seq);
    v = batch_matvec(W_v, x_ln, d_model, d_model, seq);
    attn_out = attention(q, k, v, seq, d_model, 1);
    x_res = mat_add(x_block, attn_out);
    x_ln2 = layer_norm(x_res, d_model);
    W_ff1 = randn(64, 0.25);
    ff_out = batch_matvec(W_ff1, relu(x_ln2), d_model, d_model, seq);
    x_final = mat_add(x_res, ff_out);
    printf("%s %ld\n", "  input:", (long)(slice(x_block, 0, 4)));
    printf("%s %ld\n", "  after attn+res:", (long)(slice(x_res, 0, 4)));
    printf("%s %ld\n", "  after ffn+res:", (long)(slice(x_final, 0, 4)));
    run_tests();
    run_benchmarks();
    printf("%s\n", "\n--- transformer+attention+gru PASS ---");
    return 0;
}
