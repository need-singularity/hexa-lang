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

long* event_new(const char* type_tag, const char* data, double confidence, const char* source);
const char* event_summary(long *ev);
long valid_type(const char* tag);
const char* event_to_json(long *ev);
long* parse_event_line(const char* line);
const char* extract_json_string(const char* json, const char* key);
const char* extract_json_number(const char* json, const char* key);
const char* emit_event(const char* type_tag, const char* data, double confidence, const char* source);
const char* load_history();
long filter_by_type(const char* type_tag);
long count_by_type();
long recent_events(long n);
long clear_history();
const char* hexa_user_main();

static const char* event_log = "shared/event_history.jsonl";
static const char* event_types = "discovery,lens_forged,experiment,bt_candidate,anomaly,scan_completed";

long* event_new(const char* type_tag, const char* data, double confidence, const char* source) {
    return hexa_struct_alloc((long[]){(long)(type_tag), (long)(hexa_int_to_str((long)(hexa_time_unix()))), (long)(data), (long)(confidence), (long)(source)}, 5);
}

const char* event_summary(long *ev) {
    if ((ev[0] == "discovery")) {
        return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("Discovery [", hexa_int_to_str((long)(ev[4]))), "] confidence="), hexa_int_to_str((long)(ev[3]))), " "), hexa_int_to_str((long)(ev[2])));
    } else if ((ev[0] == "lens_forged")) {
        return hexa_concat("Lens forged: ", hexa_int_to_str((long)(ev[2])));
    } else if ((ev[0] == "experiment")) {
        return hexa_concat("Experiment completed: ", hexa_int_to_str((long)(ev[2])));
    } else if ((ev[0] == "bt_candidate")) {
        return hexa_concat("BT candidate: ", hexa_int_to_str((long)(ev[2])));
    } else if ((ev[0] == "anomaly")) {
        return hexa_concat(hexa_concat(hexa_concat("Anomaly (severity=", hexa_int_to_str((long)(ev[3]))), "): "), hexa_int_to_str((long)(ev[2])));
    } else if ((ev[0] == "scan_completed")) {
        return hexa_concat("Scan completed: ", hexa_int_to_str((long)(ev[2])));
    } else {
        return hexa_concat(hexa_concat(hexa_concat("[", hexa_int_to_str((long)(ev[0]))), "] "), hexa_int_to_str((long)(ev[2])));
    }





}

long valid_type(const char* tag) {
    hexa_arr types = hexa_split(event_types, ",");
    long i = 0;
    while ((i < types.n)) {
        if ((strcmp(((const char*)types.d[i]), tag) == 0)) {
            return 1;
        }
        i = (i + 1);
    }
    return 0;
}

const char* event_to_json(long *ev) {
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"type\":\"", hexa_int_to_str((long)(ev[0]))), "\",\"timestamp\":\""), hexa_int_to_str((long)(ev[1]))), "\",\"data\":\""), hexa_int_to_str((long)(ev[2]))), "\",\"confidence\":"), hexa_int_to_str((long)(ev[3]))), ",\"source\":\""), hexa_int_to_str((long)(ev[4]))), "\"}");
}

long* parse_event_line(const char* line) {
    const char* type_tag = extract_json_string(line, "type");
    const char* timestamp = extract_json_string(line, "timestamp");
    const char* data = extract_json_string(line, "data");
    const char* source = extract_json_string(line, "source");
    const char* conf_str = extract_json_number(line, "confidence");
    double confidence = 0.0;
    confidence = hexa_to_float(conf_str);
    return hexa_struct_alloc((long[]){(long)(type_tag), (long)(timestamp), (long)(data), (long)(confidence), (long)(source)}, 5);
}

const char* extract_json_string(const char* json, const char* key) {
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

const char* extract_json_number(const char* json, const char* key) {
    const char* needle = hexa_concat(hexa_concat("\"", key), "\":");
    long idx = hexa_index_of(json, needle);
    if ((idx < 0)) {
        return "0";
    }
    long start = (idx + hexa_str_len(needle));
    const char* rest = hexa_substr(json, start, hexa_str_len(json));
    long end_comma = hexa_index_of(rest, ",");
    long end_brace = hexa_index_of(rest, "}");
    long end = hexa_str_len(rest);
    if ((end_comma >= 0)) {
        end = end_comma;
    }
    if ((end_brace >= 0)) {
        if ((end_brace < end)) {
            end = end_brace;
        }
    }
    return hexa_trim(hexa_substr(rest, 0, end));
}

const char* emit_event(const char* type_tag, const char* data, double confidence, const char* source) {
    long* ev = event_new(type_tag, data, confidence, source);
    const char* line = event_to_json(ev);
    const char* existing = hexa_read_file(event_log);
    const char* new_content = (((strcmp(existing, "") == 0)) ? (hexa_concat(line, "\n")) : (hexa_concat(hexa_concat(existing, line), "\n")));
    hexa_write_file(event_log, new_content);
    const char* summary = event_summary(ev);
    printf("%s", hexa_concat("[EVENT] ", summary));
    return summary;
}

const char* load_history() {
    const char* raw = hexa_read_file(event_log);
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "(no events recorded)");
        return "";
    }
    return raw;
}

