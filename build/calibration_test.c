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

double safe_float(const char* s);
double abs_f(double x);
double max_f(double a, double b);
double min_f(double a, double b);
double sin_f(double x);
double cos_f(double x);
hexa_arr load_n6_consts();
double n6_const(hexa_arr consts, const char* name);
long n6_values(hexa_arr consts);
const char* tier_assignment(double hit_rate);
double n6_alignment(hexa_arr values);
long pattern_matches(hexa_arr actual, hexa_arr expected, double tolerance);
hexa_arr generate_datasets();
hexa_arr calibrate_against(hexa_arr lens_results, hexa_arr dataset_patterns);
long show_help();

static hexa_arr _n6;
static double n6;
static double sigma;
static double phi;
static double tau_n;
static double j2;
static double sopfr;
static double sigma_minus_phi;
static double sigma_minus_tau;
static double t0_threshold = 0.8;
static double t1_threshold = 0.5;
static hexa_arr cli_args;
static const char* cmd = "help";

double safe_float(const char* s) {
    double v = 0.0;
    v = hexa_to_float(s);
    return v;
}

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    }
    return x;
}

double max_f(double a, double b) {
    if ((a > b)) {
        return a;
    }
    return b;
}

double min_f(double a, double b) {
    if ((a < b)) {
        return a;
    }
    return b;
}

double sin_f(double x) {
    double xx = x;
    double pi = 3.14159265358979;
    while ((xx > pi)) {
        xx = (xx - (2.0 * pi));
    }
    while ((xx < (0.0 - pi))) {
        xx = (xx + (2.0 * pi));
    }
    double term = xx;
    double sum = xx;
    long i = 1;
    while ((i < 10)) {
        term = (0.0 - ((((term * xx) * xx) / ((double)((2 * i)))) / ((double)(((2 * i) + 1)))));
        sum = (sum + term);
        i = (i + 1);
    }
    return sum;
}

double cos_f(double x) {
    double pi = 3.14159265358979;
    return sin_f((x + (pi / 2.0)));
}

hexa_arr load_n6_consts() {
    const char* home = hexa_exec("printenv HOME");
    const char* path = hexa_concat(home, "/Dev/nexus/shared/n6_constants.jsonl");
    const char* raw = hexa_read_file(path);
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr names = hexa_arr_new();
    hexa_arr values = hexa_arr_new();
    long __fi_line_1 = 0;
    while ((__fi_line_1 < lines.n)) {
        const char* line = ((const char*)lines.d[__fi_line_1]);
        const char* trimmed = hexa_trim(line);
        if ((strcmp(trimmed, "") == 0)) {
            continue;
        }
        const char* name = "";
        double value = 0.0;
        if (hexa_contains(trimmed, "\"name\":\"")) {
            hexa_arr p = hexa_split(trimmed, "\"name\":\"");
            if ((p.n >= 2)) {
                name = ((const char*)(hexa_split(((const char*)p.d[1]), "\"")).d[0]);
            }
        }
        if (hexa_contains(trimmed, "\"value\":")) {
            hexa_arr p = hexa_split(trimmed, "\"value\":");
            if ((p.n >= 2)) {
                const char* num_str = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)p.d[1]), ",")).d[0]), "}")).d[0]));
                value = hexa_to_float(num_str);
            }
        }
        if ((strcmp(name, "") != 0)) {
            names = hexa_arr_concat(names, hexa_arr_lit((long[]){(long)(name)}, 1));
            values = hexa_arr_concat(values, hexa_arr_lit((long[]){(long)(value)}, 1));
        }
        __fi_line_1 = (__fi_line_1 + 1);
    }
    return hexa_arr_lit((long[]){(long)(names), (long)(values)}, 2);
}

double n6_const(hexa_arr consts, const char* name) {
    long names = consts.d[0];
    long values = consts.d[1];
    long i = 0;
    while ((i < hexa_str_len(names))) {
        if ((names[i] == name)) {
            return ((double)(values[i]));
        }
        i = (i + 1);
    }
    return 0.0;
}

long n6_values(hexa_arr consts) {
    return consts.d[1];
}

const char* tier_assignment(double hit_rate) {
    if ((hit_rate >= 0.8)) {
        return "T0";
    }
    if ((hit_rate >= 0.5)) {
        return "T1";
    }
    return "T2";
}

