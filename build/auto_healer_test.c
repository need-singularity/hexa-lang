#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static long hexa_time_unix(void) { return (long)time(NULL); }
static double hexa_clock(void) { return (double)clock() / CLOCKS_PER_SEC; }
#define HEXA_ARENA_SIZE (256*1024*1024)
static char* hexa_arena = NULL;
static size_t hexa_arena_p = 0;
static void hexa_arena_init(void) {
    if (!hexa_arena) hexa_arena = (char*)malloc(HEXA_ARENA_SIZE);
}
static char* hexa_alloc(size_t n) {
    hexa_arena_init();
    if (hexa_arena_p + n >= HEXA_ARENA_SIZE) { fprintf(stderr, "hexa_arena overflow\n"); exit(1); }
    char* p = &hexa_arena[hexa_arena_p];
    hexa_arena_p += n;
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

hexa_arr engine_registry();
const char* timestamp();
long file_exists(const char* path);
long dir_exists(const char* path);
double safe_float(const char* s);
long safe_int(const char* s);
long log_heal(const char* msg);
long append_bus(const char* source, const char* phase, const char* status, const char* detail);
hexa_arr scan_engine_health();
hexa_arr scan_bus_failures();
hexa_arr scan_system_resources();
hexa_arr scan_launch_agents();
hexa_arr scan_ssh_connections();
hexa_arr scan_all();
long print_issues(hexa_arr issues);

static const char* version = "1.0.0";
static double critical_health = 0.3;
static long consecutive_fail_threshold = 3;
static long swap_limit_gb = 6;
static double loadavg_limit = 12.0;
static long disk_percent_limit = 90;
static const char* home;
static const char* dev;
static const char* hexa;
static const char* engine_dir;
static const char* shared_dir;
static const char* bus_file;
static const char* heal_log;
static const char* halt_file;

hexa_arr engine_registry() {
    hexa_arr reg = hexa_arr_new();
    reg = hexa_arr_concat(reg, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("nexus"), (long)("engine_nexus.hexa"), (long)(hexa_concat(dev, "/nexus"))}, 3))}, 1));
    reg = hexa_arr_concat(reg, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("anima"), (long)("engine_anima.hexa"), (long)(hexa_concat(dev, "/anima"))}, 3))}, 1));
    reg = hexa_arr_concat(reg, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("n6_architecture"), (long)("engine_n6_architecture.hexa"), (long)(hexa_concat(dev, "/n6-architecture"))}, 3))}, 1));
    reg = hexa_arr_concat(reg, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("hexa_lang"), (long)("engine_hexa_lang.hexa"), (long)(hexa_concat(dev, "/hexa-lang"))}, 3))}, 1));
    reg = hexa_arr_concat(reg, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("papers"), (long)("engine_papers.hexa"), (long)(hexa_concat(dev, "/papers"))}, 3))}, 1));
    reg = hexa_arr_concat(reg, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("airgenome"), (long)("engine_airgenome.hexa"), (long)(hexa_concat(dev, "/airgenome"))}, 3))}, 1));
    reg = hexa_arr_concat(reg, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("token_forge"), (long)("engine_token_forge.hexa"), (long)(hexa_concat(dev, "/token-forge"))}, 3))}, 1));
    return reg;
}

const char* timestamp() {
    return hexa_trim(hexa_exec("date +%Y-%m-%dT%H:%M:%S"));
}

long file_exists(const char* path) {
    return (strcmp(hexa_trim(hexa_exec("test")), "1") == 0);
}

long dir_exists(const char* path) {
    return (strcmp(hexa_trim(hexa_exec("test")), "1") == 0);
}

double safe_float(const char* s) {
    double v = 0.0;
    v = hexa_to_float(s);
    return v;
}

long safe_int(const char* s) {
    long v = 0;
    v = (long)(hexa_to_float(s));
    return v;
}

long log_heal(const char* msg) {
    const char* ts = timestamp();
    const char* line = hexa_concat(hexa_concat(hexa_concat(ts, " "), msg), "\n");
    return hexa_append_file(heal_log, line);
}

long append_bus(const char* source, const char* phase, const char* status, const char* detail) {
    const char* ts = timestamp();
    const char* line = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"source\":\"", source), "\",\"timestamp\":\""), ts), "\",\"phase\":\""), phase), "\",\"status\":\""), status), "\",\"detail\":\""), detail), "\"}");
    return hexa_append_file(bus_file, hexa_concat(line, "\n"));
}

