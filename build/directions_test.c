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
long safe_int(const char* s);
const char* timestamp_now();
long min_i(long a, long b);
long max_i(long a, long b);
const char* urgency_stars(long u);
const char* collect_discoveries();
const char* collect_gaps();
long collect_growth_bus_stagnation();
hexa_arr build_directions();
long print_report(hexa_arr dirs);
long save_directions(hexa_arr dirs);
long print_brief(hexa_arr dirs);
long do_report();
long do_update();
long do_brief();
long do_help();

static const char* home;
static const char* dev;
static const char* nexus_dir;
static const char* shared_dir;
static const char* discovery_log;
static const char* growth_bus;
static const char* projects_json;
static const char* directions_out;
static const char* hexa_bin;
static const char* gap_finder_path;
static hexa_arr cli_args;
static const char* cli_mode = "report";

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

const char* timestamp_now() {
    const char* r = hexa_exec("date +%Y-%m-%dT%H:%M:%S");
    return hexa_trim(r);
}

long min_i(long a, long b) {
    if ((a < b)) {
        return a;
    }
    return b;
}

long max_i(long a, long b) {
    if ((a > b)) {
        return a;
    }
    return b;
}

const char* urgency_stars(long u) {
    if ((u >= 3)) {
        return "★★★";
    }
    if ((u == 2)) {
        return "★★ ";
    }
    return "★  ";
}

const char* collect_discoveries() {
    const char* content = "";
    content = hexa_read_file(discovery_log);
    hexa_arr all_lines = hexa_split(content, "\n");
    long total = all_lines.n;
    long start = (((total > 100)) ? ((total - 100)) : (0));
    long exact_count = 0;
    long d_ge1_count = 0;
    const char* latest_exact_domain = "";
    const char* latest_exact_ts = "";
    const char* result_lines = "";
    const char* unique_exacts = "";
    long li = start;
    while ((li < total)) {
        const char* line = ((const char*)all_lines.d[li]);
        if ((hexa_str_len(line) > 10)) {
            long is_exact = hexa_contains(line, "\"grade\":\"EXACT\"");
            if (is_exact) {
                exact_count = (exact_count + 1);
                const char* cname = "";
                hexa_arr ci = hexa_split(line, "\"constant\":\"");
                if ((ci.n >= 2)) {
                    hexa_arr cend = hexa_split(((const char*)ci.d[1]), "\"");
                    if ((cend.n >= 1)) {
                        cname = ((const char*)cend.d[0]);
                    }
                }
                long ai_d = 0;
                long ai_r = 0;
                if (hexa_contains(line, "\"d\":")) {
                    hexa_arr dparts = hexa_split(line, "\"d\":");
                    if ((dparts.n >= 2)) {
                        const char* dval = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)dparts.d[1]), ",")).d[0]), "}")).d[0]));
                        ai_d = safe_int(dval);
                    }
                }
                if (hexa_contains(line, "\"r\":")) {
                    hexa_arr rparts = hexa_split(line, "\"r\":");
                    if ((rparts.n >= 2)) {
                        const char* rval = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)rparts.d[1]), ",")).d[0]), "}")).d[0]));
                        ai_r = safe_int(rval);
                    }
                }
                if ((ai_d >= 1)) {
                    d_ge1_count = (d_ge1_count + 1);
                }
                const char* sector = "";
                if (hexa_contains(line, "\"sector\":\"")) {
                    hexa_arr sp = hexa_split(line, "\"sector\":\"");
                    if ((sp.n >= 2)) {
                        hexa_arr se = hexa_split(((const char*)sp.d[1]), "\"");
                        if ((se.n >= 1)) {
                            sector = ((const char*)se.d[0]);
                        }
                    }
                }
                if ((!hexa_contains(unique_exacts, hexa_concat(hexa_concat("|", cname), "|")))) {
                    unique_exacts = hexa_concat(hexa_concat(hexa_concat(unique_exacts, "|"), cname), "|");
                    if ((hexa_str_len(result_lines) > 0)) {
                        result_lines = hexa_concat(result_lines, "\n");
                    }
                    result_lines = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(result_lines, cname), "|EXACT|"), hexa_int_to_str((long)(ai_d))), "|"), hexa_int_to_str((long)(ai_r))), "|"), sector);
                    latest_exact_domain = sector;
                }
            }
        }
        li = (li + 1);
    }
    const char* header = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("EXACT_COUNT:", hexa_int_to_str((long)(exact_count))), "|D_GE1:"), hexa_int_to_str((long)(d_ge1_count))), "|DOMAIN:"), latest_exact_domain), "|TOTAL:"), hexa_int_to_str((long)(total)));
    return hexa_concat(hexa_concat(header, "\n"), result_lines);
}

