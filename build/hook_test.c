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
hexa_arr load_jsonl(const char* path);
const char* n6_match_name(double value);
const char* n6_match_grade(double value);
hexa_arr extract_numbers(const char* text);
long record_discovery(double value, const char* name, const char* grade, const char* source);
const char* extract_stdout(const char* json_text);
const char* extract_command(const char* json_text);
const char* extract_project(const char* json_text);

static const char* _home;
static const char* _shared;
static const char* log_path;
static hexa_arr _n6;
static long _n6_names;
static long _n6_values;
static hexa_arr _ph;
static long _ph_names;
static long _ph_values;
static hexa_arr a;
static const char* mode;
static const char* stdin_raw = "";
static long _reading = 1;
static const char* project;
static const char* stdin_text;
static long discoveries = 0;
static hexa_arr nums;
static const char* exact_names = "";
static hexa_arr bt_keywords_list;
static hexa_arr bt_actions_list;
static long bt_detected = 0;
static const char* bt_domain = "math";
static const char* bt_action = "blowup";
static long ki = 0;
static hexa_arr domain_lines;
static hexa_arr gap_keywords;
static long gap_detected = 0;

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    } else {
        return x;
    }
}

hexa_arr load_jsonl(const char* path) {
    const char* raw = "";
    raw = hexa_read_file(path);
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

const char* n6_match_name(double value) {
    long i = 0;
    while ((i < hexa_str_len(_n6_names))) {
        if ((abs_f((value - _n6_values[i])) < 0.0001)) {
            return "";
        }
        i = (i + 1);
    }
    long j = 0;
    while ((j < hexa_str_len(_ph_names))) {
        if ((_ph_values[j] != 0.0)) {
            double rel = (abs_f((value - _ph_values[j])) / abs_f(_ph_values[j]));
            if ((rel < 0.01)) {
                return "";
            }
        }
        j = (j + 1);
    }
    return "";
}

const char* n6_match_grade(double value) {
    if ((strcmp(n6_match_name(value), "") != 0)) {
        return "EXACT";
    }
    long j = 0;
    while ((j < hexa_str_len(_ph_names))) {
        if ((_ph_values[j] != 0.0)) {
            double rel = (abs_f((value - _ph_values[j])) / abs_f(_ph_values[j]));
            if ((rel < 0.05)) {
                return "NEAR";
            }
        }
        j = (j + 1);
    }
    return "";
}

hexa_arr extract_numbers(const char* text) {
    hexa_arr nums = hexa_arr_new();
    const char* clean = hexa_replace(hexa_replace(hexa_replace(text, "\n", " "), "\t", " "), "\r", " ");
    hexa_arr parts = hexa_split(clean, " ");
    long __fi_p_2 = 0;
    while ((__fi_p_2 < parts.n)) {
        const char* p = ((const char*)parts.d[__fi_p_2]);
        double v = hexa_to_float(p);
        if (((v > 2.0) && (v < 100000.0))) {
            nums = hexa_arr_push(nums, v);
        }
        __fi_p_2 = (__fi_p_2 + 1);
    }
    return nums;
}

long record_discovery(double value, const char* name, const char* grade, const char* source) {
    const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"constant\":\"", name), "\",\"value\":\""), hexa_float_to_str((double)(value))), "\",\"grade\":\""), grade), "\",\"source\":\""), source), "\",\"processed\":true,\"alien_index\":{\"d\":0,\"r\":8},\"mk2\":{\"sector\":\"n6\",\"paths\":1}}");
    return hexa_append_file(log_path, hexa_concat(entry, "\n"));
}

