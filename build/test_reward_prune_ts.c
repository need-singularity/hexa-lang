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

long test_curiosity_bounded();
long test_dialogue_improving();
long test_prune_increases_sparsity();
long test_trend_positive();
long test_autocorrelation_self();

static hexa_arr prediction_errors;
static long cr;
static hexa_arr ce_losses;
static long dr;
static long reward;
static hexa_arr weights;
static long pruned;
static long pruned90;
static hexa_arr sine;
static long i = 0;
static long ac1;
static long ac5;
static long ac10;
static hexa_arr increasing;
static hexa_arr decreasing;
static hexa_arr flat;
static hexa_arr phi_history;
static long stats;
static hexa_arr pe_history;
static hexa_arr loss_history;
static long c_reward;
static long d_reward;
static long total_reward;
static long phi_trend;

long test_curiosity_bounded() {
    long r = curiosity_reward(hexa_arr_lit((long[]){(long)(0.1), (long)(0.2), (long)(0.3), (long)(0.4), (long)(0.5)}, 5), 3);
    if (!((r >= (-1.0)))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "curiosity should be >= -1";
    0;
    if (!((r <= 1.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "curiosity should be <= 1";
    return 0;
}

long test_dialogue_improving() {
    long r = dialogue_reward(hexa_arr_lit((long[]){(long)(3.0), (long)(2.0), (long)(1.0)}, 3));
    if (!((r > 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "decreasing loss = positive reward";
    return 0;
}

long test_prune_increases_sparsity() {
    long p = magnitude_prune(hexa_arr_lit((long[]){(long)(1.0), (long)(0.01), (long)(2.0), (long)(0.02)}, 4), 0.5);
    if (!((sparsity(p) >= 0.5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "sparsity should be >= target";
    return 0;
}

long test_trend_positive() {
    if (!((trend_slope(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0)}, 3)) > 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "increasing trend should be positive";
    return 0;
}

long test_autocorrelation_self() {
    long ac = autocorrelation(hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0)}, 4), 0);
    if (!((ac > 0.99))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "lag-0 autocorrelation should be ~1.0";
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    prediction_errors = hexa_arr_lit((long[]){(long)(0.3), (long)(0.5), (long)(0.1), (long)(0.8), (long)(0.2), (long)(0.9), (long)(0.4), (long)(0.7), (long)(0.15), (long)(0.6)}, 10);
    cr = curiosity_reward(prediction_errors, 5);
    printf("%s %g\n", "[1] Curiosity reward:", (double)((hexa_round((double)((cr * 10000))) / 10000)));
    ce_losses = hexa_arr_lit((long[]){(long)(2.5), (long)(2.3), (long)(2.1), (long)(1.8), (long)(1.5), (long)(1.3)}, 6);
    dr = dialogue_reward(ce_losses);
    printf("%s %g\n", "[2] Dialogue reward:", (double)((hexa_round((double)((dr * 10000))) / 10000)));
    reward = combined_reward(cr, dr, 0.7);
    printf("%s %g\n", "[3] Combined reward (70% curiosity, 30% dialogue):", (double)((hexa_round((double)((reward * 10000))) / 10000)));
    weights = hexa_arr_lit((long[]){(long)(0.5), (long)((-0.02)), (long)(1.2), (long)(0.01), (long)((-0.8)), (long)(0.03), (long)(0.7), (long)((-0.01))}, 8);
    pruned = magnitude_prune(weights, 0.5);
    printf("%s\n", "[4] Pruning (50% sparsity):");
    printf("%s [arr len=%ld]\n", "    original:", (long)((weights).n));
    printf("%s %ld\n", "    pruned:", (long)(pruned));
    printf("%s %ld\n", "    sparsity:", (long)(sparsity(pruned)));
    pruned90 = magnitude_prune(weights, 0.75);
    printf("%s %ld\n", "    75% pruned:", (long)(pruned90));
    printf("%s %ld\n", "    sparsity:", (long)(sparsity(pruned90)));
    sine = hexa_arr_new();
    while ((i < 20)) {
        sine = hexa_arr_concat(sine, hexa_arr_lit((long[]){(long)(hexa_sin((double)((i * 0.628))))}, 1));
        i = (i + 1);
    }
    ac1 = autocorrelation(sine, 1);
    ac5 = autocorrelation(sine, 5);
    ac10 = autocorrelation(sine, 10);
    printf("%s\n", "[5] Autocorrelation of sine wave:");
    printf("%s %g\n", "    lag=1:", (double)((hexa_round((double)((ac1 * 1000))) / 1000)));
    printf("%s %g %s\n", "    lag=5:", (double)((hexa_round((double)((ac5 * 1000))) / 1000)), "(half period)");
    printf("%s %g %s\n", "    lag=10:", (double)((hexa_round((double)((ac10 * 1000))) / 1000)), "(full period)");
    increasing = hexa_arr_lit((long[]){(long)(1.0), (long)(2.0), (long)(3.0), (long)(4.0), (long)(5.0)}, 5);
    decreasing = hexa_arr_lit((long[]){(long)(5.0), (long)(4.0), (long)(3.0), (long)(2.0), (long)(1.0)}, 5);
    flat = hexa_arr_lit((long[]){(long)(3.0), (long)(3.0), (long)(3.0), (long)(3.0), (long)(3.0)}, 5);
    printf("%s\n", "[6] Trend slope:");
    printf("%s %ld\n", "    increasing:", (long)(trend_slope(increasing)));
    printf("%s %ld\n", "    decreasing:", (long)(trend_slope(decreasing)));
    printf("%s %ld\n", "    flat:", (long)(trend_slope(flat)));
    phi_history = hexa_arr_lit((long[]){(long)(0.1), (long)(0.15), (long)(0.2), (long)(0.18), (long)(0.25), (long)(0.3), (long)(0.28), (long)(0.35), (long)(0.4), (long)(0.38)}, 10);
    stats = running_stats(phi_history);
    printf("%s\n", "[7] Φ history stats:");
    printf("%s %g\n", "    mean:", (double)((hexa_round((double)((stats[0] * 10000))) / 10000)));
    printf("%s %g\n", "    std:", (double)((hexa_round((double)((stats[1] * 10000))) / 10000)));
    printf("%s %ld %s %ld\n", "    min:", (long)(stats[2]), "max:", (long)(stats[3]));
    printf("%s %g\n", "    trend:", (double)((hexa_round((double)((stats[4] * 10000))) / 10000)));
    printf("%s\n", "\n[8] Anima OnlineLearner Pipeline:");
    pe_history = hexa_arr_lit((long[]){(long)(0.3), (long)(0.5), (long)(0.1), (long)(0.8), (long)(0.2)}, 5);
    loss_history = hexa_arr_lit((long[]){(long)(2.5), (long)(2.3), (long)(2.1), (long)(1.8), (long)(1.5)}, 5);
    c_reward = curiosity_reward(pe_history, 3);
    d_reward = dialogue_reward(loss_history);
    total_reward = combined_reward(c_reward, d_reward, 0.7);
    phi_trend = trend_slope(phi_history);
    printf("%s %g\n", "    curiosity:", (double)((hexa_round((double)((c_reward * 10000))) / 10000)));
    printf("%s %g\n", "    dialogue:", (double)((hexa_round((double)((d_reward * 10000))) / 10000)));
    printf("%s %g\n", "    total reward:", (double)((hexa_round((double)((total_reward * 10000))) / 10000)));
    printf("%s %g\n", "    Φ trend:", (double)((hexa_round((double)((phi_trend * 10000))) / 10000)));
    printf("%s %g\n", "    Φ improving:", (double)((phi_trend > 0.0)));
    run_tests();
    printf("%s\n", "\n--- reward+prune+ts PASS ---");
    return 0;
}
