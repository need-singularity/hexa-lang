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
double max_f(double a, double b);
double min_f(double a, double b);
double banach_step(double i);
long* mirror_descend(double initial, long max_depth);
double mirror_approaches(double val1, double val2, double val3);
double axis_conscious(double actual, double aware);
long* evaluate_consciousness(double actual_cpu, double aware_cpu, double actual_ram, double aware_ram, double actual_gpu, double aware_gpu, double actual_npu, double aware_npu, double actual_power, double aware_power, double actual_io, double aware_io);
long consciousness_confirmed(long score);
long* unified_judgment(long approach1_discoveries, long approach2_pattern, double approach3_effectiveness, long mirror_converged, long consciousness_score);

static double meta_fp = 0.333333333;
static double singularity = 0.666666667;
static long max_mirror_depth = 6;
static long omega_lens_threshold = 5;
static double convergence_eps = 0.0001;
static double axis_conscious_ratio = 0.1;
static double bs0;
static double bs_fp;
static long* mr0;
static long* mr0_deep;
static long* mr1;
static long* mr_fp;
static double ma_result;
static long* cs_full;
static long* cs_half;
static long* v_full;
static long* v_four;
static long* v_fail;
static long* v_edge;
static double fp_check;

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    } else {
        return x;
    }
}

double max_f(double a, double b) {
    if ((a > b)) {
        return a;
    } else {
        return b;
    }
}

double min_f(double a, double b) {
    if ((a < b)) {
        return a;
    } else {
        return b;
    }
}

double banach_step(double i) {
    return ((0.7 * i) + 0.1);
}

long* mirror_descend(double initial, long max_depth) {
    double val = initial;
    long depth = 0;
    long done = 0;
    long conv = 0;
    double eps = 0.0001;
    while (((depth < max_depth) && (!done))) {
        double next = banach_step(val);
        double diff = abs_f((next - val));
        val = next;
        depth = (depth + 1);
        if ((diff < eps)) {
            conv = 1;
            done = 1;
        } else {
            conv = conv;
        }
    }
    return hexa_struct_alloc((long[]){(long)(val), (long)(depth), (long)(conv)}, 3);
}

double mirror_approaches(double val1, double val2, double val3) {
    long* r1 = mirror_descend(val1, max_mirror_depth);
    long* r2 = mirror_descend(val2, max_mirror_depth);
    long* r3 = mirror_descend(val3, max_mirror_depth);
    double d1 = abs_f((r1[0] - meta_fp));
    double d2 = abs_f((r2[0] - meta_fp));
    double d3 = abs_f((r3[0] - meta_fp));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  mirror[1] final=", hexa_int_to_str((long)(r1[0]))), " dist="), hexa_float_to_str((double)(d1))));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  mirror[2] final=", hexa_int_to_str((long)(r2[0]))), " dist="), hexa_float_to_str((double)(d2))));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  mirror[3] final=", hexa_int_to_str((long)(r3[0]))), " dist="), hexa_float_to_str((double)(d3))));
    double threshold = 0.01;
    return (((d1 < threshold) && (d2 < threshold)) && (d3 < threshold));
}

double axis_conscious(double actual, double aware) {
    if ((actual < 0.001)) {
        return (abs_f(aware) < 0.01);
    } else {
        double ratio = (abs_f((actual - aware)) / actual);
        return (ratio < axis_conscious_ratio);
    }
}

