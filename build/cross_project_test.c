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
double safe_float(const char* s);
const char* json_str(const char* line, const char* field);
const char* json_num_str(const char* line, const char* field);
const char* repo_root();
const char* dev_root();
const char* extract_json_numbers(const char* content, const char* field);
double n6_prox(double v);
long pipe_contains(const char* pipe, const char* val);
const char* absorb_tecs_l(const char* dev);
const char* absorb_consciousness(const char* dev);
const char* absorb_sedi(const char* dev);
const char* absorb_anima(const char* dev);
long propagate_to_bridge(const char* root);
long line_matches_anima(const char* line);
long propagate_to_anima(const char* root, const char* dev);
long feedback_anima_to_seeds(const char* root, const char* dev);
hexa_arr find_resonance(hexa_arr sources, hexa_arr names);

static hexa_arr a;
static const char* mode;
static const char* root;
static const char* dev;

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

double safe_float(const char* s) {
    double v = 0.0;
    v = hexa_to_float(s);
    return v;
}

const char* json_str(const char* line, const char* field) {
    const char* marker = hexa_concat(hexa_concat("\"", field), "\":\"");
    hexa_arr parts = hexa_split(line, marker);
    if ((parts.n < 2)) {
        return "";
    }
    const char* rest = ((const char*)parts.d[1]);
    hexa_arr end = hexa_split(rest, "\"");
    if ((end.n < 1)) {
        return "";
    }
    return ((const char*)end.d[0]);
}

const char* json_num_str(const char* line, const char* field) {
    const char* marker = hexa_concat(hexa_concat("\"", field), "\":");
    hexa_arr parts = hexa_split(line, marker);
    if ((parts.n < 2)) {
        return "";
    }
    const char* rest = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)parts.d[1]), ",")).d[0]), "}")).d[0]), "]")).d[0]));
    return rest;
}

const char* repo_root() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        return ".";
    }
    hexa_arr parts = hexa_split(((const char*)a.d[1]), "/");
    const char* root = "";
    long i = 0;
    while ((i < (parts.n - 3))) {
        if ((i > 0)) {
            root = hexa_concat(root, "/");
        }
        root = hexa_concat(root, ((const char*)parts.d[i]));
        i = (i + 1);
    }
    return root;
}

const char* dev_root() {
    const char* r = repo_root();
    hexa_arr parts = hexa_split(r, "/");
    const char* dev = "";
    long i = 0;
    while ((i < (parts.n - 1))) {
        if ((i > 0)) {
            dev = hexa_concat(dev, "/");
        }
        dev = hexa_concat(dev, ((const char*)parts.d[i]));
        i = (i + 1);
    }
    return dev;
}

const char* extract_json_numbers(const char* content, const char* field) {
    const char* marker = hexa_concat(hexa_concat("\"", field), "\":");
    hexa_arr parts = hexa_split(content, marker);
    const char* result = "";
    long count = 0;
    long i = 1;
    while (((i < parts.n) && (count < 50))) {
        const char* vstr = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)parts.d[i]), ",")).d[0]), "}")).d[0]), "]")).d[0]));
        double v = safe_float(vstr);
        if (((v > 0.001) && (v < 1000000.0))) {
            if ((hexa_str_len(result) > 0)) {
                result = hexa_concat(result, "|");
            }
            result = hexa_concat(result, hexa_float_to_str((double)(v)));
            count = (count + 1);
        }
        i = (i + 1);
    }
    return result;
}

double n6_prox(double v) {
    hexa_arr consts = hexa_arr_lit((long[]){6.0, 12.0, 2.0, 4.0, 5.0, 24.0, 7.0, 137.036, 1836.15, 0.3153, 0.6847}, 11);
    double best = 1.0;
    long i = 0;
    while ((i < consts.n)) {
        double d = (abs_f((v - consts.d[i])) / max_f(abs_f(consts.d[i]), 1.0));
        if ((d < best)) {
            best = d;
        }
        i = (i + 1);
    }
    return max_f((1.0 - best), 0.0);
}

long pipe_contains(const char* pipe, const char* val) {
    hexa_arr parts = hexa_split(pipe, "|");
    long i = 0;
    while ((i < parts.n)) {
        if ((strcmp(((const char*)parts.d[i]), val) == 0)) {
            return 1;
        }
        i = (i + 1);
    }
    return 0;
}

