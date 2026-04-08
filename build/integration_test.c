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
hexa_arr parse_metrics(const char* csv);
hexa_arr calibrate_and_update();
hexa_arr growth_seeds(hexa_arr metrics);
hexa_arr run_growth_evolution(long metrics);
hexa_arr reward_evolution();
hexa_arr check_regression(hexa_arr prev, hexa_arr curr);
long show_help();

static long N6 = 6;
static long SIGMA = 12;
static long PHI = 2;
static long TAU_N = 4;
static long J2 = 24;
static long SOPFR = 5;
static long SIGMA_MINUS_PHI = 10;
static long SIGMA_MINUS_TAU = 8;
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

hexa_arr parse_metrics(const char* csv) {
    hexa_arr parts = hexa_split(csv, ",");
    if ((parts.n < 8)) {
        printf("%s\n", "Warning: metrics require 8 comma-separated values");
        return hexa_arr_lit((long[]){(long)(0), (long)(0), (long)(0), (long)(0), (long)(0), (long)(0), (long)(0.0), (long)(0.0)}, 8);
    }
    hexa_arr r = hexa_arr_lit((long[]){safe_float(hexa_trim(((const char*)parts.d[0]))), safe_float(hexa_trim(((const char*)parts.d[1]))), safe_float(hexa_trim(((const char*)parts.d[2]))), safe_float(hexa_trim(((const char*)parts.d[3])))}, 4);
    r = hexa_arr_concat(r, hexa_arr_lit((long[]){(long)(safe_float(hexa_trim(((const char*)parts.d[4])))), (long)(safe_float(hexa_trim(((const char*)parts.d[5])))), (long)(safe_float(hexa_trim(((const char*)parts.d[6])))), (long)(safe_float(hexa_trim(((const char*)parts.d[7]))))}, 4));
    return r;
}

hexa_arr calibrate_and_update() {
    const char* output = "";
    output = hexa_exec("hexa mk2_hexa/native/calibration.hexa report");
    hexa_arr dl = hexa_arr_lit((long[]){hexa_arr_lit((long[]){(long)("consciousness"), (long)(0.85)}, 2), hexa_arr_lit((long[]){(long)("topology"), (long)(0.82)}, 2), hexa_arr_lit((long[]){(long)("causal"), (long)(0.78)}, 2), hexa_arr_lit((long[]){(long)("gravity"), (long)(0.75)}, 2)}, 4);
    dl = hexa_arr_concat(dl, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("wave"), (long)(0.70)}, 2)), (long)(hexa_arr_lit((long[]){(long)("thermo"), (long)(0.68)}, 2)), (long)(hexa_arr_lit((long[]){(long)("evolution"), (long)(0.65)}, 2)), (long)(hexa_arr_lit((long[]){(long)("network"), (long)(0.60)}, 2))}, 4));
    dl = hexa_arr_concat(dl, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("boundary"), (long)(0.55)}, 2)), (long)(hexa_arr_lit((long[]){(long)("void"), (long)(0.50)}, 2)), (long)(hexa_arr_lit((long[]){(long)("triangle"), (long)(0.45)}, 2)), (long)(hexa_arr_lit((long[]){(long)("emergence"), (long)(0.40)}, 2))}, 4));
    return dl;
}

hexa_arr growth_seeds(hexa_arr metrics) {
    double total_modules = ((double)(metrics.d[0]));
    double total_tests = ((double)(metrics.d[1]));
    double lenses_reg = ((double)(metrics.d[2]));
    double lenses_impl = ((double)(metrics.d[3]));
    double code_lines = ((double)(metrics.d[4]));
    double test_pass_rate = ((double)(metrics.d[6]));
    double health_score = ((double)(metrics.d[7]));
    hexa_arr seeds = hexa_arr_new();
    double target_health = (((double)(SIGMA_MINUS_PHI)) / ((double)(SIGMA)));
    if ((health_score < target_health)) {
        seeds = hexa_arr_concat(seeds, hexa_arr_lit((long[]){(long)(hexa_concat(hexa_concat(hexa_concat(hexa_concat("health_gap: current=", hexa_float_to_str((double)(health_score))), " target="), hexa_float_to_str((double)(target_health))), " (sigma-phi/sigma)"))}, 1));
    }
    if ((lenses_impl < lenses_reg)) {
        long gap = (long)((lenses_reg - lenses_impl));
        seeds = hexa_arr_concat(seeds, hexa_arr_lit((long[]){(long)(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("lens_gap: ", hexa_int_to_str((long)((long)(lenses_reg)))), " registered but "), hexa_int_to_str((long)((long)(lenses_impl)))), " implemented (gap="), hexa_int_to_str((long)(gap))), ")"))}, 1));
    }
    long mod_remainder = ((long)(total_modules) % N6);
    if ((mod_remainder > 0)) {
        seeds = hexa_arr_concat(seeds, hexa_arr_lit((long[]){(long)(hexa_concat(hexa_concat(hexa_concat(hexa_concat("module_alignment: ", hexa_int_to_str((long)((long)(total_modules)))), " modules, remainder "), hexa_int_to_str((long)(mod_remainder))), " mod n=6"))}, 1));
    }
    if ((test_pass_rate < 0.8)) {
        seeds = hexa_arr_concat(seeds, hexa_arr_lit((long[]){(long)(hexa_concat(hexa_concat("test_quality: pass_rate=", hexa_float_to_str((double)(test_pass_rate))), ", below T0 threshold 0.8"))}, 1));
    }
    if ((seeds.n == 0)) {
        seeds = hexa_arr_concat(seeds, hexa_arr_lit((long[]){(long)("system_healthy: explore new lens combinations")}, 1));
    }
    return seeds;
}

