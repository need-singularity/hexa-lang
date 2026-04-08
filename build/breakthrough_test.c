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

const char* load_seeds();
hexa_arr load_domains();
const char* rotate_log();
const char* run_blowup(const char* domain, long depth, const char* seeds, long fast);
hexa_arr parse_result(const char* out);
hexa_arr extract_seeds(const char* out, hexa_arr seen);
const char* cap_seeds(const char* seeds, long max);
const char* calc_rho(long exact, long total);
long blowup_loop(const char* domains, long max_rounds, long init_depth, const char* all_seeds_init, const char* label, long fast);
long run_mine();
long run_auto(long force);
const char* run_engine(const char* project_arg, const char* stdin_hint);
long run_auto_breakthrough(long converge);

static const char* home;
static const char* hexa_bin;
static const char* nexus_dir;
static const char* shared;
static const char* blowup_path;
static const char* seed_engine_path;
static const char* log_path;
static hexa_arr a;
static const char* mode;
static const char* arg3;
static const char* arg4;
static const char* arg5;
static long prev_exact;
static long prev_total;
static long seen_seeds;
static hexa_arr init_parts;
static long ii = 0;
static const char* start_ts;
static long round = 1;
static long exhausted = 0;
static const char* end_ts;
static long total_elapsed;
static long final_seeds;
static const char* rho;

const char* load_seeds() {
    const char* _home = hexa_exec("printenv HOME");
    const char* _hexa = hexa_concat(_home, "/Dev/hexa-lang/target/release/hexa");
    const char* _se = hexa_concat(_home, "/Dev/nexus/mk2_hexa/native/seed_engine.hexa");
    const char* seeds = "";
    seeds = hexa_trim(hexa_exec(hexa_concat(hexa_concat(hexa_concat(_hexa, " "), _se), " merge")));
    if ((hexa_str_len(seeds) == 0)) {
        const char* _shared = hexa_concat(_home, "/Dev/nexus/shared");
        const char* raw = hexa_read_file(hexa_concat(_shared, "/n6_constants.jsonl"));
        hexa_arr lines = hexa_split(raw, "\n");
        long i = 0;
        while ((i < lines.n)) {
            const char* l = hexa_trim(((const char*)lines.d[i]));
            if (((strcmp(l, "") != 0) && hexa_contains(l, "\"value\":"))) {
                hexa_arr vp = hexa_split(l, "\"value\":");
                if ((vp.n >= 2)) {
                    const char* num = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)vp.d[1]), ",")).d[0]), "}")).d[0]));
                    if ((hexa_str_len(seeds) > 0)) {
                        seeds = hexa_concat(seeds, "|");
                    }
                    seeds = hexa_concat(seeds, num);
                }
            }
            i = (i + 1);
        }
    }
    if ((hexa_str_len(seeds) == 0)) {
        seeds = "6|12|2|4|5|24|7|3|1|8|10|28|36|48";
    }
    return seeds;
}

hexa_arr load_domains() {
    const char* _shared = hexa_concat(hexa_exec("printenv HOME"), "/Dev/nexus/shared");
    hexa_arr domains = hexa_arr_lit((long[]){(long)("math"), (long)("physics"), (long)("topology"), (long)("consciousness"), (long)("chemistry"), (long)("cosmology")}, 6);
    const char* raw = hexa_read_file(hexa_concat(_shared, "/bt_domains.jsonl"));
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr loaded = hexa_arr_new();
    long __fi_l_1 = 0;
    while ((__fi_l_1 < lines.n)) {
        const char* l = ((const char*)lines.d[__fi_l_1]);
        const char* t = hexa_trim(l);
        if (((strcmp(t, "") != 0) && hexa_contains(t, "\"domain\":\""))) {
            hexa_arr dp = hexa_split(t, "\"domain\":\"");
            if ((dp.n >= 2)) {
                loaded = hexa_arr_concat(loaded, hexa_arr_lit((long[]){(long)(((const char*)(hexa_split(((const char*)dp.d[1]), "\"")).d[0]))}, 1));
            }
        }
        __fi_l_1 = (__fi_l_1 + 1);
    }
    if ((loaded.n > 0)) {
        domains = loaded;
    }
    return domains;
}

