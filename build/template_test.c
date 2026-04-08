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

long* builtin_basic_scan();
long* builtin_full_scan();
long* builtin_ouroboros();
const char* substitute(const char* text, const char* params);
long* instantiate_template(long *tmpl, const char* overrides);
const char* template_to_json(long *t);
long save_template(long t);
const char* load_template_by_name(const char* name);
long display_template(const char* name, const char* desc, const char* steps, const char* metrics);
long hexa_user_main();

static const char* template_file = "shared/experiment_templates.json";

long* builtin_basic_scan() {
    return hexa_struct_alloc((long[]){(long)("n6-basic-scan"), (long)("Basic 3-lens scan on {{domain}}"), (long)("Initialize telescope with lenses: {{lenses}};Run scan on domain: {{domain}};Collect n=6 resonance hits;Report consensus level"), (long)("domain=physics,lenses=consciousness,topology,causal"), (long)("n6_score
        consensus
        discovery_count")}, 5);
}

long* builtin_full_scan() {
    return hexa_struct_alloc((long[]){(long)("n6-full-scan"), (long)("Full 22-lens scan on {{domain}}"), (long)("Load all 22 lenses from registry;Run parallel scan on {{domain}};Cross-validate with 3+ lens consensus;Grade: 3+=candidate, 7+=high-confidence, 12+=confirmed"), (long)("domain=all"), (long)("n6_score
        consensus
        lens_agreement")}, 5);
}

long* builtin_ouroboros() {
    return hexa_struct_alloc((long[]){(long)("n6-ouroboros"), (long)("OUROBOROS evolution on {{domain}} for {{cycles}} cycles"), (long)("Seed initial hypotheses for {{domain}};Run {{cycles}} evolution cycles;Forge new lenses from discoveries;Report convergence and new BT candidates"), (long)("domain=discovery,cycles=6"), (long)("discoveries
        convergence
        lenses_forged")}, 5);
}

const char* substitute(const char* text, const char* params) {
    hexa_arr pairs = hexa_split(params, ",");
    const char* result = text;
    long i = 0;
    while ((i < pairs.n)) {
        hexa_arr p = hexa_split(((const char*)pairs.d[i]), "=");
        if ((p.n >= 2)) {
            const char* key = hexa_trim(((const char*)p.d[0]));
            const char* val = hexa_trim(((const char*)p.d[1]));
            const char* pattern = hexa_concat(hexa_concat("{{", key), "}}");
            result = hexa_replace(result, pattern, val);
        }
        i = (i + 1);
    }
    return result;
}

long* instantiate_template(long *tmpl, const char* overrides) {
    long merged = tmpl[3];
    hexa_arr ov_pairs = hexa_split(overrides, ",");
    long oi = 0;
    while ((oi < ov_pairs.n)) {
        hexa_arr p = hexa_split(((const char*)ov_pairs.d[oi]), "=");
        if ((p.n >= 2)) {
            const char* key = hexa_trim(((const char*)p.d[0]));
            const char* val = hexa_trim(((const char*)p.d[1]));
            if (hexa_contains(merged, hexa_concat(key, "="))) {
                hexa_arr parts = hexa_split(merged, ",");
                const char* new_merged = "";
                long pi = 0;
                while ((pi < parts.n)) {
                    hexa_arr pp = hexa_split(((const char*)parts.d[pi]), "=");
                    if ((pp.n >= 2)) {
                        if ((strcmp(hexa_trim(((const char*)pp.d[0])), key) == 0)) {
                            if ((strcmp(new_merged, "") != 0)) {
                                new_merged = hexa_concat(new_merged, ",");
                            }
                            new_merged = hexa_concat(hexa_concat(hexa_concat(new_merged, key), "="), val);
                        } else {
                            if ((strcmp(new_merged, "") != 0)) {
                                new_merged = hexa_concat(new_merged, ",");
                            }
                            new_merged = hexa_concat(new_merged, ((const char*)parts.d[pi]));
                        }
                    }
                    pi = (pi + 1);
                }
                merged = new_merged;
            } else {
                if ((merged != "")) {
                    merged = hexa_concat(hexa_int_to_str((long)(merged)), ",");
                }
                merged = hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(merged)), key), "="), val);
            }
        }
        oi = (oi + 1);
    }
    const char* desc = substitute(tmpl[1], merged);
    const char* steps = substitute(tmpl[2], merged);
    return hexa_struct_alloc((long[]){(long)(hexa_concat(hexa_int_to_str((long)(tmpl[0])), "-instance")), (long)(desc), (long)(steps), (long)(merged), (long)(tmpl[4])}, 5);
}

const char* template_to_json(long *t) {
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"name\":\"", hexa_int_to_str((long)(t[0]))), "\",\"description\":\""), hexa_int_to_str((long)(t[1]))), "\",\"steps\":\""), hexa_int_to_str((long)(t[2]))), "\",\"parameters\":\""), hexa_int_to_str((long)(t[3]))), "\",\"expected_metrics\":\""), hexa_int_to_str((long)(t[4]))), "\"}");
}

