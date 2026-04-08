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
hexa_arr parse_csv_floats(const char* csv);
const char* list_to_csv(hexa_arr lst);
hexa_arr sandbox_scale(const char* data, double factor);
hexa_arr sandbox_shift(const char* data, double offset);
hexa_arr sandbox_normalize(hexa_arr data);
hexa_arr sandbox_set(const char* data, long idx, double val);
hexa_arr sandbox_sort(const char* data);
hexa_arr sandbox_reverse(const char* data);
hexa_arr sandbox_modify(const char* data, const char* operation);
hexa_arr sandbox_diff(long original, long working);
long show_help();

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

hexa_arr parse_csv_floats(const char* csv) {
    hexa_arr parts = hexa_split(csv, ",");
    hexa_arr result = hexa_arr_new();
    long i = 0;
    while ((i < parts.n)) {
        result = hexa_arr_concat(result, hexa_arr_lit((long[]){(long)(safe_float(hexa_trim(((const char*)parts.d[i]))))}, 1));
        i = (i + 1);
    }
    return result;
}

const char* list_to_csv(hexa_arr lst) {
    const char* s = "";
    long i = 0;
    while ((i < lst.n)) {
        if ((i > 0)) {
            s = hexa_concat(s, ",");
        }
        s = hexa_concat(s, ((const char*)lst.d[i]));
        i = (i + 1);
    }
    return s;
}

hexa_arr sandbox_scale(const char* data, double factor) {
    hexa_arr result = hexa_arr_new();
    long i = 0;
    while ((i < hexa_str_len(data))) {
        result = hexa_arr_concat(result, hexa_arr_lit((long[]){(long)((((double)(data[i])) * factor))}, 1));
        i = (i + 1);
    }
    return result;
}

hexa_arr sandbox_shift(const char* data, double offset) {
    hexa_arr result = hexa_arr_new();
    long i = 0;
    while ((i < hexa_str_len(data))) {
        result = hexa_arr_concat(result, hexa_arr_lit((long[]){(long)((((double)(data[i])) + offset))}, 1));
        i = (i + 1);
    }
    return result;
}

hexa_arr sandbox_normalize(hexa_arr data) {
    if ((data.n == 0)) {
        return hexa_arr_new();
    }
    double min_val = ((double)(data.d[0]));
    double max_val = ((double)(data.d[0]));
    long i = 0;
    while ((i < data.n)) {
        double v = ((double)(data.d[i]));
        if ((v < min_val)) {
            min_val = v;
        }
        if ((v > max_val)) {
            max_val = v;
        }
        i = (i + 1);
    }
    double range = (max_val - min_val);
    if ((range <= 0.0)) {
        return data;
    }
    hexa_arr result = hexa_arr_new();
    long j = 0;
    while ((j < data.n)) {
        result = hexa_arr_concat(result, hexa_arr_lit((long[]){(long)(((((double)(data.d[j])) - min_val) / range))}, 1));
        j = (j + 1);
    }
    return result;
}

hexa_arr sandbox_set(const char* data, long idx, double val) {
    hexa_arr result = hexa_arr_new();
    long i = 0;
    while ((i < hexa_str_len(data))) {
        if ((i == idx)) {
            result = hexa_arr_concat(result, hexa_arr_lit((long[]){(long)(val)}, 1));
        } else {
            result = hexa_arr_concat(result, hexa_arr_lit((long[]){(long)(((double)(data[i])))}, 1));
        }
        i = (i + 1);
    }
    return result;
}

hexa_arr sandbox_sort(const char* data) {
    hexa_arr result = hexa_arr_new();
    long i = 0;
    while ((i < hexa_str_len(data))) {
        result = hexa_arr_concat(result, hexa_arr_lit((long[]){(long)(((double)(data[i])))}, 1));
        i = (i + 1);
    }
    long si = 0;
    while ((si < result.n)) {
        long sj = (si + 1);
        while ((sj < result.n)) {
            if ((((double)(result.d[sj])) < ((double)(result.d[si])))) {
                long tmp = result.d[si];
                result.d[si] = result.d[sj];
                result.d[sj] = tmp;
            }
            sj = (sj + 1);
        }
        si = (si + 1);
    }
    return result;
}

hexa_arr sandbox_reverse(const char* data) {
    hexa_arr result = hexa_arr_new();
    long i = (hexa_str_len(data) - 1);
    while ((i >= 0)) {
        result = hexa_arr_concat(result, hexa_arr_lit((long[]){(long)(((double)(data[i])))}, 1));
        i = (i - 1);
    }
    return result;
}

