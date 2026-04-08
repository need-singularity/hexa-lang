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

const char* step_type_name(long t);
long* step_scan(const char* domain, long tier);
long* step_verify(double tolerance);
long* step_filter(double min_confidence);
long* step_experiment(const char* exp_type);
long* step_register();
long* step_redteam();
long* step_publish();
long* step_custom(const char* name, const char* desc);
const char* step_display(long *s);
long pipeline_len(long *p);
long pipeline_is_empty(long *p);
const char* pipeline_display(long *p);
double completion_ratio(long *r);
long is_complete(long *r);
long* execute_pipeline(long *p);
long* parse_dsl_step(const char* s);
long* build_from_dsl(const char* name, hexa_arr dsl_steps);
long* standard_discovery(const char* domain);
long* deep_exploration(const char* domain);

static long step_scan = 0;
static long step_verify = 1;
static long step_experiment = 2;
static long step_filter = 3;
static long step_register = 4;
static long step_redteam = 5;
static long step_publish = 6;
static long step_custom = 7;
static long* std_pipe;
static long* deep_pipe;
static long* result;
static double cr;
static long* empty_pipe;
static long* empty_result;
static long* partial;
static long* dsl_pipe;

const char* step_type_name(long t) {
    if ((t == 0)) {
        return "Scan";
    } else if ((t == 1)) {
        return "Verify";
    } else if ((t == 2)) {
        return "Experiment";
    } else if ((t == 3)) {
        return "Filter";
    } else if ((t == 4)) {
        return "Register";
    } else if ((t == 5)) {
        return "RedTeam";
    } else if ((t == 6)) {
        return "Publish";
    } else if ((t == 7)) {
        return "Custom";
    } else {
        return "Unknown";
    }







}

long* step_scan(const char* domain, long tier) {
    return hexa_struct_alloc((long[]){(long)(step_scan), (long)(domain), (long)(tier), (long)(0.0), (long)(0.0), (long)(""), (long)(""), (long)("")}, 8);
}

long* step_verify(double tolerance) {
    return hexa_struct_alloc((long[]){(long)(step_verify), (long)(""), (long)(0), (long)(tolerance), (long)(0.0), (long)(""), (long)(""), (long)("")}, 8);
}

long* step_filter(double min_confidence) {
    return hexa_struct_alloc((long[]){(long)(step_filter), (long)(""), (long)(0), (long)(0.0), (long)(min_confidence), (long)(""), (long)(""), (long)("")}, 8);
}

long* step_experiment(const char* exp_type) {
    return hexa_struct_alloc((long[]){(long)(step_experiment), (long)(""), (long)(0), (long)(0.0), (long)(0.0), (long)(exp_type), (long)(""), (long)("")}, 8);
}

long* step_register() {
    return hexa_struct_alloc((long[]){(long)(step_register), (long)(""), (long)(0), (long)(0.0), (long)(0.0), (long)(""), (long)(""), (long)("")}, 8);
}

long* step_redteam() {
    return hexa_struct_alloc((long[]){(long)(step_redteam), (long)(""), (long)(0), (long)(0.0), (long)(0.0), (long)(""), (long)(""), (long)("")}, 8);
}

long* step_publish() {
    return hexa_struct_alloc((long[]){(long)(step_publish), (long)(""), (long)(0), (long)(0.0), (long)(0.0), (long)(""), (long)(""), (long)("")}, 8);
}

long* step_custom(const char* name, const char* desc) {
    return hexa_struct_alloc((long[]){(long)(step_custom), (long)(""), (long)(0), (long)(0.0), (long)(0.0), (long)(""), (long)(name), (long)(desc)}, 8);
}

const char* step_display(long *s) {
    if ((s[0] == 0)) {
        return hexa_concat(hexa_concat(hexa_concat(hexa_concat("Scan(", hexa_int_to_str((long)(s[1]))), ", tier="), hexa_int_to_str((long)(s[2]))), ")");
    } else if ((s[0] == 1)) {
        return hexa_concat(hexa_concat("Verify(tol=", hexa_int_to_str((long)(s[3]))), ")");
    } else if ((s[0] == 2)) {
        return hexa_concat(hexa_concat("Experiment(", hexa_int_to_str((long)(s[5]))), ")");
    } else if ((s[0] == 3)) {
        return hexa_concat(hexa_concat("Filter(min=", hexa_int_to_str((long)(s[4]))), ")");
    } else if ((s[0] == 4)) {
        return "Register";
    } else if ((s[0] == 5)) {
        return "RedTeam";
    } else if ((s[0] == 6)) {
        return "Publish";
    } else if ((s[0] == 7)) {
        return hexa_concat(hexa_concat("Custom(", hexa_int_to_str((long)(s[6]))), ")");
    } else {
        return "Unknown";
    }







}