long filter_by_type(const char* type_tag) {
    const char* raw = hexa_read_file(event_log);
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "(no events)");
        return 0;
    }
    hexa_arr lines = hexa_split(raw, "\n");
    long count = 0;
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") != 0)) {
            long* ev = parse_event_line(line);
            if ((ev[0] == type_tag)) {
                printf("%s", event_summary(ev));
                count = (count + 1);
            }
        }
        i = (i + 1);
    }
    return count;
}

long count_by_type() {
    const char* raw = hexa_read_file(event_log);
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "(no events)");
        return 0;
    }
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr types = hexa_split(event_types, ",");
    long total = 0;
    long ti = 0;
    while ((ti < types.n)) {
        const char* tag = ((const char*)types.d[ti]);
        long count = 0;
        long i = 0;
        while ((i < lines.n)) {
            const char* line = hexa_trim(((const char*)lines.d[i]));
            if ((strcmp(line, "") != 0)) {
                long* ev = parse_event_line(line);
                if ((ev[0] == tag)) {
                    count = (count + 1);
                }
            }
            i = (i + 1);
        }
        if ((count > 0)) {
            printf("%s", hexa_concat(hexa_concat(tag, ": "), hexa_int_to_str((long)(count))));
            total = (total + count);
        }
        ti = (ti + 1);
    }
    printf("%s", hexa_concat("total: ", hexa_int_to_str((long)(total))));
    return total;
}

long recent_events(long n) {
    const char* raw = hexa_read_file(event_log);
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "(no events)");
        return 0;
    }
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr valid = hexa_arr_new();
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") != 0)) {
            valid = hexa_arr_push(valid, (long)(line));
        }
        i = (i + 1);
    }
    long total = valid.n;
    long start = (((total > n)) ? ((total - n)) : (0));
    long j = start;
    long shown = 0;
    while ((j < total)) {
        long* ev = parse_event_line(((const char*)valid.d[j]));
        printf("%s", event_summary(ev));
        j = (j + 1);
        shown = (shown + 1);
    }
    return shown;
}

long clear_history() {
    hexa_write_file(event_log, "");
    return printf("%s", "event history cleared");
}

const char* hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "event.hexa — pub-sub event bus");
        printf("%s", "usage:");
        printf("%s", "  hexa event.hexa emit <type> <data> [confidence] [source]");
        printf("%s", "  hexa event.hexa listen <type>");
        printf("%s", "  hexa event.hexa history [N]");
        printf("%s", "  hexa event.hexa count");
        printf("%s", "  hexa event.hexa clear");
        printf("%s", hexa_concat("types: ", event_types));
        return "";
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "emit") == 0)) {
        if ((a.n < 4)) {
            printf("%s", "usage: hexa event.hexa emit <type> <data> [confidence] [source]");
            return "";
        }
        const char* type_tag = ((const char*)a.d[2]);
        const char* data = ((const char*)a.d[3]);
        double confidence = 1.0;
        const char* source = "cli";
        if ((a.n >= 5)) {
            confidence = hexa_to_float(((const char*)a.d[4]));
        }
        if ((a.n >= 6)) {
            source = ((const char*)a.d[5]);
        }
        if ((!valid_type(type_tag))) {
            printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat("warning: unknown type '", type_tag), "' (known: "), event_types), ")"));
        }
        return emit_event(type_tag, data, confidence, source);
    } else if ((strcmp(cmd, "listen") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "usage: hexa event.hexa listen <type>");
            return "";
        }
        const char* type_tag = ((const char*)a.d[2]);
        long n = filter_by_type(type_tag);
        return "";
    } else if ((strcmp(cmd, "history") == 0)) {
        long n = 20;
        if ((a.n >= 3)) {
            n = hexa_to_int_str(((const char*)a.d[2]));
        }
        return "";
    } else if ((strcmp(cmd, "count") == 0)) {
        return "";
    } else if ((strcmp(cmd, "clear") == 0)) {
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
