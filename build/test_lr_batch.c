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

long test_warmup_zero_at_start();
long test_warmup_full_at_end();
long test_cosine_decreases();
long test_batch_norm_centered();

static long i = 0;
static hexa_arr steps;
static long j = 0;
static hexa_arr phases;
static long k = 0;
static hexa_arr W;
static hexa_arr batch;
static long result;
static hexa_arr W2;
static long r2;
static hexa_arr batch_data;
static long normed;
static long buffer;
static hexa_arr g1;
static hexa_arr g2;
static long accum_steps = 4;
static long W_train;
static long total_steps = 20;
static long warmup = 5;
static long t = 0;
static long grad_buf;
static long accum = 4;

long test_warmup_zero_at_start() {
    if (!((warmup_lr(0.001, 0, 100) == 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "lr should be 0 at step 0";
    return 0;
}

long test_warmup_full_at_end() {
    if (!((warmup_lr(0.001, 100, 100) == 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "lr should be base at warmup end";
    return 0;
}

long test_cosine_decreases() {
    long lr0 = cosine_lr(0.001, 0, 1000);
    long lr500 = cosine_lr(0.001, 500, 1000);
    if (!((lr0 > lr500))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "cosine lr should decrease";
    return 0;
}

long test_batch_norm_centered() {
    long bn = batch_norm(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0)}, 3), 1, 3);
    long m = mean(bn);
    if (!((m < 0.01))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "batch norm should center to ~0";
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    printf("%s\n", "[1] Warmup LR (base=0.001, warmup=100):");
    while ((i <= 100)) {
        if (((i % 25) == 0)) {
            long lr = warmup_lr(0.001, i, 100);
            printf("%s %ld %s %ld\n", "    step", (long)(i), "→ lr=", (long)(lr));
        }
        i = (i + 1);
    }
    printf("%s\n", "[2] Cosine LR (base=0.001, total=1000):");
    steps = hexa_arr_lit((long[]){(long)(0), (long)(250), (long)(500), (long)(750), (long)(1000)}, 5);
    while ((j < 5)) {
        long lr = cosine_lr(0.001, steps.d[j], 1000, 0.0001);
        printf("%s %ld %s %g\n", "    step", (long)(steps.d[j]), "→ lr=", (double)((hexa_round((double)((lr * 1000000))) / 1000000)));
        j = (j + 1);
    }
    printf("%s\n", "[3] Phase LR (3 phases: C-only → +D → +Hexad):");
    phases = hexa_arr_lit((long[]){(long)(0), (long)(333), (long)(666), (long)(999)}, 4);
    while ((k < 4)) {
        long lr = phase_lr(0.001, phases.d[k], 1000, 3);
        const char* phase_name = (((phases.d[k] < 333)) ? ("P1:C-only") : ((((phases.d[k] < 666)) ? ("P2:+Dialogue") : ("P3:+Hexad"))));
        printf("%s %ld %s %s %ld\n", "    step", (long)(phases.d[k]), phase_name, "→ lr=", (long)(lr));
        k = (k + 1);
    }
    W = hexa_arr_lit((long[]){(long)(1.0), (long)(0.0), (long)(0.0), (long)(1.0)}, 4);
    batch = hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0), (long)(5.0), (long)(6.0)}, 6);
    result = batch_matvec(W, batch, 2, 2, 3);
    printf("%s %ld\n", "[4] batch_matvec(I, [[1,2],[3,4],[5,6]]) =", (long)(result));
    W2 = hexa_arr_lit((long[]){(long)(2.0), (long)(1.0), (long)(0.0), (long)(3.0)}, 4);
    r2 = batch_matvec(W2, batch, 2, 2, 3);
    printf("%s %ld\n", "    batch_matvec([[2,1],[0,3]], batch) =", (long)(r2));
    batch_data = hexa_arr_lit((long[]){(long)(1.0), (long)(10.0), (long)(3.0), (long)(20.0), (long)(5.0), (long)(30.0)}, 6);
    normed = batch_norm(batch_data, 2, 3);
    printf("%s\n", "[5] batch_norm: mean-centered + unit-var");
    printf("%s [arr len=%ld]\n", "    input:", (long)((batch_data).n));
    printf("%s %ld\n", "    output:", (long)(normed));
    buffer = zeros(4);
    g1 = hexa_arr_lit((long[]){(long)(0.1), (long)(0.2), (long)(0.3), (long)(0.4)}, 4);
    g2 = hexa_arr_lit((long[]){(long)(0.4), (long)(0.3), (long)(0.2), (long)(0.1)}, 4);
    buffer = grad_accumulate(buffer, g1, accum_steps);
    buffer = grad_accumulate(buffer, g2, accum_steps);
    printf("%s %ld\n", "[6] Grad accumulate (2/4 steps):", (long)(buffer));
    printf("%s\n", "\n[7] Anima training with LR schedule:");
    W_train = randn(4, 0.5);
    grad_buf = zeros(4);
    while ((t < total_steps)) {
        long base_lr = phase_lr(0.001, t, total_steps, 3);
        long lr = warmup_lr(base_lr, t, warmup);
        long grads = mat_scale(W_train, 0.1);
        grad_buf = grad_accumulate(grad_buf, grads, accum);
        if ((((t + 1) % accum) == 0)) {
            W_train = sgd_step(W_train, grad_buf, lr);
            grad_buf = zeros(4);
        }
        if (((t % 5) == 0)) {
            printf("%s %ld %s %g %s %g\n", "    t=", (long)(t), "lr=", (double)((hexa_round((double)((lr * 1000000))) / 1000000)), "W_norm=", (double)((hexa_round((double)((sum(hadamard(W_train, W_train)) * 10000))) / 10000)));
        }
        t = (t + 1);
    }
    run_tests();
    printf("%s\n", "\n--- lr+batch PASS ---");
    return 0;
}