long pipeline_len(long *p) {
    return hexa_str_len(p[1]);
}

long pipeline_is_empty(long *p) {
    return (hexa_str_len(p[1]) == 0);
}

const char* pipeline_display(long *p) {
    const char* out = hexa_concat(hexa_concat(hexa_concat(hexa_concat("Pipeline: ", hexa_int_to_str((long)(p[0]))), " ("), hexa_int_to_str((long)(pipeline_len(p)))), " steps)\n");
    long i = 0;
    while ((i < pipeline_len(p))) {
        out = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(out, "  ["), hexa_int_to_str((long)((i + 1)))), "] "), step_display(p[1][i])), "\n");
        i = (i + 1);
    }
    return out;
}

double completion_ratio(long *r) {
    if ((r[1] == 0)) {
        return 1.0;
    } else {
        return (((double)(r[0])) / ((double)(r[1])));
    }
}

long is_complete(long *r) {
    return (r[0] == r[1]);
}

long* execute_pipeline(long *p) {
    long total = pipeline_len(p);
    long steps_done = 0;
    hexa_arr pool = hexa_arr_new();
    long disc_count = 0;
    long filtered = 0;
    long s = 0;
    while ((s < total)) {
        long step = p[1][s];
        if ((/* unknown field step_type */ 0 == 0)) {
            long n_found = ((/* unknown field tier */ 0 + 1) * 3);
            long j = 0;
            while ((j < n_found)) {
                double conf = (0.5 + (((double)(j)) * 0.05));
                double conf_capped = (((conf > 0.95)) ? (0.95) : (conf));
                const char* item_id = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(/* unknown field domain */ 0)), "-"), hexa_int_to_str((long)(/* unknown field tier */ 0))), "-"), hexa_int_to_str((long)(j)));
                pool = append(pool, hexa_struct_alloc((long[]){(long)(item_id), (long)(conf_capped)}, 2));
                j = (j + 1);
            }
        } else if ((/* unknown field step_type */ 0 == 1)) {
            hexa_arr kept = hexa_arr_new();
            long j = 0;
            while ((j < pool.n)) {
                if ((/* unknown field confidence */ 0 > /* unknown field tolerance */ 0)) {
                    kept = append(kept, pool.d[j]);
                } else {
                    filtered = (filtered + 1);
                }
                j = (j + 1);
            }
            pool = kept;
        } else if ((/* unknown field step_type */ 0 == 2)) {
            long j = 0;
            while ((j < pool.n)) {
                double boosted = (/* unknown field confidence */ 0 * 1.1);
                /* unknown field confidence */ 0;
                0;
                if ((boosted > 1.0)) {
                    1.0;
                } else {
                    boosted;
                }
                j = (j + 1);
            }
        } else if ((/* unknown field step_type */ 0 == 3)) {
            hexa_arr kept = hexa_arr_new();
            long j = 0;
            while ((j < pool.n)) {
                if ((/* unknown field confidence */ 0 >= /* unknown field min_confidence */ 0)) {
                    kept = append(kept, pool.d[j]);
                } else {
                    filtered = (filtered + 1);
                }
                j = (j + 1);
            }
            pool = kept;
        } else if ((/* unknown field step_type */ 0 == 4)) {
            disc_count = (disc_count + pool.n);
        } else if ((/* unknown field step_type */ 0 == 5)) {
            if ((pool.n > 6)) {
                long cut = (pool.n / 6);
                long j = 0;
                while ((j < (pool.n - 1))) {
                    long min_idx = j;
                    long k = (j + 1);
                    while ((k < pool.n)) {
                        if ((/* unknown field confidence */ 0 < /* unknown field confidence */ 0)) {
                            min_idx = k;
                        }
                        k = (k + 1);
                    }
                    if ((min_idx != j)) {
                        long tmp = pool.d[j];
                        pool.d[j] = pool.d[min_idx];
                        pool.d[min_idx] = tmp;
                    }
                    j = (j + 1);
                }
                hexa_arr kept = hexa_arr_new();
                long j2 = cut;
                while ((j2 < pool.n)) {
                    kept = append(kept, pool.d[j2]);
                    j2 = (j2 + 1);
                }
                filtered = (filtered + cut);
                pool = kept;
            }
        } else if ((/* unknown field step_type */ 0 == 6)) {
        } else if ((/* unknown field step_type */ 0 == 7)) {
        } else {
        }







        steps_done = (steps_done + 1);
        s = (s + 1);
    }
    return hexa_struct_alloc((long[]){(long)(steps_done), (long)(total), (long)(disc_count), (long)(filtered)}, 4);
}

