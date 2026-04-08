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

const char* mode_name(long m);
long next_mode(long m);
const char* status_name(long s);
long* default_agent_config();
long* create_agent(long *config);
long* create_agent_with_id(const char* id, long config);
double mode_serendipity(long mode, double base);
double mode_min_verification(long mode);
long* run_domain_cycle(long *agent);
long simulate_scan(const char* domain, double serendipity, long cycle);
long* run_agent(long config);
long* scheduler_run_all(hexa_arr configs);
long* scheduler_round_robin(hexa_arr configs, long rounds);
const char* alert_name(long a);
long* default_watchdog_config();
long* check_health(long *report, long *config);

static long mode_explore = 0;
static long mode_deepen = 1;
static long mode_verify = 2;
static long mode_redteam = 3;
static long mode_forge = 4;
static long mode_experiment = 5;
static long status_idle = 0;
static long status_running = 1;
static long status_paused = 2;
static long status_completed = 3;
static long alert_ok = 0;
static long alert_warning = 1;
static long alert_critical = 2;
static long* test_config;
static long* test_agent;

const char* mode_name(long m) {
    if ((m == 0)) {
        return "explore";
    } else if ((m == 1)) {
        return "deepen";
    } else if ((m == 2)) {
        return "verify";
    } else if ((m == 3)) {
        return "redteam";
    } else if ((m == 4)) {
        return "forge";
    } else if ((m == 5)) {
        return "experiment";
    } else {
        return "unknown";
    }





}

long next_mode(long m) {
    if ((m == 0)) {
        return 1;
    } else if ((m == 1)) {
        return 2;
    } else if ((m == 2)) {
        return 4;
    } else if ((m == 4)) {
        return 5;
    } else if ((m == 5)) {
        return 3;
    } else if ((m == 3)) {
        return 0;
    } else {
        return 0;
    }





}

const char* status_name(long s) {
    if ((s == 0)) {
        return "idle";
    } else if ((s == 1)) {
        return "running";
    } else if ((s == 2)) {
        return "paused";
    } else if ((s == 3)) {
        return "completed";
    } else {
        return "unknown";
    }



}

long* default_agent_config() {
    return hexa_struct_alloc((long[]){(long)(6), (long)(mode_explore), (long)(1), (long)(0.2), (long)("general")}, 5);
}

long* create_agent(long *config) {
    const char* id = hexa_concat(hexa_concat(hexa_concat("agent-", mode_name(config[1])), "-"), hexa_int_to_str((long)(config[4])));
    return hexa_struct_alloc((long[]){(long)(id), (long)(config[1]), (long)(status_idle), (long)(config)}, 4);
}

long* create_agent_with_id(const char* id, long config) {
    return hexa_struct_alloc((long[]){(long)(id), (long)(/* unknown field mode */ 0), (long)(status_idle), (long)(config)}, 4);
}

double mode_serendipity(long mode, double base) {
    if ((mode == 0)) {
        if ((base > 0.3)) {
            return base;
        } else {
            return 0.3;
        }
    } else if ((mode == 1)) {
        return 0.05;
    } else if ((mode == 2)) {
        return 0.0;
    } else if ((mode == 3)) {
        return 0.1;
    } else {
        return base;
    }



}

double mode_min_verification(long mode) {
    if ((mode == 0)) {
        return 0.2;
    } else if ((mode == 1)) {
        return 0.4;
    } else if ((mode == 2)) {
        return 0.6;
    } else if ((mode == 3)) {
        return 0.8;
    } else {
        return 0.3;
    }



}

long* run_domain_cycle(long *agent) {
    long discoveries = 0;
    long experiments = 0;
    long lenses_forged = 0;
    long cycles = 0;
    long saturated = 0;
    const char* start_ms = hexa_timestamp();
    double serendipity = mode_serendipity(agent[1], /* unknown field serendipity */ 0);
    double min_verif = mode_min_verification(agent[1]);
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[", hexa_int_to_str((long)(/* unknown field id */ 0))), "] 모드="), mode_name(agent[1])), " 도메인="), hexa_int_to_str((long)(/* unknown field domain */ 0))));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  세렌디피티=", hexa_float_to_str((double)(serendipity))), " 검증기준="), hexa_float_to_str((double)(min_verif))));
    long cyc = 0;
    while ((cyc < /* unknown field max_cycles */ 0)) {
        long found = simulate_scan(/* unknown field domain */ 0, serendipity, cyc);
        if ((found > 0)) {
            discoveries = (discoveries + found);
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  cycle ", hexa_int_to_str((long)(cyc))), ": "), hexa_int_to_str((long)(found))), " 발견"));
        } else {
            if ((cyc > 2)) {
                saturated = 1;
                printf("%s\n", hexa_concat(hexa_concat("  cycle ", hexa_int_to_str((long)(cyc))), ": 포화 감지"));
            }
        }
        cycles = (cycles + 1);
        cyc = (cyc + 1);
    }
    if ((agent[1] == mode_forge)) {
        lenses_forged = 1;
        cycles = 1;
    }
    if ((agent[1] == mode_experiment)) {
        experiments = 1;
        cycles = 1;
    }
    long elapsed = (hexa_timestamp() - start_ms);
    return hexa_struct_alloc((long[]){(long)(/* unknown field id */ 0), (long)(mode_name(agent[1])), (long)(cycles), (long)(discoveries), (long)(experiments), (long)(lenses_forged), (long)(elapsed), (long)(status_name(status_completed)), (long)(/* unknown field domain */ 0), (long)(saturated)}, 10);
}

long simulate_scan(const char* domain, double serendipity, long cycle) {
    long base = (6 - cycle);
    if ((base <= 0)) {
        return 0;
    } else {
        if ((serendipity > 0.2)) {
            return (base + 1);
        } else {
            return base;
        }
    }
}

