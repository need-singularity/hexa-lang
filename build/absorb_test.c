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

const char* extract_field(const char* line, const char* field);
long extract_int(const char* line, const char* field);
double abs_f(double x);
const char* n6_match_grade(double value);
const char* now_ts();
hexa_arr load_graph_node_ids();
hexa_arr load_recent_log(long n);
hexa_arr scan_unabsorbed();
const char* value_to_n6_node(const char* value);
const char* make_node_id(const char* source, const char* constant);

static const char* log_path = "shared/discovery_log.jsonl";
static const char* graph_path = "shared/discovery_graph.json";
static const char* dist_path = "shared/alien_index_distribution.json";
static const char* bus_path = "shared/growth_bus.jsonl";
static long scan_tail = 100;
static hexa_arr n6_constants;
static hexa_arr n6_values;

const char* extract_field(const char* line, const char* field) {
    const char* needle = hexa_concat(hexa_concat("\"", field), "\":");
    if ((!hexa_contains(line, needle))) {
        return "";
    }
    hexa_arr after = hexa_split(line, needle);
    if ((after.n < 2)) {
        return "";
    }
    const char* rest = hexa_trim(((const char*)after.d[1]));
    if (hexa_starts_with(rest, "\"")) {
        hexa_arr parts = hexa_split(rest, "\"");
        if ((parts.n >= 2)) {
            return ((const char*)parts.d[1]);
        }
    }
    if (hexa_starts_with(rest, "{")) {
        long depth = 0;
        const char* result = "";
        long i = 0;
        while ((i < hexa_str_len(rest))) {
            long ch = rest[i];
            if ((ch == "{")) {
                depth = (depth + 1);
            }
            if ((ch == "}")) {
                depth = (depth - 1);
            }
            result = hexa_concat(result, hexa_int_to_str((long)(ch)));
            if ((depth == 0)) {
                break;
            }
            i = (i + 1);
        }
        return result;
    }
    const char* result = "";
    long i = 0;
    while ((i < hexa_str_len(rest))) {
        long ch = rest[i];
        if (((((ch == ",") || (ch == "}")) || (ch == " ")) || (ch == "\n"))) {
            break;
        }
        result = hexa_concat(result, hexa_int_to_str((long)(ch)));
        i = (i + 1);
    }
    return result;
}

long extract_int(const char* line, const char* field) {
    const char* s = extract_field(line, field);
    if ((strcmp(s, "") == 0)) {
        return 0;
    }
    const char* num_str = "";
    long i = 0;
    long cont = 1;
    while (((i < hexa_str_len(s)) && cont)) {
        long ch = s[i];
        if (((((((((((ch == "0") || (ch == "1")) || (ch == "2")) || (ch == "3")) || (ch == "4")) || (ch == "5")) || (ch == "6")) || (ch == "7")) || (ch == "8")) || (ch == "9"))) {
            num_str = hexa_concat(num_str, hexa_int_to_str((long)(ch)));
        } else {
            cont = 0;
        }
        i = (i + 1);
    }
    if ((strcmp(num_str, "") == 0)) {
        return 0;
    }
    return hexa_to_int_str(num_str);
}

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    } else {
        return x;
    }
}

const char* n6_match_grade(double value) {
    if ((abs_f((value - 6.0)) < 0.0001)) {
        return "EXACT";
    }
    if ((abs_f((value - 2.0)) < 0.0001)) {
        return "EXACT";
    }
    if ((abs_f((value - 4.0)) < 0.0001)) {
        return "EXACT";
    }
    if ((abs_f((value - 12.0)) < 0.0001)) {
        return "EXACT";
    }
    if ((abs_f((value - 5.0)) < 0.0001)) {
        return "EXACT";
    }
    if ((abs_f((value - 7.0)) < 0.0001)) {
        return "EXACT";
    }
    if ((abs_f((value - 24.0)) < 0.0001)) {
        return "EXACT";
    }
    if ((abs_f((value - 144.0)) < 0.0001)) {
        return "EXACT";
    }
    double rel = (abs_f((value - 137.036)) / 137.036);
    if ((rel < 0.01)) {
        return "NEAR";
    }
    rel = (abs_f((value - 1836.15)) / 1836.15);
    if ((rel < 0.01)) {
        return "NEAR";
    }
    rel = (abs_f((value - 0.3153)) / 0.3153);
    if ((rel < 0.05)) {
        return "NEAR";
    }
    rel = (abs_f((value - 0.6847)) / 0.6847);
    if ((rel < 0.01)) {
        return "NEAR";
    }
    return "";
}