hexa_arr sandbox_modify(const char* data, const char* operation) {
    hexa_arr parts = hexa_split(operation, ":");
    const char* op = ((const char*)parts.d[0]);
    if ((strcmp(op, "scale") == 0)) {
        if ((parts.n >= 2)) {
            double factor = safe_float(((const char*)parts.d[1]));
            return sandbox_scale(data, factor);
        }
        return data;
    }
    if ((strcmp(op, "shift") == 0)) {
        if ((parts.n >= 2)) {
            double offset = safe_float(((const char*)parts.d[1]));
            return sandbox_shift(data, offset);
        }
        return data;
    }
    if ((strcmp(op, "normalize") == 0)) {
        return sandbox_normalize(data);
    }
    if ((strcmp(op, "set") == 0)) {
        if ((parts.n >= 3)) {
            long idx = (long)(safe_float(((const char*)parts.d[1])));
            double val = safe_float(((const char*)parts.d[2]));
            if ((idx >= 0)) {
                if ((idx < hexa_str_len(data))) {
                    return sandbox_set(data, idx, val);
                }
            }
        }
        return data;
    }
    if ((strcmp(op, "sort") == 0)) {
        return sandbox_sort(data);
    }
    if ((strcmp(op, "reverse") == 0)) {
        return sandbox_reverse(data);
    }
    printf("%s\n", hexa_concat("Unknown operation: ", operation));
    return data;
}

hexa_arr sandbox_diff(long original, long working) {
    hexa_arr diffs = hexa_arr_new();
    long max_len = (((hexa_str_len(original) > hexa_str_len(working))) ? (hexa_str_len(original)) : (hexa_str_len(working)));
    long i = 0;
    while ((i < max_len)) {
        double orig = (((i < hexa_str_len(original))) ? (((double)(original[i]))) : (0.0));
        double work = (((i < hexa_str_len(working))) ? (((double)(working[i]))) : (0.0));
        if ((abs_f((orig - work)) > 0.0000000000000002)) {
            diffs = hexa_arr_concat(diffs, hexa_arr_lit((long[]){(long)(hexa_arr_lit((long[]){(long)(i), (long)(orig), (long)(work)}, 3))}, 1));
        }
        i = (i + 1);
    }
    return diffs;
}