long* run_agent(long config) {
    long* agent = create_agent(config);
    agent[2];
    0;
    status_running;
    long* report = run_domain_cycle(agent);
    if ((report[9] && /* unknown field auto_register */ 0)) {
        long new_mode = next_mode(agent[1]);
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  모드 전환: ", mode_name(agent[1])), " -> "), mode_name(new_mode)));
    }
    return report;
}

long* scheduler_run_all(hexa_arr configs) {
    long count = configs.n;
    long total_cycles = 0;
    long total_disc = 0;
    long total_exp = 0;
    long total_lenses = 0;
    long total_time = 0;
    long i = 0;
    while ((i < count)) {
        long* report = run_agent(configs.d[i]);
        total_cycles = (total_cycles + report[2]);
        total_disc = (total_disc + report[3]);
        total_exp = (total_exp + report[4]);
        total_lenses = (total_lenses + report[5]);
        total_time = (total_time + report[6]);
        i = (i + 1);
    }
    return hexa_struct_alloc((long[]){(long)(count), (long)(total_cycles), (long)(total_disc), (long)(total_exp), (long)(total_lenses), (long)(total_time)}, 6);
}

long* scheduler_round_robin(hexa_arr configs, long rounds) {
    long count = configs.n;
    long total_cycles = 0;
    long total_disc = 0;
    long total_exp = 0;
    long total_lenses = 0;
    long total_time = 0;
    long r = 0;
    while ((r < rounds)) {
        long i = 0;
        while ((i < count)) {
            long one_cycle_config = configs.d[i];
            /* unknown field max_cycles */ 0;
            0;
            1;
            long* report = run_agent(one_cycle_config);
            total_cycles = (total_cycles + report[2]);
            total_disc = (total_disc + report[3]);
            total_exp = (total_exp + report[4]);
            total_lenses = (total_lenses + report[5]);
            total_time = (total_time + report[6]);
            i = (i + 1);
        }
        r = (r + 1);
    }
    return hexa_struct_alloc((long[]){(long)(count), (long)(total_cycles), (long)(total_disc), (long)(total_exp), (long)(total_lenses), (long)(total_time)}, 6);
}

const char* alert_name(long a) {
    if ((a == 0)) {
        return "OK";
    } else if ((a == 1)) {
        return "WARNING";
    } else if ((a == 2)) {
        return "CRITICAL";
    } else {
        return "UNKNOWN";
    }


}

long* default_watchdog_config() {
    return hexa_struct_alloc((long[]){(long)(60000), (long)(0.1), (long)(6)}, 3);
}

long* check_health(long *report, long *config) {
    double dpc = (((report[2] > 0)) ? ((((double)(report[3])) / ((double)(report[2])))) : (0.0));
    double avg_time = (((report[2] > 0)) ? ((((double)(report[6])) / ((double)(report[2])))) : (0.0));
    long alert = alert_ok;
    const char* msg = "";
    if ((report[2] == 0)) {
        alert = alert_critical;
        msg = "에이전트가 사이클 0회 완료";
    } else {
        if (((dpc < config[1]) && (report[3] == 0))) {
            alert = alert_critical;
            msg = hexa_concat(hexa_int_to_str((long)(report[2])), "사이클 동안 발견 없음 (정체)");
        } else {
            if ((avg_time > ((double)(config[0])))) {
                alert = alert_warning;
                msg = hexa_concat(hexa_concat(hexa_concat(hexa_concat("사이클 느림: 평균 ", hexa_int_to_str((long)((long)(avg_time)))), "ms (한도 "), hexa_int_to_str((long)(config[0]))), "ms)");
            } else {
                if ((dpc < config[1])) {
                    alert = alert_warning;
                    msg = hexa_concat(hexa_concat("낮은 발견율: ", hexa_float_to_str((double)(dpc))), "/cycle");
                } else {
                    alert = alert_ok;
                    msg = hexa_concat(hexa_concat("정상: ", hexa_float_to_str((double)(dpc))), " discoveries/cycle");
                }
            }
        }
    }
    return hexa_struct_alloc((long[]){(long)(report[0]), (long)(alert), (long)(msg), (long)(dpc), (long)(avg_time)}, 5);
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    if (!((strcmp(mode_name(0), "explore") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(mode_name(3), "redteam") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(mode_name(4), "forge") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((next_mode(0) == 1))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((next_mode(3) == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(status_name(0), "idle") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(status_name(3), "completed") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((simulate_scan("test", 0.3, 0) == 7))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((simulate_scan("test", 0.1, 0) == 6))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((simulate_scan("test", 0.1, 5) == 1))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((simulate_scan("test", 0.1, 6) == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((mode_serendipity(0, 0.1) == 0.3))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((mode_serendipity(2, 0.5) == 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((mode_min_verification(0) == 0.2))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((mode_min_verification(3) == 0.8))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "=== autonomous verified ===");
    printf("%s %s\n", "mode_name(0):", mode_name(0));
    printf("%s %ld\n", "next_mode(0):", (long)(next_mode(0)));
    printf("%s %ld\n", "next_mode(3):", (long)(next_mode(3)));
    printf("%s %ld\n", "simulate_scan(test,0.3,0):", (long)(simulate_scan("test", 0.3, 0)));
    printf("%s %ld\n", "simulate_scan(test,0.1,6):", (long)(simulate_scan("test", 0.1, 6)));
    test_config = default_agent_config();
    test_agent = create_agent(test_config);
    if (!((/* unknown field status */ 0 == status_idle))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field mode */ 0 == mode_explore))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s %ld\n", "agent_id:", (long)(/* unknown field id */ 0));
    printf("%s %s\n", "agent_status:", status_name(/* unknown field status */ 0));
    return 0;
}
