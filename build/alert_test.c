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

long level_priority(const char* level);
long level_valid(const char* level);
long* alert_new(const char* level, const char* source_lens, const char* pattern_id, double confidence, const char* message);
long* discovery_alert(const char* lens_name, const char* pattern, double confidence);
const char* alert_to_json(long *a);
const char* extract_json_str(const char* json, const char* key);
const char* extract_json_num(const char* json, const char* key);
long* parse_alert_line(const char* line);
const char* ingest_alert(long *a);
long filter_by_level(const char* min_level);
long show_prioritized();
long set_threshold(const char* metric, double value);
double get_threshold(const char* metric);
long check_alerts();
const char* hexa_user_main();

static const char* alert_log = "shared/alert_history.jsonl";
static const char* threshold_file = "shared/alert_thresholds.json";

long level_priority(const char* level) {
    if ((strcmp(level, "INFO") == 0)) {
        return 0;
    } else if ((strcmp(level, "WARNING") == 0)) {
        return 1;
    } else if ((strcmp(level, "CRITICAL") == 0)) {
        return 2;
    } else if ((strcmp(level, "DISCOVERY") == 0)) {
        return 3;
    } else {
        return (-1);
    }



}

long level_valid(const char* level) {
    return (level_priority(level) >= 0);
}

long* alert_new(const char* level, const char* source_lens, const char* pattern_id, double confidence, const char* message) {
    double conf = confidence;
    if ((conf < 0.0)) {
        conf = 0.0;
    }
    if ((conf > 1.0)) {
        conf = 1.0;
    }
    return hexa_struct_alloc((long[]){(long)(level), (long)(source_lens), (long)(pattern_id), (long)(conf), (long)(hexa_int_to_str((long)(hexa_time_unix()))), (long)(message)}, 6);
}

long* discovery_alert(const char* lens_name, const char* pattern, double confidence) {
    const char* msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("Discovery by lens '", lens_name), "': pattern '"), pattern), "' (confidence "), hexa_float_to_str((double)(confidence))), ")");
    return alert_new("DISCOVERY", lens_name, pattern, confidence, msg);
}

const char* alert_to_json(long *a) {
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"level\":\"", hexa_int_to_str((long)(a[0]))), "\",\"source\":\""), hexa_int_to_str((long)(a[1]))), "\",\"pattern\":\""), hexa_int_to_str((long)(a[2]))), "\",\"confidence\":"), hexa_int_to_str((long)(a[3]))), ",\"timestamp\":\""), hexa_int_to_str((long)(a[4]))), "\",\"message\":\""), hexa_int_to_str((long)(a[5]))), "\"}");
}

const char* extract_json_str(const char* json, const char* key) {
    const char* needle = hexa_concat(hexa_concat("\"", key), "\":\"");
    long idx = hexa_index_of(json, needle);
    if ((idx < 0)) {
        return "";
    }
    long start = (idx + hexa_str_len(needle));
    const char* rest = hexa_substr(json, start, hexa_str_len(json));
    long end = hexa_index_of(rest, "\"");
    if ((end < 0)) {
        return "";
    }
    return hexa_substr(rest, 0, end);
}

const char* extract_json_num(const char* json, const char* key) {
    const char* needle = hexa_concat(hexa_concat("\"", key), "\":");
    long idx = hexa_index_of(json, needle);
    if ((idx < 0)) {
        return "0";
    }
    long start = (idx + hexa_str_len(needle));
    const char* rest = hexa_substr(json, start, hexa_str_len(json));
    long end_c = hexa_index_of(rest, ",");
    long end_b = hexa_index_of(rest, "}");
    long end = hexa_str_len(rest);
    if ((end_c >= 0)) {
        end = end_c;
    }
    if ((end_b >= 0)) {
        if ((end_b < end)) {
            end = end_b;
        }
    }
    return hexa_trim(hexa_substr(rest, 0, end));
}

long* parse_alert_line(const char* line) {
    const char* level = extract_json_str(line, "level");
    const char* source = extract_json_str(line, "source");
    const char* pattern = extract_json_str(line, "pattern");
    const char* timestamp = extract_json_str(line, "timestamp");
    const char* message = extract_json_str(line, "message");
    double confidence = 0.0;
    confidence = hexa_to_float(extract_json_num(line, "confidence"));
    return hexa_struct_alloc((long[]){(long)(level), (long)(source), (long)(pattern), (long)(confidence), (long)(timestamp), (long)(message)}, 6);
}

