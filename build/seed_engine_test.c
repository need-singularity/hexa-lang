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
double safe_float(const char* s);
const char* json_str(const char* line, const char* field);
long pipe_contains(const char* pipe, const char* val);
long pipe_count(const char* pipe);
const char* load_discovery();
const char* load_atlas();
const char* merge_all();

static const char* _se_home;
static const char* _se_shared;
static hexa_arr a;
static const char* mode;

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    }
    return x;
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

long pipe_count(const char* pipe) {
    if ((hexa_str_len(pipe) == 0)) {
        return 0;
    }
    return (hexa_split(pipe, "|")).n;
}

const char* load_discovery() {
    const char* path = hexa_concat(_se_shared, "/discovery_log.jsonl");
    if ((!hexa_file_exists(path))) {
        return "";
    }
    const char* content = "";
    content = hexa_exec(hexa_concat("tail -n 3000 ", path));
    hexa_arr lines = hexa_split(content, "\n");
    const char* result = "";
    long count = 0;
    long i = 0;
    while (((i < lines.n) && (count < 30))) {
        const char* line = ((const char*)lines.d[i]);
        if (hexa_contains(line, "\"EXACT\"")) {
            const char* val = json_str(line, "value");
            if ((hexa_str_len(val) > 0)) {
                double fv = safe_float(val);
                if (((fv > 0.001) && (fv < 100000.0))) {
                    const char* sv = hexa_float_to_str((double)(fv));
                    if ((!pipe_contains(result, sv))) {
                        if ((hexa_str_len(result) > 0)) {
                            result = hexa_concat(result, "|");
                        }
                        result = hexa_concat(result, sv);
                        count = (count + 1);
                    }
                }
            }
        }
        i = (i + 1);
    }
    return result;
}

const char* load_atlas() {
    const char* path = hexa_concat(_se_shared, "/math_atlas.json");
    if ((!hexa_file_exists(path))) {
        return "";
    }
    const char* result = "";
    long count = 0;
    const char* raw = hexa_exec(hexa_concat(hexa_concat("python3 -c \"import json,re; d=json.load(open('", path), "')); nums=set(); [nums.update(float(m) for m in re.findall(r'(?<!\\\\w)(\\\\d+\\\\.?\\\\d*)', str(e.get('title','')))) for e in d.get('hypotheses',[])]; print('|'.join(str(int(v)) if v==int(v) else str(v) for v in sorted(nums) if 1.5<v<100000)[:50])\""));
    if ((hexa_str_len(raw) > 0)) {
        result = raw;
        count = pipe_count(result);
    }
    if ((count == 0)) {
        const char* content = hexa_read_file(path);
        hexa_arr parts = hexa_split(content, "\"title\":\"");
        long i = 1;
        while (((i < parts.n) && (count < 30))) {
            hexa_arr title_end = hexa_split(((const char*)parts.d[i]), "\"");
            if ((title_end.n > 0)) {
                hexa_arr tokens = hexa_split(((const char*)title_end.d[0]), " ");
                long ti = 0;
                while (((ti < tokens.n) && (count < 30))) {
                    double v = hexa_to_float(((const char*)tokens.d[ti]));
                    if (((v > 1.5) && (v < 100000.0))) {
                        const char* sv = hexa_float_to_str((double)(v));
                        if ((!pipe_contains(result, sv))) {
                            if ((hexa_str_len(result) > 0)) {
                                result = hexa_concat(result, "|");
                            }
                            result = hexa_concat(result, sv);
                            count = (count + 1);
                        }
                    }
                    ti = (ti + 1);
                }
            }
            i = (i + 1);
        }
    }
    return result;
}

