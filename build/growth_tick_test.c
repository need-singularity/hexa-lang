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

long preflight_check();
long hexa_user_main();


long preflight_check() {
    const char* home = hexa_exec("printenv HOME");
    const char* shared_dir = hexa_concat(home, "/Dev/nexus/shared");
    const char* log_path = hexa_concat(shared_dir, "/growth_tick_preflight.log");
    const char* ts = hexa_trim(hexa_exec("date +%Y-%m-%dT%H:%M:%S"));
    const char* halt_path = hexa_concat(shared_dir, "/.halt");
    const char* halt_check = hexa_trim(hexa_exec(hexa_concat(hexa_concat("bash -c test -f ", halt_path), " && echo YES || echo NO")));
    if ((strcmp(halt_check, "YES") == 0)) {
        const char* msg = hexa_concat(ts, " [SKIP] .halt file present — tick aborted\n");
        hexa_append_file(log_path, msg);
        return 0;
    }
    const char* vm_raw = hexa_exec("vm_stat");
    long free_mb = 9999;
    if (hexa_contains(vm_raw, "Pages free")) {
        hexa_arr parts = hexa_split(vm_raw, "Pages free:");
        if ((parts.n >= 2)) {
            const char* val_str = hexa_replace(hexa_trim(((const char*)(hexa_split(((const char*)parts.d[1]), "\n")).d[0])), ".", "");
            long pages = hexa_to_int_str(hexa_trim(val_str));
            free_mb = ((pages * 4) / 1024);
        }
    }
    if ((free_mb < 500)) {
        const char* msg = hexa_concat(hexa_concat(hexa_concat(ts, " [SKIP] low memory: "), hexa_int_to_str((long)(free_mb))), "MB free (threshold: 500MB)\n");
        hexa_append_file(log_path, msg);
        return 0;
    }
    const char* load_raw = hexa_trim(hexa_exec("sysctl -n vm.loadavg"));
    double loadavg_1m = 0.0;
    const char* clean_load = hexa_trim(hexa_replace(hexa_replace(load_raw, "{", ""), "}", ""));
    hexa_arr load_parts = hexa_split(clean_load, " ");
    long __fi_lp_1 = 0;
    while ((__fi_lp_1 < load_parts.n)) {
        const char* lp = ((const char*)load_parts.d[__fi_lp_1]);
        if ((strcmp(hexa_trim(lp), "") != 0)) {
            loadavg_1m = hexa_to_float(hexa_trim(lp));
            break;
        }
        __fi_lp_1 = (__fi_lp_1 + 1);
    }
    if ((loadavg_1m > 12.0)) {
        const char* msg = hexa_concat(hexa_concat(hexa_concat(ts, " [SKIP] high load: "), hexa_float_to_str((double)(loadavg_1m))), " (threshold: 12.0)\n");
        hexa_append_file(log_path, msg);
        return 0;
    }
    const char* lock_path = hexa_concat(shared_dir, "/growth_tick.lock");
    const char* lock_exists = hexa_trim(hexa_exec(hexa_concat(hexa_concat("bash -c test -f ", lock_path), " && echo YES || echo NO")));
    if ((strcmp(lock_exists, "YES") == 0)) {
        const char* stale = hexa_trim(hexa_exec(hexa_concat(hexa_concat("bash -c find ", lock_path), " -mmin +30 2>/dev/null | head -1")));
        if ((strcmp(stale, "") != 0)) {
            hexa_exec("bash");
            const char* msg = hexa_concat(hexa_concat(hexa_concat(ts, " [CLEAN] stale lock removed (>30min): "), lock_path), "\n");
            hexa_append_file(log_path, msg);
        } else {
            const char* msg = hexa_concat(hexa_concat(hexa_concat(ts, " [SKIP] active lock present: "), lock_path), "\n");
            hexa_append_file(log_path, msg);
            return 0;
        }
    }
    const char* msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(ts, " [PASS] preflight ok (free="), hexa_int_to_str((long)(free_mb))), "MB, load="), hexa_float_to_str((double)(loadavg_1m))), ")\n");
    hexa_append_file(log_path, msg);
    return 1;
}