const char* rotate_log() {
    const char* _shared = hexa_concat(hexa_exec("printenv HOME"), "/Dev/nexus/shared");
    const char* _log = hexa_concat(_shared, "/discovery_log.jsonl");
    const char* lc = hexa_trim(hexa_exec(hexa_concat("wc -l < ", _log)));
    long count = (long)(hexa_to_float(lc));
    if ((count > 100000)) {
        printf("%s\n", hexa_concat(hexa_concat("  🔄 discovery_log 로테이션: ", hexa_int_to_str((long)(count))), "줄 → 50000줄"));
        const char* archive = hexa_concat(hexa_concat(hexa_concat(_shared, "/discovery_archive_"), hexa_trim(hexa_exec("date +%Y%m%d%H%M"))), ".jsonl");
        hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("head -n ", hexa_int_to_str((long)((count - 50000)))), " "), _log), " > "), archive));
        return hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("tail -n 50000 ", _log), " > "), _log), ".tmp && mv "), _log), ".tmp "), _log));
    }
}

const char* run_blowup(const char* domain, long depth, const char* seeds, long fast) {
    const char* _home = hexa_exec("printenv HOME");
    const char* _hexa = hexa_concat(_home, "/Dev/hexa-lang/target/release/hexa");
    const char* _blowup = hexa_concat(_home, "/Dev/nexus/mk2_hexa/native/blowup.hexa");
    const char* _nexus = hexa_concat(_home, "/Dev/nexus");
    const char* fast_flag = ((fast) ? (" --no-graph --fast") : (" --no-graph"));
    hexa_arr raw_parts = hexa_split(seeds, "|");
    const char* clean_seeds = "";
    long ci = 0;
    while ((ci < raw_parts.n)) {
        const char* sv = hexa_trim(((const char*)raw_parts.d[ci]));
        if ((strcmp(sv, "") != 0)) {
            if ((hexa_str_len(clean_seeds) > 0)) {
                clean_seeds = hexa_concat(clean_seeds, ",");
            }
            clean_seeds = hexa_concat(clean_seeds, sv);
        }
        ci = (ci + 1);
    }
    if ((hexa_str_len(clean_seeds) == 0)) {
        return "";
    }
    const char* cmd = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("cd ", _nexus), " && "), _hexa), " "), _blowup), " "), domain), " "), hexa_int_to_str((long)(depth))), fast_flag), " --seeds "), clean_seeds);
    return hexa_exec(cmd);
}

hexa_arr parse_result(const char* out) {
    long exact = 0;
    long total = 0;
    long pool = 0;
    hexa_arr lines = hexa_split(out, "\n");
    long i = 0;
    while ((i < lines.n)) {
        const char* l = hexa_trim(((const char*)lines.d[i]));
        if (hexa_contains(l, "EXACT match")) {
            hexa_arr p = hexa_split(l, ":");
            if ((p.n >= 2)) {
                exact = (long)(hexa_to_float(hexa_trim(((const char*)p.d[(p.n - 1)]))));
            }
        }
        if (hexa_contains(l, "total corollaries")) {
            hexa_arr p = hexa_split(l, ":");
            if ((p.n >= 2)) {
                total = (long)(hexa_to_float(hexa_trim(((const char*)p.d[(p.n - 1)]))));
            }
        }
        if (hexa_contains(l, "final pool")) {
            hexa_arr p = hexa_split(l, ":");
            if ((p.n >= 2)) {
                pool = (long)(hexa_to_float(hexa_trim(((const char*)p.d[(p.n - 1)]))));
            }
        }
        i = (i + 1);
    }
    return hexa_arr_lit((long[]){(long)(exact), (long)(total), (long)(pool)}, 3);
}