const char* ingest_alert(long *a) {
    const char* raw = hexa_read_file(alert_log);
    hexa_arr lines = hexa_arr_new();
    long replaced = 0;
    if ((strcmp(raw, "") != 0)) {
        hexa_arr parts = hexa_split(raw, "\n");
        long i = 0;
        while ((i < parts.n)) {
            const char* line = hexa_trim(((const char*)parts.d[i]));
            if ((strcmp(line, "") != 0)) {
                long* existing = parse_alert_line(line);
                if ((existing[1] == a[1])) {
                    if ((existing[2] == a[2])) {
                        if ((a[3] > existing[3])) {
                            lines = hexa_arr_push(lines, (long)(alert_to_json(a)));
                        } else {
                            lines = hexa_arr_push(lines, (long)(line));
                        }
                        replaced = 1;
                    } else {
                        lines = hexa_arr_push(lines, (long)(line));
                    }
                } else {
                    lines = hexa_arr_push(lines, (long)(line));
                }
            }
            i = (i + 1);
        }
    }
    if ((!replaced)) {
        lines = hexa_arr_push(lines, (long)(alert_to_json(a)));
    }
    const char* out = "";
    long j = 0;
    while ((j < lines.n)) {
        out = hexa_concat(hexa_concat(out, ((const char*)lines.d[j])), "\n");
        j = (j + 1);
    }
    hexa_write_file(alert_log, out);
    const char* label = hexa_concat(hexa_concat(hexa_concat("[", hexa_int_to_str((long)(a[0]))), "] "), hexa_int_to_str((long)(a[5])));
    printf("%s", label);
    return label;
}

long filter_by_level(const char* min_level) {
    long min_pri = level_priority(min_level);
    const char* raw = hexa_read_file(alert_log);
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "(no alerts)");
        return 0;
    }
    hexa_arr lines = hexa_split(raw, "\n");
    long count = 0;
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") != 0)) {
            long* a = parse_alert_line(line);
            if ((level_priority(a[0]) >= min_pri)) {
                printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[", hexa_int_to_str((long)(a[0]))), "] "), hexa_int_to_str((long)(a[1]))), "/"), hexa_int_to_str((long)(a[2]))), " conf="), hexa_int_to_str((long)(a[3]))), " — "), hexa_int_to_str((long)(a[5]))));
                count = (count + 1);
            }
        }
        i = (i + 1);
    }
    return count;
}

long show_prioritized() {
    const char* raw = hexa_read_file(alert_log);
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "(no alerts)");
        return 0;
    }
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr disc = hexa_arr_new();
    hexa_arr crit = hexa_arr_new();
    hexa_arr warn = hexa_arr_new();
    hexa_arr info = hexa_arr_new();
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") != 0)) {
            long* a = parse_alert_line(line);
            if ((a[0] == "DISCOVERY")) {
                disc = hexa_arr_push(disc, (long)(line));
            } else if ((a[0] == "CRITICAL")) {
                crit = hexa_arr_push(crit, (long)(line));
            } else if ((a[0] == "WARNING")) {
                warn = hexa_arr_push(warn, (long)(line));
            } else {
                info = hexa_arr_push(info, (long)(line));
            }


        }
        i = (i + 1);
    }
    long count = 0;
    long j = 0;
    while ((j < disc.n)) {
        long* a = parse_alert_line(((const char*)disc.d[j]));
        printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[DISCOVERY] ", hexa_int_to_str((long)(a[1]))), "/"), hexa_int_to_str((long)(a[2]))), " conf="), hexa_int_to_str((long)(a[3]))));
        j = (j + 1);
        count = (count + 1);
    }
    j = 0;
    while ((j < crit.n)) {
        long* a = parse_alert_line(((const char*)crit.d[j]));
        printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[CRITICAL] ", hexa_int_to_str((long)(a[1]))), "/"), hexa_int_to_str((long)(a[2]))), " conf="), hexa_int_to_str((long)(a[3]))));
        j = (j + 1);
        count = (count + 1);
    }
    j = 0;
    while ((j < warn.n)) {
        long* a = parse_alert_line(((const char*)warn.d[j]));
        printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[WARNING] ", hexa_int_to_str((long)(a[1]))), "/"), hexa_int_to_str((long)(a[2]))), " conf="), hexa_int_to_str((long)(a[3]))));
        j = (j + 1);
        count = (count + 1);
    }
    j = 0;
    while ((j < info.n)) {
        long* a = parse_alert_line(((const char*)info.d[j]));
        printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[INFO] ", hexa_int_to_str((long)(a[1]))), "/"), hexa_int_to_str((long)(a[2]))), " conf="), hexa_int_to_str((long)(a[3]))));
        j = (j + 1);
        count = (count + 1);
    }
    return count;
}

