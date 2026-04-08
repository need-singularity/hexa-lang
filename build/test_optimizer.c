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

double hebbian_objective(long lr);
long test_sgd_direction();
long test_adam_converges();
long test_numerical_grad_quadratic();

static hexa_arr params;
static hexa_arr grads;
static double lr = 0.01;
static long updated;
static hexa_arr p;
static hexa_arr g;
static long m;
static long v;
static long result;
static long new_p;
static long new_m;
static long new_v;
static long step = 2;
static long grad_at_3;
static long grad_x;
static long grad_y;
static hexa_arr x;
static long m_opt;
static long v_opt;
static long t = 1;
static hexa_arr opt_lr;
static long m_lr;
static long v_lr;
static long t2 = 1;

double hebbian_objective(long lr) {
    hexa_arr sims = hexa_arr_lit((long[]){0.85, 0.15, 0.92, 0.45}, 4);
    hexa_arr weights = hexa_arr_lit((long[]){0.3, (-0.1), 0.7, 0.2}, 4);
    double total = 0.0;
    long i = 0;
    while ((i < 4)) {
        double delta = (lr * (sims.d[i] - (0.01 * weights.d[i])));
        total = (total + (delta * delta));
        i = (i + 1);
    }
    return total;
}

long test_sgd_direction() {
    long p = sgd_step(hexa_arr_lit((long[]){(long)(1.0)}, 1), hexa_arr_lit((long[]){(long)(1.0)}, 1), 0.1);
    if (!((p[0] < 1.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "SGD should decrease params when grad > 0";
    return 0;
}

long test_adam_converges() {
    if (!((x.d[0] > 2.5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "Adam should converge toward 3";
    return 0;
}

long test_numerical_grad_quadratic() {
    long g = numerical_grad(x);
    (x * x);
    0;
    hexa_arr_lit((long[]){(long)(5.0)}, 1);
    0;
    0;
    0;
    if (!((g > 9.9))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "grad of x² at 5 should be ~10";
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    params = hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0)}, 4);
    grads = hexa_arr_lit((long[]){(long)(0.1), (long)((-0.2)), (long)(0.3), (long)((-0.1))}, 4);
    updated = sgd_step(params, grads, lr);
    printf("%s [arr len=%ld] %s %ld\n", "[1] SGD: params", (long)((params).n), "→", (long)(updated));
    p = hexa_arr_lit((long[]){(long)(1.0), (long)(2.0)}, 2);
    g = hexa_arr_lit((long[]){(long)(0.5), (long)((-0.3))}, 2);
    m = zeros(2);
    v = zeros(2);
    result = adam_step(p, g, m, v, 0.001, 0.9, 0.999, 0.00000001, 1);
    new_p = slice(result, 0, 2);
    new_m = slice(result, 2, 4);
    new_v = slice(result, 4, 6);
    printf("%s\n", "[2] Adam step 1:");
    printf("%s [arr len=%ld] %s %ld\n", "    params:", (long)((p).n), "→", (long)(new_p));
    printf("%s %ld\n", "    m:", (long)(new_m));
    printf("%s %ld\n", "    v:", (long)(new_v));
    while ((step <= 5)) {
        long r = adam_step(new_p, g, new_m, new_v, 0.001, 0.9, 0.999, 0.00000001, step);
        new_p = slice(r, 0, 2);
        new_m = slice(r, 2, 4);
        new_v = slice(r, 4, 6);
        step = (step + 1);
    }
    printf("%s %ld\n", "    after 5 steps:", (long)(new_p));
    grad_at_3 = numerical_grad(x);
    (x * x);
    0;
    hexa_arr_lit((long[]){(long)(3.0)}, 1);
    0;
    0;
    0;
    printf("%s %ld\n", "[3] numerical_grad(x², at x=3) =", (long)(grad_at_3));
    grad_x = numerical_grad(x, y);
    ((x * y) + (x * x));
    0;
    hexa_arr_lit((long[]){(long)(2.0), (long)(3.0)}, 2);
    0;
    0;
    0;
    grad_y = numerical_grad(x, y);
    ((x * y) + (x * x));
    0;
    hexa_arr_lit((long[]){(long)(2.0), (long)(3.0)}, 2);
    0;
    1;
    0;
    printf("%s %ld %s\n", "    ∂(xy+x²)/∂x at (2,3) =", (long)(grad_x), "(expected: y+2x = 7)");
    printf("%s %ld %s\n", "    ∂(xy+x²)/∂y at (2,3) =", (long)(grad_y), "(expected: x = 2)");
    printf("%s\n", "\n[4] Gradient descent: min (x-3)²");
    x = hexa_arr_lit((long[]){(long)(0.0)}, 1);
    m_opt = zeros(1);
    v_opt = zeros(1);
    while ((t <= 50)) {
        long g_val = numerical_grad((x((x - 3.0)) * (x - 3.0)), x, 0);
        long r = adam_step(x, hexa_arr_lit((long[]){(long)(g_val)}, 1), m_opt, v_opt, 0.1, 0.9, 0.999, 0.00000001, t);
        x = slice(r, 0, 1);
        m_opt = slice(r, 1, 2);
        v_opt = slice(r, 2, 3);
        if (((t % 10) == 0)) {
            printf("%s %ld %s %g %s %g\n", "    t=", (long)(t), "x=", (double)((hexa_round((double)((x.d[0] * 10000))) / 10000)), "loss=", (double)((hexa_round((double)((((x.d[0] - 3.0) * (x.d[0] - 3.0)) * 10000))) / 10000)));
        }
        t = (t + 1);
    }
    printf("%s %g %s\n", "    final x =", (double)((hexa_round((double)((x.d[0] * 10000))) / 10000)), "(target: 3.0)");
    printf("%s\n", "\n[5] Anima Hebbian lr optimization");
    opt_lr = hexa_arr_lit((long[]){(long)(0.1)}, 1);
    m_lr = zeros(1);
    v_lr = zeros(1);
    while ((t2 <= 30)) {
        long g_lr = numerical_grad(hebbian_objective, opt_lr, 0);
        long r2 = adam_step(opt_lr, hexa_arr_lit((long[]){(long)(g_lr)}, 1), m_lr, v_lr, 0.001, 0.9, 0.999, 0.00000001, t2);
        opt_lr = slice(r2, 0, 1);
        m_lr = slice(r2, 1, 2);
        v_lr = slice(r2, 2, 3);
        t2 = (t2 + 1);
    }
    printf("%s %g\n", "    optimized lr =", (double)((hexa_round((double)((opt_lr.d[0] * 100000))) / 100000)));
    printf("%s %g\n", "    objective =", (double)((hexa_round((double)((hebbian_objective(opt_lr.d[0]) * 100000))) / 100000)));
    run_tests();
    printf("%s\n", "\n--- optimizer PASS ---");
    return 0;
}