long hexa_user_main() {
    if ((!preflight_check())) {
        printf("%s\n", "[growth_tick] preflight failed — skipped");
        return 0;
    }
    hexa_arr a = hexa_args();
    const char* source = (((a.n >= 3)) ? (((const char*)a.d[2])) : ("unknown"));
    const char* growth_json = (((a.n >= 4)) ? (((const char*)a.d[3])) : (hexa_concat(hexa_exec("printenv HOME"), "/Dev/anima/anima/config/growth_state.json")));
    const char* content = hexa_read_file(growth_json);
    if ((strcmp(content, "") == 0)) {
        return 0;
    }
    long count = 0;
    if (hexa_contains(content, "interaction_count")) {
        hexa_arr parts = hexa_split(content, "interaction_count");
        if ((parts.n >= 2)) {
            const char* after = hexa_trim(((const char*)(hexa_split(((const char*)parts.d[1]), ":")).d[1]));
            const char* clean = after;
            if (hexa_contains(clean, ",")) {
                clean = ((const char*)(hexa_split(clean, ",")).d[0]);
            }
            if (hexa_contains(clean, "}")) {
                clean = ((const char*)(hexa_split(clean, "}")).d[0]);
            }
            count = hexa_to_int_str(hexa_trim(clean));
        }
    }
    count = (count + 1);
    long stage_index = 0;
    if (hexa_contains(content, "stage_index")) {
        hexa_arr parts = hexa_split(content, "stage_index");
        if ((parts.n >= 2)) {
            const char* after = hexa_trim(((const char*)(hexa_split(((const char*)parts.d[1]), ":")).d[1]));
            const char* clean = after;
            if (hexa_contains(clean, ",")) {
                clean = ((const char*)(hexa_split(clean, ",")).d[0]);
            }
            if (hexa_contains(clean, "}")) {
                clean = ((const char*)(hexa_split(clean, "}")).d[0]);
            }
            stage_index = hexa_to_int_str(hexa_trim(clean));
        }
    }
    if (((count >= 10000) && (stage_index < 4))) {
        stage_index = 4;
    } else {
        if (((count >= 2000) && (stage_index < 3))) {
            stage_index = 3;
        } else {
            if (((count >= 500) && (stage_index < 2))) {
                stage_index = 2;
            } else {
                if (((count >= 100) && (stage_index < 1))) {
                    stage_index = 1;
                }
            }
        }
    }
    const char* updated = content;
    if (hexa_contains(updated, "interaction_count")) {
        const char* before = ((const char*)(hexa_split(updated, "\"interaction_count\"")).d[0]);
        const char* after_full = ((const char*)(hexa_split(updated, "\"interaction_count\"")).d[1]);
        const char* colon_and_rest = ((const char*)(hexa_split(after_full, ":")).d[0]);
        hexa_arr value_and_rest = hexa_split(after_full, ":");
        if ((value_and_rest.n >= 2)) {
            const char* rest_str = ((const char*)value_and_rest.d[1]);
            const char* end_char = ",";
            const char* after_value = "";
            if (hexa_contains(rest_str, ",")) {
                after_value = ((const char*)(hexa_split(rest_str, ",")).d[1]);
                hexa_arr rest_parts = hexa_split(rest_str, ",");
                long ri = 2;
                while ((ri < rest_parts.n)) {
                    after_value = hexa_concat(hexa_concat(after_value, ","), ((const char*)rest_parts.d[ri]));
                    ri = (ri + 1);
                }
            }
            updated = hexa_concat(hexa_concat(hexa_concat(hexa_concat(before, "\"interaction_count\": "), hexa_int_to_str((long)(count))), ","), after_value);
        }
    }
    if (hexa_contains(updated, "stage_index")) {
        const char* before = ((const char*)(hexa_split(updated, "\"stage_index\"")).d[0]);
        const char* after_full = ((const char*)(hexa_split(updated, "\"stage_index\"")).d[1]);
        hexa_arr value_and_rest = hexa_split(after_full, ":");
        if ((value_and_rest.n >= 2)) {
            const char* rest_str = ((const char*)value_and_rest.d[1]);
            const char* after_value = "";
            if (hexa_contains(rest_str, ",")) {
                hexa_arr rest_parts = hexa_split(rest_str, ",");
                long ri = 1;
                while ((ri < rest_parts.n)) {
                    if ((ri > 1)) {
                        after_value = hexa_concat(after_value, ",");
                    }
                    after_value = hexa_concat(after_value, ((const char*)rest_parts.d[ri]));
                    ri = (ri + 1);
                }
            }
            updated = hexa_concat(hexa_concat(hexa_concat(hexa_concat(before, "\"stage_index\": "), hexa_int_to_str((long)(stage_index))), ","), after_value);
        }
    }
    return hexa_write_file(growth_json, updated);
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    hexa_user_main();
    return 0;
}
