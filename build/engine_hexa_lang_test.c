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

double safe_float(const char* s);
long safe_int(const char* s);
double abs_f(double x);
double max_f(double a, double b);
double min_f(double a, double b);
double clamp01(double x);
const char* timestamp();
const char* read_file_safe(const char* path);
long file_exists_check(const char* path);
double json_float(const char* content, const char* field);
long json_int(const char* content, const char* field);
long append_bus(const char* phase, const char* status, const char* detail);
hexa_arr count_tests();
hexa_arr read_emergence_metrics();
long read_pattern_count();
long* collect_metrics();
double calc_health(long *m);
const char* classify(long *m, double health);
hexa_arr load_prev_state();
long save_state(long *m, double health);
const char* evolve_report(long *m);
long tick_priority_fix(long *m);

static const char* version = "1.0.0";
static const char* project = "hexa_lang";
static const char* personality = "Mathematician";
static const char* goal = "테스트 800+, 이머전스 100% 수렴";
static const char* home;
static const char* dev;
static const char* project_dir;
static const char* metrics_path;
static const char* patterns_path;
static const char* bus_file;
static const char* state_path;
static long test_goal = 800;
static double emergence_target = 1.0;

double safe_float(const char* s) {
    double v = 0.0;
    v = hexa_to_float(s);
    return v;
}

long safe_int(const char* s) {
    long v = 0;
    v = hexa_to_int_str(hexa_trim(s));
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

double clamp01(double x) {
    if ((x < 0.0)) {
        return 0.0;
    }
    if ((x > 1.0)) {
        return 1.0;
    }
    return x;
}

const char* timestamp() {
    const char* r = hexa_exec("date +%Y-%m-%dT%H:%M:%S");
    return hexa_trim(r);
}

const char* read_file_safe(const char* path) {
    return hexa_read_file(path);
}

long file_exists_check(const char* path) {
    const char* r = "";
    r = hexa_exec(hexa_concat(hexa_concat("test -f ", path), " && echo 1"));
    return (strcmp(hexa_trim(r), "1") == 0);
}

double json_float(const char* content, const char* field) {
    const char* marker = hexa_concat(hexa_concat("\"", field), "\"");
    hexa_arr parts = hexa_split(content, marker);
    if ((parts.n < 2)) {
        return 0.0;
    }
    const char* after = ((const char*)parts.d[1]);
    hexa_arr colon_parts = hexa_split(after, ":");
    if ((colon_parts.n < 2)) {
        return 0.0;
    }
    const char* val_str = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)colon_parts.d[1]), ",")).d[0]), "}")).d[0]), "]")).d[0]));
    return safe_float(val_str);
}

long json_int(const char* content, const char* field) {
    const char* marker = hexa_concat(hexa_concat("\"", field), "\"");
    hexa_arr parts = hexa_split(content, marker);
    if ((parts.n < 2)) {
        return 0;
    }
    const char* after = ((const char*)parts.d[1]);
    hexa_arr colon_parts = hexa_split(after, ":");
    if ((colon_parts.n < 2)) {
        return 0;
    }
    const char* val_str = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)colon_parts.d[1]), ",")).d[0]), "}")).d[0]), "]")).d[0]));
    return safe_int(val_str);
}

long append_bus(const char* phase, const char* status, const char* detail) {
    const char* ts = timestamp();
    const char* line = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"source\":\"", project), "\",\"timestamp\":\""), ts), "\",\"phase\":\""), phase), "\",\"status\":\""), status), "\",\"detail\":\""), detail), "\"}");
    return hexa_append_file(bus_file, hexa_concat(line, "\n"));
}

hexa_arr count_tests() {
    const char* result = "";
    hexa_exec(hexa_concat(hexa_concat("bash -c cd ", project_dir), " && cargo test 2>&1 | tail -5"));
    long total = 0;
    long passed = 0;
    long failed = 0;
    if (hexa_contains(result, "passed")) {
        hexa_arr parts = hexa_split(result, "passed");
        if ((parts.n >= 1)) {
            const char* before = ((const char*)parts.d[0]);
            hexa_arr tokens = hexa_split(before, " ");
            long i = (tokens.n - 1);
            while ((i >= 0)) {
                long n = safe_int(((const char*)tokens.d[i]));
                if ((n > 0)) {
                    passed = n;
                    break;
                }
                i = (i - 1);
            }
        }
    }
    if (hexa_contains(result, "failed")) {
        hexa_arr parts = hexa_split(result, "failed");
        if ((parts.n >= 1)) {
            const char* before = ((const char*)parts.d[0]);
            hexa_arr tokens = hexa_split(before, " ");
            long i = (tokens.n - 1);
            while ((i >= 0)) {
                long n = safe_int(((const char*)tokens.d[i]));
                if ((n >= 0)) {
                    failed = n;
                    break;
                }
                i = (i - 1);
            }
        }
    }
    total = (passed + failed);
    return hexa_arr_lit((long[]){(long)(total), (long)(passed), (long)(failed)}, 3);
}

hexa_arr read_emergence_metrics() {
    const char* content = read_file_safe(metrics_path);
    if ((strcmp(content, "") == 0)) {
        return hexa_arr_lit((long[]){(long)(0.0), (long)(0)}, 2);
    }
    double density = json_float(content, "emergence_density");
    long round = json_int(content, "convergence_round");
    return hexa_arr_lit((long[]){(long)(density), (long)(round)}, 2);
}