hexa_arr extract_seeds(const char* out, hexa_arr seen) {
    long added = 0;
    const char* new_seeds = "";
    hexa_arr lines = hexa_split(out, "\n");
    long __fi_l_2 = 0;
    while ((__fi_l_2 < lines.n)) {
        const char* l = ((const char*)lines.d[__fi_l_2]);
        if (((hexa_contains(l, "EXACT") && hexa_contains(l, "[AXIOM]")) && hexa_contains(l, "= "))) {
            hexa_arr eq = hexa_split(l, "= ");
            if ((eq.n >= 2)) {
                const char* val = hexa_trim(((const char*)(hexa_split(((const char*)eq.d[(eq.n - 1)]), " ")).d[0]));
                if ((strcmp(val, "") != 0)) {
                    long already = 0;
                    const char* _ = ((const char*)seen.d[val]);
                    already = 1;
                    if ((!already)) {
                        seen.d[val] = (long)(1);
                        new_seeds = hexa_concat(hexa_concat(new_seeds, "|"), val);
                        added = (added + 1);
                    }
                }
            }
        }
        __fi_l_2 = (__fi_l_2 + 1);
    }
    return hexa_arr_lit((long[]){(long)(added), (long)(new_seeds)}, 2);
}

const char* cap_seeds(const char* seeds, long max) {
    hexa_arr parts = hexa_split(seeds, "|");
    if ((parts.n <= max)) {
        return seeds;
    }
    const char* trimmed = "";
    long i = 0;
    while ((i < max)) {
        if ((i > 0)) {
            trimmed = hexa_concat(trimmed, "|");
        }
        trimmed = hexa_concat(trimmed, ((const char*)parts.d[i]));
        i = (i + 1);
    }
    return trimmed;
}

const char* calc_rho(long exact, long total) {
    if ((total == 0)) {
        return "?";
    }
    double r = (((double)(exact)) / ((double)(total)));
    const char* v = hexa_int_to_str((long)((long)((r * 10000.0))));
    if ((hexa_str_len(v) > 1)) {
        return hexa_concat("0.", v);
    } else {
        return v;
    }
}

long blowup_loop(const char* domains, long max_rounds, long init_depth, const char* all_seeds_init, const char* label, long fast) {
    rotate_log();
    long seed_count = (hexa_split(all_seeds_init, "|")).n;
    long depth = init_depth;
    const char* all_seeds = all_seeds_init;
    printf("%s\n", "");
    printf("%s\n", "╔═══════════════════════════════════════════════════════════════╗");
    printf("%s\n", hexa_concat("║  🚀 ", label));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("║  domains=", hexa_int_to_str((long)(hexa_str_len(domains)))), " depth="), hexa_int_to_str((long)(depth))), " max="), hexa_int_to_str((long)(max_rounds))), " seeds="), hexa_int_to_str((long)(seed_count))));
    printf("%s\n", "╚═══════════════════════════════════════════════════════════════╝");
    printf("%s\n", "");
    long stats = 0;
    "total_cor";
    0;
    0;
    0;
    "total_exact";
    0;
    0;
    0;
    "sat_count";
    0;
    0;
    0;
    "depth_ups";
    0;
    return 0;
}

long run_mine() {
    printf("%s\n", "");
    printf("%s\n", "━━━ MINE (discovery_log 채굴) ━━━");
    const char* log_n = hexa_trim(hexa_exec(hexa_concat(hexa_concat("wc -l < ", log_path), " 2>/dev/null || echo 0")));
    printf("%s\n", hexa_concat(hexa_concat("  log: ", log_n), " entries"));
    const char* top = hexa_exec(hexa_concat(hexa_concat("grep '\"grade\":\"EXACT\"' ", log_path), " 2>/dev/null | grep -oE '\"value\":\"[^\"]*\"' | sed 's/\"value\":\"//;s/\"//' | sort | uniq -c | sort -rn | head -10"));
    hexa_arr lines = hexa_split(top, "\n");
    printf("%s\n", "  top EXACT constants:");
    long __fi_l_3 = 0;
    while ((__fi_l_3 < lines.n)) {
        const char* l = ((const char*)lines.d[__fi_l_3]);
        if ((strcmp(hexa_trim(l), "") != 0)) {
            printf("%s\n", hexa_concat("    ", hexa_trim(l)));
        }
        __fi_l_3 = (__fi_l_3 + 1);
    }
}