double n6_alignment(hexa_arr values) {
    hexa_arr attractors = hexa_arr_lit((long[]){n6, sigma, phi, tau_n, j2, sopfr, sigma_minus_phi, sigma_minus_tau}, 8);
    double total_dist = 0.0;
    long count = 0;
    long i = 0;
    while ((i < values.n)) {
        double v = ((double)(values.d[i]));
        if ((abs_f(v) > 0.000000000000001)) {
            double min_rel = 999999.0;
            long j = 0;
            while ((j < attractors.n)) {
                double a = ((double)(attractors.d[j]));
                double diff = abs_f((v - a));
                double scale = max_f(abs_f(a), max_f(abs_f(v), 0.000000000001));
                double rel = (diff / scale);
                if ((rel < min_rel)) {
                    min_rel = rel;
                }
                j = (j + 1);
            }
            total_dist = (total_dist + min_f(min_rel, 1.0));
            count = (count + 1);
        }
        i = (i + 1);
    }
    if ((count == 0)) {
        return 0.0;
    }
    double mean_dist = (total_dist / ((double)(count)));
    return max_f((1.0 - mean_dist), 0.0);
}

long pattern_matches(hexa_arr actual, hexa_arr expected, double tolerance) {
    if ((actual.n == 0)) {
        return 0;
    }
    if ((expected.n == 0)) {
        return 1;
    }
    long len = actual.n;
    if ((expected.n < len)) {
        len = expected.n;
    }
    if ((len == 0)) {
        return 0;
    }
    long matches = 0;
    long i = 0;
    while ((i < len)) {
        double exp_val = ((double)(expected.d[i]));
        double act_val = ((double)(actual.d[i]));
        double diff = abs_f((act_val - exp_val));
        double scale = max_f(abs_f(exp_val), 0.000000000001);
        if (((diff / scale) <= tolerance)) {
            matches = (matches + 1);
        }
        i = (i + 1);
    }
    return ((matches * 2) >= len);
}