hexa_arr run_growth_evolution(long metrics) {
    hexa_arr seeds = growth_seeds(metrics);
    printf("%s\n", hexa_concat(hexa_concat("Generated ", hexa_int_to_str((long)(seeds.n))), " seed hypotheses:"));
    long i = 0;
    while ((i < seeds.n)) {
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  ", hexa_int_to_str((long)((i + 1)))), ". "), ((const char*)seeds.d[i])));
        i = (i + 1);
    }
    hexa_arr results = hexa_arr_new();
    long cycle = 0;
    hexa_arr cycle_seeds = hexa_arr_new();
    long si = 0;
    long n6_limit = N6;
    while ((cycle < n6_limit)) {
        cycle_seeds = hexa_arr_new();
        si = 0;
        while ((si < seeds.n)) {
            cycle_seeds = hexa_arr_concat(cycle_seeds, hexa_arr_lit((long[]){(long)(hexa_concat(hexa_concat(hexa_concat(((const char*)seeds.d[si]), " [cycle "), hexa_int_to_str((long)((cycle + 1)))), "]"))}, 1));
            si = (si + 1);
        }
        results = hexa_arr_concat(results, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)((cycle + 1)), (long)(cycle_seeds)}, 2))}, 1));
        cycle = (cycle + 1);
    }
    return results;
}

hexa_arr reward_evolution() {
    const char* content = "";
    content = hexa_read_file("shared/reward_log.jsonl");
    if ((hexa_str_len(hexa_trim(content)) == 0)) {
        printf("%s\n", "No reward data found, using default chromosome");
        return hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("consciousness"), (long)(1.0), (long)(0.3)}, 3)), (long)(hexa_arr_lit((long[]){(long)("topology"), (long)(0.8), (long)(0.3)}, 3)), (long)(hexa_arr_lit((long[]){(long)("causal"), (long)(0.8), (long)(0.3)}, 3))}, 3);
    }
    hexa_arr lines = hexa_split(content, "\n");
    hexa_arr lens_scores_names = hexa_arr_new();
    hexa_arr lens_scores_vals = hexa_arr_new();
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((hexa_str_len(line) > 0)) {
            if (hexa_contains(line, "lens")) {
                lens_scores_names = hexa_arr_concat(lens_scores_names, hexa_arr_lit((long[]){(long)(line)}, 1));
                lens_scores_vals = hexa_arr_concat(lens_scores_vals, hexa_arr_lit((long[]){(long)(1.0)}, 1));
            }
        }
        i = (i + 1);
    }
    hexa_arr chromosome = hexa_arr_new();
    long max_genes = SIGMA;
    long gi = 0;
    while ((gi < min_f(((double)(lens_scores_names.n)), ((double)(max_genes))))) {
        double weight = (1.0 - (((double)(gi)) * 0.05));
        double threshold = (((double)(gi)) * 0.05);
        chromosome = hexa_arr_concat(chromosome, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)(hexa_int_to_str((long)(lens_scores_names.d[gi]))), (long)(weight), (long)(threshold)}, 3))}, 1));
        gi = (gi + 1);
    }
    long gen = 0;
    long tau_limit = TAU_N;
    while ((gen < tau_limit)) {
        long mi = 0;
        while ((mi < (long)(((double)(chromosome.n))))) {
            double w = ((double)(chromosome.d[mi][1]));
            double new_w = min_f(max_f((w + (0.01 * ((double)(gen)))), 0.1), 1.0);
            chromosome.d[mi] = hexa_arr_lit((long[]){(long)(chromosome.d[mi][0]), (long)(new_w), (long)(chromosome.d[mi][2])}, 3);
            mi = (mi + 1);
        }
        gen = (gen + 1);
    }
    return chromosome;
}