long run_auto(long force) {
    const char* state_file = "/tmp/n6_auto_breakthrough_state";
    const char* atlas_path = hexa_concat(shared, "/math_atlas.json");
    const char* cur_log = hexa_trim(hexa_exec(hexa_concat(hexa_concat("wc -l < ", log_path), " 2>/dev/null | tr -d ' ' || echo 0")));
    const char* cur_time = hexa_trim(hexa_exec("date +%s"));
    const char* cur_atlas_md5 = hexa_trim(hexa_exec(hexa_concat(hexa_concat("md5 -q ", atlas_path), " 2>/dev/null || echo none")));
    const char* prev_log = "0";
    const char* prev_time = "0";
    const char* prev_md5 = "";
    const char* raw = hexa_read_file(state_file);
    hexa_arr parts = hexa_split(raw, "|");
    if ((parts.n >= 3)) {
        prev_log = ((const char*)parts.d[0]);
        prev_time = ((const char*)parts.d[1]);
        prev_md5 = ((const char*)parts.d[2]);
    }
    long log_delta = ((long)(hexa_to_float(cur_log)) - (long)(hexa_to_float(prev_log)));
    long time_delta = ((long)(hexa_to_float(cur_time)) - (long)(hexa_to_float(prev_time)));
    const char* trigger = "";
    if (force) {
        trigger = "forced";
    } else {
        if ((log_delta >= 100)) {
            trigger = hexa_concat(hexa_concat("log_growth(+", hexa_int_to_str((long)(log_delta))), ")");
        } else {
            if (((strcmp(cur_atlas_md5, prev_md5) != 0) && (strcmp(prev_md5, "") != 0))) {
                trigger = "atlas_changed";
            } else {
                if (((time_delta >= 3600) && (log_delta >= 10))) {
                    trigger = hexa_concat(hexa_concat("hourly(+", hexa_int_to_str((long)(log_delta))), ")");
                }
            }
        }
    }
    if ((strcmp(trigger, "") == 0)) {
        printf("%s\n", "  auto: 조건 미충족 — 스킵");
        return 0;
    }
    printf("%s\n", "");
    printf("%s\n", hexa_concat("⚡ AUTO-BREAKTHROUGH triggered: ", trigger));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  log: ", prev_log), " → "), cur_log), " (+"), hexa_int_to_str((long)(log_delta))), ")"));
    const char* bt_path = hexa_concat(nexus_dir, "/mk2_hexa/native/real_breakthrough.hexa");
    const char* result = hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("cd ", nexus_dir), " && "), hexa_bin), " "), bt_path), " surprise 2>&1 | tail -20"));
    printf("%s\n", result);
    hexa_write_file(state_file, hexa_concat(hexa_concat(hexa_concat(hexa_concat(cur_log, "|"), cur_time), "|"), cur_atlas_md5));
    return printf("%s\n", "  state saved");
}