long show_help() {
    printf("%s\n", "sandbox.hexa — 샌드박스 실험 환경, copy-on-write 데이터 격리 (mk2 hexa)");
    printf("%s\n", "");
    printf("%s\n", "Usage:");
    printf("%s\n", "  hexa sandbox.hexa create <values_csv>");
    printf("%s\n", "  hexa sandbox.hexa modify <operation> <values_csv>");
    printf("%s\n", "  hexa sandbox.hexa diff <original_csv> <modified_csv>");
    printf("%s\n", "  hexa sandbox.hexa commit <values_csv>");
    printf("%s\n", "  hexa sandbox.hexa demo");
    printf("%s\n", "  hexa sandbox.hexa help");
    printf("%s\n", "");
    printf("%s\n", "Operations: scale:<f>, shift:<f>, normalize, set:<i>:<v>, sort, reverse");
    printf("%s\n", "");
    printf("%s\n", "Example:");
    printf("%s\n", "  hexa sandbox.hexa modify scale:2.0 1,2,3,4,5,6");
    return printf("%s\n", "  hexa sandbox.hexa modify normalize 0,5,10");
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    cli_args = hexa_args();
    if ((cli_args.n > 2)) {
        cmd = ((const char*)cli_args.d[2]);
    }
    if ((strcmp(cmd, "help") == 0)) {
        show_help();
    }
    if ((strcmp(cmd, "create") == 0)) {
        if ((cli_args.n < 4)) {
            printf("%s\n", "Error: create requires <values_csv>");
        } else {
            hexa_arr data = parse_csv_floats(((const char*)cli_args.d[3]));
            printf("%s\n", hexa_concat(hexa_concat("Sandbox created: ", hexa_int_to_str((long)(data.n))), " points"));
            printf("%s\n", hexa_concat("Data: ", list_to_csv(data)));
        }
    }
    if ((strcmp(cmd, "modify") == 0)) {
        if ((cli_args.n < 5)) {
            printf("%s\n", "Error: modify requires <operation> <values_csv>");
        } else {
            const char* operation = ((const char*)cli_args.d[3]);
            hexa_arr data = parse_csv_floats(((const char*)cli_args.d[4]));
            hexa_arr result = sandbox_modify(data, operation);
            printf("%s\n", hexa_concat("Operation: ", operation));
            printf("%s\n", hexa_concat("Original:  ", list_to_csv(data)));
            printf("%s\n", hexa_concat("Result:    ", list_to_csv(result)));
        }
    }
    if ((strcmp(cmd, "diff") == 0)) {
        if ((cli_args.n < 5)) {
            printf("%s\n", "Error: diff requires <original_csv> <modified_csv>");
        } else {
            hexa_arr original = parse_csv_floats(((const char*)cli_args.d[3]));
            hexa_arr modified = parse_csv_floats(((const char*)cli_args.d[4]));
            hexa_arr diffs = sandbox_diff(original, modified);
            if (((long)(((double)(diffs.n))) == 0)) {
                printf("%s\n", "No differences.");
            } else {
                printf("%s\n", hexa_concat(hexa_concat("Differences (", hexa_int_to_str((long)((long)(((double)(diffs.n)))))), "):"));
                long i = 0;
                while ((i < (long)(((double)(diffs.n))))) {
                    long d = diffs.d[i];
                    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  [", hexa_int_to_str((long)(d[0]))), "] "), hexa_int_to_str((long)(d[1]))), " -> "), hexa_int_to_str((long)(d[2]))));
                    i = (i + 1);
                }
            }
        }
    }
    if ((strcmp(cmd, "commit") == 0)) {
        if ((cli_args.n < 4)) {
            printf("%s\n", "Error: commit requires <values_csv>");
        } else {
            hexa_arr data = parse_csv_floats(((const char*)cli_args.d[3]));
            printf("%s\n", hexa_concat(hexa_concat("Committed ", hexa_int_to_str((long)(data.n))), " values:"));
            printf("%s\n", list_to_csv(data));
        }
    }
    if ((strcmp(cmd, "demo") == 0)) {
        printf("%s\n", "=== Sandbox Demo (n=6 data) ===");
        printf("%s\n", "");
        hexa_arr data = hexa_arr_lit((long[]){1.0, 2.0, 3.0, 4.0, 5.0, 6.0}, 6);
        printf("%s\n", hexa_concat("Original:   ", list_to_csv(data)));
        hexa_arr scaled = sandbox_modify(data, "scale:2.0");
        printf("%s\n", hexa_concat("scale:2.0   ", list_to_csv(scaled)));
        printf("%s\n", hexa_concat(hexa_concat("Original:   ", list_to_csv(data)), " (unchanged)"));
        hexa_arr shifted = sandbox_modify(data, "shift:10.0");
        printf("%s\n", hexa_concat("shift:10.0  ", list_to_csv(shifted)));
        hexa_arr normed = sandbox_modify(data, "normalize");
        printf("%s\n", hexa_concat("normalize:  ", list_to_csv(normed)));
        hexa_arr setted = sandbox_modify(data, "set:1:99.0");
        printf("%s\n", hexa_concat("set:1:99.0  ", list_to_csv(setted)));
        hexa_arr reversed = sandbox_modify(data, "reverse");
        printf("%s\n", hexa_concat("reverse:    ", list_to_csv(reversed)));
        hexa_arr sorted = sandbox_modify(reversed, "sort");
        printf("%s\n", hexa_concat("sort:       ", list_to_csv(sorted)));
        printf("%s\n", "");
        printf("%s\n", "Diff (original vs set:1:99.0):");
        hexa_arr diffs = sandbox_diff(data, setted);
        long i = 0;
        while ((i < (long)(((double)(diffs.n))))) {
            long d = diffs.d[i];
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  [", hexa_int_to_str((long)(d[0]))), "] "), hexa_int_to_str((long)(d[1]))), " -> "), hexa_int_to_str((long)(d[2]))));
            i = (i + 1);
        }
        printf("%s\n", "");
        printf("%s\n", "Chained: scale:2 -> shift:1 -> normalize");
        hexa_arr step1 = sandbox_modify(data, "scale:2.0");
        hexa_arr step2 = sandbox_modify(step1, "shift:1.0");
        hexa_arr step3 = sandbox_modify(step2, "normalize");
        printf("%s\n", hexa_concat("Result:     ", list_to_csv(step3)));
        printf("%s\n", "");
        printf("%s\n", "Demo complete. All operations are copy-on-write (original preserved).");
    }
    return 0;
}