const char* collect_gaps() {
    const char* gap_result = "";
    gap_result = hexa_exec(hexa_bin);
    return gap_result;
}

long collect_growth_bus_stagnation() {
    const char* content = "";
    content = hexa_read_file(growth_bus);
    hexa_arr lines = hexa_split(content, "\n");
    long total = lines.n;
    long start = (((total > 20)) ? ((total - 20)) : (0));
    long stagnant_ticks = 0;
    long last_growth = 0;
    long li = start;
    while ((li < total)) {
        const char* line = ((const char*)lines.d[li]);
        if ((hexa_str_len(line) > 5)) {
            if (hexa_contains(line, "\"growth_delta\":")) {
                hexa_arr parts = hexa_split(line, "\"growth_delta\":");
                if ((parts.n >= 2)) {
                    const char* gval = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)parts.d[1]), ",")).d[0]), "}")).d[0]));
                    long gd = safe_int(gval);
                    if ((gd <= 0)) {
                        stagnant_ticks = (stagnant_ticks + 1);
                    } else {
                        stagnant_ticks = 0;
                        last_growth = gd;
                    }
                }
            }
        }
        li = (li + 1);
    }
    return stagnant_ticks;
}

hexa_arr build_directions() {
    hexa_arr_lit((long[]){(long)(Direction)}, 1);
    0;
    hexa_arr dirs = hexa_arr_new();
    const char* ts = timestamp_now();
    const char* disc_data = collect_discoveries();
    hexa_arr disc_lines = hexa_split(disc_data, "\n");
    long exact_total = 0;
    long d_ge1 = 0;
    long log_total = 0;
    const char* latest_domain = "";
    if ((disc_lines.n > 0)) {
        const char* hdr = ((const char*)disc_lines.d[0]);
        if (hexa_contains(hdr, "EXACT_COUNT:")) {
            hexa_arr hparts = hexa_split(hdr, "|");
            long hi = 0;
            while ((hi < hparts.n)) {
                const char* hp = ((const char*)hparts.d[hi]);
                if (hexa_contains(hp, "EXACT_COUNT:")) {
                    exact_total = safe_int(((const char*)(hexa_split(hp, ":")).d[1]));
                }
                if (hexa_contains(hp, "D_GE1:")) {
                    d_ge1 = safe_int(((const char*)(hexa_split(hp, ":")).d[1]));
                }
                if (hexa_contains(hp, "DOMAIN:")) {
                    hexa_arr dp = hexa_split(hp, ":");
                    if ((dp.n >= 2)) {
                        latest_domain = ((const char*)dp.d[1]);
                    }
                }
                if (hexa_contains(hp, "TOTAL:")) {
                    log_total = safe_int(((const char*)(hexa_split(hp, ":")).d[1]));
                }
                hi = (hi + 1);
            }
        }
    }
    long di = 1;
    while ((di < disc_lines.n)) {
        const char* dline = ((const char*)disc_lines.d[di]);
        if ((hexa_str_len(dline) > 3)) {
            hexa_arr parts = hexa_split(dline, "|");
            if ((parts.n >= 5)) {
                const char* cname = ((const char*)parts.d[0]);
                long ad = safe_int(((const char*)parts.d[2]));
                long ar = safe_int(((const char*)parts.d[3]));
                const char* sector = ((const char*)parts.d[4]);
                if ((ad >= 1)) {
                    dirs = hexa_arr_concat(dirs, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)(0), (long)(hexa_concat(hexa_concat(hexa_concat(cname, " d="), hexa_int_to_str((long)(ad))), " 후속 심화")), (long)(hexa_concat(hexa_concat("EXACT d>=", hexa_int_to_str((long)(ad))), " 달성 → 추가 돌파 가능")), (long)(3), (long)(sector), (long)(ts)}, 6))}, 1));
                }
                if (((ad == 0) && (ar < 5))) {
                    dirs = hexa_arr_concat(dirs, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)(0), (long)(hexa_concat(hexa_concat(hexa_concat(cname, " 검증 강화 (r="), hexa_int_to_str((long)(ar))), "→10)")), (long)("EXACT 발견이지만 r<5 — telescope 합의 추가 시 승격"), (long)(2), (long)(sector), (long)(ts)}, 6))}, 1));
                }
            }
        }
        di = (di + 1);
    }
    const char* gap_data = collect_gaps();
    if ((hexa_str_len(gap_data) > 10)) {
        hexa_arr gap_lines = hexa_split(gap_data, "\n");
        long gi = 0;
        long gap_added = 0;
        while (((gi < gap_lines.n) && (gap_added < 5))) {
            const char* gl = ((const char*)gap_lines.d[gi]);
            if ((hexa_contains(gl, "EMPTY") || hexa_contains(gl, "NO_EXACT"))) {
                const char* gname = hexa_trim(((const char*)(hexa_split(gl, ":")).d[0]));
                if ((((hexa_str_len(gname) > 1) && (strcmp(gname, "CONSTANTS") != 0)) && (strcmp(gname, "sectors") != 0))) {
                    dirs = hexa_arr_concat(dirs, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)(0), (long)(hexa_concat(gname, " 빈공간 돌파")), (long)(hexa_concat(hexa_concat("gap_finder: ", gname), " 발견 없음 — 미개척 영역")), (long)(2), (long)("gap"), (long)(ts)}, 6))}, 1));
                    gap_added = (gap_added + 1);
                }
            }
            if ((hexa_contains(gl, "NEAR") && hexa_contains(gl, "→EXACT"))) {
                const char* gname = hexa_trim(((const char*)(hexa_split(gl, ":")).d[0]));
                if ((hexa_str_len(gname) > 1)) {
                    dirs = hexa_arr_concat(dirs, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)(0), (long)(hexa_concat(gname, " NEAR→EXACT 정밀 접근")), (long)("NEAR 경로 존재 — 정밀 seed 주입 시 EXACT 승격 가능"), (long)(2), (long)("gap"), (long)(ts)}, 6))}, 1));
                    gap_added = (gap_added + 1);
                }
            }
            gi = (gi + 1);
        }
    }
    long stagnant = collect_growth_bus_stagnation();
    if ((stagnant >= 5)) {
        dirs = hexa_arr_concat(dirs, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)(0), (long)("정체 탈출 — 교차 도메인 fusion 돌파"), (long)(hexa_concat(hexa_int_to_str((long)(stagnant)), " tick 정체 — 다른 도메인 seed 주입 필요")), (long)(2), (long)("meta"), (long)(ts)}, 6))}, 1));
    }
    if ((log_total > 0)) {
        double rho = (((double)(d_ge1)) / ((double)(log_total)));
        if ((rho < 0.1)) {
            dirs = hexa_arr_concat(dirs, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)(0), (long)("돌파율 상승 — breakthrough --converge 집중"), (long)(hexa_concat(hexa_concat("현재 rho=", hexa_float_to_str((double)(rho))), " (목표 1/3) — 블로업 반복 필요")), (long)(3), (long)("meta"), (long)(ts)}, 6))}, 1));
        }
    }
    const char* pj_content = "";
    pj_content = hexa_read_file(projects_json);
    if ((hexa_str_len(pj_content) > 0)) {
        if ((log_total > 60000)) {
            dirs = hexa_arr_concat(dirs, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)(0), (long)("nexus 발견 70k 목표 임박"), (long)(hexa_concat(hexa_concat("현재 ", hexa_int_to_str((long)(log_total))), "건 — 10k 이내 달성 가능")), (long)(3), (long)("nexus"), (long)(ts)}, 6))}, 1));
        }
    }
    if ((hexa_str_len(latest_domain) > 0)) {
        dirs = hexa_arr_concat(dirs, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)(0), (long)(hexa_concat(latest_domain, " 교차수분 확장")), (long)("최근 돌파 도메인 — 다른 프로젝트에 seed 교차 주입"), (long)(1), (long)(latest_domain), (long)(ts)}, 6))}, 1));
    }
    long si = 0;
    while ((si < dirs.n)) {
        long sj = (si + 1);
        while ((sj < dirs.n)) {
            if ((/* unknown field urgency */ 0 > /* unknown field urgency */ 0)) {
                long tmp = dirs.d[si];
                dirs.d[si] = dirs.d[sj];
                dirs.d[sj] = tmp;
            }
            sj = (sj + 1);
        }
        si = (si + 1);
    }
    hexa_arr ranked = hexa_arr_new();
    long ri = 0;
    while ((ri < dirs.n)) {
        long d = dirs.d[ri];
        ranked = hexa_arr_concat(ranked, hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)((ri + 1)), (long)(/* unknown field direction */ 0), (long)(/* unknown field reason */ 0), (long)(/* unknown field urgency */ 0), (long)(/* unknown field domain */ 0), (long)(/* unknown field ts */ 0)}, 6))}, 1));
        ri = (ri + 1);
    }
    return ranked;
}