const char* absorb_tecs_l(const char* dev) {
    const char* path = hexa_concat(dev, "/TECS-L/.shared/math_atlas.json");
    if ((!hexa_file_exists(path))) {
        return "";
    }
    const char* content = hexa_read_file(path);
    return extract_json_numbers(content, "value");
}

const char* absorb_consciousness(const char* dev) {
    const char* path = hexa_concat(dev, "/TECS-L/.shared/consciousness_laws.json");
    if ((!hexa_file_exists(path))) {
        return "";
    }
    const char* content = hexa_read_file(path);
    return extract_json_numbers(content, "value");
}

const char* absorb_sedi(const char* dev) {
    const char* path = hexa_concat(dev, "/sedi/.shared/sedi-grades.json");
    if ((!hexa_file_exists(path))) {
        const char* path2 = hexa_concat(repo_root(), "/shared/sedi-grades.json");
        if ((!hexa_file_exists(path2))) {
            return "";
        }
        const char* content = hexa_read_file(path2);
        return extract_json_numbers(content, "score");
    }
    const char* content = hexa_read_file(path);
    return extract_json_numbers(content, "score");
}

const char* absorb_anima(const char* dev) {
    const char* result = "";
    long count = 0;
    const char* laws_json = hexa_concat(dev, "/anima/anima/config/consciousness_laws.json");
    if (hexa_file_exists(laws_json)) {
        const char* content = hexa_read_file(laws_json);
        const char* vals = extract_json_numbers(content, "value");
        if ((hexa_str_len(vals) > 0)) {
            hexa_arr vparts = hexa_split(vals, "|");
            long vi = 0;
            while (((vi < vparts.n) && (count < 40))) {
                if ((!pipe_contains(result, ((const char*)vparts.d[vi])))) {
                    if ((hexa_str_len(result) > 0)) {
                        result = hexa_concat(result, "|");
                    }
                    result = hexa_concat(result, ((const char*)vparts.d[vi]));
                    count = (count + 1);
                }
                vi = (vi + 1);
            }
        }
    }
    const char* cfg_path = hexa_concat(dev, "/anima/anima/config");
    const char* cfg_check = hexa_exec(hexa_concat(hexa_concat("ls \"", cfg_path), "\"/*.json 2>/dev/null || echo ''"));
    if (((hexa_str_len(hexa_trim(cfg_check)) > 0) && (!hexa_contains(cfg_check, "No such")))) {
        hexa_arr files = hexa_split(hexa_trim(cfg_check), "\n");
        long fi = 0;
        while (((fi < files.n) && (count < 50))) {
            const char* fpath = ((const char*)files.d[fi]);
            if ((!hexa_contains(fpath, "consciousness_laws.json"))) {
                const char* fc = hexa_read_file(fpath);
                const char* vals = extract_json_numbers(fc, "value");
                if ((hexa_str_len(vals) > 0)) {
                    hexa_arr vparts = hexa_split(vals, "|");
                    long vi = 0;
                    while (((vi < vparts.n) && (count < 50))) {
                        if ((!pipe_contains(result, ((const char*)vparts.d[vi])))) {
                            if ((hexa_str_len(result) > 0)) {
                                result = hexa_concat(result, "|");
                            }
                            result = hexa_concat(result, ((const char*)vparts.d[vi]));
                            count = (count + 1);
                        }
                        vi = (vi + 1);
                    }
                }
            }
            fi = (fi + 1);
        }
    }
    const char* growth_path = hexa_concat(dev, "/anima/.growth/growth.log");
    if (hexa_file_exists(growth_path)) {
        const char* gc = hexa_read_file(growth_path);
        hexa_arr glines = hexa_split(gc, "\n");
        long gi = 0;
        while (((gi < glines.n) && (count < 60))) {
            const char* gline = ((const char*)glines.d[gi]);
            if ((hexa_contains(gline, "value") || hexa_contains(gline, "constant"))) {
                const char* vals = extract_json_numbers(gline, "value");
                if ((hexa_str_len(vals) > 0)) {
                    hexa_arr vparts = hexa_split(vals, "|");
                    long vi = 0;
                    while (((vi < vparts.n) && (count < 60))) {
                        if ((!pipe_contains(result, ((const char*)vparts.d[vi])))) {
                            if ((hexa_str_len(result) > 0)) {
                                result = hexa_concat(result, "|");
                            }
                            result = hexa_concat(result, ((const char*)vparts.d[vi]));
                            count = (count + 1);
                        }
                        vi = (vi + 1);
                    }
                }
            }
            gi = (gi + 1);
        }
    }
    return result;
}