long* parse_dsl_step(const char* s) {
    long parts = split(s, ":");
    long cmd = parts[0];
    if ((cmd == "scan")) {
        return step_scan(parts[1], (long)(parts[2]));
    } else if ((cmd == "verify")) {
        return step_verify(((double)(parts[1])));
    } else if ((cmd == "filter")) {
        return step_filter(((double)(parts[1])));
    } else if ((cmd == "experiment")) {
        return step_experiment(parts[1]);
    } else if ((cmd == "register")) {
        return step_register();
    } else if ((cmd == "red_team")) {
        return step_redteam();
    } else if ((cmd == "publish")) {
        return step_publish();
    } else if ((cmd == "custom")) {
        return step_custom(parts[1], parts[2]);
    } else {
        return step_custom("error", hexa_concat("unknown step: ", hexa_int_to_str((long)(cmd))));
    }







}

long* build_from_dsl(const char* name, hexa_arr dsl_steps) {
    hexa_arr steps = hexa_arr_new();
    long i = 0;
    while ((i < dsl_steps.n)) {
        steps = append(steps, parse_dsl_step(dsl_steps.d[i]));
        i = (i + 1);
    }
    return hexa_struct_alloc((long[]){(long)(name), (long)(steps)}, 2);
}

long* standard_discovery(const char* domain) {
    return hexa_struct_alloc((long[]){(long)(hexa_concat("standard-discovery-", domain)), (long)(hexa_arr_lit((long[]){(long)(step_scan(domain, 1)), (long)(step_verify(0.05)), (long)(step_filter(0.6)), (long)(step_experiment("tension")), (long)(step_redteam()), (long)(step_register()), (long)(step_publish())}, 7))}, 2);
}

long* deep_exploration(const char* domain) {
    return hexa_struct_alloc((long[]){(long)(hexa_concat("deep-exploration-", domain)), (long)(hexa_arr_lit((long[]){(long)(step_scan(domain, 3)), (long)(step_verify(0.01)), (long)(step_filter(0.8)), (long)(step_experiment("perturbation")), (long)(step_experiment("cross_domain")), (long)(step_redteam()), (long)(step_register()), (long)(step_publish())}, 8))}, 2);
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    if (!((strcmp(step_display(step_scan("energy", 6)), "Scan(energy, tier=6)") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(step_display(step_redteam()), "RedTeam") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(step_display(step_register()), "Register") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    std_pipe = standard_discovery("fusion");
    if (!((pipeline_len(std_pipe) == 7))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((!pipeline_is_empty(std_pipe)))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    deep_pipe = deep_exploration("quantum");
    if (!((pipeline_len(deep_pipe) == 8))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    result = execute_pipeline(std_pipe);
    if (!(is_complete(result))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field steps_completed */ 0 == 7))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    cr = completion_ratio(result);
    if (!((cr == 1.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field discovery_count */ 0 > 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    empty_pipe = hexa_struct_alloc((long[]){(long)("empty"), (long)(hexa_arr_new())}, 2);
    empty_result = execute_pipeline(empty_pipe);
    if (!(is_complete(empty_result))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((completion_ratio(empty_result) == 1.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field discovery_count */ 0 == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    partial = hexa_struct_alloc((long[]){(long)(3), (long)(6), (long)(0), (long)(0)}, 4);
    if (!((!is_complete(partial)))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    dsl_pipe = build_from_dsl("dsl-test", hexa_arr_lit((long[]){(long)("scan:ai:1"), (long)("verify:0.05"), (long)("filter:0.6"), (long)("register"), (long)("publish")}, 5));
    if (!((pipeline_len(dsl_pipe) == 5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((/* unknown field name */ 0 == "dsl-test"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "=== pipeline verified ===");
    printf("%s %ld\n", "standard_discovery steps:", (long)(pipeline_len(std_pipe)));
    printf("%s %ld\n", "deep_exploration steps:", (long)(pipeline_len(deep_pipe)));
    printf("%s %ld\n", "execute result discoveries:", (long)(/* unknown field discovery_count */ 0));
    printf("%s %ld\n", "execute result filtered:", (long)(/* unknown field filtered_out */ 0));
    printf("%s %g\n", "completion_ratio:", (double)(completion_ratio(result)));
    printf("%s\n", pipeline_display(std_pipe));
    return 0;
}
