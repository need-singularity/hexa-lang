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

long test_sigmoid_zero();
long test_relu_negative();
long test_softmax_sum();
long test_cosine_self();
long test_dot_product();
long bench_sigmoid_10k();
long bench_matvec_1k();
long bench_softmax_1k();

static long s1;
static long s2;
static long s_arr;
static long t1;
static long t2;
static long t_arr;
static long r1;
static long r2;
static long r_arr;
static long sm;
static long sm_sum;
static hexa_arr a;
static hexa_arr b;
static hexa_arr c;
static hexa_arr cell0;
static hexa_arr cell1;
static hexa_arr W_z;
static hexa_arr combined;
static long z;
static long r;
static long h_cand;
static long all_pass;

long test_sigmoid_zero() {
    if (!((sigmoid(0.0) == 0.5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "sigmoid(0) should be 0.5";
    return 0;
}

long test_relu_negative() {
    if (!((relu((-1.0)) == 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "relu(-1) should be 0";
    return 0;
}

long test_softmax_sum() {
    long s = softmax(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0)}, 4));
    long sum = (((s[0] + s[1]) + s[2]) + s[3]);
    if (!((sum > 0.999))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "softmax sum should be ~1.0";
    return 0;
}

long test_cosine_self() {
    hexa_arr v = hexa_arr_lit((long[]){1.0, 2.0, 3.0}, 3);
    if (!((cosine_sim(v, v) > 0.999))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "cosine_sim(v,v) should be ~1.0";
    return 0;
}

long test_dot_product() {
    if (!((dot(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0)}, 3), hexa_arr_lit((long[]){(long)(1.0), (long)(1.0), (long)(1.0)}, 3)) == 6.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "dot should be 6";
    return 0;
}

long bench_sigmoid_10k() {
    long i = 0;
    while ((i < 10000)) {
        sigmoid((i * 0.001));
        i = (i + 1);
    }
}

long bench_matvec_1k() {
    hexa_arr W = hexa_arr_lit((long[]){0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6}, 16);
    hexa_arr v = hexa_arr_lit((long[]){1.0, 2.0, 3.0, 4.0}, 4);
    long i = 0;
    while ((i < 1000)) {
        matvec(W, v, 4, 4);
        i = (i + 1);
    }
}

long bench_softmax_1k() {
    hexa_arr v = hexa_arr_lit((long[]){1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0}, 8);
    long i = 0;
    while ((i < 1000)) {
        softmax(v);
        i = (i + 1);
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    s1 = sigmoid(0.0);
    s2 = sigmoid(2.0);
    s_arr = sigmoid(hexa_arr_lit((long[]){(long)(0.0), (long)(1.0), (long)((-1.0)), (long)(5.0)}, 4));
    printf("%s %ld %s %g\n", "[1] sigmoid(0)=", (long)(s1), "sigmoid(2)=", (double)((hexa_round((double)((s2 * 10000))) / 10000)));
    printf("%s %ld\n", "    sigmoid([0,1,-1,5])=", (long)(s_arr));
    t1 = tanh_(0.0);
    t2 = tanh_(1.0);
    t_arr = tanh_(hexa_arr_lit((long[]){(long)((-2.0)), (long)((-1.0)), (long)(0.0), (long)(1.0), (long)(2.0)}, 5));
    printf("%s %ld %s %g\n", "[2] tanh_(0)=", (long)(t1), "tanh_(1)=", (double)((hexa_round((double)((t2 * 10000))) / 10000)));
    printf("%s %ld\n", "    tanh_([-2..2])=", (long)(t_arr));
    r1 = relu((-5.0));
    r2 = relu(3.0);
    r_arr = relu(hexa_arr_lit((long[]){(long)((-2.0)), (long)((-1.0)), (long)(0.0), (long)(1.0), (long)(2.0)}, 5));
    printf("%s %ld %s %ld\n", "[3] relu(-5)=", (long)(r1), "relu(3)=", (long)(r2));
    printf("%s %ld\n", "    relu([-2..2])=", (long)(r_arr));
    sm = softmax(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0)}, 3));
    printf("%s %ld\n", "[4] softmax([1,2,3])=", (long)(sm));
    sm_sum = ((sm[0] + sm[1]) + sm[2]);
    printf("%s %g\n", "    sum=", (double)((hexa_round((double)((sm_sum * 10000))) / 10000)));
    a = hexa_arr_lit((long[]){(long)(1.0), (long)(0.0), (long)(0.0)}, 3);
    b = hexa_arr_lit((long[]){(long)(0.0), (long)(1.0), (long)(0.0)}, 3);
    c = hexa_arr_lit((long[]){(long)(1.0), (long)(0.0), (long)(0.0)}, 3);
    printf("%s %ld\n", "[5] cosine_sim orthogonal=", (long)(cosine_sim(a, b)));
    printf("%s %ld\n", "    cosine_sim identical=", (long)(cosine_sim(a, c)));
    cell0 = hexa_arr_lit((long[]){(long)(0.1), (long)(0.2), (long)((-0.1)), (long)(0.3)}, 4);
    cell1 = hexa_arr_lit((long[]){(long)((-0.2)), (long)(0.4), (long)(0.1), (long)((-0.1))}, 4);
    printf("%s %g\n", "    cosine_sim(cell0, cell1)=", (double)((hexa_round((double)((cosine_sim(cell0, cell1) * 10000))) / 10000)));
    W_z = hexa_arr_lit((long[]){(long)(0.5), (long)((-0.3)), (long)(0.1), (long)(0.2), (long)((-0.2)), (long)(0.6), (long)((-0.1)), (long)(0.3), (long)(0.4), (long)((-0.5)), (long)(0.2), (long)(0.1), (long)((-0.1)), (long)(0.7), (long)((-0.4)), (long)(0.2)}, 16);
    combined = hexa_arr_lit((long[]){(long)(0.1), (long)(0.5), (long)(0.3), (long)((-0.2))}, 4);
    z = sigmoid(matvec(W_z, combined, 4, 4));
    r = sigmoid(matvec(W_z, combined, 4, 4));
    h_cand = tanh_(matvec(W_z, combined, 4, 4));
    printf("%s\n", "[6] GRU gates via builtins:");
    printf("%s %ld\n", "    z=", (long)(z));
    printf("%s %ld\n", "    h_cand=", (long)(h_cand));
    all_pass = run_tests();
    printf("%s %ld\n", "[7] All tests passed:", (long)(all_pass));
    run_benchmarks();
    printf("%s\n", "--- neural+test+bench PASS ---");
    return 0;
}