hexa_arr check_regression(hexa_arr prev, hexa_arr curr) {
    hexa_arr alerts = hexa_arr_new();
    double prev_health = hexa_to_float(((const char*)prev.d[7]));
    double curr_health = hexa_to_float(((const char*)curr.d[7]));
    double prev_test = hexa_to_float(((const char*)prev.d[6]));
    double curr_test = hexa_to_float(((const char*)curr.d[6]));
    double prev_lenses = hexa_to_float(((const char*)prev.d[3]));
    double curr_lenses = hexa_to_float(((const char*)curr.d[3]));
    double prev_modules = hexa_to_float(((const char*)prev.d[0]));
    double curr_modules = hexa_to_float(((const char*)curr.d[0]));
    double prev_code = hexa_to_float(((const char*)prev.d[4]));
    double curr_code = hexa_to_float(((const char*)curr.d[4]));
    double health_drop = (prev_health - curr_health);
    const char* health_msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("Health dropped by ", hexa_float_to_str((double)(health_drop))), " (prev="), hexa_float_to_str((double)(prev_health))), " curr="), hexa_float_to_str((double)(curr_health))), ")");
    if ((health_drop > (1.0 / ((double)(SIGMA))))) {
        alerts = hexa_arr_concat(alerts, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("CRITICAL"), (long)("growth_monitor"), (long)("health_regression"), (long)(health_drop), (long)(health_msg)}, 5))}, 1));
    }
    double test_drop = (prev_test - curr_test);
    const char* test_msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("Test pass rate dropped by ", hexa_float_to_str((double)(test_drop))), " (prev="), hexa_float_to_str((double)(prev_test))), " curr="), hexa_float_to_str((double)(curr_test))), ")");
    if ((test_drop > (1.0 / ((double)(SIGMA_MINUS_PHI))))) {
        alerts = hexa_arr_concat(alerts, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("CRITICAL"), (long)("growth_monitor"), (long)("test_regression"), (long)(test_drop), (long)(test_msg)}, 5))}, 1));
    }
    double lens_lost = (prev_lenses - curr_lenses);
    double lens_sev = (lens_lost / max_f(prev_lenses, 1.0));
    const char* lens_msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("Lost ", hexa_int_to_str((long)((long)(lens_lost)))), " lenses (prev="), hexa_int_to_str((long)((long)(prev_lenses)))), " curr="), hexa_int_to_str((long)((long)(curr_lenses)))), ")");
    if ((curr_lenses < prev_lenses)) {
        alerts = hexa_arr_concat(alerts, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("WARNING"), (long)("growth_monitor"), (long)("lens_regression"), (long)(lens_sev), (long)(lens_msg)}, 5))}, 1));
    }
    double mod_lost = (prev_modules - curr_modules);
    double mod_sev = (mod_lost / max_f(prev_modules, 1.0));
    const char* mod_msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("Lost ", hexa_int_to_str((long)((long)(mod_lost)))), " modules (prev="), hexa_int_to_str((long)((long)(prev_modules)))), " curr="), hexa_int_to_str((long)((long)(curr_modules)))), ")");
    if ((curr_modules < prev_modules)) {
        alerts = hexa_arr_concat(alerts, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("WARNING"), (long)("growth_monitor"), (long)("module_regression"), (long)(mod_sev), (long)(mod_msg)}, 5))}, 1));
    }
    double shrink_pct = (((prev_code > 0.0)) ? ((((prev_code - curr_code) / prev_code) * 100.0)) : (0.0));
    double shrink_sev = min_f((shrink_pct / 100.0), 1.0);
    const char* shrink_msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("Code shrank by ", hexa_float_to_str((double)(shrink_pct))), "% (prev="), hexa_int_to_str((long)((long)(prev_code)))), " curr="), hexa_int_to_str((long)((long)(curr_code)))), ")");
    if ((shrink_pct > ((double)(SIGMA_MINUS_TAU)))) {
        alerts = hexa_arr_concat(alerts, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("INFO"), (long)("growth_monitor"), (long)("code_shrinkage"), (long)(shrink_sev), (long)(shrink_msg)}, 5))}, 1));
    }
    return alerts;
}