const char* extract_stdout(const char* json_text) {
    if (hexa_contains(json_text, "\"stdout\":\"")) {
        hexa_arr parts = hexa_split(json_text, "\"stdout\":\"");
        if ((parts.n >= 2)) {
            const char* rest = ((const char*)parts.d[1]);
            const char* result = "";
            long i = 0;
            hexa_arr chars = hexa_split(rest, "");
            long prev_backslash = 0;
            while ((i < chars.n)) {
                const char* c = ((const char*)chars.d[i]);
                if (((strcmp(c, "\\") == 0) && (!prev_backslash))) {
                    prev_backslash = 1;
                    i = (i + 1);
                    continue;
                }
                if (((strcmp(c, "\"") == 0) && (!prev_backslash))) {
                    return result;
                }
                if (prev_backslash) {
                    if ((strcmp(c, "n") == 0)) {
                        result = hexa_concat(result, "\n");
                    } else {
                        if ((strcmp(c, "t") == 0)) {
                            result = hexa_concat(result, "\t");
                        } else {
                            result = hexa_concat(result, c);
                        }
                    }
                    prev_backslash = 0;
                } else {
                    result = hexa_concat(result, c);
                }
                i = (i + 1);
            }
            return result;
        }
    }
    return json_text;
}

const char* extract_command(const char* json_text) {
    if (hexa_contains(json_text, "\"command\":\"")) {
        hexa_arr parts = hexa_split(json_text, "\"command\":\"");
        if ((parts.n >= 2)) {
            const char* rest = ((const char*)parts.d[1]);
            const char* end = ((const char*)(hexa_split(rest, "\"")).d[0]);
            if ((hexa_str_len(end) > 40)) {
                return hexa_concat(((const char*)(hexa_split(end, "")).d[0]), "...");
            }
            return end;
        }
    }
    return "";
}

