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

long* task_new(const char* name, const char* command, long interval_secs);
const char* task_to_json(long *t);
const char* jstr(const char* json, const char* key);
const char* jnum(const char* json, const char* key);
long jbool(const char* json, const char* key);
long* parse_task(const char* line);
const char* load_tasks();
long save_tasks_raw(const char* content);
long add_task(const char* name, const char* command, long interval_secs);
long remove_task(const char* name);
long list_tasks();
long toggle_task(const char* name);
long run_due(long now_secs);
long install_defaults();
long hexa_user_main();

static const char* task_file = "shared/scheduler_tasks.jsonl";

long* task_new(const char* name, const char* command, long interval_secs) {
    return hexa_struct_alloc((long[]){(long)(name), (long)(command), (long)(interval_secs), (long)(""), (long)(1)}, 5);
}

const char* task_to_json(long *t) {
    const char* enabled_str = ((t[4]) ? ("true") : ("false"));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"name\":\"", hexa_int_to_str((long)(t[0]))), "\",\"command\":\""), hexa_int_to_str((long)(t[1]))), "\",\"interval\":"), hexa_int_to_str((long)(t[2]))), ",\"last_run\":\""), hexa_int_to_str((long)(t[3]))), "\",\"enabled\":"), enabled_str), "}");
}

const char* jstr(const char* json, const char* key) {
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

const char* jnum(const char* json, const char* key) {
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

long jbool(const char* json, const char* key) {
    const char* val = jnum(json, key);
    return (strcmp(val, "true") == 0);
}

long* parse_task(const char* line) {
    const char* name = jstr(line, "name");
    const char* command = jstr(line, "command");
    const char* last_run = jstr(line, "last_run");
    long enabled = jbool(line, "enabled");
    long interval = 3600;
    interval = hexa_to_int_str(jnum(line, "interval"));
    return hexa_struct_alloc((long[]){(long)(name), (long)(command), (long)(interval), (long)(last_run), (long)(enabled)}, 5);
}

const char* load_tasks() {
    return hexa_read_file(task_file);
}

long save_tasks_raw(const char* content) {
    return hexa_write_file(task_file, content);
}

long add_task(const char* name, const char* command, long interval_secs) {
    const char* raw = load_tasks();
    long* task = task_new(name, command, interval_secs);
    hexa_arr lines = hexa_arr_new();
    if ((strcmp(raw, "") != 0)) {
        hexa_arr parts = hexa_split(raw, "\n");
        long i = 0;
        while ((i < parts.n)) {
            const char* line = hexa_trim(((const char*)parts.d[i]));
            if ((strcmp(line, "") != 0)) {
                long* existing = parse_task(line);
                if ((existing[0] != name)) {
                    lines = hexa_arr_push(lines, (long)(line));
                }
            }
            i = (i + 1);
        }
    }
    lines = hexa_arr_push(lines, (long)(task_to_json(task)));
    const char* out = "";
    long j = 0;
    while ((j < lines.n)) {
        out = hexa_concat(hexa_concat(out, ((const char*)lines.d[j])), "\n");
        j = (j + 1);
    }
    save_tasks_raw(out);
    return printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("added: ", name), " (every "), hexa_int_to_str((long)(interval_secs))), "s) -> "), command));
}

long remove_task(const char* name) {
    const char* raw = load_tasks();
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "no tasks");
        return 0;
    }
    hexa_arr parts = hexa_split(raw, "\n");
    hexa_arr lines = hexa_arr_new();
    long found = 0;
    long i = 0;
    while ((i < parts.n)) {
        const char* line = hexa_trim(((const char*)parts.d[i]));
        if ((strcmp(line, "") != 0)) {
            long* t = parse_task(line);
            if ((t[0] == name)) {
                found = 1;
            } else {
                lines = hexa_arr_push(lines, (long)(line));
            }
        }
        i = (i + 1);
    }
    if ((!found)) {
        printf("%s", hexa_concat("task not found: ", name));
        return 0;
    }
    const char* out = "";
    long j = 0;
    while ((j < lines.n)) {
        out = hexa_concat(hexa_concat(out, ((const char*)lines.d[j])), "\n");
        j = (j + 1);
    }
    save_tasks_raw(out);
    printf("%s", hexa_concat("removed: ", name));
    return 1;
}

long list_tasks() {
    const char* raw = load_tasks();
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "(no tasks)");
        return 0;
    }
    hexa_arr parts = hexa_split(raw, "\n");
    long count = 0;
    long i = 0;
    while ((i < parts.n)) {
        const char* line = hexa_trim(((const char*)parts.d[i]));
        if ((strcmp(line, "") != 0)) {
            long* t = parse_task(line);
            const char* status = ((t[4]) ? ("ON ") : ("OFF"));
            const char* last = (((t[3] == "")) ? ("never") : (t[3]));
            printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[", status), "] "), hexa_int_to_str((long)(t[0]))), " | every "), hexa_int_to_str((long)(t[2]))), "s | last: "), last), " | cmd: "), hexa_int_to_str((long)(t[1]))));
            count = (count + 1);
        }
        i = (i + 1);
    }
    return count;
}

