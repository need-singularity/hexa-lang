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
const char* str_lower(const char* s);
long list_contains(const char* lst, const char* item);
const char* str_take(const char* s, long n);
hexa_arr load_n6_consts();
double n6_const(hexa_arr consts, const char* name);
long n6_values(hexa_arr consts);
long n6_names_list(hexa_arr consts);
hexa_arr n6_match(double val);
long keyword_hits(const char* text, hexa_arr keywords);
hexa_arr load_dse_domain_names();
hexa_arr extract_domains(const char* text);
hexa_arr extract_variables(const char* text);
double try_parse_value(const char* text);
hexa_arr classify(const char* raw, double n6_score, const char* source);
hexa_arr register(hexa_arr classified);
hexa_arr determine_sync(hexa_arr target_lists);
const char* determine_level(const char* targets_csv);
const char* format_notification(const char* level, const char* disc_id, const char* details);
hexa_arr run_pipeline(hexa_arr discoveries, hexa_arr scores, const char* source);
long show_help();

static hexa_arr _n6;
static double n6;
static double sigma;
static double phi;
static double tau_n;
static double j2;
static double sopfr;
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

const char* str_lower(const char* s) {
    const char* result = "";
    const char* out = hexa_exec("printf");
    result = out;
    return s;
}

long list_contains(const char* lst, const char* item) {
    long i = 0;
    while ((i < hexa_str_len(lst))) {
        if ((strcmp(hexa_int_to_str((long)(lst[i])), item) == 0)) {
            return 1;
        }
        i = (i + 1);
    }
    return 0;
}

