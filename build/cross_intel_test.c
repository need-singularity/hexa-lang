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

const char* n6_constant_names();
const char* n6_constant_values();
double n6_lookup(const char* name);
const char* hit_to_string(long *h);
double confidence_from_count(long count);
double check_resonance(double value, double constant, double tolerance);
const char* parse_domain_line(const char* line);
const char* parse_values_from_line(const char* line);
const char* find_resonances_from_file(const char* path, double tolerance);
const char* ref_to_string(long *r);
const char* load_bridge(const char* path);
const char* bridge_report(const char* path);
const char* compare_domain_files(const char* paths);
long hexa_user_main();

static long N = 6;
static long sigma = 12;
static long phi = 2;
static long tau = 4;
static long sopfr = 5;
static long j2 = 24;
static double default_tolerance = 0.001;
static long min_domains = 2;
static const char* shared_dir = "shared";
static const char* bridge_file = "shared/bridge_state.json";

const char* n6_constant_names() {
    return "n|sigma|phi|tau|j2|sopfr|sigma-phi|sigma-tau|sigma*tau|sigma^2|j2-tau";
}

const char* n6_constant_values() {
    return "6|12|2|4|24|5|10|8|48|144|20";
}

double n6_lookup(const char* name) {
    long names = split(n6_constant_names(), "|");
    long vals = split(n6_constant_values(), "|");
    long i = 0;
    while ((i < hexa_str_len(names))) {
        if ((names[i] == name)) {
            return ((double)(vals[i]));
        }
        i = (i + 1);
    }
    return 0.0;
}

const char* hit_to_string(long *h) {
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[", hexa_int_to_str((long)(h[0]))), "="), hexa_int_to_str((long)(h[1]))), " domains="), hexa_int_to_str((long)(h[3]))), " conf="), hexa_int_to_str((long)(h[4]))), " ("), hexa_int_to_str((long)(h[2]))), ")]");
}

double confidence_from_count(long count) {
    if ((count >= 5)) {
        return 1.0;
    }
    if ((count >= 3)) {
        return 0.8;
    }
    if ((count >= 2)) {
        return 0.5;
    }
    return 0.0;
}

double check_resonance(double value, double constant, double tolerance) {
    if ((constant == 0.0)) {
        return (double)(0);
    }
    double error = (value - constant);
    if ((error < 0.0)) {
        error = (0.0 - error);
    }
    error = (error / constant);
    if ((error < 0.0)) {
        error = (0.0 - error);
    }
    return (error < tolerance);
}

const char* parse_domain_line(const char* line) {
    long dpos = find(line, "\"domain\"");
    if ((dpos < 0)) {
        return "";
    }
    long colon_pos = find_from(line, ":", (dpos + 8));
    long q1 = find_from(line, "\"", (colon_pos + 1));
    long q2 = find_from(line, "\"", (q1 + 1));
    long domain = substring(line, (q1 + 1), q2);
    return "";
}

const char* parse_values_from_line(const char* line) {
    long bstart = find(line, "[");
    long bend = find(line, "]");
    if ((bstart < 0)) {
        return "";
    }
    if ((bend < 0)) {
        return "";
    }
    return "";
}

const char* find_resonances_from_file(const char* path, double tolerance) {
    const char* content = "";
    content = hexa_read_file(path);
    long lines = split(content, "\n");
    long c_names = split(n6_constant_names(), "|");
    long c_vals = split(n6_constant_values(), "|");
    hexa_arr match_domains = hexa_arr_new();
    long ci = 0;
    while ((ci < hexa_str_len(c_names))) {
        match_domains = hexa_arr_push(match_domains, (long)(""));
        ci = (ci + 1);
    }
    long li = 0;
    while ((li < hexa_str_len(lines))) {
        long line = trim(lines[li]);
        if ((hexa_str_len(line) == 0)) {
            li = (li + 1);
            continue;
        }
        const char* domain = parse_domain_line(line);
        if ((hexa_str_len(domain) == 0)) {
            li = (li + 1);
            continue;
        }
        const char* vals_str = parse_values_from_line(line);
        if ((hexa_str_len(vals_str) == 0)) {
            li = (li + 1);
            continue;
        }
        long vals = split(vals_str, ",");
        long vi = 0;
        while ((vi < hexa_str_len(vals))) {
            double v = 0.0;
            v = ((double)(trim(vals[vi])));
            long ki = 0;
            while ((ki < hexa_str_len(c_names))) {
                double cv = 0.0;
                cv = ((double)(c_vals[ki]));
                if (check_resonance(v, cv, tolerance)) {
                    const char* existing = ((const char*)match_domains.d[ki]);
                    if ((find(existing, domain) < 0)) {
                        if ((hexa_str_len(existing) == 0)) {
                            match_domains.d[ki] = (long)(domain);
                        } else {
                            match_domains.d[ki] = (long)(hexa_concat(hexa_concat(existing, "|"), domain));
                        }
                    }
                }
                ki = (ki + 1);
            }
            vi = (vi + 1);
        }
        li = (li + 1);
    }
    hexa_arr results = hexa_arr_new();
    long ri = 0;
    while ((ri < hexa_str_len(c_names))) {
        const char* doms = ((const char*)match_domains.d[ri]);
        if ((hexa_str_len(doms) > 0)) {
            long dom_parts = split(doms, "|");
            if ((hexa_str_len(dom_parts) >= min_domains)) {
                double cv = 0.0;
                cv = ((double)(c_vals[ri]));
                double conf = confidence_from_count(hexa_str_len(dom_parts));
                long hit[5] = {c_names[ri], cv, doms, hexa_str_len(dom_parts), conf};
                results = hexa_arr_push(results, hit);
            }
        }
        ri = (ri + 1);
    }
    const char* out = "";
    long oi = 0;
    while ((oi < results.n)) {
        out = hexa_concat(hexa_concat(out, hit_to_string(results.d[oi])), "\n");
        oi = (oi + 1);
    }
    return out;
}