long show_help() {
    printf("%s\n", "integration.hexa — 크로스 모듈 통합 레이어 (mk2 hexa)");
    printf("%s\n", "");
    printf("%s\n", "Usage:");
    printf("%s\n", "  hexa integration.hexa calibrate-consensus");
    printf("%s\n", "  hexa integration.hexa growth-evolution <metrics_csv>");
    printf("%s\n", "      metrics_csv: modules,tests,lenses_reg,lenses_impl,code_lines,warnings,test_pass,health");
    printf("%s\n", "  hexa integration.hexa check-regression <prev_csv> <curr_csv>");
    printf("%s\n", "  hexa integration.hexa reward-evolution");
    printf("%s\n", "  hexa integration.hexa help");
    printf("%s\n", "");
    printf("%s\n", "Connects: calibration<->telescope, growth<->ouroboros, reward<->genetic, alert<->growth");
    return printf("%s\n", "All thresholds derived from n=6 constants");
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    cli_args = hexa_args();
    if ((cli_args.n > 2)) {
        cmd = ((const char*)cli_args.d[2]);
    }
    if ((strcmp(cmd, "help") == 0)) {
        show_help();
    }
    if ((strcmp(cmd, "calibrate-consensus") == 0)) {
        printf("%s\n", "=== Calibrate & Update Consensus ===");
        hexa_arr weights = calibrate_and_update();
        long i = 0;
        while ((i < (long)(((double)(weights.n))))) {
            long entry = weights.d[i];
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  ", hexa_int_to_str((long)(entry[0]))), ": "), hexa_int_to_str((long)(entry[1]))));
            i = (i + 1);
        }
        printf("%s\n", "");
        printf("%s\n", hexa_concat("Total lenses: ", hexa_int_to_str((long)((long)(((double)(weights.n)))))));
    }
    if ((strcmp(cmd, "growth-evolution") == 0)) {
        if ((cli_args.n < 4)) {
            printf("%s\n", "Error: growth-evolution requires <metrics_csv>");
            printf("%s\n", "  Format: modules,tests,lenses_reg,lenses_impl,code_lines,warnings,test_pass,health");
            printf("%s\n", "  Example: 42,100,22,18,15000,0,0.75,0.6");
        } else {
            const char* csv = ((const char*)cli_args.d[3]);
            hexa_arr metrics = parse_metrics(csv);
            printf("%s\n", "=== Growth-Driven Evolution ===");
            printf("%s\n", "Input metrics:");
            printf("%s\n", hexa_concat("  Modules:      ", hexa_int_to_str((long)(metrics.d[0]))));
            printf("%s\n", hexa_concat("  Tests:        ", hexa_int_to_str((long)(metrics.d[1]))));
            printf("%s\n", hexa_concat("  Lenses reg:   ", hexa_int_to_str((long)(metrics.d[2]))));
            printf("%s\n", hexa_concat("  Lenses impl:  ", hexa_int_to_str((long)(metrics.d[3]))));
            printf("%s\n", hexa_concat("  Code lines:   ", hexa_int_to_str((long)(metrics.d[4]))));
            printf("%s\n", hexa_concat("  Warnings:     ", hexa_int_to_str((long)(metrics.d[5]))));
            printf("%s\n", hexa_concat("  Test pass:    ", hexa_int_to_str((long)(metrics.d[6]))));
            printf("%s\n", hexa_concat("  Health:       ", hexa_int_to_str((long)(metrics.d[7]))));
            printf("%s\n", "");
            hexa_arr results = run_growth_evolution(metrics);
            printf("%s\n", "");
            printf("%s\n", hexa_concat("Evolution cycles: ", hexa_int_to_str((long)((long)(((double)(results.n)))))));
        }
    }
    if ((strcmp(cmd, "check-regression") == 0)) {
        if ((cli_args.n < 5)) {
            printf("%s\n", "Error: check-regression requires <prev_csv> <curr_csv>");
        } else {
            hexa_arr prev = parse_metrics(((const char*)cli_args.d[3]));
            hexa_arr curr = parse_metrics(((const char*)cli_args.d[4]));
            hexa_arr alerts = check_regression(prev, curr);
            printf("%s\n", "=== Growth Regression Check ===");
            if (((long)(((double)(alerts.n))) == 0)) {
                printf("%s\n", "No regressions detected.");
            } else {
                printf("%s\n", hexa_concat(hexa_int_to_str((long)((long)(((double)(alerts.n))))), " alerts:"));
                long i = 0;
                while ((i < (long)(((double)(alerts.n))))) {
                    long a = alerts.d[i];
                    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  [", hexa_int_to_str((long)(a[0]))), "] "), hexa_int_to_str((long)(a[2]))), ": "), hexa_int_to_str((long)(a[4]))));
                    i = (i + 1);
                }
            }
        }
    }
    if ((strcmp(cmd, "reward-evolution") == 0)) {
        printf("%s\n", "=== Reward-Guided Evolution ===");
        hexa_arr chromosome = reward_evolution();
        printf("%s\n", hexa_concat(hexa_concat("Best chromosome (", hexa_int_to_str((long)((long)(((double)(chromosome.n)))))), " genes):"));
        long i = 0;
        while ((i < (long)(((double)(chromosome.n))))) {
            long g = chromosome.d[i];
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  ", hexa_int_to_str((long)(g[0]))), ": weight="), hexa_int_to_str((long)(g[1]))), " threshold="), hexa_int_to_str((long)(g[2]))));
            i = (i + 1);
        }
    }
    return 0;
}