hexa_arr scan_engine_health() {
    hexa_arr registry = engine_registry();
    hexa_arr issues = hexa_arr_new();
    long __fi_entry_1 = 0;
    while ((__fi_entry_1 < registry.n)) {
        long entry = registry.d[__fi_entry_1];
        const char* engine_path = hexa_concat(hexa_concat(engine_dir, "/"), hexa_int_to_str((long)(/* unknown field engine */ 0)));
        if ((!dir_exists(/* unknown field path */ 0))) {
            issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("engine"), (long)(/* unknown field name */ 0), (long)("info"), (long)(hexa_concat("프로젝트 디렉토리 없음: ", hexa_int_to_str((long)(/* unknown field path */ 0)))), (long)(0)}, 5))}, 1));
            continue;
        }
        if ((!hexa_file_exists(engine_path))) {
            issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("engine"), (long)(/* unknown field name */ 0), (long)("warning"), (long)(hexa_concat("엔진 파일 없음: ", hexa_int_to_str((long)(/* unknown field engine */ 0)))), (long)(0)}, 5))}, 1));
            continue;
        }
        const char* result = "";
        result = hexa_exec(hexa);
        if (hexa_contains(result, "ERROR")) {
            issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("engine"), (long)(/* unknown field name */ 0), (long)("warning"), (long)("status 호출 실패"), (long)(1)}, 5))}, 1));
            continue;
        }
        double health = 0.5;
        if (hexa_contains(result, "health:")) {
            hexa_arr parts = hexa_split(result, "health:");
            if ((parts.n > 1)) {
                const char* hstr = hexa_trim(((const char*)(hexa_split(((const char*)parts.d[1]), "\n")).d[0]));
                const char* clean = hstr;
                if (hexa_contains(clean, ",")) {
                    clean = ((const char*)(hexa_split(clean, ",")).d[0]);
                }
                if (hexa_contains(clean, "}")) {
                    clean = ((const char*)(hexa_split(clean, "}")).d[0]);
                }
                health = safe_float(hexa_trim(clean));
            }
        }
        if ((health < critical_health)) {
            issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("engine"), (long)(/* unknown field name */ 0), (long)("critical"), (long)(hexa_concat(hexa_concat(hexa_concat(hexa_concat("health=", hexa_float_to_str((double)(health))), " (< "), hexa_float_to_str((double)(critical_health))), ")")), (long)(1)}, 5))}, 1));
        }
        __fi_entry_1 = (__fi_entry_1 + 1);
    }
    return issues;
}

hexa_arr scan_bus_failures() {
    hexa_arr issues = hexa_arr_new();
    const char* content = "";
    content = hexa_read_file(bus_file);
    if ((strcmp(content, "") == 0)) {
        return issues;
    }
    hexa_arr lines = hexa_split(content, "\n");
    hexa_arr registry = engine_registry();
    long __fi_entry_2 = 0;
    while ((__fi_entry_2 < registry.n)) {
        long entry = registry.d[__fi_entry_2];
        long consecutive_fails = 0;
        long i = (lines.n - 1);
        long checked = 0;
        while (((i >= 0) && (checked < 50))) {
            const char* line = ((const char*)lines.d[i]);
            if ((hexa_contains(line, hexa_concat(hexa_concat("\"source\":\"", hexa_int_to_str((long)(/* unknown field name */ 0))), "\"")) || hexa_contains(line, hexa_concat("\"detail\":\"", hexa_int_to_str((long)(/* unknown field name */ 0)))))) {
                if ((hexa_contains(line, "\"status\":\"fail\"") || hexa_contains(line, "\"status\":\"error\""))) {
                    consecutive_fails = (consecutive_fails + 1);
                } else {
                    if ((hexa_contains(line, "\"status\":\"ok\"") || hexa_contains(line, "\"status\":\"complete\""))) {
                        break;
                    }
                }
            }
            i = (i - 1);
            checked = (checked + 1);
        }
        if ((consecutive_fails >= consecutive_fail_threshold)) {
            issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("bus_fail"), (long)(/* unknown field name */ 0), (long)("warning"), (long)(hexa_concat(hexa_concat(hexa_concat(hexa_concat("연속 실패 ", hexa_int_to_str((long)(consecutive_fails))), "회 (>="), hexa_int_to_str((long)(consecutive_fail_threshold))), ")")), (long)(1)}, 5))}, 1));
        }
        __fi_entry_2 = (__fi_entry_2 + 1);
    }
    return issues;
}