const char* str_take(const char* s, long n) {
    hexa_arr chars = hexa_split(s, "");
    const char* result = "";
    long i = 0;
    double limit = min_f(((double)(n)), ((double)(chars.n)));
    while ((i < (long)(limit))) {
        result = hexa_concat(result, ((const char*)chars.d[i]));
        i = (i + 1);
    }
    return result;
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

long n6_names_list(hexa_arr consts) {
    return consts.d[0];
}

hexa_arr n6_match(double val) {
    long names = n6_names_list(_n6);
    long vals = n6_values(_n6);
    const char* best_name = "unknown";
    double best_quality = 0.0;
    long i = 0;
    while ((i < hexa_str_len(vals))) {
        double c = ((double)(vals[i]));
        double denom = max_f(abs_f(c), 1.0);
        double dist = (abs_f((val - c)) / denom);
        double q = (1.0 - dist);
        if ((q > best_quality)) {
            best_quality = q;
            best_name = hexa_int_to_str((long)(names[i]));
        }
        i = (i + 1);
    }
    return hexa_arr_lit((long[]){(long)(best_name), (long)(best_quality)}, 2);
}

long keyword_hits(const char* text, hexa_arr keywords) {
    long count = 0;
    long i = 0;
    while ((i < keywords.n)) {
        if (hexa_contains(text, ((const char*)keywords.d[i]))) {
            count = (count + 1);
        }
        i = (i + 1);
    }
    return count;
}

hexa_arr load_dse_domain_names() {
    const char* home = hexa_exec("printenv HOME");
    const char* path = hexa_concat(home, "/Dev/nexus/shared/dse_domains.jsonl");
    const char* raw = hexa_read_file(path);
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr result = hexa_arr_new();
    long __fi_line_2 = 0;
    while ((__fi_line_2 < lines.n)) {
        const char* line = ((const char*)lines.d[__fi_line_2]);
        const char* trimmed = hexa_trim(line);
        if ((strcmp(trimmed, "") == 0)) {
            continue;
        }
        if (hexa_contains(trimmed, "\"domain\":\"")) {
            hexa_arr p = hexa_split(trimmed, "\"domain\":\"");
            if ((p.n >= 2)) {
                const char* domain = ((const char*)(hexa_split(((const char*)p.d[1]), "\"")).d[0]);
                if ((strcmp(domain, "") != 0)) {
                    result = hexa_arr_concat(result, hexa_arr_lit((long[]){(long)(domain)}, 1));
                }
            }
        }
        __fi_line_2 = (__fi_line_2 + 1);
    }
    return result;
}

hexa_arr extract_domains(const char* text) {
    hexa_arr known = load_dse_domain_names();
    hexa_arr found = hexa_arr_new();
    long i = 0;
    while ((i < known.n)) {
        if (hexa_contains(text, ((const char*)known.d[i]))) {
            found = hexa_arr_concat(found, hexa_arr_lit((long[]){(long)(((const char*)known.d[i]))}, 1));
        }
        i = (i + 1);
    }
    return found;
}

hexa_arr extract_variables(const char* text) {
    hexa_arr vars = hexa_arr_lit((long[]){(long)("sigma"), (long)("phi"), (long)("tau"), (long)("n"), (long)("j2"), (long)("sopfr"), (long)("mu")}, 7);
    hexa_arr found = hexa_arr_new();
    long i = 0;
    while ((i < vars.n)) {
        if (hexa_contains(text, ((const char*)vars.d[i]))) {
            found = hexa_arr_concat(found, hexa_arr_lit((long[]){(long)(((const char*)vars.d[i]))}, 1));
        }
        i = (i + 1);
    }
    return found;
}

double try_parse_value(const char* text) {
    hexa_arr tokens = hexa_split(text, " ");
    long i = 0;
    while ((i < tokens.n)) {
        const char* tok = hexa_trim(((const char*)tokens.d[i]));
        double v = 0.0;
        long ok = 0;
        v = hexa_to_float(tok);
        ok = 1;
        if (ok) {
            return v;
        }
        i = (i + 1);
    }
    return (0.0 - 999999.0);
}

hexa_arr classify(const char* raw, double n6_score, const char* source) {
    hexa_arr formula_hints = hexa_arr_lit((long[]){(long)("="), (long)("+"), (long)("-"), (long)("*"), (long)("/"), (long)("^"), (long)("sigma"), (long)("phi"), (long)("tau")}, 9);
    hexa_arr pattern_hints = hexa_arr_lit((long[]){(long)("repeats"), (long)("periodic"), (long)("cycle"), (long)("ladder"), (long)("chain"), (long)("sequence")}, 6);
    hexa_arr law_hints = hexa_arr_lit((long[]){(long)("universality"), (long)("law"), (long)("conservation"), (long)("invariant"), (long)("bridge")}, 5);
    hexa_arr bt_hints = hexa_arr_lit((long[]){(long)("BT-"), (long)("breakthrough"), (long)("cross-domain"), (long)("resonance"), (long)("convergence")}, 5);
    hexa_arr lens_hints = hexa_arr_lit((long[]){(long)("lens"), (long)("perspective"), (long)("viewpoint"), (long)("gap"), (long)("blind spot")}, 5);
    double val = try_parse_value(raw);
    if ((val > (0.0 - 999998.0))) {
        hexa_arr match_result = n6_match(val);
        double quality = ((double)(match_result.d[1]));
        if ((quality >= 0.8)) {
            return hexa_arr_lit((long[]){(long)("constant"), (long)(quality), (long)(hexa_int_to_str((long)(match_result.d[0]))), (long)(hexa_float_to_str((double)(val)))}, 4);
        }
    }
    long bt_h = keyword_hits(raw, bt_hints);
    hexa_arr domains = extract_domains(raw);
    if ((bt_h >= 1)) {
        if ((domains.n >= 2)) {
            double conf = min_f((((double)(bt_h)) * 0.25), 1.0);
            return hexa_arr_lit((long[]){(long)("bt_candidate"), (long)(conf), (long)(str_take(raw, 120)), (long)(hexa_int_to_str((long)(domains.n)))}, 4);
        }
    }
    long lens_h = keyword_hits(raw, lens_hints);
    if ((lens_h >= 1)) {
        double conf = min_f((((double)(lens_h)) * 0.3), 1.0);
        return hexa_arr_lit((long[]){(long)("lens_candidate"), (long)(conf), (long)(str_take(raw, 60)), (long)(raw)}, 4);
    }
    long formula_h = keyword_hits(raw, formula_hints);
    if ((formula_h >= 2)) {
        hexa_arr vars = extract_variables(raw);
        double conf = min_f((((double)(formula_h)) * 0.15), 1.0);
        return hexa_arr_lit((long[]){(long)("formula"), (long)(conf), (long)(raw), (long)(hexa_int_to_str((long)(vars.n)))}, 4);
    }
    long law_h = keyword_hits(raw, law_hints);
    if ((law_h >= 1)) {
        if ((domains.n >= 2)) {
            double conf = min_f((((double)(law_h)) * 0.2), 1.0);
            return hexa_arr_lit((long[]){(long)("law"), (long)(conf), (long)(raw), (long)(hexa_int_to_str((long)(domains.n)))}, 4);
        }
    }
    long pattern_h = keyword_hits(raw, pattern_hints);
    if ((pattern_h > 0)) {
        return hexa_arr_lit((long[]){(long)("pattern"), (long)(min_f((((double)(pattern_h)) * 0.2), 1.0)), (long)("structural"), (long)(raw)}, 4);
    }
    return hexa_arr_lit((long[]){(long)("pattern"), (long)(0.1), (long)("unknown"), (long)(raw)}, 4);
}

hexa_arr register(hexa_arr classified) {
    const char* dtype = ((const char*)classified.d[0]);
    double conf = hexa_to_float(((const char*)classified.d[1]));
    if ((strcmp(dtype, "constant") == 0)) {
        return hexa_arr_lit((long[]){(long)("math_atlas,graph"), (long)(1), (long)(hexa_concat(hexa_concat(hexa_concat("Constant '", ((const char*)classified.d[2])), "' = "), ((const char*)classified.d[3])))}, 3);
    }
    if ((strcmp(dtype, "formula") == 0)) {
        return hexa_arr_lit((long[]){(long)("math_atlas,graph"), (long)(1), (long)(hexa_concat("Formula: ", ((const char*)classified.d[2])))}, 3);
    }
    if ((strcmp(dtype, "bt_candidate") == 0)) {
        return hexa_arr_lit((long[]){(long)("bt_list,graph,hypotheses"), (long)(1), (long)(hexa_concat("BT candidate: ", ((const char*)classified.d[2])))}, 3);
    }
    if ((strcmp(dtype, "lens_candidate") == 0)) {
        return hexa_arr_lit((long[]){(long)("lens_forge,graph"), (long)(1), (long)(hexa_concat("Lens candidate: ", ((const char*)classified.d[2])))}, 3);
    }
    if ((strcmp(dtype, "law") == 0)) {
        const char* targets = "hypotheses,graph";
        double n_domains = safe_float(((const char*)classified.d[3]));
        if ((n_domains >= 3.0)) {
            return hexa_arr_lit((long[]){(long)("hypotheses,graph,bt_candidates"), (long)(1), (long)(hexa_concat(hexa_concat("Law across ", ((const char*)classified.d[3])), " domains"))}, 3);
        }
        return hexa_arr_lit((long[]){(long)(targets), (long)(1), (long)(hexa_concat("Law: ", str_take(((const char*)classified.d[2]), 80)))}, 3);
    }
    return hexa_arr_lit((long[]){(long)("graph"), (long)(1), (long)(hexa_concat(hexa_concat(hexa_concat("Pattern [", ((const char*)classified.d[2])), "]: "), str_take(((const char*)classified.d[3]), 80)))}, 3);
}

hexa_arr determine_sync(hexa_arr target_lists) {
    hexa_arr commands = hexa_arr_new();
    hexa_arr seen = hexa_arr_new();
    long i = 0;
    while ((i < target_lists.n)) {
        const char* targets_csv = ((const char*)target_lists.d[i]);
        hexa_arr targets = hexa_split(targets_csv, ",");
        long j = 0;
        while ((j < targets.n)) {
            const char* t = hexa_trim(((const char*)targets.d[j]));
            if ((list_contains(seen, t) == 0)) {
                seen = hexa_arr_concat(seen, hexa_arr_lit((long[]){(long)(t)}, 1));
                if ((strcmp(t, "math_atlas") == 0)) {
                    commands = hexa_arr_concat(commands, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("math_atlas"), (long)("python3 shared/scan_math_atlas.py --save --summary"), (long)("Sync math atlas")}, 3))}, 1));
                }
                if ((strcmp(t, "bt_list") == 0)) {
                    if ((list_contains(seen, "bt_sync") == 0)) {
                        seen = hexa_arr_concat(seen, hexa_arr_lit((long[]){(long)("bt_sync")}, 1));
                        commands = hexa_arr_concat(commands, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("bt_list"), (long)("# Update docs/breakthrough-theorems.md"), (long)("Register BT candidate")}, 3))}, 1));
                    }
                }
                if ((strcmp(t, "bt_candidates") == 0)) {
                    if ((list_contains(seen, "bt_sync") == 0)) {
                        seen = hexa_arr_concat(seen, hexa_arr_lit((long[]){(long)("bt_sync")}, 1));
                        commands = hexa_arr_concat(commands, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("bt_candidates"), (long)("# Update docs/breakthrough-theorems.md"), (long)("Register BT candidate")}, 3))}, 1));
                    }
                }
                if ((strcmp(t, "lens_forge") == 0)) {
                    commands = hexa_arr_concat(commands, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("lens_forge"), (long)("# Run lens forge validation"), (long)("Validate lens candidate")}, 3))}, 1));
                }
                if ((strcmp(t, "graph") == 0)) {
                    if ((list_contains(seen, "graph_sync") == 0)) {
                        seen = hexa_arr_concat(seen, hexa_arr_lit((long[]){(long)("graph_sync")}, 1));
                        commands = hexa_arr_concat(commands, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("graph"), (long)("# Save updated discovery graph"), (long)("Persist graph changes")}, 3))}, 1));
                    }
                }
                if ((strcmp(t, "hypotheses") == 0)) {
                    commands = hexa_arr_concat(commands, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)("hypotheses"), (long)("# Update hypotheses docs"), (long)("Register new hypothesis")}, 3))}, 1));
                }
            }
            j = (j + 1);
        }
        i = (i + 1);
    }
    return commands;
}

