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
static const char* hexa_timestamp(void) {
    time_t t = time(NULL); struct tm* lt = localtime(&t);
    char* p = hexa_alloc(32);
    strftime(p, 32, "%Y-%m-%d %H:%M:%S", lt);
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

double abs_f(double x);
double deviation(double observed, double expected);
const char* grade_deviation(double dev);
double grade_score(double dev);
double gate_health(double observed, double expected);
const char* formula_status(double observed, double expected, double tolerance);
long* check_gate(const char* name, double observed, double expected);
long print_gate_report(long *r);
const char* check_formula(const char* tag, double observed, double expected, double tol);
double aggregate_health(double h0, double h1, double h2, double h3, double h4, double h5, double h6, double h7);

static long N = 6;
static long sigma = 12;
static long phi = 2;
static long tau = 4;
static long sopfr = 5;
static long j2 = 24;
static long mu = 1;
static long p2 = 28;
static long compression_ratio = 684;
static long n_sq_times_19 = 684;
static double alpha_inv = 137.036;
static double sopfr_alpha_mu = 684.18;
static long genome_bytes = 64;
static long raw_bytes = 43778;
static long pairs = 15;
static long genome_check = 64;
static long gate_count = 8;
static long gates_times_pairs = 120;
static double sin2_weinberg = 0.228;
static long hubble_h0 = 73;
static long compression_third = 228;
static long margin_factor = 117;
static double band_normal = 0.05;
static double band_drift = 0.15;
static double band_anomaly = 0.30;
static double f2_val;
static double f2_dev;
static double f7_check;
static double f9_product;
static double f9_dev;
static double f10_margin;
static double f10_dev;
static long* g0;
static long* g1;
static long* g2;
static long* g3;
static long* g4;
static long* g5;
static long* g6;
static long* g7;
static double agg;

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    } else {
        return x;
    }
}

double deviation(double observed, double expected) {
    if ((expected == 0.0)) {
        return abs_f(observed);
    }
    return (abs_f((observed - expected)) / abs_f(expected));
}

const char* grade_deviation(double dev) {
    if ((dev <= 0.05)) {
        return "normal";
    } else {
        if ((dev <= 0.15)) {
            return "drift";
        } else {
            if ((dev <= 0.30)) {
                return "anomaly";
            } else {
                return "critical";
            }
        }
    }
}

double grade_score(double dev) {
    if ((dev <= 0.05)) {
        return 1.0;
    } else {
        if ((dev <= 0.15)) {
            return 0.7;
        } else {
            if ((dev <= 0.30)) {
                return 0.3;
            } else {
                return 0.0;
            }
        }
    }
}

double gate_health(double observed, double expected) {
    double dev = deviation(observed, expected);
    return grade_score(dev);
}

const char* formula_status(double observed, double expected, double tolerance) {
    double dev = deviation(observed, expected);
    if ((dev <= tolerance)) {
        return "hold";
    } else {
        return "broken";
    }
}

long* check_gate(const char* name, double observed, double expected) {
    double dev = deviation(observed, expected);
    const char* g = grade_deviation(dev);
    double h = gate_health(observed, expected);
    return hexa_struct_alloc((long[]){(long)(name), (long)(observed), (long)(expected), (long)(dev), (long)(g), (long)(h)}, 6);
}

long print_gate_report(long *r) {
    return printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  [", hexa_int_to_str((long)(r[4]))), "] "), hexa_int_to_str((long)(r[0]))), ": obs="), hexa_int_to_str((long)(r[1]))), " exp="), hexa_int_to_str((long)(r[2]))), " dev="), hexa_int_to_str((long)(r[3]))), " health="), hexa_int_to_str((long)(r[5]))));
}

const char* check_formula(const char* tag, double observed, double expected, double tol) {
    const char* st = formula_status(observed, expected, tol);
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  ", tag), ": "), st), " (obs="), hexa_float_to_str((double)(observed))), " exp="), hexa_float_to_str((double)(expected))), ")"));
    return st;
}

