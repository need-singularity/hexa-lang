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
const char* factorize_primes(long n);
long* n6_match(double value);
const char* classify_sector(const char* text);
long gate_count(long paths, long blowup, double conf, double pval);
const char* phase_name(long p);

static long N = 6;
static long sigma = 12;
static long phi = 2;
static long tau = 4;
static long sopfr = 5;
static long j2 = 24;
static double meta_fp = 0.333333333;
static hexa_arr args;

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    } else {
        return x;
    }
}

const char* factorize_primes(long n) {
    long m = n;
    long d = 2;
    const char* result = "";
    while (((d * d) <= m)) {
        while (((m % d) == 0)) {
            if ((strcmp(result, "") != 0)) {
                result = hexa_concat(result, ",");
            }
            result = hexa_concat(result, hexa_int_to_str((long)(d)));
            m = (m / d);
        }
        d = (d + 1);
    }
    if ((m > 1)) {
        if ((strcmp(result, "") != 0)) {
            result = hexa_concat(result, ",");
        }
        result = hexa_concat(result, hexa_int_to_str((long)(m)));
    }
    return result;
}

long* n6_match(double value) {
    const char* best_name = "none";
    double best_q = 0.0;
    const char* best_grade = "";
    if ((abs_f((value - 6.0)) < 0.0001)) {
        best_name = "n";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 2.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "phi";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 4.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "tau";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 12.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sigma";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 5.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sopfr";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 7.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "M3";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 24.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "j2";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 10.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sigma-phi";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 20.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "j2-tau";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 144.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sigma^2";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 288.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sigma*j2";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 8.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "phi*tau";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 28.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "P2";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 496.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "P3";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 8128.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "P4";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 36.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "n^2";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 48.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sigma*tau";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 60.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sigma*sopfr";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 120.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sigma*sigma-phi";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 30.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sopfr*n";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 168.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "sigma*tau*M3/2";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 3.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "n/phi";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    if (((abs_f((value - 1.0)) < 0.0001) && (best_q < 1.0))) {
        best_name = "mu";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    double rel = (abs_f((value - 137.036)) / 137.036);
    if (((rel < 0.01) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "alpha_inv";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 1836.15)) / 1836.15);
    if (((rel < 0.01) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "mp_me";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 0.3153)) / 0.3153);
    if (((rel < 0.05) && ((0.5 - (rel * 5.0)) > best_q))) {
        best_name = "Omega_m";
        best_q = (0.5 - (rel * 5.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 0.6847)) / 0.6847);
    if (((rel < 0.01) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "Omega_Lambda";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 0.2286)) / 0.2286);
    if (((rel < 0.01) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "sin2_theta_W";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 0.2462)) / 0.2462);
    if (((rel < 0.01) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "Y_p";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 0.9649)) / 0.9649);
    if (((rel < 0.005) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "n_s";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 73.0)) / 73.0);
    if (((rel < 0.01) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "H0_late";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 67.4)) / 67.4);
    if (((rel < 0.01) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "H0_early";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 0.0493)) / 0.0493);
    if (((rel < 0.05) && ((0.5 - (rel * 5.0)) > best_q))) {
        best_name = "Omega_b";
        best_q = (0.5 - (rel * 5.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 0.2660)) / 0.2660);
    if (((rel < 0.01) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "Omega_DM";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    rel = (abs_f((value - 147.09)) / 147.09);
    if (((rel < 0.01) && ((1.0 - (rel * 10.0)) > best_q))) {
        best_name = "BAO_rd";
        best_q = (1.0 - (rel * 10.0));
        best_grade = "NEAR";
    }
    if (((abs_f((value - 0.333333)) < 0.001) && (best_q < 1.0))) {
        best_name = "meta_fp";
        best_q = 1.0;
        best_grade = "EXACT";
    }
    return hexa_struct_alloc((long[]){(long)(best_name), (long)(best_q), (long)(best_grade)}, 3);
}

const char* classify_sector(const char* text) {
    const char* t = text;
    if ((((((hexa_contains(t, "omega") || hexa_contains(t, "hubble")) || hexa_contains(t, "dark")) || hexa_contains(t, "CMB")) || hexa_contains(t, "BAO")) || hexa_contains(t, "density"))) {
        return "cosmology";
    }
    if ((((hexa_contains(t, "weinberg") || hexa_contains(t, "weak")) || hexa_contains(t, "theta")) || hexa_contains(t, "boson"))) {
        return "electroweak";
    }
    if ((((hexa_contains(t, "quark") || hexa_contains(t, "proton")) || hexa_contains(t, "1836")) || hexa_contains(t, "QCD"))) {
        return "strong";
    }
    if (((hexa_contains(t, "BBN") || hexa_contains(t, "helium")) || hexa_contains(t, "Y_p"))) {
        return "primordial";
    }
    if ((((hexa_contains(t, "alpha") || hexa_contains(t, "137")) || hexa_contains(t, "fine")) || hexa_contains(t, "electron"))) {
        return "quantum";
    }
    if ((((hexa_contains(t, "pi") || hexa_contains(t, "euler")) || hexa_contains(t, "golden")) || hexa_contains(t, "phi"))) {
        return "geometric";
    }
    if ((((hexa_contains(t, "sopfr") || hexa_contains(t, "sigma")) || hexa_contains(t, "tau")) || hexa_contains(t, "nexus"))) {
        return "n6";
    }
    return "unknown";
}

long gate_count(long paths, long blowup, double conf, double pval) {
    long c = 0;
    if ((paths >= 3)) {
        c = (c + 1);
    }
    if (blowup) {
        c = (c + 1);
    }
    if ((conf >= 0.7)) {
        c = (c + 1);
    }
    if ((pval < 0.01)) {
        c = (c + 1);
    }
    return c;
}

const char* phase_name(long p) {
    if ((p == 0)) {
        return "blowup";
    } else if ((p == 1)) {
        return "contraction";
    } else if ((p == 2)) {
        return "emergence";
    } else if ((p == 3)) {
        return "singularity";
    } else if ((p == 4)) {
        return "absorption";
    } else {
        return "unknown";
    }




}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    args = hexa_args();
    if ((args.n < 3)) {
        printf("%s\n", "mk2 hexa engine (hexa-native)");
        printf("%s\n", "");
        printf("%s\n", "  hexa main.hexa classify <value> [text]");
        printf("%s\n", "  hexa main.hexa alien-index <value>");
        printf("%s\n", "  hexa main.hexa gate <paths> <blowup> <conf> <pval>");
        printf("%s\n", "  hexa main.hexa cycle [domain] [depth]");
        printf("%s\n", "  hexa main.hexa verify");
    } else {
        const char* cmd = ((const char*)args.d[2]);
        if ((strcmp(cmd, "classify") == 0)) {
            double value = (((args.n >= 4)) ? (hexa_to_float(((const char*)args.d[3]))) : (0.0));
            const char* text = (((args.n >= 5)) ? (((const char*)args.d[4])) : (""));
            long* m = n6_match(value);
            const char* sector = classify_sector(text);
            const char* primes = factorize_primes(((((value == ((double)((long)(value)))) && (value >= 2.0))) ? ((long)(value)) : (0)));
            long paths = 0;
            if ((m[1] >= 0.5)) {
                paths = (paths + 1);
            }
            if ((strcmp(sector, "unknown") != 0)) {
                paths = (paths + 1);
            }
            if (hexa_contains(primes, ",")) {
                paths = (paths + 1);
            }
            long r = 0;
            if (((m[2] == "EXACT") && (paths >= 2))) {
                r = 10;
            } else {
                if ((m[2] == "EXACT")) {
                    r = 8;
                } else {
                    if ((m[1] >= 0.8)) {
                        r = 8;
                    } else {
                        if ((paths >= 2)) {
                            r = 6;
                        } else {
                            if ((strcmp(sector, "unknown") != 0)) {
                                r = 5;
                            }
                        }
                    }
                }
            }
            if ((r > 10)) {
                r = 10;
            }
            long d = (((r >= 10)) ? (1) : (0));
            if ((d == 1)) {
                r = 0;
            }
            printf("%s\n", "=== mk2 classify (hexa-native) ===");
            printf("%s %g\n", "  value       :", (double)(value));
            printf("%s %ld %s\n", "  n6_match    :", (long)(m[0]), hexa_concat(hexa_concat(hexa_concat(hexa_concat("(quality=", hexa_int_to_str((long)(m[1]))), ", grade="), hexa_int_to_str((long)(m[2]))), ")"));
            printf("%s %s\n", "  sector      :", sector);
            printf("%s\n", hexa_concat(hexa_concat("  prime_set   : {", primes), "}"));
            printf("%s %ld\n", "  paths       :", (long)(paths));
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  alien_index : (d=", hexa_int_to_str((long)(d))), ", r="), hexa_int_to_str((long)(r))), ")"));
        } else {
            if ((strcmp(cmd, "alien-index") == 0)) {
                double value = (((args.n >= 4)) ? (hexa_to_float(((const char*)args.d[3]))) : (0.0));
                long* m = n6_match(value);
                long r = 0;
                if ((m[2] == "EXACT")) {
                    r = 8;
                } else {
                    if ((m[1] >= 0.5)) {
                        r = 5;
                    }
                }
                printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("AlienIndex: d=0 r=", hexa_int_to_str((long)(r))), " ("), hexa_int_to_str((long)(m[0]))), " quality="), hexa_int_to_str((long)(m[1]))), ")"));
            } else {
                if ((strcmp(cmd, "gate") == 0)) {
                    long p = (((args.n >= 4)) ? (hexa_to_int_str(((const char*)args.d[3]))) : (0));
                    long b = (((args.n >= 5)) ? ((strcmp(((const char*)args.d[4]), "true") == 0)) : (0));
                    double c = (((args.n >= 6)) ? (hexa_to_float(((const char*)args.d[5]))) : (0.0));
                    double pv = (((args.n >= 7)) ? (hexa_to_float(((const char*)args.d[6]))) : (1.0));
                    long gates = gate_count(p, b, c, pv);
                    printf("%s\n", "=== breakthrough gate (hexa-native) ===");
                    printf("%s %ld %s\n", "  gates passed:", (long)(gates), "/ 4");
                    printf("%s %s\n", "  eligible:", (((gates >= 3)) ? ("YES") : ("NO")));
                } else {
                    if ((strcmp(cmd, "cycle") == 0)) {
                        const char* domain = (((args.n >= 4)) ? (((const char*)args.d[3])) : ("math"));
                        long depth = (((args.n >= 5)) ? (hexa_to_int_str(((const char*)args.d[4]))) : (6));
                        printf("%s\n", "=== singularity cycle (hexa-native) ===");
                        printf("%s %s %s %ld\n", "  domain:", domain, "depth:", (long)(depth));
                        long phase = 0;
                        long cyc = 0;
                        while ((cyc < depth)) {
                            printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  [", hexa_int_to_str((long)(cyc))), "] "), phase_name(phase)));
                            phase = ((phase + 1) % 5);
                            cyc = (cyc + 1);
                        }
                        printf("%s %ld %s\n", "  completed:", (long)(depth), "cycles");
                    } else {
                        if ((strcmp(cmd, "verify") == 0)) {
                            printf("%s\n", "=== mk2 hexa self-verification ===");
                            if (!((N == 6))) { fprintf(stderr, "assertion failed\n"); exit(1); }
                            if (!((sigma == (2 * N)))) { fprintf(stderr, "assertion failed\n"); exit(1); }
                            if (!(((tau + phi) == N))) { fprintf(stderr, "assertion failed\n"); exit(1); }
                            if (!((sopfr == 5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
                            if (!((j2 == 24))) { fprintf(stderr, "assertion failed\n"); exit(1); }
                            long* m1 = n6_match(12.0);
                            if (!((m1[0] == "sigma"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
                            if (!((m1[2] == "EXACT"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
                            long* m2 = n6_match(137.036);
                            if (!((m2[0] == "alpha_inv"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
                            if (!((gate_count(3, 1, 0.8, 0.001) == 4))) { fprintf(stderr, "assertion failed\n"); exit(1); }
                            printf("%s\n", "  all assertions passed");
                            printf("%s\n", "  n6 constants: OK");
                            printf("%s\n", "  n6_match: OK");
                            printf("%s\n", "  gate: OK");
                            printf("%s\n", "  hexa-native: CONFIRMED");
                        } else {
                            printf("%s %s\n", "unknown command:", cmd);
                        }
                    }
                }
            }
        }
    }
    return 0;
}