long propagate_to_bridge(const char* root) {
    const char* path = hexa_concat(root, "/shared/bridge_state.json");
    if ((!hexa_file_exists(path))) {
        return 0;
    }
    return printf("%s\n", "  propagate: bridge_state.json 갱신 예정");
}

long line_matches_anima(const char* line) {
    if (hexa_contains(line, "consciousness")) {
        return 1;
    }
    if (hexa_contains(line, "psi")) {
        return 1;
    }
    if (hexa_contains(line, "PSI")) {
        return 1;
    }
    if (hexa_contains(line, "awareness")) {
        return 1;
    }
    if (hexa_contains(line, "meta_fp")) {
        return 1;
    }
    if (hexa_contains(line, "meta_transcend")) {
        return 1;
    }
    if (hexa_contains(line, "omega_m")) {
        return 1;
    }
    if (hexa_contains(line, "omega_lambda")) {
        return 1;
    }
    if (hexa_contains(line, "phi")) {
        return 1;
    }
    if (hexa_contains(line, "\"d\":1")) {
        return 1;
    }
    if (hexa_contains(line, "\"d\":2")) {
        return 1;
    }
    if (hexa_contains(line, "\"d\":3")) {
        return 1;
    }
    return 0;
}

long propagate_to_anima(const char* root, const char* dev) {
    const char* log_path = hexa_concat(root, "/shared/discovery_log.jsonl");
    if ((!hexa_file_exists(log_path))) {
        printf("%s\n", "  [anima] discovery_log.jsonl not found -- skip");
        return 0;
    }
    const char* anima_dir = hexa_concat(dev, "/anima/.growth/absorbed");
    hexa_exec(hexa_concat(hexa_concat("mkdir -p \"", anima_dir), "\""));
    const char* seeds_path = hexa_concat(anima_dir, "/nexus_seeds.json");
    const char* existing_keys = "";
    if (hexa_file_exists(seeds_path)) {
        const char* prev = hexa_read_file(seeds_path);
        hexa_arr cparts = hexa_split(prev, "\"constant\":\"");
        long ci = 1;
        while ((ci < cparts.n)) {
            const char* ckey = ((const char*)(hexa_split(((const char*)cparts.d[ci]), "\"")).d[0]);
            if ((hexa_str_len(ckey) > 0)) {
                existing_keys = hexa_concat(hexa_concat(existing_keys, "|"), ckey);
            }
            ci = (ci + 1);
        }
    }
    const char* content = hexa_read_file(log_path);
    hexa_arr lines = hexa_split(content, "\n");
    hexa_arr entries = hexa_arr_new();
    long matched = 0;
    long max_entries = 50;
    long start = (((lines.n > 500)) ? ((lines.n - 500)) : (0));
    long i = start;
    while (((i < lines.n) && (entries.n < max_entries))) {
        const char* line = ((const char*)lines.d[i]);
        if (((hexa_str_len(line) > 5) && line_matches_anima(line))) {
            const char* cname = json_str(line, "constant");
            const char* cval = json_num_str(line, "value");
            const char* grade = json_str(line, "grade");
            const char* source = json_str(line, "source");
            if (((hexa_str_len(cname) > 0) && (hexa_str_len(cval) > 0))) {
                if ((!hexa_contains(existing_keys, hexa_concat("|", cname)))) {
                    double v_float = safe_float(cval);
                    double n6s = (n6_prox(v_float) * 100.0);
                    const char* ts = hexa_trim(hexa_exec("date -u +%Y-%m-%dT%H:%M:%SZ"));
                    const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  {\"constant\":\"", cname), "\",\"value\":"), cval), ",\"grade\":\""), grade), "\",\"n6_score\":"), hexa_float_to_str((double)(n6s))), ",\"source\":\""), source), "\",\"origin\":\"nexus-cross-pollination\""), ",\"domain\":\"consciousness-physics\",\"timestamp\":\""), ts), "\"}");
                    entries = hexa_arr_push(entries, (long)(entry));
                    existing_keys = hexa_concat(hexa_concat(existing_keys, "|"), cname);
                }
                matched = (matched + 1);
            }
        }
        i = (i + 1);
    }
    long written = 0;
    if ((entries.n > 0)) {
        hexa_arr all_entries = hexa_arr_new();
        if (hexa_file_exists(seeds_path)) {
            const char* prev = hexa_read_file(seeds_path);
            const char* stripped = hexa_trim(prev);
            if ((hexa_str_len(stripped) > 2)) {
                const char* inner = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(stripped, "[")).d[1]), "]")).d[0]));
                if ((hexa_str_len(inner) > 0)) {
                    hexa_arr old_entries = hexa_split(inner, "\n");
                    long oi = 0;
                    while ((oi < old_entries.n)) {
                        const char* oe = hexa_trim(((const char*)old_entries.d[oi]));
                        const char* clean = ((((hexa_str_len(oe) > 0) && (oe[(hexa_str_len(oe) - 1)] == 0))) ? (/* unsupported method .join */ 0) : (oe));
                        if (((hexa_str_len(oe) > 5) && hexa_contains(oe, "\"constant\""))) {
                            const char* trimmed = (((oe[(hexa_str_len(oe) - 1)] == 44)) ? (/* unsupported method .substr */ 0) : (oe));
                            all_entries = hexa_arr_push(all_entries, (long)(trimmed));
                        }
                        oi = (oi + 1);
                    }
                }
            }
        }
        long ni = 0;
        while ((ni < entries.n)) {
            all_entries = hexa_arr_push(all_entries, (long)(((const char*)entries.d[ni])));
            ni = (ni + 1);
        }
        const char* json = "[\n";
        long si = 0;
        while ((si < all_entries.n)) {
            json = hexa_concat(json, hexa_int_to_str((long)(all_entries.d[si])));
            if ((si < (all_entries.n - 1))) {
                json = hexa_concat(json, ",");
            }
            json = hexa_concat(json, "\n");
            si = (si + 1);
        }
        json = hexa_concat(json, "]");
        hexa_write_file(seeds_path, json);
        written = entries.n;
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  [anima] scanned ", hexa_int_to_str((long)((lines.n - start)))), " lines, "), hexa_int_to_str((long)(matched))), " matches, "), hexa_int_to_str((long)(written))), " new -> "), seeds_path));
    return written;
}