hexa_arr generate_datasets() {
    hexa_arr datasets = hexa_arr_new();
    double pi2 = 6.28318530717959;
    hexa_arr data1 = hexa_arr_new();
    long n1 = 24;
    long d1 = 2;
    long i = 0;
    while ((i < n1)) {
        double t = ((double)(i));
        double x = (sin_f(((t * pi2) / sigma)) * sigma);
        double y = (t * phi);
        data1 = hexa_arr_concat(data1, hexa_arr_lit((long[]){(long)(x), (long)(y)}, 2));
        i = (i + 1);
    }
    hexa_arr patterns1 = hexa_arr_lit((long[]){hexa_arr_lit((long[]){(long)("period"), (long)(hexa_arr_lit((long[]){(long)(sigma)}, 1)), (long)(0.2)}, 3)}, 1);
    patterns1 = hexa_arr_concat(patterns1, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("dominant_frequency"), (long)(hexa_arr_lit((long[]){(long)((1.0 / sigma))}, 1)), (long)(0.3)}, 3))}, 1));
    datasets = hexa_arr_concat(datasets, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("periodic_sigma12"), (long)(data1), (long)(n1), (long)(d1), (long)(patterns1)}, 5))}, 1));
    hexa_arr centers = hexa_arr_lit((long[]){hexa_arr_lit((long[]){(long)(n6), (long)(sigma)}, 2), hexa_arr_lit((long[]){(long)(phi), (long)(tau_n)}, 2), hexa_arr_lit((long[]){(long)(j2), (long)(sopfr)}, 2)}, 3);
    hexa_arr data2 = hexa_arr_new();
    long pts_per = 6;
    long ci = 0;
    long rng = 6;
    while ((ci < 3)) {
        double cx = ((double)(centers.d[ci][0]));
        double cy = ((double)(centers.d[ci][1]));
        long pi = 0;
        while ((pi < pts_per)) {
            rng = (((rng * 6364136) + 1442695) % 2147483647);
            double rx = ((((double)((rng % 1000))) / 1000.0) - 0.5);
            rng = (((rng * 6364136) + 1442695) % 2147483647);
            double ry = ((((double)((rng % 1000))) / 1000.0) - 0.5);
            data2 = hexa_arr_concat(data2, hexa_arr_lit((long[]){(long)((cx + (rx * 0.5))), (long)((cy + (ry * 0.5)))}, 2));
            pi = (pi + 1);
        }
        ci = (ci + 1);
    }
    hexa_arr patterns2 = hexa_arr_lit((long[]){hexa_arr_lit((long[]){(long)("cluster_count"), (long)(hexa_arr_lit((long[]){(long)(3.0)}, 1)), (long)(0.1)}, 3)}, 1);
    patterns2 = hexa_arr_concat(patterns2, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("cluster_centers"), (long)(hexa_arr_lit((long[]){(long)(n6), (long)(phi), (long)(j2)}, 3)), (long)(0.3)}, 3))}, 1));
    datasets = hexa_arr_concat(datasets, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("n6_clusters"), (long)(data2), (long)(18), (long)(2), (long)(patterns2)}, 5))}, 1));
    hexa_arr data3 = hexa_arr_new();
    long i3 = 0;
    while ((i3 < 12)) {
        double x = ((double)(i3));
        double y = (sigma_minus_tau * x);
        data3 = hexa_arr_concat(data3, hexa_arr_lit((long[]){(long)(x), (long)(y)}, 2));
        i3 = (i3 + 1);
    }
    hexa_arr patterns3 = hexa_arr_lit((long[]){hexa_arr_lit((long[]){(long)("slope"), (long)(hexa_arr_lit((long[]){(long)(sigma_minus_tau)}, 1)), (long)(0.15)}, 3)}, 1);
    patterns3 = hexa_arr_concat(patterns3, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("correlation"), (long)(hexa_arr_lit((long[]){(long)(1.0)}, 1)), (long)(0.05)}, 3))}, 1));
    datasets = hexa_arr_concat(datasets, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("linear_sigma_minus_tau"), (long)(data3), (long)(12), (long)(2), (long)(patterns3)}, 5))}, 1));
    hexa_arr data4 = hexa_arr_new();
    long rng4 = 42;
    long i4 = 0;
    while ((i4 < 96)) {
        rng4 = (((rng4 * 6364136) + 1442695) % 2147483647);
        double v = (((double)((rng4 % 10000))) / 100.0);
        data4 = hexa_arr_concat(data4, hexa_arr_lit((long[]){(long)(v)}, 1));
        i4 = (i4 + 1);
    }
    datasets = hexa_arr_concat(datasets, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("uniform_noise"), (long)(data4), (long)(24), (long)(4), (long)(hexa_arr_new())}, 5))}, 1));
    hexa_arr data5 = hexa_arr_new();
    long i5 = 0;
    while ((i5 < 24)) {
        double t = (((double)(i5)) / 23.0);
        double x = ((n6 + ((sigma_minus_phi - n6) * (1.0 - t))) + (((1.0 - t) * 2.0) * sin_f((((double)(i5)) * 0.7))));
        double y = ((sigma + ((sopfr - sigma) * (1.0 - t))) + (((1.0 - t) * 1.5) * cos_f((((double)(i5)) * 1.3))));
        double z = ((j2 + ((tau_n - j2) * (1.0 - t))) + (((1.0 - t) * 3.0) * sin_f((((double)(i5)) * 0.5))));
        data5 = hexa_arr_concat(data5, hexa_arr_lit((long[]){(long)(x), (long)(y), (long)(z)}, 3));
        i5 = (i5 + 1);
    }
    hexa_arr patterns5 = hexa_arr_lit((long[]){hexa_arr_lit((long[]){(long)("attractor"), (long)(hexa_arr_lit((long[]){(long)(n6), (long)(sigma), (long)(j2)}, 3)), (long)(0.15)}, 3)}, 1);
    patterns5 = hexa_arr_concat(patterns5, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("convergence_detected"), (long)(hexa_arr_lit((long[]){(long)(1.0)}, 1)), (long)(0.5)}, 3))}, 1));
    datasets = hexa_arr_concat(datasets, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("n6_attractor_convergence"), (long)(data5), (long)(24), (long)(3), (long)(patterns5)}, 5))}, 1));
    return datasets;
}