long set_threshold(const char* metric, double value) {
    const char* raw = hexa_read_file(threshold_file);
    const char* entry = hexa_concat(hexa_concat(hexa_concat("\"", metric), "\":"), hexa_float_to_str((double)(value)));
    hexa_arr pairs = hexa_arr_new();
    long found = 0;
    if ((strcmp(raw, "") != 0)) {
        const char* inner = hexa_trim(hexa_replace(hexa_replace(raw, "{", ""), "}", ""));
        if ((strcmp(inner, "") != 0)) {
            hexa_arr parts = hexa_split(inner, ",");
            long i = 0;
            while ((i < parts.n)) {
                const char* p = hexa_trim(((const char*)parts.d[i]));
                if ((strcmp(p, "") != 0)) {
                    long colon = hexa_index_of(p, ":");
                    if ((colon > 0)) {
                        const char* k = hexa_replace(hexa_trim(hexa_substr(p, 0, colon)), "\"", "");
                        if ((strcmp(k, metric) == 0)) {
                            pairs = hexa_arr_push(pairs, (long)(entry));
                            found = 1;
                        } else {
                            pairs = hexa_arr_push(pairs, (long)(p));
                        }
                    }
                }
                i = (i + 1);
            }
        }
    }
    if ((!found)) {
        pairs = hexa_arr_push(pairs, (long)(entry));
    }
    const char* json = "{";
    long j = 0;
    while ((j < pairs.n)) {
        if ((j > 0)) {
            json = hexa_concat(json, ",");
        }
        json = hexa_concat(json, ((const char*)pairs.d[j]));
        j = (j + 1);
    }
    json = hexa_concat(json, "}");
    hexa_write_file(threshold_file, json);
    return printf("%s", hexa_concat(hexa_concat(hexa_concat("threshold set: ", metric), " = "), hexa_float_to_str((double)(value))));
}

double get_threshold(const char* metric) {
    const char* raw = hexa_read_file(threshold_file);
    if ((strcmp(raw, "") == 0)) {
        return (-1.0);
    }
    const char* needle = hexa_concat(hexa_concat("\"", metric), "\":");
    long idx = hexa_index_of(raw, needle);
    if ((idx < 0)) {
        return (-1.0);
    }
    long start = (idx + hexa_str_len(needle));
    const char* rest = hexa_substr(raw, start, hexa_str_len(raw));
    long end_c = hexa_index_of(rest, ",");
    long end_b = hexa_index_of(rest, "}");
    long end = hexa_str_len(rest);
    if ((end_c >= 0)) {
        end = end_c;
    }
    if ((end_b >= 0)) {
        if ((end_b < end)) {
            end = end_b;
        }
    }
    const char* val_str = hexa_trim(hexa_substr(rest, 0, end));
    double val = (-1.0);
    val = hexa_to_float(val_str);
    return val;
}

long check_alerts() {
    printf("%s", "=== Alert Status ===");
    long total = show_prioritized();
    return printf("%s", hexa_concat(hexa_concat("--- ", hexa_int_to_str((long)(total))), " alert(s) total"));
}

const char* hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "alert.hexa — growth regression monitoring & alerts");
        printf("%s", "usage:");
        printf("%s", "  hexa alert.hexa check                              — show all alerts (prioritized)");
        printf("%s", "  hexa alert.hexa history [min_level]                — filter by level (INFO/WARNING/CRITICAL/DISCOVERY)");
        printf("%s", "  hexa alert.hexa threshold <metric> <value>         — set/get threshold");
        printf("%s", "  hexa alert.hexa emit <level> <source> <pattern> <confidence> <message>");
        printf("%s", "  hexa alert.hexa clear");
        return "";
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "check") == 0)) {
        return "";
    } else if ((strcmp(cmd, "history") == 0)) {
        const char* min_level = (((a.n >= 3)) ? (((const char*)a.d[2])) : ("INFO"));
        if ((!level_valid(min_level))) {
            printf("%s", hexa_concat(hexa_concat("invalid level: ", min_level), " (use INFO/WARNING/CRITICAL/DISCOVERY)"));
            return "";
        }
        long n = filter_by_level(min_level);
        return "";
    } else if ((strcmp(cmd, "threshold") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "usage: hexa alert.hexa threshold <metric> [value]");
            return "";
        }
        const char* metric = ((const char*)a.d[2]);
        if ((a.n >= 4)) {
            double value = 0.0;
            value = hexa_to_float(((const char*)a.d[3]));
            return "";
        } else {
            double val = get_threshold(metric);
            if ((val < 0.0)) {
                return "";
            } else {
                return "";
            }
        }
    } else if ((strcmp(cmd, "emit") == 0)) {
        if ((a.n < 7)) {
            printf("%s", "usage: hexa alert.hexa emit <level> <source> <pattern> <confidence> <message>");
            return "";
        }
        const char* level = ((const char*)a.d[2]);
        if ((!level_valid(level))) {
            printf("%s", hexa_concat("invalid level: ", level));
            return "";
        }
        const char* source = ((const char*)a.d[3]);
        const char* pattern = ((const char*)a.d[4]);
        double confidence = 0.0;
        confidence = hexa_to_float(((const char*)a.d[5]));
        const char* message = ((const char*)a.d[6]);
        long* al = alert_new(level, source, pattern, confidence, message);
        return ingest_alert(al);
    } else if ((strcmp(cmd, "clear") == 0)) {
        hexa_write_file(alert_log, "");
        return "";
    } else {
        printf("%s", hexa_concat("unknown command: ", cmd));
        return "";
    }




}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    hexa_user_main();
    return 0;
}
