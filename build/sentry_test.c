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

const char* read_pid(const char* path);
long sentry_log(const char* message);
const char* check_pid_alive(const char* pid_str);
const char* check_daemon();
const char* check_sentry();
const char* health_check();
const char* tail_log(long n);
const char* generate_report();
long hexa_user_main();

static const char* nexus_dir = ".nexus";
static const char* sentry_pid_file = ".nexus/sentry.pid";
static const char* sentry_log_file = ".nexus/sentry.log";
static const char* daemon_pid_file = ".nexus/daemon.pid";

const char* read_pid(const char* path) {
    const char* raw = hexa_read_file(path);
    if ((strcmp(raw, "") == 0)) {
        return "";
    }
    return hexa_trim(raw);
}

long sentry_log(const char* message) {
    const char* existing = hexa_read_file(sentry_log_file);
    const char* line = hexa_concat("[sentry] ", message);
    if ((strcmp(existing, "") == 0)) {
        return hexa_write_file(sentry_log_file, hexa_concat(line, "\n"));
    } else {
        return hexa_write_file(sentry_log_file, hexa_concat(hexa_concat(existing, line), "\n"));
    }
}

const char* check_pid_alive(const char* pid_str) {
    if ((strcmp(pid_str, "") == 0)) {
        return "not_running";
    }
    return hexa_concat("possible:", pid_str);
}

const char* check_daemon() {
    const char* pid = read_pid(daemon_pid_file);
    if ((strcmp(pid, "") == 0)) {
        return "daemon: not running (no pid file)";
    }
    return hexa_concat(hexa_concat("daemon: pid=", pid), " (file exists)");
}

const char* check_sentry() {
    const char* pid = read_pid(sentry_pid_file);
    if ((strcmp(pid, "") == 0)) {
        return "sentry: not running (no pid file)";
    }
    return hexa_concat(hexa_concat("sentry: pid=", pid), " (file exists)");
}

const char* health_check() {
    const char* daemon_status = check_daemon();
    const char* sentry_status = check_sentry();
    long log_exists = (strcmp(hexa_read_file(sentry_log_file), "") != 0);
    const char* report = "=== nexus sentry health check ===\n";
    report = hexa_concat(hexa_concat(hexa_concat(report, "  "), sentry_status), "\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "  "), daemon_status), "\n");
    report = hexa_concat(hexa_concat(report, "  log: "), sentry_log_file);
    if (log_exists) {
        report = hexa_concat(report, " (exists)");
    } else {
        report = hexa_concat(report, " (not found)");
    }
    return report;
}

const char* tail_log(long n) {
    const char* raw = hexa_read_file(sentry_log_file);
    if ((strcmp(raw, "") == 0)) {
        return "(no log file)";
    }
    hexa_arr lines = hexa_split(raw, "\n");
    long total = lines.n;
    long start = (((total > n)) ? ((total - n)) : (0));
    const char* result = "";
    long i = start;
    while ((i < total)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") != 0)) {
            if ((strcmp(result, "") != 0)) {
                result = hexa_concat(result, "\n");
            }
            result = hexa_concat(result, line);
        }
        i = (i + 1);
    }
    if ((strcmp(result, "") == 0)) {
        return "(empty log)";
    }
    return result;
}

const char* generate_report() {
    const char* daemon_pid = read_pid(daemon_pid_file);
    const char* sentry_pid = read_pid(sentry_pid_file);
    const char* report = "{\"sentry_pid\":\"";
    report = hexa_concat(hexa_concat(report, sentry_pid), "\",");
    report = hexa_concat(hexa_concat(hexa_concat(report, "\"daemon_pid\":\""), daemon_pid), "\",");
    const char* raw = hexa_read_file(sentry_log_file);
    long log_lines = 0;
    if ((strcmp(raw, "") != 0)) {
        hexa_arr ls = hexa_split(raw, "\n");
        long li = 0;
        while ((li < ls.n)) {
            if ((strcmp(hexa_trim(((const char*)ls.d[li])), "") != 0)) {
                log_lines = (log_lines + 1);
            }
            li = (li + 1);
        }
    }
    report = hexa_concat(hexa_concat(hexa_concat(report, "\"log_lines\":"), hexa_int_to_str((long)(log_lines))), "}");
    return report;
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "sentry.hexa — 보안 감시");
        printf("%s", "Usage:");
        printf("%s", "  status              전체 상태");
        printf("%s", "  check               헬스 체크");
        printf("%s", "  tail [lines=20]     로그 tail");
        printf("%s", "  log <message>       수동 로그 기록");
        printf("%s", "  report              JSON 리포트");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "status") == 0)) {
        const char* report = health_check();
        printf("%s", report);
    }
    if ((strcmp(cmd, "check") == 0)) {
        const char* daemon = check_daemon();
        const char* sentry = check_sentry();
        printf("%s", hexa_concat("[sentry] ", daemon));
        printf("%s", hexa_concat("[sentry] ", sentry));
        sentry_log(hexa_concat(hexa_concat(hexa_concat("check: ", daemon), " | "), sentry));
        printf("%s", "[sentry] check logged");
    }
    if ((strcmp(cmd, "tail") == 0)) {
        long n = (((a.n > 2)) ? (0) : (20));
        const char* result = tail_log(n);
        printf("%s", result);
    }
    if ((strcmp(cmd, "log") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: log <message>");
            return 0;
        }
        const char* msg = "";
        long mi = 2;
        while ((mi < a.n)) {
            if ((strcmp(msg, "") == 0)) {
                msg = ((const char*)a.d[mi]);
            } else {
                msg = hexa_concat(hexa_concat(msg, " "), ((const char*)a.d[mi]));
            }
            mi = (mi + 1);
        }
        sentry_log(msg);
        printf("%s", hexa_concat("[sentry] logged: ", msg));
    }
    if ((strcmp(cmd, "report") == 0)) {
        const char* report = generate_report();
        return printf("%s", report);
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    return 0;
}