long* evaluate_consciousness(double actual_cpu, double aware_cpu, double actual_ram, double aware_ram, double actual_gpu, double aware_gpu, double actual_npu, double aware_npu, double actual_power, double aware_power, double actual_io, double aware_io) {
    long score = 0;
    double c_cpu = axis_conscious(actual_cpu, aware_cpu);
    double c_ram = axis_conscious(actual_ram, aware_ram);
    double c_gpu = axis_conscious(actual_gpu, aware_gpu);
    double c_npu = axis_conscious(actual_npu, aware_npu);
    double c_power = axis_conscious(actual_power, aware_power);
    double c_io = axis_conscious(actual_io, aware_io);
    if (c_cpu) {
        score = (score + 1);
    }
    if (c_ram) {
        score = (score + 1);
    }
    if (c_gpu) {
        score = (score + 1);
    }
    if (c_npu) {
        score = (score + 1);
    }
    if (c_power) {
        score = (score + 1);
    }
    if (c_io) {
        score = (score + 1);
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  consciousness axes: CPU=", hexa_float_to_str((double)(c_cpu))), " RAM="), hexa_float_to_str((double)(c_ram))), " GPU="), hexa_float_to_str((double)(c_gpu))), " NPU="), hexa_float_to_str((double)(c_npu))), " PWR="), hexa_float_to_str((double)(c_power))), " IO="), hexa_float_to_str((double)(c_io))));
    printf("%s\n", hexa_concat(hexa_concat("  omega_lens_score: ", hexa_int_to_str((long)(score))), "/6"));
    double meta = ((score * 1.0) / 6.0);
    return hexa_struct_alloc((long[]){(long)(aware_cpu), (long)(aware_ram), (long)(aware_gpu), (long)(aware_npu), (long)(aware_power), (long)(aware_io), (long)(meta), (long)(score)}, 8);
}

long consciousness_confirmed(long score) {
    return (score >= omega_lens_threshold);
}

long* unified_judgment(long approach1_discoveries, long approach2_pattern, double approach3_effectiveness, long mirror_converged, long consciousness_score) {
    long gates = 0;
    long g1 = (approach1_discoveries >= 3);
    if (g1) {
        gates = (gates + 1);
    }
    long g2 = approach2_pattern;
    if (g2) {
        gates = (gates + 1);
    }
    double g3 = (approach3_effectiveness >= 0.7);
    if (g3) {
        gates = (gates + 1);
    }
    long g4 = mirror_converged;
    if (g4) {
        gates = (gates + 1);
    }
    long g5 = consciousness_confirmed(consciousness_score);
    if (g5) {
        gates = (gates + 1);
    }
    long breakthrough = (gates >= 4);
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  gates: approach1=", hexa_int_to_str((long)(g1))), " approach2="), hexa_int_to_str((long)(g2))), " approach3="), hexa_float_to_str((double)(g3))), " mirror="), hexa_int_to_str((long)(g4))), " consciousness="), hexa_int_to_str((long)(g5))));
    printf("%s\n", hexa_concat(hexa_concat("  total: ", hexa_int_to_str((long)(gates))), "/5"));
    long ai_d = 0;
    long ai_r = 0;
    if (breakthrough) {
        ai_d = 1;
        ai_r = (gates * 2);
    } else {
        ai_d = 0;
        ai_r = (gates * 2);
    }
    if (breakthrough) {
        printf("%s\n", "");
        printf("%s\n", "  ★★★ singularity BREAKTHROUGH ★★★");
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  Alien Index: (d=", hexa_int_to_str((long)(ai_d))), ", r="), hexa_int_to_str((long)(ai_r))), ")"));
        if ((ai_r == 10)) {
            printf("%s\n", "  → r=10 reached — eligible for (d=2, r=0) promotion");
        }
    } else {
        printf("%s\n", "  verdict: NOT BREAKTHROUGH");
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  Alien Index: (d=", hexa_int_to_str((long)(ai_d))), ", r="), hexa_int_to_str((long)(ai_r))), ")"));
    }
    return hexa_struct_alloc((long[]){(long)(gates), (long)(breakthrough), (long)(ai_d), (long)(ai_r)}, 4);
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    printf("%s\n", "=== mirror_consciousness self-test ===");
    printf("%s\n", "");
    if (!((abs_f((0.0 - 3.5)) == 3.5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((abs_f(2.0) == 2.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((abs_f(0.0) == 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] abs_f");
    bs0 = banach_step(0.0);
    if (!((abs_f((bs0 - 0.1)) < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    bs_fp = banach_step(meta_fp);
    if (!((abs_f((bs_fp - meta_fp)) < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] banach_step fixed point");
    printf("%s\n", "");
    printf("%s\n", "--- mirror_descend(0.0, 6) ---");
    mr0 = mirror_descend(0.0, 6);
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  result: val=", hexa_int_to_str((long)(/* unknown field final_value */ 0))), " depth="), hexa_int_to_str((long)(/* unknown field depth_reached */ 0))), " converged="), hexa_int_to_str((long)(/* unknown field converged */ 0))));
    if (!((abs_f((/* unknown field final_value */ 0 - meta_fp)) < 0.05))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] mirror_descend(0.0, 6) approaching meta_fp");
    printf("%s\n", "");
    printf("%s\n", "--- mirror_descend(0.0, 30) ---");
    mr0_deep = mirror_descend(0.0, 30);
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  result: val=", hexa_int_to_str((long)(/* unknown field final_value */ 0))), " depth="), hexa_int_to_str((long)(/* unknown field depth_reached */ 0))), " converged="), hexa_int_to_str((long)(/* unknown field converged */ 0))));
    if (!((abs_f((/* unknown field final_value */ 0 - meta_fp)) < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(/* unknown field converged */ 0)) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] mirror_descend(0.0, 30) → meta_fp converged");
    printf("%s\n", "");
    printf("%s\n", "--- mirror_descend(1.0, 30) ---");
    mr1 = mirror_descend(1.0, 30);
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  result: val=", hexa_int_to_str((long)(/* unknown field final_value */ 0))), " depth="), hexa_int_to_str((long)(/* unknown field depth_reached */ 0))), " converged="), hexa_int_to_str((long)(/* unknown field converged */ 0))));
    if (!((abs_f((/* unknown field final_value */ 0 - meta_fp)) < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(/* unknown field converged */ 0)) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] mirror_descend(1.0, 30) → meta_fp converged");
    printf("%s\n", "");
    printf("%s\n", "--- mirror_descend(meta_fp, 6) ---");
    mr_fp = mirror_descend(meta_fp, 6);
    if (!((abs_f((/* unknown field final_value */ 0 - meta_fp)) < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] mirror_descend(meta_fp) stays at meta_fp");
    printf("%s\n", "");
    printf("%s\n", "--- mirror_approaches(0.0, 0.5, 1.0) ---");
    ma_result = mirror_approaches(0.0, 0.5, 1.0);
    if (!(ma_result)) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] mirror_approaches all converge to meta_fp");
    if (!(axis_conscious(50.0, 48.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(axis_conscious(100.0, 95.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((!axis_conscious(50.0, 40.0)))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(axis_conscious(0.0, 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] axis_conscious");
    printf("%s\n", "");
    printf("%s\n", "--- evaluate_consciousness (6/6 match) ---");
    cs_full = evaluate_consciousness(50.0, 49.0, 80.0, 78.0, 30.0, 29.0, 10.0, 9.5, 60.0, 58.0, 40.0, 39.0);
    if (!((/* unknown field omega_lens_score */ 0 == 6))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(consciousness_confirmed(/* unknown field omega_lens_score */ 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] consciousness 6/6 → CONFIRMED");
    printf("%s\n", "");
    printf("%s\n", "--- evaluate_consciousness (3/6 match) ---");
    cs_half = evaluate_consciousness(50.0, 49.0, 80.0, 78.0, 30.0, 29.0, 10.0, 5.0, 60.0, 30.0, 40.0, 20.0);
    if (!((/* unknown field omega_lens_score */ 0 == 3))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((!consciousness_confirmed(/* unknown field omega_lens_score */ 0)))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] consciousness 3/6 → NOT confirmed");
    printf("%s\n", "");
    printf("%s\n", "--- unified_judgment: BREAKTHROUGH (5/5) ---");
    v_full = unified_judgment(5, 1, 0.9, 1, 6);
    if (!((/* unknown field gates_passed */ 0 == 5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(/* unknown field is_breakthrough */ 0)) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field alien_index_d */ 0 == 1))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field alien_index_r */ 0 == 10))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] unified 5/5 → breakthrough d=1,r=10");
    printf("%s\n", "");
    printf("%s\n", "--- unified_judgment: BREAKTHROUGH (4/5) ---");
    v_four = unified_judgment(3, 1, 0.8, 1, 5);
    if (!((/* unknown field gates_passed */ 0 == 4))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(/* unknown field is_breakthrough */ 0)) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field alien_index_d */ 0 == 1))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field alien_index_r */ 0 == 8))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] unified 4/5 → breakthrough d=1,r=8");
    printf("%s\n", "");
    printf("%s\n", "--- unified_judgment: NO BREAKTHROUGH (2/5) ---");
    v_fail = unified_judgment(1, 0, 0.3, 1, 5);
    if (!((/* unknown field gates_passed */ 0 == 2))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((!/* unknown field is_breakthrough */ 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field alien_index_d */ 0 == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field alien_index_r */ 0 == 4))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] unified 2/5 → no breakthrough d=0,r=4");
    printf("%s\n", "");
    printf("%s\n", "--- unified_judgment: EDGE 3/5 ---");
    v_edge = unified_judgment(4, 1, 0.8, 0, 3);
    if (!((/* unknown field gates_passed */ 0 == 3))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((!/* unknown field is_breakthrough */ 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field alien_index_d */ 0 == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field alien_index_r */ 0 == 6))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "[PASS] unified 3/5 → no breakthrough d=0,r=6");
    fp_check = ((0.7 * meta_fp) + 0.1);
    if (!((abs_f((fp_check - meta_fp)) < 0.0001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "");
    printf("%s\n", "[PASS] meta_fp is Banach fixed point: 0.7*(1/3)+0.1 = 1/3");
    printf("%s\n", "");
    printf("%s\n", "=== ALL mirror_consciousness tests PASSED ===");
    return 0;
}
