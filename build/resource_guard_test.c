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

long* default_hard();
long* default_soft();
long apply_hard_limits(long *h);
long get_rss_mb();
long* check_and_adapt(long *s, long *state);
const char* throttle_sleep(long ms);
const char* level_name(long l);
const char* guard_status(long *state);
long* init_guard();

static long Normal = 0;
static long Warn = 1;
static long Critical = 2;

static long max_rss_mb = 512;
static long max_data_mb = 1024;
static long nice_level = 10;
static long warn_rss_mb = 384;
static long critical_rss_mb = 480;
static long throttle_sleep_ms = 100;
static double batch_scale_warn = 0.5;
static double batch_scale_critical = 0.25;
static long* test_state;
static long* soft;
static long* after;

long* default_hard() {
    return hexa_struct_alloc((long[]){(long)(max_rss_mb), (long)(max_data_mb), (long)(nice_level)}, 3);
}

long* default_soft() {
    return hexa_struct_alloc((long[]){(long)(warn_rss_mb), (long)(critical_rss_mb), (long)(throttle_sleep_ms), (long)(batch_scale_warn), (long)(batch_scale_critical)}, 5);
}

long apply_hard_limits(long *h) {
    const char* cmd_rss = hexa_concat("ulimit -m ", hexa_int_to_str((long)((h[0] * 1024))));
    const char* r1 = hexa_exec(cmd_rss);
    const char* cmd_data = hexa_concat("ulimit -d ", hexa_int_to_str((long)((h[1] * 1024))));
    const char* r2 = hexa_exec(cmd_data);
    const char* pid = hexa_exec("echo $$");
    const char* cmd_nice = hexa_concat(hexa_concat(hexa_concat("renice -n ", hexa_int_to_str((long)(h[2]))), " -p "), pid);
    const char* r3 = hexa_exec(cmd_nice);
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[resource_guard] hard limits applied: RSS=", hexa_int_to_str((long)(h[0]))), "MB, DATA="), hexa_int_to_str((long)(h[1]))), "MB, nice="), hexa_int_to_str((long)(h[2]))));
    return 1;
}

long get_rss_mb() {
    const char* raw = hexa_exec("ps -o rss= -p $PPID 2>/dev/null || echo 0");
    long kb = hexa_to_int_str(raw);
    return (kb / 1024);
}

long* check_and_adapt(long *s, long *state) {
    long rss = get_rss_mb();
    long new_level = 0;
    double new_scale = 1.0;
    long new_sleep = 0;
    long new_throttled = state[2];
    if ((rss >= s[1])) {
        new_level = 2;
        new_scale = s[4];
        new_sleep = (s[2] * 3);
        new_throttled = (new_throttled + 1);
    } else {
        if ((rss >= s[0])) {
            new_level = 1;
            new_scale = s[3];
            new_sleep = s[2];
            new_throttled = (new_throttled + 1);
        } else {
            new_level = 0;
            new_scale = 1.0;
            new_sleep = 0;
        }
    }
    return hexa_struct_alloc((long[]){(long)(new_level), (long)((state[1] + 1)), (long)(new_throttled), (long)(new_scale), (long)(new_sleep)}, 5);
}

const char* throttle_sleep(long ms) {
    if ((ms > 0)) {
        return hexa_exec(hexa_concat(hexa_concat(hexa_concat("sleep ", hexa_int_to_str((long)((ms / 1000)))), "."), hexa_int_to_str((long)((ms % 1000)))));
    }
}

const char* level_name(long l) {
    if ((l == 0)) {
        return "Normal";
    } else if ((l == 1)) {
        return "Warn";
    } else if ((l == 2)) {
        return "Critical";
    } else {
        return "Unknown";
    }


}

const char* guard_status(long *state) {
    long rss = get_rss_mb();
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("rss=", hexa_int_to_str((long)(rss))), "MB level="), level_name(state[0])), " checks="), hexa_int_to_str((long)(state[1]))), " throttled="), hexa_int_to_str((long)(state[2]))), " batch_scale="), hexa_int_to_str((long)(state[3])));
}

long* init_guard() {
    apply_hard_limits(default_hard());
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("[resource_guard] soft limits: warn=", hexa_int_to_str((long)(warn_rss_mb))), "MB critical="), hexa_int_to_str((long)(critical_rss_mb))), "MB"));
    return hexa_struct_alloc((long[]){(long)(0), (long)(0), (long)(0), (long)(1.0), (long)(0)}, 5);
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    if (!((max_rss_mb > 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((warn_rss_mb < max_rss_mb))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((critical_rss_mb < max_rss_mb))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((critical_rss_mb > warn_rss_mb))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((batch_scale_warn > 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((batch_scale_warn <= 1.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((batch_scale_critical > 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((batch_scale_critical < batch_scale_warn))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    test_state = hexa_struct_alloc((long[]){(long)(0), (long)(0), (long)(0), (long)(1.0), (long)(0)}, 5);
    soft = default_soft();
    after = check_and_adapt(soft, test_state);
    if (!((/* unknown field checks */ 0 == 1))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field batch_scale */ 0 > 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field batch_scale */ 0 <= 1.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "=== resource_guard verified ===");
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("hard: RSS=", hexa_int_to_str((long)(max_rss_mb))), "MB DATA="), hexa_int_to_str((long)(max_data_mb))), "MB nice="), hexa_int_to_str((long)(nice_level))));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("soft: warn=", hexa_int_to_str((long)(warn_rss_mb))), "MB critical="), hexa_int_to_str((long)(critical_rss_mb))), "MB"));
    return 0;
}