const char* extract_project(const char* json_text) {
    if (hexa_contains(json_text, "\"cwd\":\"")) {
        hexa_arr parts = hexa_split(json_text, "\"cwd\":\"");
        if ((parts.n >= 2)) {
            const char* cwd = ((const char*)(hexa_split(((const char*)parts.d[1]), "\"")).d[0]);
            hexa_arr segs = hexa_split(cwd, "/");
            return ((const char*)segs.d[(segs.n - 1)]);
        }
    }
    return "nexus";
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    _home = hexa_exec("printenv HOME");
    _shared = hexa_concat(_home, "/Dev/nexus/shared");
    log_path = hexa_concat(_shared, "/discovery_log.jsonl");
    _n6 = load_jsonl(hexa_concat(_shared, "/n6_constants.jsonl"));
    _n6_names = _n6.d[0];
    _n6_values = _n6.d[1];
    _ph = load_jsonl(hexa_concat(_shared, "/n6_physics.jsonl"));
    _ph_names = _ph.d[0];
    _ph_values = _ph.d[1];
    a = hexa_args();
    mode = (((a.n >= 3)) ? (((const char*)a.d[2])) : ("post-edit"));
    while (_reading) {
        long line = input("");
        if ((line == "")) {
            _reading = 0;
        } else {
            if ((strcmp(stdin_raw, "") != 0)) {
                stdin_raw = hexa_concat(stdin_raw, "\n");
            }
            stdin_raw = hexa_concat(stdin_raw, hexa_int_to_str((long)(line)));
        }
    }
    project = extract_project(stdin_raw);
    stdin_text = extract_stdout(stdin_raw);
    nums = extract_numbers(stdin_text);
    long __fi_v_3 = 0;
    while ((__fi_v_3 < nums.n)) {
        long v = nums.d[__fi_v_3];
        const char* name = n6_match_name(v);
        if ((strcmp(name, "") != 0)) {
            const char* grade = n6_match_grade(v);
            record_discovery(v, name, grade, hexa_concat("hexa-hook:", mode));
            discoveries = (discoveries + 1);
            if ((strcmp(exact_names, "") != 0)) {
                exact_names = hexa_concat(exact_names, ",");
            }
            exact_names = hexa_concat(hexa_concat(hexa_concat(exact_names, hexa_int_to_str((long)(v))), "="), name);
        }
        __fi_v_3 = (__fi_v_3 + 1);
    }
    if (((hexa_contains(stdin_text, "error[Syntax]") || hexa_contains(stdin_text, "error[Runtime]")) || hexa_contains(stdin_text, "error[Type]"))) {
        const char* pitfalls_log = hexa_concat(_shared, "/hexa_pitfalls_log.jsonl");
        const char* err_msg = "";
        hexa_arr err_lines = hexa_split(stdin_text, "\n");
        long ei = 0;
        while ((ei < err_lines.n)) {
            const char* el = ((const char*)err_lines.d[ei]);
            if (hexa_contains(el, "error[")) {
                err_msg = hexa_trim(el);
                ei = err_lines.n;
            }
            ei = (ei + 1);
        }
        if ((strcmp(err_msg, "") != 0)) {
            const char* ts = hexa_trim(hexa_exec("date +%Y-%m-%dT%H:%M:%S"));
            const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"timestamp\":\"", ts), "\",\"error\":\""), hexa_replace(err_msg, "\"", "'")), "\",\"mode\":\""), mode), "\",\"auto_pitfall\":true}");
            hexa_append_file(pitfalls_log, hexa_concat(entry, "\n"));
            const char* pit_msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"systemMessage\":\"⚠️ ", project), " | hexa 에러 → pitfalls 기록: "), hexa_replace(err_msg, "\"", "'")), "\"}");
            printf("%s\n", pit_msg);
        }
    }
    bt_keywords_list = hexa_arr_new();
    bt_actions_list = hexa_arr_new();
    const char* bt_raw = hexa_read_file(hexa_concat(_shared, "/bt_keywords.jsonl"));
    hexa_arr bt_lines = hexa_split(bt_raw, "\n");
    long __fi_btl_4 = 0;
    while ((__fi_btl_4 < bt_lines.n)) {
        const char* btl = ((const char*)bt_lines.d[__fi_btl_4]);
        const char* trimmed_btl = hexa_trim(btl);
        if ((strcmp(trimmed_btl, "") == 0)) {
            continue;
        }
        if (hexa_contains(trimmed_btl, "\"keyword\":\"")) {
            hexa_arr kp = hexa_split(trimmed_btl, "\"keyword\":\"");
            if ((kp.n >= 2)) {
                const char* kw_val = ((const char*)(hexa_split(((const char*)kp.d[1]), "\"")).d[0]);
                bt_keywords_list = hexa_arr_concat(bt_keywords_list, hexa_arr_lit((long[]){(long)(kw_val)}, 1));
                const char* act = "blowup";
                if (hexa_contains(trimmed_btl, "\"action\":\"")) {
                    hexa_arr ap = hexa_split(trimmed_btl, "\"action\":\"");
                    if ((ap.n >= 2)) {
                        act = ((const char*)(hexa_split(((const char*)ap.d[1]), "\"")).d[0]);
                    }
                }
                bt_actions_list = hexa_arr_concat(bt_actions_list, hexa_arr_lit((long[]){(long)(act)}, 1));
            }
        }
        __fi_btl_4 = (__fi_btl_4 + 1);
    }
    while ((ki < bt_keywords_list.n)) {
        if (hexa_contains(stdin_text, bt_keywords_list.d[ki])) {
            bt_detected = 1;
            if ((ki < bt_actions_list.n)) {
                bt_action = bt_actions_list.d[ki];
            }
        }
        ki = (ki + 1);
    }
    domain_lines = hexa_arr_new();
    const char* raw = hexa_exec(hexa_concat(hexa_concat("cat ", _shared), "/bt_domains.jsonl 2>/dev/null"));
    domain_lines = hexa_split(raw, "\n");
    long __fi_dline_6 = 0;
    while ((__fi_dline_6 < domain_lines.n)) {
        const char* dline = ((const char*)domain_lines.d[__fi_dline_6]);
        if ((strcmp(hexa_trim(dline), "") == 0)) {
            continue;
        }
        const char* d_name = "";
        if (hexa_contains(dline, "\"domain\":\"")) {
            hexa_arr d_parts = hexa_split(dline, "\"domain\":\"");
            if ((d_parts.n >= 2)) {
                hexa_arr d_rest = hexa_split(((const char*)d_parts.d[1]), "\"");
                if ((d_rest.n >= 1)) {
                    d_name = ((const char*)d_rest.d[0]);
                }
            }
        }
        if ((strcmp(d_name, "") == 0)) {
            continue;
        }
        if (hexa_contains(dline, "\"keywords\":[")) {
            hexa_arr kw_parts = hexa_split(dline, "\"keywords\":[");
            if ((kw_parts.n >= 2)) {
                hexa_arr kw_body = hexa_split(((const char*)kw_parts.d[1]), "]");
                if ((kw_body.n >= 1)) {
                    hexa_arr kw_items = hexa_split(((const char*)kw_body.d[0]), ",");
                    long __fi_kp_5 = 0;
                    while ((__fi_kp_5 < kw_items.n)) {
                        const char* kp = ((const char*)kw_items.d[__fi_kp_5]);
                        const char* kw_clean = hexa_replace(hexa_trim(kp), "\"", "");
                        if (((strcmp(kw_clean, "") != 0) && hexa_contains(stdin_text, kw_clean))) {
                            bt_domain = d_name;
                        }
                        __fi_kp_5 = (__fi_kp_5 + 1);
                    }
                }
            }
        }
        __fi_dline_6 = (__fi_dline_6 + 1);
    }
    if (bt_detected) {
        const char* cooldown_file = hexa_concat(_shared, "/.bt_cooldown");
        long should_run = 1;
        const char* last_ts = hexa_trim(hexa_exec(hexa_concat("cat ", cooldown_file)));
        const char* now_ts = hexa_trim(hexa_exec("date +%s"));
        long diff = ((long)(hexa_to_float(now_ts)) - (long)(hexa_to_float(last_ts)));
        if ((diff < 300)) {
            should_run = 0;
        }
        if (should_run) {
            hexa_exec("bash");
            const char* hexa_bin = hexa_concat(hexa_exec("printenv HOME"), "/Dev/hexa-lang/target/release/hexa");
            const char* nexus_dir = hexa_concat(hexa_exec("printenv HOME"), "/Dev/nexus");
            if ((strcmp(bt_action, "blowup_loop") == 0)) {
                const char* breakthrough = hexa_concat(nexus_dir, "/mk2_hexa/native/breakthrough.hexa");
                hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("cd ", nexus_dir), " && "), hexa_bin), " "), breakthrough), " --engine auto '"), hexa_replace(stdin_text, "'", "")), "' >> "), _shared), "/auto_bt.log 2>&1 &"));
                discoveries = (discoveries + 1);
                if ((strcmp(exact_names, "") != 0)) {
                    exact_names = hexa_concat(exact_names, ",");
                }
                exact_names = hexa_concat(hexa_concat(exact_names, "🔄GROWTH:"), bt_domain);
            } else {
                const char* blowup = hexa_concat(nexus_dir, "/mk2_hexa/native/blowup.hexa");
                const char* se = hexa_concat(nexus_dir, "/mk2_hexa/native/seed_engine.hexa");
                const char* seeds = "";
                seeds = hexa_replace(hexa_trim(hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_bin, " "), se), " merge"))), "|", ",");
                if ((hexa_str_len(seeds) == 0)) {
                    seeds = "6,12,2,4,5,24,7,3,1,8,10,28,36,48,60,120,144,288,496,720";
                }
                hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("cd ", nexus_dir), " && "), hexa_bin), " "), blowup), " "), bt_domain), " 3 --no-graph --fast --seeds '"), seeds), "' >> "), _shared), "/auto_bt.log 2>&1 &"));
                discoveries = (discoveries + 1);
                if ((strcmp(exact_names, "") != 0)) {
                    exact_names = hexa_concat(exact_names, ",");
                }
                exact_names = hexa_concat(hexa_concat(exact_names, "🛸BT:"), bt_domain);
            }
        }
    }
    gap_keywords = hexa_arr_lit((long[]){(long)("빈공간"), (long)("미발견"), (long)("미탐색"), (long)("미지 영역"), (long)("empty space"), (long)("gap"), (long)("DFS")}, 7);
    long __fi_gkw_7 = 0;
    while ((__fi_gkw_7 < gap_keywords.n)) {
        const char* gkw = ((const char*)gap_keywords.d[__fi_gkw_7]);
        if (hexa_contains(stdin_text, gkw)) {
            gap_detected = 1;
        }
        __fi_gkw_7 = (__fi_gkw_7 + 1);
    }
    if (gap_detected) {
        const char* gap_cooldown = hexa_concat(_shared, "/.gap_cooldown");
        long gap_should_run = 1;
        const char* gap_last = hexa_trim(hexa_exec(hexa_concat("cat ", gap_cooldown)));
        const char* gap_now = hexa_trim(hexa_exec("date +%s"));
        long gap_diff = ((long)(hexa_to_float(gap_now)) - (long)(hexa_to_float(gap_last)));
        if ((gap_diff < 300)) {
            gap_should_run = 0;
        }
        if (gap_should_run) {
            hexa_exec("bash");
            const char* hexa_bin2 = hexa_concat(hexa_trim(hexa_exec("printenv HOME")), "/Dev/hexa-lang/target/release/hexa");
            const char* gap_finder = hexa_concat(hexa_trim(hexa_exec("printenv HOME")), "/Dev/nexus/mk2_hexa/native/gap_finder.hexa");
            hexa_exec("bash");
            discoveries = (discoveries + 1);
            if ((strcmp(exact_names, "") != 0)) {
                exact_names = hexa_concat(exact_names, ",");
            }
            exact_names = hexa_concat(exact_names, "🔍GAP:quick+dfs+bridge");
        }
    }
    if ((((discoveries > 0) || bt_detected) || gap_detected)) {
        const char* tags = "";
        if (bt_detected) {
            tags = hexa_concat(hexa_concat(tags, " 🛸돌파:"), bt_domain);
        }
        if (gap_detected) {
            tags = hexa_concat(tags, " 🔍GAP");
        }
        const char* body = "";
        if ((discoveries > 0)) {
            body = hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(discoveries)), "건 감지 ["), exact_names), "]");
        }
        if (((strcmp(body, "") == 0) && bt_detected)) {
            if ((strcmp(bt_action, "blowup_loop") == 0)) {
                body = "연속 돌파 키워드 감지";
            } else {
                body = "돌파 키워드 감지";
            }
        }
        if (((strcmp(body, "") == 0) && gap_detected)) {
            body = "갭 탐색 트리거";
        }
        const char* msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"systemMessage\":\"⚡ ", project), " | "), body), tags), "\"}");
        printf("%s\n", msg);
    }
    if ((strcmp(mode, "prompt") == 0)) {
        hexa_arr todo_keywords = hexa_arr_lit((long[]){(long)("다음"), (long)("플랜"), (long)("할일"), (long)("할 일"), (long)("todo"), (long)("작업목록"), (long)("작업 목록"), (long)("next plan"), (long)("진행상황"), (long)("진행 상황"), (long)("progress")}, 11);
        long todo_found = 0;
        long __fi_tkw_8 = 0;
        while ((__fi_tkw_8 < todo_keywords.n)) {
            const char* tkw = ((const char*)todo_keywords.d[__fi_tkw_8]);
            if (hexa_contains(stdin_text, tkw)) {
                todo_found = 1;
            }
            __fi_tkw_8 = (__fi_tkw_8 + 1);
        }
        if (todo_found) {
            const char* todo_project = ((((strcmp(project, "TECS-L") == 0) || (strcmp(project, "fathom") == 0))) ? ("nexus") : (project));
            const char* hexa_bin = hexa_concat(_home, "/Dev/hexa-lang/target/release/hexa");
            const char* todo_hexa = hexa_concat(_home, "/Dev/nexus/mk2_hexa/native/todo_display.hexa");
            const char* result = hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_bin, " "), todo_hexa), " "), todo_project), " hook 2>/dev/null"));
            if ((strcmp(hexa_trim(result), "") != 0)) {
                printf("%s\n", hexa_trim(result));
            }
        }
    }
    return 0;
}