const char* run_engine(const char* project_arg, const char* stdin_hint) {
    long from_hook = hexa_contains(stdin_hint, "hook_event_name");
    const char* lockfile = hexa_concat(shared, "/.growth_engine.lock");
    long lock_acquired = 0;
    if ((!from_hook)) {
        const char* my_pid = hexa_trim(hexa_exec("echo $PPID"));
        long lock_free = 1;
        const char* existing_pid = hexa_trim(hexa_read_file(lockfile));
        if ((strcmp(existing_pid, "") != 0)) {
            const char* alive = hexa_trim(hexa_exec(hexa_concat(hexa_concat("kill -0 ", existing_pid), " 2>&1 && echo alive || echo dead")));
            if (hexa_contains(alive, "alive")) {
                lock_free = 0;
            }
        }
        if (lock_free) {
            hexa_write_file(lockfile, my_pid);
            lock_acquired = 1;
        } else {
            printf("%s\n", "⏭ breakthrough --engine 루프 1/1 사용 중 — 스킵");
            return "";
        }
    }
    const char* strat_path = hexa_concat(shared, "/growth_strategies.jsonl");
    const char* matched_project = "";
    hexa_arr matched_domains = hexa_arr_lit((long[]){(long)("math"), (long)("physics"), (long)("topology"), (long)("consciousness"), (long)("chemistry"), (long)("cosmology")}, 6);
    long matched_depth = 3;
    long matched_rounds = 50;
    hexa_arr matched_extra = hexa_arr_new();
    const char* strat_raw = hexa_read_file(strat_path);
    hexa_arr strat_lines = hexa_split(strat_raw, "\n");
    long si = 0;
    while ((si < strat_lines.n)) {
        const char* sl = hexa_trim(((const char*)strat_lines.d[si]));
        if ((strcmp(sl, "") == 0)) {
            si = (si + 1);
            continue;
        }
        const char* proj_name = "";
        if (hexa_contains(sl, "\"project\":\"")) {
            hexa_arr pp = hexa_split(sl, "\"project\":\"");
            if ((pp.n >= 2)) {
                proj_name = ((const char*)(hexa_split(((const char*)pp.d[1]), "\"")).d[0]);
            }
        }
        if ((strcmp(proj_name, "") == 0)) {
            si = (si + 1);
            continue;
        }
        long is_match = 0;
        if (((strcmp(project_arg, "auto") == 0) && (strcmp(stdin_hint, "") != 0))) {
            if (hexa_contains(sl, "\"keywords\":[")) {
                hexa_arr kp = hexa_split(sl, "\"keywords\":[");
                if ((kp.n >= 2)) {
                    hexa_arr kb = hexa_split(((const char*)kp.d[1]), "]");
                    if ((kb.n >= 1)) {
                        hexa_arr kw_items = hexa_split(((const char*)kb.d[0]), ",");
                        long __fi_kw_4 = 0;
                        while ((__fi_kw_4 < kw_items.n)) {
                            const char* kw = ((const char*)kw_items.d[__fi_kw_4]);
                            const char* kw_clean = hexa_replace(hexa_trim(kw), "\"", "");
                            if (((strcmp(kw_clean, "") != 0) && hexa_contains(stdin_hint, kw_clean))) {
                                is_match = 1;
                            }
                            __fi_kw_4 = (__fi_kw_4 + 1);
                        }
                    }
                }
            }
        } else {
            if ((strcmp(proj_name, project_arg) == 0)) {
                is_match = 1;
            }
        }
        if (is_match) {
            matched_project = proj_name;
            if (hexa_contains(sl, "\"domains\":[")) {
                hexa_arr dp = hexa_split(sl, "\"domains\":[");
                if ((dp.n >= 2)) {
                    hexa_arr db = hexa_split(((const char*)dp.d[1]), "]");
                    if ((db.n >= 1)) {
                        hexa_arr d_items = hexa_split(((const char*)db.d[0]), ",");
                        hexa_arr loaded = hexa_arr_new();
                        long __fi_d_5 = 0;
                        while ((__fi_d_5 < d_items.n)) {
                            const char* d = ((const char*)d_items.d[__fi_d_5]);
                            const char* dc = hexa_replace(hexa_trim(d), "\"", "");
                            if ((strcmp(dc, "") != 0)) {
                                loaded = hexa_arr_concat(loaded, hexa_arr_lit((long[]){(long)(dc)}, 1));
                            }
                            __fi_d_5 = (__fi_d_5 + 1);
                        }
                        if ((loaded.n > 0)) {
                            matched_domains = loaded;
                        }
                    }
                }
            }
            if (hexa_contains(sl, "\"depth\":")) {
                hexa_arr dep = hexa_split(sl, "\"depth\":");
                if ((dep.n >= 2)) {
                    const char* dv = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)dep.d[1]), ",")).d[0]), "}")).d[0]));
                    matched_depth = (long)(hexa_to_float(dv));
                }
            }
            if (hexa_contains(sl, "\"max_rounds\":")) {
                hexa_arr rp = hexa_split(sl, "\"max_rounds\":");
                if ((rp.n >= 2)) {
                    const char* rv = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)rp.d[1]), ",")).d[0]), "}")).d[0]));
                    matched_rounds = (long)(hexa_to_float(rv));
                }
            }
            if (hexa_contains(sl, "\"extra_cmds\":[")) {
                hexa_arr ep = hexa_split(sl, "\"extra_cmds\":[");
                if ((ep.n >= 2)) {
                    hexa_arr eb = hexa_split(((const char*)ep.d[1]), "]");
                    if ((eb.n >= 1)) {
                        hexa_arr e_items = hexa_split(((const char*)eb.d[0]), ",");
                        long __fi_ec_6 = 0;
                        while ((__fi_ec_6 < e_items.n)) {
                            const char* ec = ((const char*)e_items.d[__fi_ec_6]);
                            const char* ecc = hexa_replace(hexa_trim(ec), "\"", "");
                            if ((strcmp(ecc, "") != 0)) {
                                matched_extra = hexa_arr_concat(matched_extra, hexa_arr_lit((long[]){(long)(ecc)}, 1));
                            }
                            __fi_ec_6 = (__fi_ec_6 + 1);
                        }
                    }
                }
            }
        }
        si = (si + 1);
    }
    if ((strcmp(matched_project, "") == 0)) {
        matched_project = "nexus";
    }
    const char* seeds = load_seeds();
    blowup_loop(matched_domains, matched_rounds, matched_depth, seeds, hexa_concat("ENGINE — ", matched_project), 1);
    if ((matched_extra.n > 0)) {
        printf("%s\n", "");
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  📦 프로젝트 전용 명령 (", matched_project), "): "), hexa_int_to_str((long)(matched_extra.n))), "개"));
        long __fi_ecmd_7 = 0;
        while ((__fi_ecmd_7 < matched_extra.n)) {
            long ecmd = matched_extra.d[__fi_ecmd_7];
            const char* full_cmd = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("cd ", nexus_dir), " && "), hexa_bin), " "), nexus_dir), "/mk2_hexa/native/"), hexa_int_to_str((long)(ecmd)));
            printf("%s\n", hexa_concat("  → ", hexa_int_to_str((long)(ecmd))));
            const char* eout = hexa_exec(full_cmd);
            hexa_arr elines = hexa_split(eout, "\n");
            long ei = 0;
            while (((ei < elines.n) && (ei < 5))) {
                if ((strcmp(hexa_trim(((const char*)elines.d[ei])), "") != 0)) {
                    printf("%s\n", hexa_concat("    ", hexa_trim(((const char*)elines.d[ei]))));
                }
                ei = (ei + 1);
            }
            __fi_ecmd_7 = (__fi_ecmd_7 + 1);
        }
    }
    if (lock_acquired) {
        return hexa_exec(hexa_concat("rm -f ", lockfile));
    }
}