hexa_arr calibrate_against(hexa_arr lens_results, hexa_arr dataset_patterns) {
    long total_patterns = 0;
    long true_positives = 0;
    long false_negatives = 0;
    long total_metrics = 0;
    long matched_metrics = 0;
    hexa_arr known_names = hexa_arr_new();
    long pi = 0;
    while ((pi < (long)(((double)(dataset_patterns.n))))) {
        long pat = dataset_patterns.d[pi];
        known_names = hexa_arr_concat(known_names, hexa_arr_lit((long[]){(long)(hexa_int_to_str((long)(pat[0])))}, 1));
        pi = (pi + 1);
    }
    long pi2 = 0;
    while ((pi2 < (long)(((double)(dataset_patterns.n))))) {
        long pat = dataset_patterns.d[pi2];
        const char* pname = hexa_int_to_str((long)(pat[0]));
        long expected = pat[1];
        double tol = ((double)(pat[2]));
        total_patterns = (total_patterns + 1);
        long found = 0;
        long li = 0;
        while ((li < (long)(((double)(lens_results.n))))) {
            long lr = lens_results.d[li];
            if ((strcmp(hexa_int_to_str((long)(lr[0])), pname) == 0)) {
                if (pattern_matches(lr[1], expected, tol)) {
                    true_positives = (true_positives + 1);
                } else {
                    false_negatives = (false_negatives + 1);
                }
                found = 1;
            }
            li = (li + 1);
        }
        if ((found == 0)) {
            false_negatives = (false_negatives + 1);
        }
        pi2 = (pi2 + 1);
    }
    long li2 = 0;
    while ((li2 < (long)(((double)(lens_results.n))))) {
        long lr = lens_results.d[li2];
        const char* mname = hexa_int_to_str((long)(lr[0]));
        total_metrics = (total_metrics + 1);
        long is_known = 0;
        long ki = 0;
        while ((ki < known_names.n)) {
            if ((strcmp(((const char*)known_names.d[ki]), mname) == 0)) {
                is_known = 1;
            }
            ki = (ki + 1);
        }
        if (is_known) {
            matched_metrics = (matched_metrics + 1);
        }
        li2 = (li2 + 1);
    }
    double hit_rate = (((total_patterns > 0)) ? ((((double)(true_positives)) / ((double)(total_patterns)))) : (0.0));
    double fp_rate = (((total_metrics > 0)) ? ((((double)((total_metrics - matched_metrics))) / ((double)(total_metrics)))) : (0.0));
    double fn_rate = (((total_patterns > 0)) ? ((((double)(false_negatives)) / ((double)(total_patterns)))) : (0.0));
    hexa_arr all_vals = hexa_arr_new();
    long vi = 0;
    while ((vi < (long)(((double)(lens_results.n))))) {
        long lr = lens_results.d[vi];
        long vals = lr[1];
        long vj = 0;
        while ((vj < (long)(((double)(hexa_str_len(vals)))))) {
            all_vals = hexa_arr_concat(all_vals, hexa_arr_lit((long[]){(long)(((double)(vals[vj])))}, 1));
            vj = (vj + 1);
        }
        vi = (vi + 1);
    }
    double n6a = n6_alignment(all_vals);
    return hexa_arr_lit((long[]){(long)(hit_rate), (long)(fp_rate), (long)(fn_rate), (long)(n6a)}, 4);
}