const char* merge_all() {
    const char* result = "6|12|2|4|5|24|7";
    const char* disc = load_discovery();
    if ((hexa_str_len(disc) > 0)) {
        hexa_arr dparts = hexa_split(disc, "|");
        long di = 0;
        while (((di < dparts.n) && (pipe_count(result) < 50))) {
            if ((!pipe_contains(result, ((const char*)dparts.d[di])))) {
                result = hexa_concat(hexa_concat(result, "|"), ((const char*)dparts.d[di]));
            }
            di = (di + 1);
        }
    }
    const char* atlas = load_atlas();
    if ((hexa_str_len(atlas) > 0)) {
        hexa_arr aparts = hexa_split(atlas, "|");
        long ai = 0;
        while (((ai < aparts.n) && (pipe_count(result) < 50))) {
            if ((!pipe_contains(result, ((const char*)aparts.d[ai])))) {
                result = hexa_concat(hexa_concat(result, "|"), ((const char*)aparts.d[ai]));
            }
            ai = (ai + 1);
        }
    }
    const char* psi_path = hexa_concat(_se_shared, "/consciousness_laws.json");
    if (hexa_file_exists(psi_path)) {
        const char* psi_content = hexa_read_file(psi_path);
        hexa_arr vparts = hexa_split(psi_content, "\"value\":");
        long pi = 1;
        while (((pi < vparts.n) && (pipe_count(result) < 60))) {
            const char* vstr = ((const char*)(hexa_split(((const char*)(hexa_split(((const char*)vparts.d[pi]), ",")).d[0]), "}")).d[0]);
            double v = safe_float(vstr);
            if (((v > 0.001) && (v < 100000.0))) {
                const char* sv = hexa_float_to_str((double)(v));
                if ((!pipe_contains(result, sv))) {
                    result = hexa_concat(hexa_concat(result, "|"), sv);
                }
            }
            pi = (pi + 1);
        }
    }
    const char* sedi_path = hexa_concat(_se_shared, "/sedi-grades.json");
    if (hexa_file_exists(sedi_path)) {
        const char* sedi_content = hexa_read_file(sedi_path);
        hexa_arr sparts = hexa_split(sedi_content, "\"score\":");
        long si = 1;
        while (((si < sparts.n) && (pipe_count(result) < 70))) {
            const char* vstr = ((const char*)(hexa_split(((const char*)(hexa_split(((const char*)sparts.d[si]), ",")).d[0]), "}")).d[0]);
            double v = safe_float(vstr);
            if (((v > 0.01) && (v < 10000.0))) {
                const char* sv = hexa_float_to_str((double)(v));
                if ((!pipe_contains(result, sv))) {
                    result = hexa_concat(hexa_concat(result, "|"), sv);
                }
            }
            si = (si + 1);
        }
    }
    const char* anima_path = hexa_concat(_se_shared, "/anima_seeds.json");
    if (hexa_file_exists(anima_path)) {
        const char* anima_content = hexa_read_file(anima_path);
        hexa_arr aparts = hexa_split(anima_content, "\"value\":");
        long ai = 1;
        while (((ai < aparts.n) && (pipe_count(result) < 80))) {
            const char* vstr = ((const char*)(hexa_split(((const char*)(hexa_split(((const char*)aparts.d[ai]), ",")).d[0]), "}")).d[0]);
            double v = safe_float(vstr);
            if (((v > 0.001) && (v < 100000.0))) {
                const char* sv = hexa_float_to_str((double)(v));
                if ((!pipe_contains(result, sv))) {
                    result = hexa_concat(hexa_concat(result, "|"), sv);
                }
            }
            ai = (ai + 1);
        }
    }
    const char* dom_path = hexa_concat(_se_shared, "/domain_seeds.jsonl");
    if (hexa_file_exists(dom_path)) {
        const char* dom_content = hexa_read_file(dom_path);
        hexa_arr dlines = hexa_split(dom_content, "\n");
        long dli = 0;
        while (((dli < dlines.n) && (pipe_count(result) < 100))) {
            const char* dline = ((const char*)dlines.d[dli]);
            if (hexa_contains(dline, "\"value\":")) {
                hexa_arr vparts = hexa_split(dline, "\"value\":");
                if ((vparts.n > 1)) {
                    const char* vstr = ((const char*)(hexa_split(((const char*)(hexa_split(((const char*)vparts.d[1]), ",")).d[0]), "}")).d[0]);
                    double v = safe_float(vstr);
                    if (((v > 0.001) && (v < 100000.0))) {
                        const char* sv = hexa_float_to_str((double)(v));
                        if ((!pipe_contains(result, sv))) {
                            result = hexa_concat(hexa_concat(result, "|"), sv);
                        }
                    }
                }
            }
            dli = (dli + 1);
        }
    }
    return result;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    _se_home = hexa_exec("printenv HOME");
    _se_shared = hexa_concat(_se_home, "/Dev/nexus/shared");
    a = hexa_args();
    mode = (((a.n >= 3)) ? (((const char*)a.d[2])) : ("info"));
    if ((strcmp(mode, "discovery") == 0)) {
        const char* d = load_discovery();
        if ((hexa_str_len(d) > 0)) {
            printf("%s\n", d);
        } else {
            printf("%s\n", "(empty)");
        }
    }
    if ((strcmp(mode, "atlas") == 0)) {
        const char* at = load_atlas();
        if ((hexa_str_len(at) > 0)) {
            printf("%s\n", at);
        } else {
            printf("%s\n", "(empty)");
        }
    }
    if ((strcmp(mode, "merge") == 0)) {
        printf("%s\n", merge_all());
    }
    if ((strcmp(mode, "info") == 0)) {
        const char* disc = load_discovery();
        const char* atlas = load_atlas();
        const char* merged = merge_all();
        long anima_n = 0;
        const char* anima_path2 = hexa_concat(_se_shared, "/anima_seeds.json");
        if (hexa_file_exists(anima_path2)) {
            const char* ac = hexa_read_file(anima_path2);
            hexa_arr ap = hexa_split(ac, "\"value\":");
            anima_n = (ap.n - 1);
        }
        printf("%s\n", "========================================");
        printf("%s\n", "  SEED ENGINE -- Dynamic Axiom Loader");
        printf("%s\n", "========================================");
        printf("%s\n", "  base n=6      : 7 constants");
        printf("%s\n", hexa_concat(hexa_concat("  discovery_log : ", hexa_int_to_str((long)(pipe_count(disc)))), " EXACT values"));
        printf("%s\n", hexa_concat(hexa_concat("  math_atlas    : ", hexa_int_to_str((long)(pipe_count(atlas)))), " hypothesis values"));
        printf("%s\n", hexa_concat(hexa_concat("  anima seeds   : ", hexa_int_to_str((long)(anima_n))), " cross-pollinated"));
        printf("%s\n", hexa_concat(hexa_concat("  merged total  : ", hexa_int_to_str((long)(pipe_count(merged)))), " unique seeds"));
        printf("%s\n", "========================================");
        printf("%s\n", "");
        printf("%s\n", hexa_concat("  discovery: ", disc));
        printf("%s\n", hexa_concat("  atlas:     ", atlas));
        printf("%s\n", hexa_concat("  merged:    ", merged));
    }
    return 0;
}