const char* determine_level(const char* targets_csv) {
    if (hexa_contains(targets_csv, "bt_list")) {
        return "breakthrough";
    }
    if (hexa_contains(targets_csv, "bt_candidates")) {
        return "breakthrough";
    }
    if (hexa_contains(targets_csv, "math_atlas")) {
        return "discovery";
    }
    if (hexa_contains(targets_csv, "hypotheses")) {
        return "discovery";
    }
    if (hexa_contains(targets_csv, "lens_forge")) {
        return "discovery";
    }
    return "info";
}

const char* format_notification(const char* level, const char* disc_id, const char* details) {
    const char* prefix = "*   ";
    if ((strcmp(level, "breakthrough") == 0)) {
        prefix = "*** ";
    }
    if ((strcmp(level, "discovery") == 0)) {
        prefix = "**  ";
    }
    const char* label = "[INFO]";
    if ((strcmp(level, "breakthrough") == 0)) {
        label = "[BT]";
    }
    if ((strcmp(level, "discovery") == 0)) {
        label = "[DISCOVERY]";
    }
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(prefix, label), " "), disc_id), ": "), details);
}

hexa_arr run_pipeline(hexa_arr discoveries, hexa_arr scores, const char* source) {
    hexa_arr results = hexa_arr_new();
    hexa_arr target_lists = hexa_arr_new();
    hexa_arr notifications = hexa_arr_new();
    long i = 0;
    while ((i < discoveries.n)) {
        const char* raw = ((const char*)discoveries.d[i]);
        double score = hexa_to_float(((const char*)scores.d[i]));
        hexa_arr classified = classify(raw, score, source);
        hexa_arr reg = register(classified);
        const char* targets_csv = hexa_int_to_str((long)(reg.d[0]));
        target_lists = hexa_arr_concat(target_lists, hexa_arr_lit((long[]){(long)(targets_csv)}, 1));
        const char* level = determine_level(targets_csv);
        const char* note = format_notification(level, hexa_concat("disc-", hexa_int_to_str((long)(i))), hexa_int_to_str((long)(reg.d[2])));
        notifications = hexa_arr_concat(notifications, hexa_arr_lit((long[]){(long)(note)}, 1));
        results = hexa_arr_concat(results, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)(classified), (long)(reg)}, 2))}, 1));
        i = (i + 1);
    }
    hexa_arr sync_cmds = determine_sync(target_lists);
    return hexa_arr_lit((long[]){(long)(results), (long)(notifications), (long)(sync_cmds)}, 3);
}