long show_help() {
    printf("%s\n", "calibration.hexa — 렌즈 캘리브레이션 엔진 (mk2 hexa)");
    printf("%s\n", "");
    printf("%s\n", "Usage:");
    printf("%s\n", "  hexa calibration.hexa generate          — 합성 데이터셋 생성");
    printf("%s\n", "  hexa calibration.hexa tier <hit_rate>    — 티어 판정");
    printf("%s\n", "  hexa calibration.hexa n6align <v1,v2..>  — n=6 정렬도");
    printf("%s\n", "  hexa calibration.hexa report             — 전체 리포트");
    printf("%s\n", "  hexa calibration.hexa help");
    printf("%s\n", "");
    printf("%s\n", "Tiers: T0 (>=0.8), T1 (>=0.5), T2 (<0.5)");
    return printf("%s\n", "n=6 constants: 6, 12, 2, 4, 24, 5, 10, 8");
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    _n6 = load_n6_consts();
    n6 = n6_const(_n6, "n");
    sigma = n6_const(_n6, "sigma");
    phi = n6_const(_n6, "phi");
    tau_n = n6_const(_n6, "tau");
    j2 = n6_const(_n6, "j2");
    sopfr = n6_const(_n6, "sopfr");
    sigma_minus_phi = n6_const(_n6, "sigma_minus_phi");
    sigma_minus_tau = n6_const(_n6, "phi_tau");
    cli_args = hexa_args();
    if ((cli_args.n > 2)) {
        cmd = ((const char*)cli_args.d[2]);
    }
    if ((strcmp(cmd, "help") == 0)) {
        show_help();
    }
    if ((strcmp(cmd, "tier") == 0)) {
        if ((cli_args.n < 4)) {
            printf("%s\n", "Error: tier requires <hit_rate> argument");
        } else {
            double hr = safe_float(((const char*)cli_args.d[3]));
            const char* tier = tier_assignment(hr);
            printf("%s\n", hexa_concat("Hit rate: ", hexa_float_to_str((double)(hr))));
            printf("%s\n", hexa_concat("Tier:     ", tier));
        }
    }
    if ((strcmp(cmd, "n6align") == 0)) {
        if ((cli_args.n < 4)) {
            printf("%s\n", "Error: n6align requires comma-separated values");
        } else {
            hexa_arr parts = hexa_split(((const char*)cli_args.d[3]), ",");
            hexa_arr vals = hexa_arr_new();
            long i = 0;
            while ((i < parts.n)) {
                vals = hexa_arr_concat(vals, hexa_arr_lit((long[]){(long)(safe_float(hexa_trim(((const char*)parts.d[i]))))}, 1));
                i = (i + 1);
            }
            double align = n6_alignment(vals);
            printf("%s\n", hexa_concat("Values:    ", ((const char*)cli_args.d[3])));
            printf("%s\n", hexa_concat("Alignment: ", hexa_float_to_str((double)(align))));
        }
    }
    if ((strcmp(cmd, "generate") == 0)) {
        hexa_arr datasets = generate_datasets();
        printf("%s\n", "=== Synthetic Calibration Datasets ===");
        long i = 0;
        while ((i < (long)(((double)(datasets.n))))) {
            long ds = datasets.d[i];
            const char* name = hexa_int_to_str((long)(ds[0]));
            long n = (long)(((double)(ds[2])));
            long d = (long)(((double)(ds[3])));
            long patterns = ds[4];
            printf("%s\n", "");
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat("Dataset ", hexa_int_to_str((long)((i + 1)))), ": "), name));
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  Points: ", hexa_int_to_str((long)(n))), " x "), hexa_int_to_str((long)(d))), "D"));
            printf("%s\n", hexa_concat("  Patterns: ", hexa_int_to_str((long)((long)(((double)(hexa_str_len(patterns))))))));
            long j = 0;
            while ((j < (long)(((double)(hexa_str_len(patterns)))))) {
                long p = patterns[j];
                printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("    - ", hexa_int_to_str((long)(p[0]))), " (tol="), hexa_int_to_str((long)(p[2]))), ")"));
                j = (j + 1);
            }
            i = (i + 1);
        }
    }
    if ((strcmp(cmd, "report") == 0)) {
        hexa_arr datasets = generate_datasets();
        printf("%s\n", "=== Calibration Report (Synthetic Data) ===");
        printf("%s\n", hexa_concat("Datasets: ", hexa_int_to_str((long)((long)(((double)(datasets.n)))))));
        printf("%s\n", "");
        hexa_arr fixed_results = hexa_arr_lit((long[]){hexa_arr_lit((long[]){(long)("period"), (long)(hexa_arr_lit((long[]){(long)(sigma)}, 1))}, 2), hexa_arr_lit((long[]){(long)("slope"), (long)(hexa_arr_lit((long[]){(long)(sigma_minus_tau)}, 1))}, 2)}, 2);
        hexa_arr null_results = hexa_arr_new();
        hexa_arr noisy_results = hexa_arr_lit((long[]){hexa_arr_lit((long[]){(long)("period"), (long)(hexa_arr_lit((long[]){(long)(999.0)}, 1))}, 2), hexa_arr_lit((long[]){(long)("bogus_metric"), (long)(hexa_arr_lit((long[]){(long)(42.0)}, 1))}, 2)}, 2);
        hexa_arr lens_names = hexa_arr_lit((long[]){(long)("FixedPeriodLens"), (long)("NullLens"), (long)("NoisyLens")}, 3);
        hexa_arr lens_outputs = hexa_arr_lit((long[]){fixed_results, null_results, noisy_results}, 3);
        double total_hr = 0.0;
        long i = 0;
        while ((i < 3)) {
            const char* name = ((const char*)lens_names.d[i]);
            long outputs = lens_outputs.d[i];
            long ds = datasets.d[0];
            hexa_arr cal = calibrate_against(outputs, ds[4]);
            double hr = ((double)(cal.d[0]));
            total_hr = (total_hr + hr);
            const char* tier = tier_assignment(hr);
            printf("%s\n", hexa_concat("Lens: ", name));
            printf("%s\n", hexa_concat("  Hit rate:       ", hexa_float_to_str((double)(hr))));
            printf("%s\n", hexa_concat("  FP rate:        ", hexa_int_to_str((long)(cal.d[1]))));
            printf("%s\n", hexa_concat("  FN rate:        ", hexa_int_to_str((long)(cal.d[2]))));
            printf("%s\n", hexa_concat("  n6 alignment:   ", hexa_int_to_str((long)(cal.d[3]))));
            printf("%s\n", hexa_concat("  Tier:           ", tier));
            printf("%s\n", "");
            i = (i + 1);
        }
        double mean_hr = (total_hr / 3.0);
        printf("%s\n", hexa_concat("Mean hit rate: ", hexa_float_to_str((double)(mean_hr))));
    }
    return 0;
}