long toggle_task(const char* name) {
    const char* raw = load_tasks();
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "no tasks");
        return 0;
    }
    hexa_arr parts = hexa_split(raw, "\n");
    hexa_arr lines = hexa_arr_new();
    long found = 0;
    long i = 0;
    while ((i < parts.n)) {
        const char* line = hexa_trim(((const char*)parts.d[i]));
        if ((strcmp(line, "") != 0)) {
            long* t = parse_task(line);
            if ((t[0] == name)) {
                found = 1;
                long toggled[5] = {t[0], t[1], t[2], t[3], (!t[4])};
                lines = hexa_arr_push(lines, (long)(task_to_json(toggled)));
                const char* new_status = ((toggled[4]) ? ("enabled") : ("disabled"));
                printf("%s", hexa_concat(hexa_concat(name, " -> "), new_status));
            } else {
                lines = hexa_arr_push(lines, (long)(line));
            }
        }
        i = (i + 1);
    }
    if ((!found)) {
        printf("%s", hexa_concat("task not found: ", name));
        return 0;
    }
    const char* out = "";
    long j = 0;
    while ((j < lines.n)) {
        out = hexa_concat(hexa_concat(out, ((const char*)lines.d[j])), "\n");
        j = (j + 1);
    }
    return save_tasks_raw(out);
}

long run_due(long now_secs) {
    const char* raw = load_tasks();
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "(no tasks)");
        return 0;
    }
    hexa_arr parts = hexa_split(raw, "\n");
    hexa_arr lines = hexa_arr_new();
    long due_count = 0;
    long i = 0;
    while ((i < parts.n)) {
        const char* line = hexa_trim(((const char*)parts.d[i]));
        if ((strcmp(line, "") != 0)) {
            long* t = parse_task(line);
            long is_due = 0;
            if (t[4]) {
                if ((t[3] == "")) {
                    is_due = 1;
                } else {
                    long last_secs = 0;
                    last_secs = (long)(t[3]);
                    if ((!is_due)) {
                        long elapsed = (now_secs - last_secs);
                        if ((elapsed >= t[2])) {
                            is_due = 1;
                        }
                    }
                }
            }
            if (is_due) {
                printf("%s", hexa_concat(hexa_concat(hexa_concat("[RUN] ", hexa_int_to_str((long)(t[0]))), " -> "), hexa_int_to_str((long)(t[1]))));
                long updated[5] = {t[0], t[1], t[2], hexa_int_to_str((long)(now_secs)), t[4]};
                lines = hexa_arr_push(lines, (long)(task_to_json(updated)));
                due_count = (due_count + 1);
            } else {
                lines = hexa_arr_push(lines, (long)(line));
            }
        }
        i = (i + 1);
    }
    const char* out = "";
    long j = 0;
    while ((j < lines.n)) {
        out = hexa_concat(hexa_concat(out, ((const char*)lines.d[j])), "\n");
        j = (j + 1);
    }
    save_tasks_raw(out);
    if ((due_count == 0)) {
        printf("%s", hexa_concat(hexa_concat("(no tasks due at t=", hexa_int_to_str((long)(now_secs))), ")"));
    } else {
        printf("%s", hexa_concat(hexa_concat("--- ", hexa_int_to_str((long)(due_count))), " task(s) executed"));
    }
    return due_count;
}

long install_defaults() {
    add_task("daily-full-scan", "scan all --full", 86400);
    add_task("hourly-quick-scan", "scan physics --lenses consciousness,topology,causal", 3600);
    add_task("weekly-ouroboros", "evolve all --max-cycles 6", 604800);
    toggle_task("weekly-ouroboros");
    return printf("%s", "--- default tasks installed");
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "scheduler.hexa — task scheduling & job management");
        printf("%s", "usage:");
        printf("%s", "  hexa scheduler.hexa add <name> <command> <interval_secs>");
        printf("%s", "  hexa scheduler.hexa list");
        printf("%s", "  hexa scheduler.hexa run [now_secs]          — run due tasks");
        printf("%s", "  hexa scheduler.hexa remove <name>");
        printf("%s", "  hexa scheduler.hexa toggle <name>");
        printf("%s", "  hexa scheduler.hexa defaults                — install default n=6 tasks");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "add") == 0)) {
        if ((a.n < 5)) {
            printf("%s", "usage: hexa scheduler.hexa add <name> <command> <interval_secs>");
            return 0;
        }
        const char* name = ((const char*)a.d[2]);
        const char* command = ((const char*)a.d[3]);
        long interval = 3600;
        interval = hexa_to_int_str(((const char*)a.d[4]));
        return add_task(name, command, interval);
    } else if ((strcmp(cmd, "list") == 0)) {
        long n = list_tasks();
        return printf("%s", hexa_concat(hexa_concat("--- ", hexa_int_to_str((long)(n))), " task(s)"));
    } else if ((strcmp(cmd, "run") == 0)) {
        long now = hexa_to_int_str(hexa_int_to_str((long)(hexa_time_unix())));
        if ((a.n >= 3)) {
            now = hexa_to_int_str(((const char*)a.d[2]));
        }
        return run_due(now);
    } else if ((strcmp(cmd, "remove") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "usage: hexa scheduler.hexa remove <name>");
            return 0;
        }
        return remove_task(((const char*)a.d[2]));
    } else if ((strcmp(cmd, "toggle") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "usage: hexa scheduler.hexa toggle <name>");
            return 0;
        }
        return toggle_task(((const char*)a.d[2]));
    } else if ((strcmp(cmd, "defaults") == 0)) {
        return install_defaults();
    } else {
        printf("%s", hexa_concat("unknown command: ", cmd));
        return printf("%s", "commands: add, list, run, remove, toggle, defaults");
    }





}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    hexa_user_main();
    return 0;
}
