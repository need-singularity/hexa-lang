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

hexa_arr load_n6_consts();
double abs_f(double x);
double max_f(double a, double b);
double n6_dist_one(double value, double constant);
long best_match(double value);
const char* grade_for(double value, double cv);
const char* extract_str(const char* line, const char* key);
const char* extract_num(const char* line, const char* key);
long is_numeric(const char* s);

static hexa_arr _n6;
static long _n6_names;
static long _n6_values;
static long _n6_bts;
static long _n6_count;
static const char* home;
static const char* rm_path;
static const char* raw = "";
static hexa_arr lines;
static const char* cur_id = "";
static const char* cur_level = "";
static const char* cur_measured_str = "";
static long total = 0;
static long exact_n = 0;
static long close_n = 0;
static long miss_n = 0;
static const char* events = "";
static long event_count = 0;
static long seen_pairs;
static const char* date_str;

hexa_arr load_n6_consts() {
    const char* home = hexa_exec("printenv HOME");
    const char* path = hexa_concat(home, "/Dev/nexus/shared/n6_constants.jsonl");
    const char* raw = "";
    raw = hexa_read_file(path);
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr names = hexa_arr_new();
    hexa_arr values = hexa_arr_new();
    hexa_arr bts = hexa_arr_new();
    long __fi_line_1 = 0;
    while ((__fi_line_1 < lines.n)) {
        const char* line = ((const char*)lines.d[__fi_line_1]);
        const char* trimmed = hexa_trim(line);
        if ((strcmp(trimmed, "") == 0)) {
            continue;
        }
        const char* name = "";
        double value = 0.0;
        const char* bt = "";
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
        if (hexa_contains(trimmed, "\"bt\":\"")) {
            hexa_arr p = hexa_split(trimmed, "\"bt\":\"");
            if ((p.n >= 2)) {
                bt = ((const char*)(hexa_split(((const char*)p.d[1]), "\"")).d[0]);
            }
        }
        if ((strcmp(name, "") != 0)) {
            names = hexa_arr_concat(names, hexa_arr_lit((long[]){(long)(name)}, 1));
            values = hexa_arr_concat(values, hexa_arr_lit((long[]){(long)(value)}, 1));
            bts = hexa_arr_concat(bts, hexa_arr_lit((long[]){(long)(bt)}, 1));
        }
        __fi_line_1 = (__fi_line_1 + 1);
    }
    return hexa_arr_lit((long[]){(long)(names), (long)(values), (long)(bts)}, 3);
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

double n6_dist_one(double value, double constant) {
    double denom = max_f(abs_f(constant), 1.0);
    return (abs_f((value - constant)) / denom);
}

long best_match(double value) {
    long best_i = (0 - 1);
    double best_d = 1.0;
    long i = 0;
    while ((i < _n6_count)) {
        double cv = ((double)(_n6_values[i]));
        double d = n6_dist_one(value, cv);
        if ((d < best_d)) {
            best_d = d;
            best_i = i;
        }
        i = (i + 1);
    }
    return best_i;
}

const char* grade_for(double value, double cv) {
    double diff = abs_f((value - cv));
    if ((diff < 0.000001)) {
        return "EXACT";
    }
    double denom = max_f(abs_f(cv), 1.0);
    if (((diff / denom) < 0.001)) {
        return "CLOSE";
    }
    return "MISS";
}

const char* extract_str(const char* line, const char* key) {
    const char* pat = hexa_concat(hexa_concat("\"", key), "\":");
    if ((!hexa_contains(line, pat))) {
        return "";
    }
    hexa_arr p = hexa_split(line, pat);
    if ((p.n < 2)) {
        return "";
    }
    const char* rest = hexa_trim(((const char*)p.d[1]));
    if ((!hexa_contains(rest, "\""))) {
        return "";
    }
    hexa_arr q = hexa_split(rest, "\"");
    if ((q.n < 2)) {
        return "";
    }
    return ((const char*)q.d[1]);
}

const char* extract_num(const char* line, const char* key) {
    const char* pat = hexa_concat(hexa_concat("\"", key), "\":");
    if ((!hexa_contains(line, pat))) {
        return "";
    }
    hexa_arr p = hexa_split(line, pat);
    if ((p.n < 2)) {
        return "";
    }
    const char* rest = hexa_trim(((const char*)p.d[1]));
    if (hexa_contains(rest, ",")) {
        rest = ((const char*)(hexa_split(rest, ",")).d[0]);
    }
    if (hexa_contains(rest, "}")) {
        rest = ((const char*)(hexa_split(rest, "}")).d[0]);
    }
    return hexa_trim(rest);
}

long is_numeric(const char* s) {
    if ((strcmp(s, "") == 0)) {
        return 0;
    }
    long ok = 0;
    double _v = hexa_to_float(s);
    ok = 1;
    return ok;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    _n6 = load_n6_consts();
    _n6_names = _n6.d[0];
    _n6_values = _n6.d[1];
    _n6_bts = _n6.d[2];
    _n6_count = hexa_str_len(_n6_values);
    printf("%s\n", hexa_concat(hexa_concat("=== absorb_reality (n6=", hexa_int_to_str((long)(_n6_count))), ") ==="));
    home = hexa_exec("printenv HOME");
    rm_path = hexa_concat(home, "/Dev/nexus/shared/reality_map.json");
    raw = hexa_read_file(rm_path);
    lines = hexa_split(raw, "\n");
    seen_pairs = 0;
    0;
    date_str = hexa_trim(hexa_exec("date +%Y-%m-%d"));
    long __fi_line_2 = 0;
    while ((__fi_line_2 < lines.n)) {
        const char* line = ((const char*)lines.d[__fi_line_2]);
        const char* id_v = extract_str(line, "id");
        if (((strcmp(id_v, "") != 0) && (!hexa_contains(line, "source_url")))) {
            cur_id = id_v;
            cur_level = "";
            cur_measured_str = "";
            continue;
        }
        const char* lvl_v = extract_str(line, "level");
        if ((strcmp(lvl_v, "") != 0)) {
            cur_level = lvl_v;
            continue;
        }
        const char* m_raw = extract_num(line, "measured");
        if ((((strcmp(m_raw, "") != 0) && (strcmp(cur_id, "") != 0)) && (strcmp(cur_measured_str, "") == 0))) {
            if (is_numeric(m_raw)) {
                cur_measured_str = m_raw;
                double v = hexa_to_float(m_raw);
                total = (total + 1);
                long bi = best_match(v);
                if ((bi >= 0)) {
                    double cv = ((double)(_n6_values[bi]));
                    const char* g = grade_for(v, cv);
                    if ((strcmp(g, "EXACT") == 0)) {
                        exact_n = (exact_n + 1);
                    }
                    if ((strcmp(g, "CLOSE") == 0)) {
                        close_n = (close_n + 1);
                    }
                    if ((strcmp(g, "MISS") == 0)) {
                        miss_n = (miss_n + 1);
                    }
                    if ((strcmp(g, "MISS") != 0)) {
                        const char* bt = hexa_int_to_str((long)(_n6_bts[bi]));
                        const char* nm = hexa_int_to_str((long)(_n6_names[bi]));
                        const char* key = hexa_concat(hexa_concat(cur_id, "|"), bt);
                        long already = 0;
                        long _ = seen_pairs[key];
                        already = 1;
                        if ((!already)) {
                            seen_pairs[key] = 1;
                            const char* ev = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"type\":\"absorb\",\"source\":\"absorb_reality\",\"phase\":\"7.0\",\"node\":\"", cur_id), "\",\"value\":"), m_raw), ",\"matched_bt\":\""), bt), "\",\"matched_name\":\""), nm), "\",\"grade\":\""), g), "\",\"domain\":\""), cur_level), "\",\"timestamp\":\""), date_str), "\"}");
                            if ((strcmp(events, "") == 0)) {
                                events = ev;
                            } else {
                                events = hexa_concat(hexa_concat(events, "\n"), ev);
                            }
                            event_count = (event_count + 1);
                        }
                    }
                }
            }
        }
        __fi_line_2 = (__fi_line_2 + 1);
    }
    if ((event_count > 0)) {
        hexa_append_file("shared/growth_bus.jsonl", hexa_concat("\n", events));
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("총 ", hexa_int_to_str((long)(total))), "노드 → EXACT="), hexa_int_to_str((long)(exact_n))), ", CLOSE="), hexa_int_to_str((long)(close_n))), ", MISS="), hexa_int_to_str((long)(miss_n))));
    printf("%s\n", hexa_concat(hexa_concat("growth_bus append: ", hexa_int_to_str((long)(event_count))), " events"));
    return 0;
}