long save_template(long t) {
    const char* raw = hexa_read_file(template_file);
    const char* entry = template_to_json(t);
    if ((strcmp(raw, "") != 0)) {
        const char* trimmed = hexa_trim(raw);
        if (hexa_contains(trimmed, hexa_concat(hexa_concat("\"name\":\"", hexa_int_to_str((long)(/* unknown field name */ 0))), "\""))) {
            hexa_arr entries = hexa_split(trimmed, "},{");
            const char* new_json = "[";
            long first = 1;
            long ei = 0;
            while ((ei < entries.n)) {
                const char* e = ((const char*)entries.d[ei]);
                if ((!hexa_contains(e, hexa_concat(hexa_concat("\"name\":\"", hexa_int_to_str((long)(/* unknown field name */ 0))), "\"")))) {
                    const char* clean = hexa_replace(hexa_replace(e, "[", ""), "]", "");
                    const char* fixed = (((!hexa_starts_with(clean, "{"))) ? (hexa_concat("{", clean)) : (clean));
                    const char* fixed2 = (((!hexa_ends_with(fixed, "}"))) ? (hexa_concat(fixed, "}")) : (fixed));
                    if ((!first)) {
                        new_json = hexa_concat(new_json, ",");
                    }
                    new_json = hexa_concat(new_json, fixed2);
                    first = 0;
                }
                ei = (ei + 1);
            }
            if ((!first)) {
                new_json = hexa_concat(new_json, ",");
            }
            new_json = hexa_concat(hexa_concat(new_json, entry), "]");
            hexa_write_file(template_file, new_json);
            return 0;
        }
        if ((strcmp(trimmed, "[]") == 0)) {
            hexa_write_file(template_file, hexa_concat(hexa_concat("[", entry), "]"));
            return 0;
        }
        const char* without_bracket = hexa_substr(trimmed, 0, (hexa_str_len(trimmed) - 1));
        hexa_write_file(template_file, hexa_concat(hexa_concat(hexa_concat(without_bracket, ","), entry), "]"));
        return 0;
    }
    return hexa_write_file(template_file, hexa_concat(hexa_concat("[", entry), "]"));
}

const char* load_template_by_name(const char* name) {
    const char* raw = hexa_read_file(template_file);
    if ((strcmp(raw, "") == 0)) {
        return "";
    }
    const char* needle = hexa_concat(hexa_concat("\"name\":\"", name), "\"");
    if ((!hexa_contains(raw, needle))) {
        return "";
    }
    hexa_arr after = hexa_split(raw, needle);
    if ((after.n < 2)) {
        return "";
    }
    const char* block = ((const char*)(hexa_split(((const char*)after.d[1]), "}")).d[0]);
    const char* desc = "";
    const char* dn = "\"description\":\"";
    if (hexa_contains(block, dn)) {
        hexa_arr dp = hexa_split(block, dn);
        if ((dp.n > 1)) {
            desc = ((const char*)(hexa_split(((const char*)dp.d[1]), "\"")).d[0]);
        }
    }
    const char* steps = "";
    const char* sn = "\"steps\":\"";
    if (hexa_contains(block, sn)) {
        hexa_arr sp = hexa_split(block, sn);
        if ((sp.n > 1)) {
            steps = ((const char*)(hexa_split(((const char*)sp.d[1]), "\"")).d[0]);
        }
    }
    const char* params = "";
    const char* pn = "\"parameters\":\"";
    if (hexa_contains(block, pn)) {
        hexa_arr pp = hexa_split(block, pn);
        if ((pp.n > 1)) {
            params = ((const char*)(hexa_split(((const char*)pp.d[1]), "\"")).d[0]);
        }
    }
    const char* metrics = "";
    const char* mn = "\"expected_metrics\":\"";
    if (hexa_contains(block, mn)) {
        hexa_arr mp = hexa_split(block, mn);
        if ((mp.n > 1)) {
            metrics = ((const char*)(hexa_split(((const char*)mp.d[1]), "\"")).d[0]);
        }
    }
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(name, "|"), desc), "|"), steps), "|"), params), "|"), metrics);
}