hexa_arr scan_system_resources() {
    hexa_arr issues = hexa_arr_new();
    double swap_used_gb = 0.0;
    const char* swap_raw = hexa_exec("sysctl -n vm.swapusage");
    if (hexa_contains(swap_raw, "used =")) {
        hexa_arr parts = hexa_split(swap_raw, "used =");
        if ((parts.n > 1)) {
            const char* used_str = hexa_trim(((const char*)(hexa_split(hexa_trim(((const char*)parts.d[1])), "M")).d[0]));
            swap_used_gb = (safe_float(used_str) / 1024.0);
        }
    }
    if ((swap_used_gb > ((double)(swap_limit_gb)))) {
        issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("swap"), (long)("system"), (long)("critical"), (long)(hexa_concat(hexa_concat(hexa_concat(hexa_concat("swap=", hexa_float_to_str((double)(swap_used_gb))), "GB (> "), hexa_int_to_str((long)(swap_limit_gb))), "GB)")), (long)(1)}, 5))}, 1));
    }
    const char* load_raw = hexa_trim(hexa_exec("sysctl -n vm.loadavg"));
    const char* clean_load = hexa_trim(hexa_replace(hexa_replace(load_raw, "{", ""), "}", ""));
    hexa_arr load_parts = hexa_split(clean_load, " ");
    double loadavg_1m = 0.0;
    long __fi_lp_3 = 0;
    while ((__fi_lp_3 < load_parts.n)) {
        const char* lp = ((const char*)load_parts.d[__fi_lp_3]);
        if ((strcmp(hexa_trim(lp), "") != 0)) {
            loadavg_1m = safe_float(hexa_trim(lp));
            break;
        }
        __fi_lp_3 = (__fi_lp_3 + 1);
    }
    if ((loadavg_1m > loadavg_limit)) {
        issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("load"), (long)("system"), (long)("critical"), (long)(hexa_concat(hexa_concat(hexa_concat(hexa_concat("loadavg=", hexa_float_to_str((double)(loadavg_1m))), " (> "), hexa_float_to_str((double)(loadavg_limit))), ")")), (long)(1)}, 5))}, 1));
    }
    const char* df_raw = hexa_exec("df -h /");
    hexa_arr df_lines = hexa_split(df_raw, "\n");
    if ((df_lines.n >= 2)) {
        const char* data_line = ((const char*)df_lines.d[1]);
        hexa_arr cols = hexa_split(data_line, " ");
        long __fi_col_4 = 0;
        while ((__fi_col_4 < cols.n)) {
            const char* col = ((const char*)cols.d[__fi_col_4]);
            const char* c = hexa_trim(col);
            if ((hexa_ends_with(c, "%") && (hexa_str_len(c) <= 4))) {
                const char* pct_str = hexa_replace(c, "%", "");
                long pct = safe_int(pct_str);
                if ((pct >= disk_percent_limit)) {
                    issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("disk"), (long)("system"), (long)("critical"), (long)(hexa_concat(hexa_concat(hexa_concat(hexa_concat("디스크 사용률=", hexa_int_to_str((long)(pct))), "% (>= "), hexa_int_to_str((long)(disk_percent_limit))), "%)")), (long)(1)}, 5))}, 1));
                }
                break;
            }
            __fi_col_4 = (__fi_col_4 + 1);
        }
    }
    return issues;
}

hexa_arr scan_launch_agents() {
    hexa_arr issues = hexa_arr_new();
    hexa_arr agents = hexa_arr_lit((long[]){(long)("com.nexus.watch-atlas"), (long)("com.nexus.singularity-tick"), (long)("com.nexus.watch-papers")}, 3);
    const char* all_agents = hexa_exec("launchctl list");
    long __fi_agent_5 = 0;
    while ((__fi_agent_5 < agents.n)) {
        const char* agent = ((const char*)agents.d[__fi_agent_5]);
        if ((!hexa_contains(all_agents, agent))) {
            issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("agent"), (long)(agent), (long)("warning"), (long)(hexa_concat("LaunchAgent 미등록/중지: ", agent)), (long)(1)}, 5))}, 1));
        }
        __fi_agent_5 = (__fi_agent_5 + 1);
    }
    return issues;
}

hexa_arr scan_ssh_connections() {
    hexa_arr issues = hexa_arr_new();
    hexa_arr known_hosts = hexa_arr_lit((long[]){(long)("h100")}, 1);
    long __fi_host_6 = 0;
    while ((__fi_host_6 < known_hosts.n)) {
        const char* host = ((const char*)known_hosts.d[__fi_host_6]);
        long reachable = 0;
        const char* result = hexa_exec("bash");
        if (hexa_contains(result, "ok")) {
            reachable = 1;
        }
        if ((!reachable)) {
            issues = hexa_arr_concat(issues, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)("ssh"), (long)(host), (long)("warning"), (long)(hexa_concat("SSH 연결 불가: ", host)), (long)(1)}, 5))}, 1));
        }
        __fi_host_6 = (__fi_host_6 + 1);
    }
    return issues;
}