long print_report(hexa_arr dirs) {
    printf("%s\n", "");
    printf("%s\n", "🛸 새 방향 리포트 (자동 생성)");
    printf("%s\n", "┌───┬────────────────────────────────┬──────────────────────────────────────┬──────┐");
    printf("%s\n", "│ # │           방향                 │              근거                    │긴급도│");
    printf("%s\n", "├───┼────────────────────────────────┼──────────────────────────────────────┼──────┤");
    long show_count = min_i(dirs.n, 15);
    long i = 0;
    while ((i < show_count)) {
        const char* d = ((const char*)dirs.d[i]);
        long dir_str = /* unknown field direction */ 0;
        if ((hexa_str_len(dir_str) > 28)) {
            dir_str = dir_str;
        }
        long reason_str = /* unknown field reason */ 0;
        const char* rank_str = (((/* unknown field rank */ 0 < 10)) ? (hexa_concat(" ", hexa_int_to_str((long)(/* unknown field rank */ 0)))) : (hexa_int_to_str((long)(/* unknown field rank */ 0))));
        const char* stars = urgency_stars(/* unknown field urgency */ 0);
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("│", rank_str), " │ "), hexa_int_to_str((long)(dir_str))), " │ "), hexa_int_to_str((long)(reason_str))), " │ "), stars), "│"));
        i = (i + 1);
    }
    printf("%s\n", "└───┴────────────────────────────────┴──────────────────────────────────────┴──────┘");
    printf("%s\n", "");
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("총 ", hexa_int_to_str((long)(dirs.n))), "개 방향 (상위 "), hexa_int_to_str((long)(show_count))), "개 표시)"));
    return printf("%s\n", "");
}