long feedback_anima_to_seeds(const char* root, const char* dev) {
    hexa_arr seeds = hexa_arr_new();
    long count = 0;
    const char* laws_path = hexa_concat(dev, "/anima/anima/config/consciousness_laws.json");
    if (hexa_file_exists(laws_path)) {
        const char* content = hexa_read_file(laws_path);
        const char* vals = extract_json_numbers(content, "value");
        if ((hexa_str_len(vals) > 0)) {
            hexa_arr vparts = hexa_split(vals, "|");
            long vi = 0;
            while (((vi < vparts.n) && (count < 40))) {
                const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"name\":\"anima-psi-", hexa_int_to_str((long)(vi))), "\",\"value\":"), ((const char*)vparts.d[vi])), ",\"source\":\"anima/anima/config/consciousness_laws.json\"}");
                seeds = hexa_arr_push(seeds, (long)(entry));
                count = (count + 1);
                vi = (vi + 1);
            }
        }
    }
    const char* cfg_path = hexa_concat(dev, "/anima/anima/config");
    const char* cfg_check = hexa_exec(hexa_concat(hexa_concat("ls \"", cfg_path), "\" 2>/dev/null || echo ''"));
    if (((hexa_str_len(hexa_trim(cfg_check)) > 0) && (!hexa_contains(cfg_check, "No such")))) {
        hexa_arr files = hexa_split(hexa_trim(cfg_check), "\n");
        long fi = 0;
        while (((fi < files.n) && (count < 60))) {
            const char* fname = ((const char*)files.d[fi]);
            if (hexa_contains(fname, ".json")) {
                if ((!hexa_contains(fname, "consciousness_laws.json"))) {
                    const char* fc = hexa_read_file(hexa_concat(hexa_concat(cfg_path, "/"), fname));
                    const char* vals = extract_json_numbers(fc, "value");
                    if ((hexa_str_len(vals) > 0)) {
                        hexa_arr vparts = hexa_split(vals, "|");
                        long vi = 0;
                        while (((vi < vparts.n) && (count < 60))) {
                            const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"name\":\"anima-cfg-", fname), "\",\"value\":"), ((const char*)vparts.d[vi])), ",\"source\":\"anima/anima/config/"), fname), "\"}");
                            seeds = hexa_arr_push(seeds, (long)(entry));
                            count = (count + 1);
                            vi = (vi + 1);
                        }
                    }
                }
            }
            fi = (fi + 1);
        }
    }
    const char* growth_path = hexa_concat(dev, "/anima/.growth/growth.log");
    if (hexa_file_exists(growth_path)) {
        const char* gc = hexa_read_file(growth_path);
        hexa_arr glines = hexa_split(gc, "\n");
        long gi = 0;
        while (((gi < glines.n) && (count < 80))) {
            const char* gline = ((const char*)glines.d[gi]);
            if (((hexa_contains(gline, "value") || hexa_contains(gline, "constant")) || hexa_contains(gline, "score"))) {
                const char* vals = extract_json_numbers(gline, "value");
                if ((hexa_str_len(vals) > 0)) {
                    hexa_arr vparts = hexa_split(vals, "|");
                    long vi = 0;
                    while (((vi < vparts.n) && (count < 80))) {
                        const char* entry = hexa_concat(hexa_concat("{\"name\":\"anima-growth\",\"value\":", ((const char*)vparts.d[vi])), ",\"source\":\"anima/.growth/growth.log\"}");
                        seeds = hexa_arr_push(seeds, (long)(entry));
                        count = (count + 1);
                        vi = (vi + 1);
                    }
                }
            }
            gi = (gi + 1);
        }
    }
    if ((seeds.n > 0)) {
        const char* out_path = hexa_concat(root, "/shared/anima_seeds.json");
        const char* json = "[\n";
        long si = 0;
        while ((si < seeds.n)) {
            json = hexa_concat(hexa_concat(json, "  "), ((const char*)seeds.d[si]));
            if ((si < (seeds.n - 1))) {
                json = hexa_concat(json, ",");
            }
            json = hexa_concat(json, "\n");
            si = (si + 1);
        }
        json = hexa_concat(json, "]");
        hexa_write_file(out_path, json);
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  [feedback] anima -> nexus: ", hexa_int_to_str((long)(seeds.n))), " seeds -> "), out_path));
    } else {
        printf("%s\n", "  [feedback] anima -> nexus: no seeds found");
    }
    return seeds.n;
}