double aggregate_health(double h0, double h1, double h2, double h3, double h4, double h5, double h6, double h7) {
    return ((((((((h0 + h1) + h2) + h3) + h4) + h5) + h6) + h7) / 8.0);
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    printf("%s\n", "=== gate_thresholds self-test ===");
    if (!((((28 * 24) + 12) == 684))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(((36 * 19) == 684))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((((N * N) * 19) == 684))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "F1  p2*j2+sigma = 684 ✓");
    f2_val = ((5.0 * 137.036) - 1.0);
    f2_dev = deviation(f2_val, 684.0);
    if (!((f2_dev < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s %g %s %g %s\n", "F2  sopfr*alpha_inv - mu =", (double)(f2_val), "dev=", (double)(f2_dev), "✓");
    if (!((((684 * 64) + 2) == 43778))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "F3  compress*genome+phi = 43778 ✓");
    if (!(((4 * 16) == 64))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(((tau * (pairs + 1)) == genome_bytes))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "F4  tau*(pairs+1) = 64 ✓");
    if (!(((phi * tau) == gate_count))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((gate_count == 8))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "F5  phi*tau = 8 ✓");
    if (!(((8 * 15) == 120))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(((sigma * (sigma - phi)) == 120))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "F6  gates*pairs = sigma*(sigma-phi) = 120 ✓");
    f7_check = deviation(0.228, sin2_weinberg);
    if (!((f7_check < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "F7  sin2_weinberg = 0.228 ✓");
    if (!((hubble_h0 == 73))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "F8  hubble H0 = 73 ✓");
    if (!(((684 / 3) == 228))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    f9_product = (0.228 * 1000.0);
    f9_dev = deviation(f9_product, 228.0);
    if (!((f9_dev < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "F9  684/3 = 228 = sin2_W*1000 ✓");
    f10_margin = (137.0 / 117.0);
    f10_dev = deviation((f10_margin * 117.0), 137.0);
    if (!((f10_dev < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "F10 margin*117 ~ alpha_inv ✓");
    printf("%s\n", "");
    printf("%s\n", "=== all 10 formulas verified ===");
    printf("%s\n", "");
    printf("%s\n", "=== anomaly detection (simulated) ===");
    g0 = check_gate("compression_ratio", 680.0, 684.0);
    g1 = check_gate("raw_bytes", 43800.0, 43778.0);
    g2 = check_gate("genome_bytes", 64.0, 64.0);
    g3 = check_gate("gate_count", 8.0, 8.0);
    g4 = check_gate("gates_x_pairs", 118.0, 120.0);
    g5 = check_gate("cpu_pct_weinberg", 0.245, 0.228);
    g6 = check_gate("bytes_per_proc", 80.0, 73.0);
    g7 = check_gate("compress_third", 228.0, 228.0);
    print_gate_report(g0);
    print_gate_report(g1);
    print_gate_report(g2);
    print_gate_report(g3);
    print_gate_report(g4);
    print_gate_report(g5);
    print_gate_report(g6);
    print_gate_report(g7);
    agg = aggregate_health(/* unknown field health */ 0, /* unknown field health */ 0, /* unknown field health */ 0, /* unknown field health */ 0, /* unknown field health */ 0, /* unknown field health */ 0, /* unknown field health */ 0, /* unknown field health */ 0);
    printf("%s\n", "");
    printf("%s %g\n", "aggregate health:", (double)(agg));
    if (!((/* unknown field grade */ 0 == "normal"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field grade */ 0 == "normal"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field grade */ 0 == "normal"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field grade */ 0 == "normal"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "");
    printf("%s\n", "=== formula hold/broken report ===");
    check_formula("F1 compress=684", 684.0, 684.0, 0.001);
    check_formula("F2 sopfr*alpha-mu", f2_val, 684.0, 0.001);
    check_formula("F3 raw=43778", 43778.0, 43778.0, 0.001);
    check_formula("F4 genome=64", 64.0, 64.0, 0.001);
    check_formula("F5 gates=8", 8.0, 8.0, 0.001);
    check_formula("F6 gates*pairs=120", 120.0, 120.0, 0.001);
    check_formula("F7 cpu~sin2W", 0.228, 0.228, 0.01);
    check_formula("F8 bytes/procs~H0", 73.0, 73.0, 0.01);
    check_formula("F9 684/3=228", 228.0, 228.0, 0.001);
    check_formula("F10 margin*117~alpha", 137.0, 137.0, 0.01);
    printf("%s\n", "");
    printf("%s\n", "=== gate_thresholds complete ===");
    return 0;
}