long read_pattern_count() {
    const char* content = read_file_safe(patterns_path);
    if ((strcmp(content, "") == 0)) {
        return 0;
    }
    hexa_arr parts = hexa_split(content, "\"pattern\"");
    long count = (parts.n - 1);
    if ((count < 0)) {
        return 0;
    }
    return count;
}

long* collect_metrics() {
    hexa_arr tests = count_tests();
    hexa_arr emergence = read_emergence_metrics();
    long patterns = read_pattern_count();
    long build_ok = ((tests.d[0] > 0) && (tests.d[2] == 0));
    return hexa_struct_alloc((long[]){(long)(tests.d[0]), (long)(tests.d[1]), (long)(tests.d[2]), (long)(emergence.d[0]), (long)(emergence.d[1]), (long)(patterns), (long)(build_ok)}, 7);
}

double calc_health(long *m) {
    double test_ratio = clamp01((((double)(m[0])) / ((double)(test_goal))));
    double test_score = (test_ratio * 0.4);
    double fail_score = (((m[2] == 0)) ? (0.2) : ((0.2 * max_f(0.0, (1.0 - (((double)(m[2])) / 10.0))))));
    double emergence_score = (clamp01((m[3] / emergence_target)) * 0.25);
    double pattern_score = (clamp01((((double)(m[5])) / 100.0)) * 0.15);
    return clamp01((((test_score + fail_score) + emergence_score) + pattern_score));
}

const char* classify(long *m, double health) {
    if ((m[2] > 0)) {
        return "STAGNANT";
    }
    if (((m[3] >= 0.95) && (m[0] >= test_goal))) {
        return "BREAKTHROUGH";
    }
    if (((m[0] >= (long)((((double)(test_goal)) * 0.9))) || (m[3] >= 0.9))) {
        return "GOAL_NEAR";
    }
    if ((health >= 0.3)) {
        return "HEALTHY";
    }
    return "STAGNANT";
}

hexa_arr load_prev_state() {
    const char* content = read_file_safe(state_path);
    if ((strcmp(content, "") == 0)) {
        return hexa_arr_lit((long[]){(long)(0), (long)(0), (long)(0.0)}, 3);
    }
    long prev_tests = json_int(content, "test_count");
    long prev_patterns = json_int(content, "pattern_count");
    double prev_density = json_float(content, "emergence_density");
    return hexa_arr_lit((long[]){(long)(prev_tests), (long)(prev_patterns), (long)(prev_density)}, 3);
}

long save_state(long *m, double health) {
    const char* content = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\n", "  \"test_count\": "), hexa_int_to_str((long)(m[0]))), ",\n"), "  \"test_pass\": "), hexa_int_to_str((long)(m[1]))), ",\n"), "  \"test_fail\": "), hexa_int_to_str((long)(m[2]))), ",\n"), "  \"emergence_density\": "), hexa_int_to_str((long)(m[3]))), ",\n"), "  \"convergence_round\": "), hexa_int_to_str((long)(m[4]))), ",\n"), "  \"pattern_count\": "), hexa_int_to_str((long)(m[5]))), ",\n"), "  \"health\": "), hexa_float_to_str((double)(health))), ",\n"), "  \"timestamp\": \""), timestamp()), "\"\n"), "}\n");
    result = hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat("bash -c cat > ", state_path), " << 'JSONEOF'\n"), content), "JSONEOF"));
}

const char* evolve_report(long *m) {
    hexa_arr prev = load_prev_state();
    long test_delta = (m[0] - prev.d[0]);
    long pattern_delta = (m[5] - prev.d[1]);
    long density_delta = (m[3] - prev.d[2]);
    const char* report = "evolve:";
    if ((test_delta > 0)) {
        report = hexa_concat(hexa_concat(report, " tests+"), hexa_int_to_str((long)(test_delta)));
    } else {
        if ((test_delta == 0)) {
            report = hexa_concat(report, " tests=stale");
        } else {
            report = hexa_concat(hexa_concat(hexa_concat(report, " tests"), hexa_int_to_str((long)(test_delta))), "(regression!)");
        }
    }
    if ((pattern_delta > 0)) {
        report = hexa_concat(hexa_concat(report, " patterns+"), hexa_int_to_str((long)(pattern_delta)));
    }
    if ((abs_f(density_delta) > 0.001)) {
        if ((density_delta > 0.0)) {
            report = hexa_concat(hexa_concat(report, " density+"), hexa_int_to_str((long)(density_delta)));
        } else {
            report = hexa_concat(hexa_concat(report, " density"), hexa_int_to_str((long)(density_delta)));
        }
    }
    return report;
}

long tick_priority_fix(long *m) {
    printf("%s\n", hexa_concat(hexa_concat("[TICK] Priority fix cycle — ", hexa_int_to_str((long)(m[2]))), " failures detected"));
    append_bus("tick", "fix_cycle", hexa_concat(hexa_int_to_str((long)(m[2])), " test failures"));
    long build_out = 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    home = hexa_exec("printenv HOME");
    dev = hexa_concat(home, "/Dev");
    project_dir = hexa_concat(dev, "/hexa-lang");
    metrics_path = hexa_concat(project_dir, "/config/eso_metrics.json");
    patterns_path = hexa_concat(project_dir, "/config/emergence_patterns.json");
    bus_file = hexa_concat(dev, "/nexus/shared/growth_bus.jsonl");
    state_path = hexa_concat(dev, "/nexus/shared/engine_hexa_lang_state.json");
    return 0;
}