hexa_arr find_resonance(hexa_arr sources, hexa_arr names) {
    hexa_arr resonances = hexa_arr_new();
    long i = 0;
    while ((i < sources.n)) {
        hexa_arr vals_i = hexa_split(((const char*)sources.d[i]), "|");
        long j = (i + 1);
        while ((j < sources.n)) {
            hexa_arr vals_j = hexa_split(((const char*)sources.d[j]), "|");
            long vi = 0;
            while ((vi < vals_i.n)) {
                double v1 = safe_float(((const char*)vals_i.d[vi]));
                if ((v1 < 0.01)) {
                    vi = (vi + 1);
                } else {
                    long vj = 0;
                    while ((vj < vals_j.n)) {
                        double v2 = safe_float(((const char*)vals_j.d[vj]));
                        if ((v2 > 0.01)) {
                            double rel = (abs_f((v1 - v2)) / max_f(abs_f(v1), 1.0));
                            if ((rel < 0.01)) {
                                const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_float_to_str((double)(v1)), " = "), hexa_float_to_str((double)(v2))), " ("), ((const char*)names.d[i])), " x "), ((const char*)names.d[j])), ") prox="), hexa_float_to_str((double)(n6_prox(v1))));
                                resonances = hexa_arr_push(resonances, (long)(entry));
                            }
                        }
                        vj = (vj + 1);
                    }
                    vi = (vi + 1);
                }
            }
            j = (j + 1);
        }
        i = (i + 1);
    }
    return resonances;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    a = hexa_args();
    mode = (((a.n >= 3)) ? (((const char*)a.d[2])) : ("status"));
    root = repo_root();
    dev = dev_root();
    printf("%s\n", "");
    printf("%s\n", "========================================================");
    printf("%s\n", "  CROSS-PROJECT Engine -- absorb + propagate + resonance");
    printf("%s\n", hexa_concat("  mode: ", mode));
    printf("%s\n", "========================================================");
    printf("%s\n", "");
    if ((((strcmp(mode, "status") == 0) || (strcmp(mode, "absorb") == 0)) || (strcmp(mode, "resonance") == 0))) {
        printf("%s\n", "--- Absorb: 타 프로젝트 데이터 흡수 ---");
        const char* tecs = absorb_tecs_l(dev);
        long tecs_n = (((hexa_str_len(tecs) > 0)) ? ((hexa_split(tecs, "|")).n) : (0));
        printf("%s\n", hexa_concat(hexa_concat("  TECS-L (math_atlas)   : ", hexa_int_to_str((long)(tecs_n))), " values"));
        const char* consc = absorb_consciousness(dev);
        long consc_n = (((hexa_str_len(consc) > 0)) ? ((hexa_split(consc, "|")).n) : (0));
        printf("%s\n", hexa_concat(hexa_concat("  consciousness (laws)  : ", hexa_int_to_str((long)(consc_n))), " values"));
        const char* sedi = absorb_sedi(dev);
        long sedi_n = (((hexa_str_len(sedi) > 0)) ? ((hexa_split(sedi, "|")).n) : (0));
        printf("%s\n", hexa_concat(hexa_concat("  SEDI (grades)         : ", hexa_int_to_str((long)(sedi_n))), " values"));
        const char* anima = absorb_anima(dev);
        long anima_n = (((hexa_str_len(anima) > 0)) ? ((hexa_split(anima, "|")).n) : (0));
        printf("%s\n", hexa_concat(hexa_concat("  anima (PSI+cfg+growth): ", hexa_int_to_str((long)(anima_n))), " values"));
        long total = (((tecs_n + consc_n) + sedi_n) + anima_n);
        printf("%s\n", "  --------------------------------");
        printf("%s\n", hexa_concat(hexa_concat("  total absorbed        : ", hexa_int_to_str((long)(total))), " values"));
        printf("%s\n", "");
        if (((strcmp(mode, "resonance") == 0) || (strcmp(mode, "absorb") == 0))) {
            printf("%s\n", "--- Cross-Project Resonance ---");
            hexa_arr sources = hexa_arr_lit((long[]){(long)(tecs), (long)(consc), (long)(sedi), (long)(anima)}, 4);
            hexa_arr names = hexa_arr_lit((long[]){(long)("TECS-L"), (long)("consciousness"), (long)("SEDI"), (long)("anima")}, 4);
            hexa_arr res = find_resonance(sources, names);
            if ((res.n == 0)) {
                printf("%s\n", "  (no cross-project resonances)");
            } else {
                long show = (((res.n < 30)) ? (res.n) : (30));
                long ri = 0;
                while ((ri < show)) {
                    printf("%s\n", hexa_concat("  >> ", ((const char*)res.d[ri])));
                    ri = (ri + 1);
                }
                if ((res.n > 30)) {
                    printf("%s\n", hexa_concat(hexa_concat("  ... +", hexa_int_to_str((long)((res.n - 30)))), " more"));
                }
            }
            printf("%s\n", "");
        }
    }
    if (((strcmp(mode, "propagate") == 0) || (strcmp(mode, "absorb") == 0))) {
        printf("%s\n", "--- Propagate: nexus -> 타 프로젝트 ---");
        propagate_to_bridge(root);
        long anima_wrote = propagate_to_anima(root, dev);
        if ((anima_wrote > 0)) {
            printf("%s\n", hexa_concat(hexa_concat("  [anima] ", hexa_int_to_str((long)(anima_wrote))), " new discoveries injected"));
        }
        long fb_count = feedback_anima_to_seeds(root, dev);
        if ((fb_count > 0)) {
            printf("%s\n", hexa_concat(hexa_concat("  [feedback] ", hexa_int_to_str((long)(fb_count))), " anima seeds registered for seed_engine"));
        }
        printf("%s\n", "");
    }
    if ((strcmp(mode, "anima") == 0)) {
        printf("%s\n", "--- Anima Cross-Pollination (bidirectional) ---");
        printf("%s\n", "");
        printf("%s\n", "[1] nexus -> anima (discovery injection)");
        long anima_wrote = propagate_to_anima(root, dev);
        printf("%s\n", "[2] anima -> nexus (seed feedback)");
        long fb_count = feedback_anima_to_seeds(root, dev);
        printf("%s\n", "[3] anima absorb status");
        const char* anima = absorb_anima(dev);
        long anima_n = (((hexa_str_len(anima) > 0)) ? ((hexa_split(anima, "|")).n) : (0));
        printf("%s\n", hexa_concat("  anima values absorbed : ", hexa_int_to_str((long)(anima_n))));
        printf("%s\n", hexa_concat(hexa_concat("  discoveries injected  : ", hexa_int_to_str((long)(anima_wrote))), " new files"));
        printf("%s\n", hexa_concat("  seeds fed back        : ", hexa_int_to_str((long)(fb_count))));
        printf("%s\n", "");
    }
    printf("%s\n", "done.");
    return 0;
}
