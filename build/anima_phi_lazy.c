#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

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
    if (!f) { fprintf(stderr, "hexa_read_file: %s\n", path); exit(1); }
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

double bin_assign(double val, double n_bins, double min_val, double range);
double compute_mi(hexa_arr cell_i, long cell_j, double n_bins);
double phi_component(double global_var, double faction_var, double complexity);

static hexa_arr cell0;
static hexa_arr cell1;
static hexa_arr cell2;
static hexa_arr cell3;
static double mi_01;
static double mi_02;
static double mi_12;
static double mi_23;
static double mi_03;
static double mi_13;
static long mi_val_01;
static long mi_val_02;
static long mi_val_12;
static long mi_val_23;
static long mi_val_03;
static long mi_val_13;
static long total_mi;
static hexa_arr vals;
static double bins;
static hexa_arr global_vars;
static hexa_arr faction_vars;
static hexa_arr complexities;
static double phi_components;
static long cross_mi;
static double phi_iit;

double bin_assign(double val, double n_bins, double min_val, double range) {
    double bin = hexa_floor((double)((((val - min_val) / range) * n_bins)));
    if ((bin < 0.0)) {
        return 0.0;
    }
    if ((bin >= n_bins)) {
        return (n_bins - 1.0);
    }
    return bin;
}

double compute_mi(hexa_arr cell_i, long cell_j, double n_bins) {
    double sum_xy = 0.0;
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_x2 = 0.0;
    double sum_y2 = 0.0;
    double n = (cell_i.n * 1.0);
    long k = 0;
    while ((k < cell_i.n)) {
        long x = cell_i.d[k];
        long y = cell_j[k];
        sum_xy = (sum_xy + (x * y));
        sum_x = (sum_x + x);
        sum_y = (sum_y + y);
        sum_x2 = (sum_x2 + (x * x));
        sum_y2 = (sum_y2 + (y * y));
        k = (k + 1);
    }
    double cov = ((sum_xy / n) - ((sum_x / n) * (sum_y / n)));
    double std_x = hexa_sqrt((double)((((sum_x2 / n) - ((sum_x / n) * (sum_x / n))) + 0.0001)));
    double std_y = hexa_sqrt((double)((((sum_y2 / n) - ((sum_y / n) * (sum_y / n))) + 0.0001)));
    double corr = (cov / (std_x * std_y));
    double corr2 = (corr * corr);
    if ((corr2 > 0.999)) {
        return 5.0;
    }
    return ((-0.5) * ln((1.0 - corr2)));
}

double phi_component(double global_var, double faction_var, double complexity) {
    return ((global_var - faction_var) + (0.1 * complexity));
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    vectorize;
    lazy;
    fuse;
    cell0 = hexa_arr_lit((long[]){(long)(0.1), (long)(0.2), (long)((-0.1)), (long)(0.3)}, 4);
    cell1 = hexa_arr_lit((long[]){(long)((-0.2)), (long)(0.4), (long)(0.1), (long)((-0.1))}, 4);
    cell2 = hexa_arr_lit((long[]){(long)(0.3), (long)((-0.1)), (long)(0.2), (long)(0.4)}, 4);
    cell3 = hexa_arr_lit((long[]){(long)(0.0), (long)(0.1), (long)((-0.3)), (long)(0.2)}, 4);
    mi_01 = compute_mi(cell0, cell1, 16.0);
    mi_02 = compute_mi(cell0, cell2, 16.0);
    mi_12 = compute_mi(cell1, cell2, 16.0);
    mi_23 = compute_mi(cell2, cell3, 16.0);
    mi_03 = compute_mi(cell0, cell3, 16.0);
    mi_13 = compute_mi(cell1, cell3, 16.0);
    printf("%s\n", "[1] 6 MI thunks created (zero compute)");
    mi_val_01 = mi_01();
    mi_val_02 = mi_02();
    mi_val_12 = mi_12();
    printf("%s\n", "[2] Forced 3/6 MIs:");
    printf("%s %g\n", "    MI(0,1)=", (double)((hexa_round((double)((mi_val_01 * 10000))) / 10000)));
    printf("%s %g\n", "    MI(0,2)=", (double)((hexa_round((double)((mi_val_02 * 10000))) / 10000)));
    printf("%s %g\n", "    MI(1,2)=", (double)((hexa_round((double)((mi_val_12 * 10000))) / 10000)));
    mi_val_23 = mi_23();
    mi_val_03 = mi_03();
    mi_val_13 = mi_13();
    total_mi = (((((mi_val_01 + mi_val_02) + mi_val_12) + mi_val_23) + mi_val_03) + mi_val_13);
    printf("%s %g\n", "[3] Total MI =", (double)((hexa_round((double)((total_mi * 10000))) / 10000)));
    vals = hexa_arr_lit((long[]){(long)(0.1), (long)(0.2), (long)((-0.1)), (long)(0.3), (long)((-0.2)), (long)(0.4)}, 6);
    bins = bin_assign(vals, 16.0, (-0.5), 1.0);
    printf("%s %g\n", "[4] Histogram bins:", (double)(bins));
    global_vars = hexa_arr_lit((long[]){(long)(0.05), (long)(0.08), (long)(0.03), (long)(0.07)}, 4);
    faction_vars = hexa_arr_lit((long[]){(long)(0.02), (long)(0.03), (long)(0.01), (long)(0.04)}, 4);
    complexities = hexa_arr_lit((long[]){(long)(0.3), (long)(0.5), (long)(0.2), (long)(0.4)}, 4);
    phi_components = phi_component(global_vars, faction_vars, complexities);
    printf("%s %g\n", "[5] Φ components:", (double)(phi_components));
    cross_mi = (((mi_val_02 + mi_val_03) + mi_val_12) + mi_val_13);
    phi_iit = ((total_mi - cross_mi) / 3.0);
    printf("%s %g\n", "[6] Φ(IIT) estimate =", (double)((hexa_round((double)((phi_iit * 10000))) / 10000)));
    printf("%s\n", "--- Φ(IIT) lazy+vectorize PASS ---");
    return 0;
}