long display_template(const char* name, const char* desc, const char* steps, const char* metrics) {
    printf("%s", hexa_concat("  name    : ", name));
    printf("%s", hexa_concat("  desc    : ", desc));
    hexa_arr step_list = hexa_split(steps, "
    ");
    long si = 0;
    while ((si < step_list.n)) {
        printf("%s", hexa_concat(hexa_concat(hexa_concat("  step ", hexa_int_to_str((long)((si + 1)))), " : "), hexa_trim(((const char*)step_list.d[si]))));
        si = (si + 1);
    }
    if ((strcmp(metrics, "") != 0)) {
        return printf("%s", hexa_concat("  metrics : ", hexa_replace(metrics, "
        ", ", ")));
    }
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "template.hexa — 실험 템플릿");
        printf("%s", "Usage:");
        printf("%s", "  builtins                          빌트인 목록");
        printf("%s", "  list                              저장된 목록");
        printf("%s", "  save <name> <desc> <steps
        > <metrics
        >");
        printf("%s", "  load <name>                       로드");
        printf("%s", "  instantiate <name> key=val ...     인스턴스화");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "builtins") == 0)) {
        printf("%s", "[template] builtin templates (3):");
        long* t1 = builtin_basic_scan();
        display_template(t1[0], t1[1], t1[2], t1[4]);
        printf("%s", "");
        long* t2 = builtin_full_scan();
        display_template(t2[0], t2[1], t2[2], t2[4]);
        printf("%s", "");
        long* t3 = builtin_ouroboros();
        display_template(t3[0], t3[1], t3[2], t3[4]);
    }
    if ((strcmp(cmd, "save") == 0)) {
        if ((a.n < 6)) {
            printf("%s", "Usage: save <name> <description> <steps_semicolon> <metrics_semicolon>");
            return 0;
        }
        long t[5] = {((const char*)a.d[2]), ((const char*)a.d[3]), ((const char*)a.d[4]), "", ((const char*)a.d[5])};
        save_template(t);
        printf("%s", hexa_concat("[template] saved: ", ((const char*)a.d[2])));
    }
    if ((strcmp(cmd, "list") == 0)) {
        const char* raw = hexa_read_file(template_file);
        if ((strcmp(raw, "") == 0)) {
            printf("%s", "[template] no templates saved");
            return 0;
        }
        hexa_arr entries = hexa_split(raw, "\"name\":\"");
        long total = (entries.n - 1);
        printf("%s", hexa_concat(hexa_concat("[template] ", hexa_int_to_str((long)(total))), " templates:"));
        long i = 1;
        while ((i < entries.n)) {
            const char* name = ((const char*)(hexa_split(((const char*)entries.d[i]), "\"")).d[0]);
            printf("%s", hexa_concat("  - ", name));
            i = (i + 1);
        }
    }
    if ((strcmp(cmd, "load") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: load <name>");
            return 0;
        }
        const char* name = ((const char*)a.d[2]);
        if ((strcmp(name, "n6-basic-scan") == 0)) {
            long* t = builtin_basic_scan();
            display_template(t[0], t[1], t[2], t[4]);
            return 0;
        }
        if ((strcmp(name, "n6-full-scan") == 0)) {
            long* t = builtin_full_scan();
            display_template(t[0], t[1], t[2], t[4]);
            return 0;
        }
        if ((strcmp(name, "n6-ouroboros") == 0)) {
            long* t = builtin_ouroboros();
            display_template(t[0], t[1], t[2], t[4]);
            return 0;
        }
        const char* result = load_template_by_name(name);
        if ((strcmp(result, "") == 0)) {
            printf("%s", hexa_concat("[template] not found: ", name));
            return 0;
        }
        hexa_arr parts = hexa_split(result, "|");
        display_template(((const char*)parts.d[0]), ((const char*)parts.d[1]), ((const char*)parts.d[2]), ((const char*)parts.d[4]));
    }
    if ((strcmp(cmd, "instantiate") == 0)) {
        if ((a.n < 4)) {
            printf("%s", "Usage: instantiate <name> key=val ...");
            return 0;
        }
        const char* name = ((const char*)a.d[2]);
        const char* overrides = "";
        long oi = 3;
        while ((oi < a.n)) {
            if ((strcmp(overrides, "") != 0)) {
                overrides = hexa_concat(overrides, ",");
            }
            overrides = hexa_concat(overrides, ((const char*)a.d[oi]));
            oi = (oi + 1);
        }
        long tmpl[5] = {"", "", "", "", ""};
        if ((strcmp(name, "n6-basic-scan") == 0)) {
            tmpl = builtin_basic_scan();
        }
        if ((strcmp(name, "n6-full-scan") == 0)) {
            tmpl = builtin_full_scan();
        }
        if ((strcmp(name, "n6-ouroboros") == 0)) {
            tmpl = builtin_ouroboros();
        }
        if ((tmpl[0] == "")) {
            const char* result = load_template_by_name(name);
            if ((strcmp(result, "") == 0)) {
                printf("%s", hexa_concat("[template] not found: ", name));
                return 0;
            }
            hexa_arr parts = hexa_split(result, "|");
            tmpl = hexa_struct_alloc((long[]){(long)(((const char*)parts.d[0])), (long)(((const char*)parts.d[1])), (long)(((const char*)parts.d[2])), (long)(((const char*)parts.d[3])), (long)(((const char*)parts.d[4]))}, 5);
        }
        long* instance = instantiate_template(tmpl, overrides);
        printf("%s", "[template] instantiated:");
        return display_template(instance[0], instance[1], instance[2], instance[4]);
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    return 0;
}