long run_auto_breakthrough(long converge) {
    rotate_log();
    hexa_arr domains = load_domains();
    const char* all_seeds = load_seeds();
    const char* gap_finder = hexa_concat(nexus_dir, "/mk2_hexa/native/gap_finder.hexa");
    long max_rounds = ((converge) ? (200) : (10));
    const char* label = ((converge) ? ("CONVERGE (수렴까지)") : ("BREAKTHROUGH (1회)"));
    printf("%s\n", "");
    printf("%s\n", "╔═══════════════════════════════════════════════════════════════╗");
    printf("%s\n", hexa_concat("║  🚀 ", label));
    printf("%s\n", "╚═══════════════════════════════════════════════════════════════╝");
    printf("%s\n", "");
    printf("%s\n", "━━━ [1/4] GAP SCAN ━━━");
    const char* gap_out = hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("cd ", nexus_dir), " && "), hexa_bin), " "), gap_finder), " quick"));
    hexa_arr lines = hexa_split(gap_out, "\n");
    long shown = 0;
    long __fi_l_8 = 0;
    while ((__fi_l_8 < lines.n)) {
        const char* l = ((const char*)lines.d[__fi_l_8]);
        if (((strcmp(hexa_trim(l), "") != 0) && (shown < 8))) {
            printf("%s\n", hexa_concat("  ", hexa_trim(l)));
            shown = (shown + 1);
        }
        __fi_l_8 = (__fi_l_8 + 1);
    }
    printf("%s\n", "");
    printf("%s\n", "━━━ [2/4] MINE ━━━");
    const char* mine_out = hexa_exec(hexa_concat(hexa_concat("grep '\"grade\":\"EXACT\"' ", log_path), " 2>/dev/null | grep -oE '\"value\":\"[^\"]*\"' | sed 's/\"value\":\"//;s/\"//' | sort | uniq -c | sort -rn | head -5"));
    hexa_arr mine_lines = hexa_split(mine_out, "\n");
    long mined = 0;
    long __fi_ml_9 = 0;
    while ((__fi_ml_9 < mine_lines.n)) {
        const char* ml = ((const char*)mine_lines.d[__fi_ml_9]);
        const char* t = hexa_trim(ml);
        if ((strcmp(t, "") != 0)) {
            printf("%s\n", hexa_concat("  ", t));
            hexa_arr parts = hexa_split(t, " ");
            if ((parts.n >= 2)) {
                const char* val = hexa_trim(((const char*)parts.d[(parts.n - 1)]));
                if (((strcmp(val, "") != 0) && (!hexa_contains(all_seeds, val)))) {
                    all_seeds = hexa_concat(hexa_concat(all_seeds, "|"), val);
                    mined = (mined + 1);
                }
            }
        }
        __fi_ml_9 = (__fi_ml_9 + 1);
    }
    if ((mined > 0)) {
        printf("%s\n", hexa_concat(hexa_concat("  +", hexa_int_to_str((long)(mined))), " mined seeds"));
    }
    printf("%s\n", "");
    printf("%s\n", "━━━ [3/4] BLOWUP ━━━");
    blowup_loop(domains, max_rounds, 3, all_seeds, label, 1);
    printf("%s\n", "");
    return printf("%s\n", "━━━ [4/4] DONE ━━━");
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    home = hexa_exec("printenv HOME");
    hexa_bin = hexa_concat(home, "/Dev/hexa-lang/target/release/hexa");
    nexus_dir = hexa_concat(home, "/Dev/nexus");
    shared = hexa_concat(nexus_dir, "/shared");
    blowup_path = hexa_concat(nexus_dir, "/mk2_hexa/native/blowup.hexa");
    seed_engine_path = hexa_concat(nexus_dir, "/mk2_hexa/native/seed_engine.hexa");
    log_path = hexa_concat(shared, "/discovery_log.jsonl");
    a = hexa_args();
    mode = (((a.n >= 3)) ? (((const char*)a.d[2])) : (""));
    arg3 = (((a.n >= 4)) ? (((const char*)a.d[3])) : (""));
    arg4 = (((a.n >= 5)) ? (((const char*)a.d[4])) : (""));
    arg5 = (((a.n >= 6)) ? (((const char*)a.d[5])) : (""));
    prev_exact = (-1);
    prev_total = (-1);
    seen_seeds = 0;
    0;
    init_parts = hexa_split(all_seeds, "|");
    while ((ii < init_parts.n)) {
        const char* sv = hexa_trim(((const char*)init_parts.d[ii]));
        if ((strcmp(sv, "") != 0)) {
            seen_seeds[sv] = 1;
        }
        ii = (ii + 1);
    }
    start_ts = hexa_trim(hexa_exec("date +%s"));
    while (((round <= max_rounds) && (!exhausted))) {
        long di = ((round - 1) % (long)(domains.n));
        const char* domain = ((const char*)domains.d[di]);
        const char* t0 = hexa_trim(hexa_exec("date +%s"));
        const char* out = "";
        out = run_blowup(domain, depth, all_seeds, fast);
        const char* t1 = hexa_trim(hexa_exec("date +%s"));
        long elapsed = ((long)(hexa_to_float(t1)) - (long)(hexa_to_float(t0)));
        hexa_arr res = parse_result(out);
        long exact = res.d[0];
        long total = res.d[1];
        long pool = res.d[2];
        stats["total_cor"] = (stats["total_cor"] + total);
        stats["total_exact"] = (stats["total_exact"] + exact);
        if (((total == prev_total) && (exact == prev_exact))) {
            stats["sat_count"] = (stats["sat_count"] + 1);
            if (((stats["sat_count"] >= 3) && (depth < 6))) {
                depth = (depth + 1);
                stats["depth_ups"] = (stats["depth_ups"] + 1);
                stats["sat_count"] = 0;
                printf("%s\n", hexa_concat("  ⚡ saturation → depth ", hexa_int_to_str((long)(depth))));
            }
            if ((stats["sat_count"] >= 6)) {
                exhausted = 1;
            }
        } else {
            stats["sat_count"] = 0;
        }
        prev_total = total;
        prev_exact = exact;
        hexa_arr seed_res = extract_seeds(out, seen_seeds);
        long added = seed_res.d[0];
        const char* new_s = hexa_int_to_str((long)(seed_res.d[1]));
        if ((strcmp(new_s, "") != 0)) {
            all_seeds = hexa_concat(all_seeds, new_s);
        }
        all_seeds = cap_seeds(all_seeds, 80);
        long cur_seeds = (hexa_split(all_seeds, "|")).n;
        const char* sat_marker = (((stats["sat_count"] > 0)) ? (hexa_concat(" sat=", hexa_int_to_str((long)(stats["sat_count"])))) : (""));
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  [", hexa_int_to_str((long)(round))), "/"), hexa_int_to_str((long)(max_rounds))), "] "), domain), " "), hexa_int_to_str((long)(total))), "cor "), hexa_int_to_str((long)(exact))), "EXACT pool="), hexa_int_to_str((long)(pool))), " +"), hexa_int_to_str((long)(added))), "seeds("), hexa_int_to_str((long)(cur_seeds))), ") d="), hexa_int_to_str((long)(depth))), " "), hexa_int_to_str((long)(elapsed))), "s"), sat_marker));
        round = (round + 1);
    }
    end_ts = hexa_trim(hexa_exec("date +%s"));
    total_elapsed = ((long)(hexa_to_float(end_ts)) - (long)(hexa_to_float(start_ts)));
    final_seeds = (hexa_split(all_seeds, "|")).n;
    rho = calc_rho(stats["total_exact"], stats["total_cor"]);
    printf("%s\n", "");
    printf("%s\n", "╔═══════════════════════════════════════════════════════════════╗");
    printf("%s\n", hexa_concat(hexa_concat("║  🚀 ", label), " RESULT"));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("║  ", hexa_int_to_str((long)(total_elapsed))), "s | "), hexa_int_to_str((long)((round - 1)))), " rounds | "), hexa_int_to_str((long)(stats["total_cor"]))), " cor | "), hexa_int_to_str((long)(stats["total_exact"]))), " EXACT"));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("║  ρ=", rho), " | seeds: "), hexa_int_to_str((long)(seed_count))), "→"), hexa_int_to_str((long)(final_seeds))), " | depth_ups="), hexa_int_to_str((long)(stats["depth_ups"]))));
    if (exhausted) {
        printf("%s\n", "║  🏁 고갈 (EXHAUSTED)");
    } else {
        printf("%s\n", hexa_concat(hexa_concat("║  ⏱ max_rounds 도달 (", hexa_int_to_str((long)(max_rounds))), ")"));
    }
    printf("%s\n", "╚═══════════════════════════════════════════════════════════════╝");
    0;
    if ((strcmp(mode, "--converge") == 0)) {
        run_auto_breakthrough(1);
    } else {
        if ((strcmp(mode, "--idea") == 0)) {
            const char* idea_seed = (((strcmp(arg3, "") != 0)) ? (arg3) : (""));
            if ((strcmp(idea_seed, "") == 0)) {
                printf("%s\n", "사용법: hexa breakthrough.hexa --idea '값|값' [depth] [rounds]");
            } else {
                rotate_log();
                const char* base_seeds = load_seeds();
                const char* combined = hexa_concat(hexa_concat(base_seeds, "|"), idea_seed);
                hexa_arr domains = load_domains();
                long depth = (((strcmp(arg4, "") != 0)) ? (0) : (6));
                long rounds = (((strcmp(arg5, "") != 0)) ? (0) : (10));
                printf("%s\n", "");
                printf("%s\n", hexa_concat(hexa_concat("━━━ IDEA: ", idea_seed), " ━━━"));
                blowup_loop(domains, rounds, depth, combined, "IDEA BLOWUP", 1);
            }
        } else {
            if ((strcmp(mode, "--engine") == 0)) {
                const char* proj = (((strcmp(arg3, "") != 0)) ? (arg3) : ("auto"));
                const char* hint = (((strcmp(arg4, "") != 0)) ? (arg4) : (""));
                run_engine(proj, hint);
            } else {
                if ((strcmp(mode, "--help") == 0)) {
                    printf("%s\n", "사용법:");
                    printf("%s\n", "  hexa breakthrough.hexa              1회 돌파 (자동 최적 전략)");
                    printf("%s\n", "  hexa breakthrough.hexa --converge   수렴까지 연속 (고갈 시 종료)");
                    printf("%s\n", "  hexa breakthrough.hexa --idea '값'  특정 seed 집중 돌파");
                } else {
                    run_auto_breakthrough(0);
                }
            }
        }
    }
    return 0;
}