long show_help() {
    printf("%s\n", "auto_register.hexa — 자동 발견 분류 + 등록 파이프라인 (mk2 hexa)");
    printf("%s\n", "");
    printf("%s\n", "Usage:");
    printf("%s\n", "  hexa auto_register.hexa classify <text> [source]");
    printf("%s\n", "  hexa auto_register.hexa register <text> [source]");
    printf("%s\n", "  hexa auto_register.hexa pipeline <file.jsonl> [source]");
    printf("%s\n", "  hexa auto_register.hexa sync <file.jsonl> [source]");
    printf("%s\n", "  hexa auto_register.hexa help");
    printf("%s\n", "");
    printf("%s\n", "Classification types: constant, formula, pattern, law, bt_candidate, lens_candidate");
    return printf("%s\n", "Registration targets: math_atlas, graph, hypotheses, bt_list, lens_forge");
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
    cli_args = hexa_args();
    if ((cli_args.n > 2)) {
        cmd = ((const char*)cli_args.d[2]);
    }
    if ((strcmp(cmd, "help") == 0)) {
        show_help();
    }
    if ((strcmp(cmd, "classify") == 0)) {
        if ((cli_args.n < 4)) {
            printf("%s\n", "Error: classify requires <text> argument");
        } else {
            const char* text = ((const char*)cli_args.d[3]);
            const char* source = "cli";
            if ((cli_args.n > 4)) {
                source = ((const char*)cli_args.d[4]);
            }
            hexa_arr result = classify(text, 1.0, source);
            printf("%s\n", hexa_concat("Type:       ", ((const char*)result.d[0])));
            printf("%s\n", hexa_concat("Confidence: ", ((const char*)result.d[1])));
            printf("%s\n", hexa_concat("Detail:     ", ((const char*)result.d[2])));
        }
    }
    if ((strcmp(cmd, "register") == 0)) {
        if ((cli_args.n < 4)) {
            printf("%s\n", "Error: register requires <text> argument");
        } else {
            const char* text = ((const char*)cli_args.d[3]);
            const char* source = "cli";
            if ((cli_args.n > 4)) {
                source = ((const char*)cli_args.d[4]);
            }
            hexa_arr classified = classify(text, 1.0, source);
            hexa_arr reg = register(classified);
            printf("%s\n", hexa_concat("Targets: ", hexa_int_to_str((long)(reg.d[0]))));
            printf("%s\n", hexa_concat("Success: ", hexa_int_to_str((long)(reg.d[1]))));
            printf("%s\n", hexa_concat("Details: ", hexa_int_to_str((long)(reg.d[2]))));
        }
    }
    if ((strcmp(cmd, "pipeline") == 0)) {
        if ((cli_args.n < 4)) {
            printf("%s\n", "Error: pipeline requires <file.jsonl> argument");
        } else {
            const char* fpath = ((const char*)cli_args.d[3]);
            const char* source = "file";
            if ((cli_args.n > 4)) {
                source = ((const char*)cli_args.d[4]);
            }
            const char* content = "";
            content = hexa_read_file(fpath);
            if ((hexa_str_len(content) > 0)) {
                hexa_arr lines = hexa_split(content, "\n");
                hexa_arr discoveries = hexa_arr_new();
                hexa_arr scores = hexa_arr_new();
                long i = 0;
                while ((i < lines.n)) {
                    const char* line = hexa_trim(((const char*)lines.d[i]));
                    if ((hexa_str_len(line) > 0)) {
                        discoveries = hexa_arr_concat(discoveries, hexa_arr_lit((long[]){(long)(line)}, 1));
                        scores = hexa_arr_concat(scores, hexa_arr_lit((long[]){(long)(1.0)}, 1));
                    }
                    i = (i + 1);
                }
                hexa_arr result = run_pipeline(discoveries, scores, source);
                long notifications = result.d[1];
                printf("%s\n", hexa_concat(hexa_concat("=== Pipeline Results (", hexa_int_to_str((long)(discoveries.n))), " discoveries) ==="));
                long notes = notifications;
                long j = 0;
                while ((j < (long)(((double)(hexa_str_len(notes)))))) {
                    printf("%s\n", hexa_int_to_str((long)(notes[j])));
                    j = (j + 1);
                }
            }
        }
    }
    if ((strcmp(cmd, "sync") == 0)) {
        if ((cli_args.n < 4)) {
            printf("%s\n", "Error: sync requires <file.jsonl> argument");
        } else {
            const char* fpath = ((const char*)cli_args.d[3]);
            const char* source = "file";
            if ((cli_args.n > 4)) {
                source = ((const char*)cli_args.d[4]);
            }
            const char* content = "";
            content = hexa_read_file(fpath);
            if ((hexa_str_len(content) > 0)) {
                hexa_arr lines = hexa_split(content, "\n");
                hexa_arr discoveries = hexa_arr_new();
                hexa_arr scores = hexa_arr_new();
                long i = 0;
                while ((i < lines.n)) {
                    const char* line = hexa_trim(((const char*)lines.d[i]));
                    if ((hexa_str_len(line) > 0)) {
                        discoveries = hexa_arr_concat(discoveries, hexa_arr_lit((long[]){(long)(line)}, 1));
                        scores = hexa_arr_concat(scores, hexa_arr_lit((long[]){(long)(1.0)}, 1));
                    }
                    i = (i + 1);
                }
                hexa_arr result = run_pipeline(discoveries, scores, source);
                long sync_cmds = result.d[2];
                printf("%s\n", "=== Sync Commands ===");
                long j = 0;
                while ((j < (long)(((double)(hexa_str_len(sync_cmds)))))) {
                    long entry = sync_cmds[j];
                    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("[", hexa_int_to_str((long)(entry[0]))), "] "), hexa_int_to_str((long)(entry[1]))));
                    j = (j + 1);
                }
            }
        }
    }
    return 0;
}