hexa_arr scan_all() {
    hexa_arr all_issues = hexa_arr_new();
    printf("%s\n", hexa_concat(hexa_concat("=== auto_healer scan v", version), " ==="));
    printf("%s\n", hexa_concat("  시각: ", timestamp()));
    printf("%s\n", "");
    printf("%s\n", "[1/5] 엔진 health 스캔...");
    hexa_arr engine_issues = scan_engine_health();
    all_issues = hexa_arr_concat(all_issues, engine_issues);
    printf("%s\n", hexa_concat(hexa_concat("  -> ", hexa_int_to_str((long)(engine_issues.n))), "건 감지"));
    printf("%s\n", "[2/5] growth_bus 연속 실패 스캔...");
    hexa_arr bus_issues = scan_bus_failures();
    all_issues = hexa_arr_concat(all_issues, bus_issues);
    printf("%s\n", hexa_concat(hexa_concat("  -> ", hexa_int_to_str((long)(bus_issues.n))), "건 감지"));
    printf("%s\n", "[3/5] 시스템 리소스 스캔...");
    hexa_arr res_issues = scan_system_resources();
    all_issues = hexa_arr_concat(all_issues, res_issues);
    printf("%s\n", hexa_concat(hexa_concat("  -> ", hexa_int_to_str((long)(res_issues.n))), "건 감지"));
    printf("%s\n", "[4/5] LaunchAgent 상태 스캔...");
    hexa_arr agent_issues = scan_launch_agents();
    all_issues = hexa_arr_concat(all_issues, agent_issues);
    printf("%s\n", hexa_concat(hexa_concat("  -> ", hexa_int_to_str((long)(agent_issues.n))), "건 감지"));
    printf("%s\n", "[5/5] SSH 연결 스캔...");
    hexa_arr ssh_issues = scan_ssh_connections();
    all_issues = hexa_arr_concat(all_issues, ssh_issues);
    printf("%s\n", hexa_concat(hexa_concat("  -> ", hexa_int_to_str((long)(ssh_issues.n))), "건 감지"));
    printf("%s\n", "");
    print_issues(all_issues);
    log_heal(hexa_concat(hexa_concat("scan: ", hexa_int_to_str((long)(all_issues.n))), " issues found"));
    append_bus("auto_healer", "scan", "complete", hexa_concat(hexa_int_to_str((long)(all_issues.n)), " issues"));
    return all_issues;
}

long print_issues(hexa_arr issues) {
    if ((issues.n == 0)) {
        printf("%s\n", "  ALL CLEAR -- 문제 없음");
        return 0;
    }
    long criticals = 0;
    long warnings = 0;
    long infos = 0;
    long __fi_issue_7 = 0;
    while ((__fi_issue_7 < issues.n)) {
        const char* issue = ((const char*)issues.d[__fi_issue_7]);
        if ((/* unknown field severity */ 0 == "critical")) {
            criticals = (criticals + 1);
        }
        if ((/* unknown field severity */ 0 == "warning")) {
            warnings = (warnings + 1);
        }
        if ((/* unknown field severity */ 0 == "info")) {
            infos = (infos + 1);
        }
        __fi_issue_7 = (__fi_issue_7 + 1);
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  요약: ", hexa_int_to_str((long)(criticals))), " critical, "), hexa_int_to_str((long)(warnings))), " warning, "), hexa_int_to_str((long)(infos))), " info"));
    printf("%s\n", "");
    long __fi_issue_8 = 0;
    while ((__fi_issue_8 < issues.n)) {
        const char* issue = ((const char*)issues.d[__fi_issue_8]);
        const char* icon = (((/* unknown field severity */ 0 == "critical")) ? ("[CRIT]") : (printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  ", icon), " ["), hexa_int_to_str((long)(/* unknown field category */ 0))), "] "), hexa_int_to_str((long)(/* unknown field target */ 0))), ": "), hexa_int_to_str((long)(/* unknown field detail */ 0))), hexa_int_to_str((long)(fixable))))));
        __fi_issue_8 = (__fi_issue_8 + 1);
    }
    /* unsupported stmt */
    /* unsupported stmt */
    /* unsupported stmt */
    /* unsupported stmt */
    /* unsupported stmt */
    /* unsupported stmt */
    /* unsupported stmt */
    /* unsupported stmt */
    /* unsupported stmt */
    /* unsupported stmt */
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    home = hexa_exec("printenv HOME");
    dev = hexa_concat(home, "/Dev");
    hexa = hexa_concat(dev, "/hexa-lang/target/release/hexa");
    engine_dir = hexa_concat(dev, "/nexus/mk2_hexa/native");
    shared_dir = hexa_concat(dev, "/nexus/shared");
    bus_file = hexa_concat(shared_dir, "/growth_bus.jsonl");
    heal_log = hexa_concat(shared_dir, "/auto_healer.log");
    halt_file = hexa_concat(shared_dir, "/.halt");
    return 0;
}