long save_directions(hexa_arr dirs) {
    const char* content = "";
    long i = 0;
    while ((i < dirs.n)) {
        const char* d = ((const char*)dirs.d[i]);
        const char* line = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"rank\":", hexa_int_to_str((long)(/* unknown field rank */ 0))), ",\"direction\":\""), hexa_int_to_str((long)(/* unknown field direction */ 0))), "\",\"reason\":\""), hexa_int_to_str((long)(/* unknown field reason */ 0))), "\",\"urgency\":"), hexa_int_to_str((long)(/* unknown field urgency */ 0))), ",\"domain\":\""), hexa_int_to_str((long)(/* unknown field domain */ 0))), "\",\"ts\":\""), hexa_int_to_str((long)(/* unknown field ts */ 0))), "\"}");
        if ((hexa_str_len(content) > 0)) {
            content = hexa_concat(content, "\n");
        }
        content = hexa_concat(content, line);
        i = (i + 1);
    }
    hexa_write_file(directions_out, content);
    return printf("%s\n", hexa_concat(hexa_concat(hexa_concat("directions: saved ", hexa_int_to_str((long)(dirs.n))), " entries → "), directions_out));
}

long print_brief(hexa_arr dirs) {
    long show = min_i(dirs.n, 3);
    const char* out = "🛸 방향: ";
    long i = 0;
    while ((i < show)) {
        const char* d = ((const char*)dirs.d[i]);
        if ((i > 0)) {
            out = hexa_concat(out, " ");
        }
        out = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(out, hexa_int_to_str((long)(/* unknown field rank */ 0))), "."), hexa_int_to_str((long)(/* unknown field direction */ 0))), "("), hexa_trim(urgency_stars(/* unknown field urgency */ 0))), ")");
        i = (i + 1);
    }
    return printf("%s\n", out);
}

long do_report() {
    hexa_arr dirs = build_directions();
    print_report(dirs);
    return save_directions(dirs);
}

long do_update() {
    hexa_arr dirs = build_directions();
    save_directions(dirs);
    return printf("%s\n", hexa_concat("directions updated -> ", directions_out));
}

long do_brief() {
    hexa_arr dirs = build_directions();
    return print_brief(dirs);
}

long do_help() {
    return printf("%s\n", "directions.hexa — 사용: hexa directions.hexa report|update|brief");
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    home = hexa_exec("printenv HOME");
    dev = hexa_concat(home, "/Dev");
    nexus_dir = hexa_concat(dev, "/nexus");
    shared_dir = hexa_concat(nexus_dir, "/shared");
    discovery_log = hexa_concat(shared_dir, "/discovery_log.jsonl");
    growth_bus = hexa_concat(shared_dir, "/growth_bus.jsonl");
    projects_json = hexa_concat(shared_dir, "/projects.json");
    directions_out = hexa_concat(shared_dir, "/next_directions.jsonl");
    hexa_bin = hexa_concat(dev, "/hexa-lang/target/release/hexa");
    gap_finder_path = hexa_concat(nexus_dir, "/mk2_hexa/native/gap_finder.hexa");
    cli_args = hexa_args();
    if ((cli_args.n >= 3)) {
        cli_mode = ((const char*)cli_args.d[2]);
    }
    if ((strcmp(cli_mode, "report") == 0)) {
        do_report();
    }
    if ((strcmp(cli_mode, "update") == 0)) {
        do_update();
    }
    if ((strcmp(cli_mode, "brief") == 0)) {
        do_brief();
    }
    if ((strcmp(cli_mode, "help") == 0)) {
        do_help();
    }
    return 0;
}