const char* now_ts() {
    const char* raw = hexa_exec("date +%Y-%m-%dT%H:%M:%S");
    return hexa_trim(raw);
}

hexa_arr load_graph_node_ids() {
    hexa_arr ids = hexa_arr_new();
    if ((!hexa_file_exists(graph_path))) {
        return ids;
    }
    const char* content = hexa_read_file(graph_path);
    hexa_arr lines = hexa_split(content, "\n");
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((hexa_contains(line, "\"type\":\"node\"") || hexa_contains(line, "\"type\": \"node\""))) {
            const char* id = extract_field(line, "id");
            if ((strcmp(id, "") != 0)) {
                ids = hexa_arr_concat(ids, hexa_arr_lit((long[]){(long)(id)}, 1));
            }
        }
        i = (i + 1);
    }
    return ids;
}

hexa_arr load_recent_log(long n) {
    hexa_arr lines_out = hexa_arr_new();
    if ((!hexa_file_exists(log_path))) {
        return lines_out;
    }
    const char* raw = hexa_exec("tail");
    hexa_arr lines = hexa_split(raw, "\n");
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if (((strcmp(line, "") != 0) && hexa_starts_with(line, "{"))) {
            lines_out = hexa_arr_concat(lines_out, hexa_arr_lit((long[]){(long)(line)}, 1));
        }
        i = (i + 1);
    }
    return lines_out;
}

hexa_arr scan_unabsorbed() {
    hexa_arr graph_ids = load_graph_node_ids();
    hexa_arr recent = load_recent_log(scan_tail);
    hexa_arr unabsorbed = hexa_arr_new();
    long ri = 0;
    while ((ri < recent.n)) {
        long line = recent.d[ri];
        const char* constant = extract_field(line, "constant");
        const char* source = extract_field(line, "source");
        long found = 0;
        long gi = 0;
        while ((gi < graph_ids.n)) {
            if ((graph_ids.d[gi] == constant)) {
                found = 1;
                break;
            }
            if ((hexa_contains(graph_ids.d[gi], constant) && (hexa_str_len(constant) > 2))) {
                found = 1;
                break;
            }
            gi = (gi + 1);
        }
        if (((!found) && (strcmp(constant, "") != 0))) {
            unabsorbed = hexa_arr_concat(unabsorbed, hexa_arr_lit((long[]){(long)(line)}, 1));
        }
        ri = (ri + 1);
    }
    return unabsorbed;
}

const char* value_to_n6_node(const char* value) {
    long i = 0;
    while ((i < 7)) {
        if ((strcmp(((const char*)n6_values.d[i]), value) == 0)) {
            return hexa_concat("n6-", ((const char*)n6_constants.d[i]));
        }
        i = (i + 1);
    }
    return "";
}

const char* make_node_id(const char* source, const char* constant) {
    const char* prefix = ((hexa_contains(source, "hook")) ? ("hook") : (hexa_concat(hexa_concat(prefix, "-"), constant)));
    /* unsupported stmt */
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    n6_constants = hexa_arr_lit((long[]){(long)("n"), (long)("sigma"), (long)("phi"), (long)("tau"), (long)("J2"), (long)("sopfr"), (long)("M3")}, 7);
    n6_values = hexa_arr_lit((long[]){(long)("6"), (long)("12"), (long)("2"), (long)("4"), (long)("24"), (long)("5"), (long)("7")}, 7);
    return 0;
}