const char* ref_to_string(long *r) {
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(r[0])), "\t"), hexa_int_to_str((long)(r[2]))), "\t"), hexa_int_to_str((long)(r[1]))), "\trel="), hexa_int_to_str((long)(r[3])));
}

const char* load_bridge(const char* path) {
    const char* content = "";
    content = hexa_read_file(path);
    return content;
}

const char* bridge_report(const char* path) {
    const char* content = load_bridge(path);
    if ((hexa_str_len(content) == 0)) {
        return "bridge 비어있음";
    }
    long lines = split(content, "\n");
    const char* out = "=== Project Bridge Report ===\n";
    long count = 0;
    long li = 0;
    while ((li < hexa_str_len(lines))) {
        long line = trim(lines[li]);
        if ((find(line, "\"project\"") >= 0)) {
            count = (count + 1);
            out = hexa_concat(hexa_concat(hexa_concat(out, "  "), hexa_int_to_str((long)(line))), "\n");
        }
        li = (li + 1);
    }
    out = hexa_concat(hexa_concat(hexa_concat(out, "Total refs: "), hexa_int_to_str((long)(count))), "\n");
    return out;
}

const char* compare_domain_files(const char* paths) {
    long file_paths = split(paths, "|");
    if ((hexa_str_len(file_paths) < 2)) {
        return "[ERROR] 최소 2개 도메인 파일 필요";
    }
    const char* combined = "";
    long fi = 0;
    while ((fi < hexa_str_len(file_paths))) {
        const char* fc = hexa_read_file(trim(file_paths[fi]));
        combined = hexa_concat(hexa_concat(combined, fc), "\n");
        fi = (fi + 1);
    }
    const char* tmp = "/tmp/n6_cross_intel_combined.jsonl";
    hexa_write_file(tmp, combined);
    return find_resonances_from_file(tmp, default_tolerance);
}

long hexa_user_main() {
    hexa_arr argv = hexa_args();
    if ((argv.n < 2)) {
        printf("%s\n", "cross_intel.hexa — 교차 도메인 공명 탐지 (n=6)");
        printf("%s\n", "");
        printf("%s\n", "Usage:");
        printf("%s\n", "  hexa cross_intel.hexa scan <file.jsonl>          — 파일에서 공명 탐색");
        printf("%s\n", "  hexa cross_intel.hexa resonance <d1> <d2> [d3..] — 도메인 파일 비교");
        printf("%s\n", "  hexa cross_intel.hexa bridge [path]              — 프로젝트 브릿지 리포트");
        printf("%s\n", "");
        printf("%s\n", "scan 입력 형식 (JSONL):");
        printf("%s\n", "  {\"domain\":\"AI\",\"values\":[12.0,24.0]}");
        printf("%s\n", "  {\"domain\":\"chip\",\"values\":[12.0,48.0]}");
        printf("%s\n", "");
        printf("%s\n", "N=6 공명 상수:");
        long names = split(n6_constant_names(), "|");
        long vals = split(n6_constant_values(), "|");
        long i = 0;
        while ((i < hexa_str_len(names))) {
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  ", hexa_int_to_str((long)(names[i]))), " = "), hexa_int_to_str((long)(vals[i]))));
            i = (i + 1);
        }
        return 0;
    }
    const char* cmd = ((const char*)argv.d[1]);
    if ((strcmp(cmd, "scan") == 0)) {
        if ((argv.n < 3)) {
            printf("%s\n", "[ERROR] 파일 경로 필요: hexa cross_intel.hexa scan <file>");
            return 1;
        }
        const char* path = ((const char*)argv.d[2]);
        double tolerance = default_tolerance;
        if ((argv.n >= 4)) {
            tolerance = hexa_to_float(((const char*)argv.d[3]));
        }
        printf("%s\n", "=== Cross-Domain Resonance Scan ===");
        printf("%s\n", hexa_concat("File: ", path));
        printf("%s\n", hexa_concat("Tolerance: ", hexa_float_to_str((double)(tolerance))));
        printf("%s\n", "");
        const char* result = find_resonances_from_file(path, tolerance);
        if ((hexa_str_len(result) == 0)) {
            printf("%s\n", "공명 없음 (2+ 도메인 교차 매칭 없음)");
        } else {
            printf("%s\n", result);
        }
    } else {
        if ((strcmp(cmd, "resonance") == 0)) {
            if ((argv.n < 4)) {
                printf("%s\n", "[ERROR] 최소 2개 도메인 파일 필요");
                return 1;
            }
            const char* paths = ((const char*)argv.d[2]);
            long ai = 3;
            while ((ai < argv.n)) {
                paths = hexa_concat(hexa_concat(paths, "|"), ((const char*)argv.d[ai]));
                ai = (ai + 1);
            }
            printf("%s\n", "=== Cross-Domain Resonance Compare ===");
            const char* result = compare_domain_files(paths);
            if ((hexa_str_len(result) == 0)) {
                printf("%s\n", "공명 없음");
            } else {
                printf("%s\n", result);
            }
        } else {
            if ((strcmp(cmd, "bridge") == 0)) {
                const char* bpath = bridge_file;
                if ((argv.n >= 3)) {
                    bpath = ((const char*)argv.d[2]);
                }
                printf("%s\n", bridge_report(bpath));
            } else {
                printf("%s\n", hexa_concat("[ERROR] 알 수 없는 명령: ", cmd));
                printf("%s\n", "  scan | resonance | bridge");
                return 1;
            }
        }
    }
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    hexa_user_main();
    return 0;
}
