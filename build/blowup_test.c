#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static long hexa_time_unix(void) { return (long)time(NULL); }
static double hexa_clock(void) { return (double)clock() / CLOCKS_PER_SEC; }
#define HEXA_ARENA_SIZE (16*1024*1024)
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

hexa_arr load_n6_consts();
long n6_values(long consts);
long n6_names_list(long consts);
double safe_float(const char* s);
double abs_f(double x);
double max_f(double a, double b);
double min_f(double a, double b);
double sqrt_f(double x);
double ln_f(double x);
double clamp01(double x);
double n6_dist_one(double value, double constant);
double n6_distance(double value);
double n6_conf(double value);
const char* match_name(double value);
const char* match_grade(double value);
const char* sector_of(const char* text);
const char* m_name(long idx, const char* dom);
double m_val(long idx, const char* dom);
long m_count(const char* dom);
const char* emit(const char* name, const char* ctype, const char* expr, double conf, double value, long axiom, const char* src);
double get_ax_val(const char* ax_vals, long idx);
const char* get_ax_name(const char* ax_names, long idx);
long ax_count_f(const char* ax_vals);
long ax_contains(const char* ax_names, const char* name);
double shift_val(long idx);
const char* shift_name(long idx);
const char* xfer_domain(long idx);
const char* graph_extract_field(const char* line, const char* field);
const char* graph_load();
long graph_count_type(const char* content, const char* ttype);
long graph_hub_count(const char* content, long min_deg);
long graph_node_exists(const char* content, const char* id);
const char* graph_append_node(const char* id, const char* node_type, const char* dom, const char* summary, double conf);
const char* graph_append_node_d(const char* id, const char* node_type, const char* dom, const char* summary, double conf, long d);
const char* graph_append_edge(const char* from_id, const char* to_id, const char* edge_type, double strength);
double evo_mutate_shift(double base_val, long shift_idx);
double evo_mutate_invert(double base_val);
double evo_mutate_combine(double a, double b);
double evo_verify(double value);
const char* evo_cycle(const char* seeds_csv, long cycle_num);
const char* evo_check_convergence(const char* history_csv);
double lens_consciousness(const char* vals);
double lens_topology(const char* vals);
double lens_causal(const char* vals);
double lens_gravity(const char* vals);
double lens_thermo(const char* vals);
const char* telescope_verify(const char* vals, double threshold);

static hexa_arr _n6_dyn;
static long _n6_dyn_names;
static long _n6_dyn_values;
static long _n6_dyn_count;
static long li = 0;
static long vals;
static long hub_count = 0;
static long hi = 0;
static long ci = 0;
static long cnt_vals;
static long num_bins;
static double total;
static double entropy = 0.0;
static double max_ent;
static hexa_arr a;
static const char* domain;
static long max_depth;
static long skip_graph = false;
static const char* ext_seeds = "";
static long ext_pool_cap = 0;
static long fast_mode = false;
static long ai = 2;
static const char* log_path = "shared/discovery_log.jsonl";
static const char* graph_path = "shared/discovery_graph.json";
static long mc;
static const char* graph_content = "";
static long graph_nodes_before = 0;
static long graph_edges_before = 0;
static long graph_hubs_before = 0;
static const char* seed_csv = "";
static long si = 0;
static long evo_max_cycles;
static const char* evo_history = "";
static long evo_total_disc = 0;
static double evo_best_score = 0.0;
static const char* current_seeds;
static const char* evo_status = "Exploring";
static long ec = 0;
static long frozen = 0;
static double closure;
static long ax_count;
static double compression;
static double evo_boost;
static double effective_closure;
static hexa_arr buf_lines;
static long total = 0;
static long exact_count = 0;
static long near_count = 0;
static long high_conf_count = 0;
static long axiom_count = 0;
static const char* log_buf = "";
static long logged = 0;
static hexa_arr val_arr;
static hexa_arr ax_name_arr;
static hexa_arr ax_val_arr;
static long ax_n = 0;
static const char* depth_stats = "";
static long depth = 0;
static const char* val_csv = "|";
static const char* tele_result;
static hexa_arr tparts;
static long tele_agree = 0;
static const char* tele_level = "None";
static double tele_boost = 0.0;
static double tele_s1 = 0.0;
static double tele_s2 = 0.0;
static double tele_s3 = 0.0;
static double tele_s4 = 0.0;
static double tele_s5 = 0.0;
static hexa_arr boosted_lines;
static long li = 0;
static long boosted_high = 0;
static long boosted_axiom = 0;
static const char* graph_new_entries = "";
static long graph_added_nodes = 0;
static long graph_added_edges = 0;
static long recurse_max_rounds = 5;
static long recurse_round = 0;
static long recurse_disc_total = 0;
static double recurse_score = 0.0;
static hexa_arr recurse_all_corollaries;
static const char* axiom_seeds = "";
static long axiom_fed = 0;
static long recurse_pool_size;
static long recurse_continue = true;
static long absorb_logged = 0;
static long absorb_graph_nodes = 0;
static long absorb_graph_edges = 0;
static long absorb_bus_events = 0;
static const char* growth_bus_path = "shared/growth_bus.jsonl";
static const char* absorb_existing_log = "";
static const char* absorb_graph_content = "";
static const char* absorb_log_entries = "";
static const char* absorb_graph_entries = "";
static const char* absorb_bus_entries = "";
static long ci = 0;
static const char* absorb_hexa_path = "mk2_hexa/native/absorb.hexa";
static const char* hexa_bin_home;
static const char* absorb_hexa_bin;
static long rline_count;
static long show_limit;
static long shown = 0;
static double rho;
static const char* new_entries = "";

hexa_arr load_n6_consts() {
    const char* home = hexa_exec("printenv HOME");
    const char* path = hexa_concat(home, "/Dev/nexus/shared/n6_constants.jsonl");
    const char* raw = "";
    raw = hexa_read_file(path);
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr names = hexa_arr_new();
    hexa_arr values = hexa_arr_new();
    long __fi_line = 0;
    while ((__fi_line < lines.n)) {
        const char* line = ((const char*)lines.d[__fi_line]);
        const char* trimmed = hexa_trim(line);
        if ((strcmp(trimmed, "") == 0)) {
            continue;
        }
        const char* name = "";
        double value = 0.0;
        if (hexa_contains(trimmed, "\"name\":\"")) {
            hexa_arr p = hexa_split(trimmed, "\"name\":\"");
            if ((p.n >= 2)) {
                name = ((const char*)(hexa_split(((const char*)p.d[1]), "\"")).d[0]);
            }
        }
        if (hexa_contains(trimmed, "\"value\":")) {
            hexa_arr p = hexa_split(trimmed, "\"value\":");
            if ((p.n >= 2)) {
                const char* num_str = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)p.d[1]), ",")).d[0]), "}")).d[0]));
                value = hexa_to_float(num_str);
            }
        }
        if ((strcmp(name, "") != 0)) {
            names = hexa_arr_concat(names, hexa_arr_lit((long[]){(long)(name)}, 1));
            values = hexa_arr_concat(values, hexa_arr_lit((long[]){(long)(value)}, 1));
        }
        __fi_line = (__fi_line + 1);
    }
    return hexa_arr_lit((long[]){(long)(names), (long)(values)}, 2);
}

long n6_values(long consts) {
    return consts[1];
}

long n6_names_list(long consts) {
    return consts[0];
}

double safe_float(const char* s) {
    double v = 0.0;
    v = hexa_to_float(s);
    return v;
}

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    }
    return x;
}

double max_f(double a, double b) {
    if ((a > b)) {
        return a;
    }
    return b;
}

double min_f(double a, double b) {
    if ((a < b)) {
        return a;
    }
    return b;
}

double sqrt_f(double x) {
    if ((x <= 0.0)) {
        return 0.0;
    }
    double g = (x / 2.0);
    if ((g < 1.0)) {
        g = 1.0;
    }
    long i = 0;
    while ((i < 15)) {
        g = ((g + (x / g)) / 2.0);
        i = (i + 1);
    }
    return g;
}

double ln_f(double x) {
    if ((x <= 0.0)) {
        return 0.0;
    }
    double sum = 0.0;
    double term = ((x - 1.0) / (x + 1.0));
    double t2 = (term * term);
    long k = 1;
    while ((k <= 15)) {
        sum = (sum + (term / ((double)(k))));
        term = (term * t2);
        k = (k + 2);
    }
    return (2.0 * sum);
}

double clamp01(double x) {
    if ((x < 0.0)) {
        return 0.0;
    }
    if ((x > 1.0)) {
        return 1.0;
    }
    return x;
}

double n6_dist_one(double value, double constant) {
    double denom = max_f(abs_f(constant), 1.0);
    return (abs_f((value - constant)) / denom);
}

double n6_distance(double value) {
    if ((value != value)) {
        return 1.0;
    }
    if ((value > 999999999.0)) {
        return 1.0;
    }
    if ((value < (0.0 - 999999999.0))) {
        return 1.0;
    }
    double d = 1.0;
    long i = 0;
    while ((i < _n6_dyn_count)) {
        double cv = ((double)(_n6_dyn_values[i]));
        d = min_f(d, n6_dist_one(value, cv));
        i = (i + 1);
    }
    return min_f(d, 1.0);
}

double n6_conf(double value) {
    return max_f((1.0 - n6_distance(value)), 0.0);
}

const char* match_name(double value) {
    long i = 0;
    while ((i < _n6_dyn_count)) {
        double cv = ((double)(_n6_dyn_values[i]));
        double threshold = (((abs_f(cv) >= 100.0)) ? (0.01) : (0));
        if ((abs_f((value - cv)) < threshold)) {
            return hexa_int_to_str((long)(_n6_dyn_names[i]));
        }
        i = (i + 1);
    }
    return "";
}

const char* match_grade(double value) {
    if ((strcmp(match_name(value), "") != 0)) {
        return "EXACT";
    }
    double d = n6_distance(value);
    if ((d < 0.01)) {
        return "NEAR";
    }
    if ((d < 0.05)) {
        return "CLOSE";
    }
    return "MISS";
}

const char* sector_of(const char* text) {
    if (((hexa_contains(text, "omega") || hexa_contains(text, "dark")) || hexa_contains(text, "cosmolog"))) {
        return "cosmology";
    }
    if (((hexa_contains(text, "alpha") || hexa_contains(text, "137")) || hexa_contains(text, "electroweak"))) {
        return "quantum";
    }
    if (((hexa_contains(text, "sigma") || hexa_contains(text, "sopfr")) || hexa_contains(text, "tau"))) {
        return "n6";
    }
    if (((hexa_contains(text, "phi") || hexa_contains(text, "mu")) || hexa_contains(text, "M3"))) {
        return "n6";
    }
    if (((((hexa_contains(text, "J2") || hexa_contains(text, "P1")) || hexa_contains(text, "P2")) || hexa_contains(text, "P3")) || hexa_contains(text, "P4"))) {
        return "n6";
    }
    if ((hexa_contains(text, "meta_fp") || hexa_contains(text, "transcend"))) {
        return "n6";
    }
    if ((((hexa_contains(text, "ded_") || hexa_contains(text, "sym_")) || hexa_contains(text, "bif_")) || hexa_contains(text, "comp_"))) {
        return "n6";
    }
    if ((((hexa_contains(text, "blowup") || hexa_contains(text, "d0_")) || hexa_contains(text, "d1_")) || hexa_contains(text, "d2_"))) {
        return "n6";
    }
    if (((hexa_contains(text, "sqrt") || hexa_contains(text, "proj")) || hexa_contains(text, "geom"))) {
        return "geometric";
    }
    return "unknown";
}

const char* m_name(long idx, const char* dom) {
    if ((strcmp(dom, "physics") == 0)) {
        if ((idx == 0)) {
            return "alpha_inv";
        }
        if ((idx == 1)) {
            return "Omega_m";
        }
        if ((idx == 2)) {
            return "Omega_L";
        }
        if ((idx == 3)) {
            return "n";
        }
        if ((idx == 4)) {
            return "sigma";
        }
        return "";
    }
    if ((idx == 0)) {
        return "n";
    }
    if ((idx == 1)) {
        return "sigma";
    }
    if ((idx == 2)) {
        return "phi";
    }
    if ((idx == 3)) {
        return "tau";
    }
    if ((idx == 4)) {
        return "sopfr";
    }
    if ((idx == 5)) {
        return "J2";
    }
    if ((idx == 6)) {
        return "M3";
    }
    return "";
}

double m_val(long idx, const char* dom) {
    if ((strcmp(dom, "physics") == 0)) {
        if ((idx == 0)) {
            return 137.036;
        }
        if ((idx == 1)) {
            return 0.3153;
        }
        if ((idx == 2)) {
            return 0.6847;
        }
        if ((idx == 3)) {
            return 6.0;
        }
        if ((idx == 4)) {
            return 12.0;
        }
        return 0.0;
    }
    if ((idx == 0)) {
        return 6.0;
    }
    if ((idx == 1)) {
        return 12.0;
    }
    if ((idx == 2)) {
        return 2.0;
    }
    if ((idx == 3)) {
        return 4.0;
    }
    if ((idx == 4)) {
        return 5.0;
    }
    if ((idx == 5)) {
        return 24.0;
    }
    if ((idx == 6)) {
        return 7.0;
    }
    return 0.0;
}

long m_count(const char* dom) {
    if ((strcmp(dom, "physics") == 0)) {
        return 5;
    }
    return 7;
}

const char* emit(const char* name, const char* ctype, const char* expr, double conf, double value, long axiom, const char* src) {
    const char* ax_s = ((axiom) ? ("1") : ("0"));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(name, "\t"), ctype), "\t"), expr), "\t"), hexa_float_to_str((double)(conf))), "\t"), hexa_float_to_str((double)(value))), "\t"), ax_s), "\t"), src);
}

double get_ax_val(const char* ax_vals, long idx) {
    hexa_arr parts = hexa_split(ax_vals, "|");
    if (((idx < 0) || (idx >= parts.n))) {
        return 0.0;
    }
    double v = 0.0;
    v = hexa_to_float(((const char*)parts.d[idx]));
    return v;
}

const char* get_ax_name(const char* ax_names, long idx) {
    hexa_arr parts = hexa_split(ax_names, "|");
    if (((idx < 0) || (idx >= parts.n))) {
        return "";
    }
    return ((const char*)parts.d[idx]);
}

long ax_count_f(const char* ax_vals) {
    hexa_arr parts = hexa_split(ax_vals, "|");
    return parts.n;
}

long ax_contains(const char* ax_names, const char* name) {
    hexa_arr parts = hexa_split(ax_names, "|");
    long ci = 0;
    while ((ci < parts.n)) {
        if ((strcmp(((const char*)parts.d[ci]), name) == 0)) {
            return 1;
        }
        ci = (ci + 1);
    }
    return 0;
}

double shift_val(long idx) {
    if ((idx == 0)) {
        return 0.287682072;
    }
    if ((idx == 1)) {
        return 1.618033989;
    }
    if ((idx == 2)) {
        return (0.0 - 1.618033989);
    }
    if ((idx == 3)) {
        return 0.166666667;
    }
    if ((idx == 4)) {
        return (0.0 - 0.166666667);
    }
    return 2.449489743;
}

const char* shift_name(long idx) {
    if ((idx == 0)) {
        return "ln43";
    }
    if ((idx == 1)) {
        return "phi";
    }
    if ((idx == 2)) {
        return "-phi";
    }
    if ((idx == 3)) {
        return "inv6";
    }
    if ((idx == 4)) {
        return "-inv6";
    }
    return "sqrt6";
}

const char* xfer_domain(long idx) {
    if ((idx == 0)) {
        return "physics";
    }
    if ((idx == 1)) {
        return "math";
    }
    if ((idx == 2)) {
        return "info";
    }
    if ((idx == 3)) {
        return "bio";
    }
    if ((idx == 4)) {
        return "mind";
    }
    return "arch";
}

const char* graph_extract_field(const char* line, const char* field) {
    const char* needle = hexa_concat(hexa_concat("\"", field), "\":");
    if ((!hexa_contains(line, needle))) {
        return "";
    }
    hexa_arr after = hexa_split(line, needle);
    if ((after.n < 2)) {
        return "";
    }
    const char* rest = ((const char*)after.d[1]);
    if ((hexa_str_len(rest) > 0)) {
        hexa_arr first_ch = hexa_split(rest, "\"");
        if ((first_ch.n >= 3)) {
            hexa_arr trimmed = hexa_split(rest, "\"");
            if ((trimmed.n >= 2)) {
                if (((hexa_str_len(((const char*)trimmed.d[0])) == 0) && (trimmed.n >= 2))) {
                    return ((const char*)trimmed.d[1]);
                }
            }
        }
    }
    const char* result = "";
    hexa_arr chars = hexa_split(rest, "");
    long ci = 0;
    while ((ci < chars.n)) {
        const char* ch = ((const char*)chars.d[ci]);
        if (((((strcmp(ch, ",") == 0) || (strcmp(ch, "}") == 0)) || (strcmp(ch, " ") == 0)) || (strcmp(ch, "]") == 0))) {
            return result;
        }
        if ((strcmp(ch, "\"") != 0)) {
            result = hexa_concat(result, ch);
        }
        ci = (ci + 1);
    }
    return result;
}

const char* graph_load() {
    const char* gpath = "shared/discovery_graph.json";
    if (hexa_file_exists(gpath)) {
        return hexa_read_file(gpath);
    }
    return "";
}

long graph_count_type(const char* content, const char* ttype) {
    if ((hexa_str_len(content) == 0)) {
        return 0;
    }
    hexa_arr lines = hexa_split(content, "\n");
    long count = 0;
    long li = 0;
    while ((li < lines.n)) {
        const char* line = ((const char*)lines.d[li]);
        if ((hexa_str_len(line) > 5)) {
            const char* t = graph_extract_field(line, "type");
            if ((strcmp(t, ttype) == 0)) {
                count = (count + 1);
            }
        }
        li = (li + 1);
    }
    return count;
}

long graph_hub_count(const char* content, long min_deg) {
    if ((hexa_str_len(content) == 0)) {
        return 0;
    }
    hexa_arr lines = hexa_split(content, "\n");
    long deg_map = 0;
}

long graph_node_exists(const char* content, const char* id) {
    if ((hexa_str_len(content) == 0)) {
        return 0;
    }
    const char* needle = hexa_concat(hexa_concat("\"id\":\"", id), "\"");
    return hexa_contains(content, needle);
}

const char* graph_append_node(const char* id, const char* node_type, const char* dom, const char* summary, double conf) {
    return graph_append_node_d(id, node_type, dom, summary, conf, 0);
}

const char* graph_append_node_d(const char* id, const char* node_type, const char* dom, const char* summary, double conf, long d) {
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"type\":\"node\",\"id\":\"", id), "\",\"node_type\":\""), node_type), "\",\"domain\":\""), dom), "\",\"summary\":\""), summary), "\",\"confidence\":"), hexa_float_to_str((double)(conf))), ",\"depth\":"), hexa_int_to_str((long)(d))), "}");
}

const char* graph_append_edge(const char* from_id, const char* to_id, const char* edge_type, double strength) {
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"type\":\"edge\",\"from\":\"", from_id), "\",\"to\":\""), to_id), "\",\"edge_type\":\""), edge_type), "\",\"strength\":"), hexa_float_to_str((double)(strength))), ",\"bidirectional\":false}");
}

double evo_mutate_shift(double base_val, long shift_idx) {
    if ((_n6_dyn_count == 0)) {
        return (base_val + 6.0);
    }
    long idx = (shift_idx % _n6_dyn_count);
    double cv = ((double)(_n6_dyn_values[idx]));
    if ((abs_f(cv) < 0.001)) {
        return base_val;
    }
    if (((shift_idx % 2) == 0)) {
        return (base_val * cv);
    }
    return (base_val + cv);
}

double evo_mutate_invert(double base_val) {
    if ((abs_f(base_val) > 0.0000000001)) {
        return (1.0 / base_val);
    }
    return 0.0;
}

double evo_mutate_combine(double a, double b) {
    return sqrt_f((abs_f(a) * abs_f(b)));
}

double evo_verify(double value) {
    return n6_conf(value);
}

const char* evo_cycle(const char* seeds_csv, long cycle_num) {
    hexa_arr parts = hexa_split(seeds_csv, "|");
    double best_score = 0.0;
    long discoveries = 0;
    const char* out_vals = "";
    long si = 0;
    while (((si < parts.n) && (si < 12))) {
        double sv = 0.0;
        sv = hexa_to_float(((const char*)parts.d[si]));
        if ((abs_f(sv) > 0.0000000001)) {
            double best_mut = sv;
            double best_conf = evo_verify(sv);
            long shift_limit = (((_n6_dyn_count < 16)) ? (_n6_dyn_count) : (16));
            long shi = 0;
            while ((shi < shift_limit)) {
                double shifted = evo_mutate_shift(sv, shi);
                double sc = evo_verify(shifted);
                if ((sc > best_conf)) {
                    best_conf = sc;
                    best_mut = shifted;
                }
                shi = (shi + 1);
            }
            double inv = evo_mutate_invert(sv);
            double inv_sc = evo_verify(inv);
            if ((inv_sc > best_conf)) {
                best_conf = inv_sc;
                best_mut = inv;
            }
            if (((si + 1) < parts.n)) {
                double sv2 = 0.0;
                sv2 = hexa_to_float(((const char*)parts.d[(si + 1)]));
                if ((abs_f(sv2) > 0.0000000001)) {
                    double combined = evo_mutate_combine(sv, sv2);
                    double comb_sc = evo_verify(combined);
                    if ((comb_sc > best_conf)) {
                        best_conf = comb_sc;
                        best_mut = combined;
                    }
                }
            }
            double novelty = (1.0 / sqrt_f(((double)((((cycle_num > 0)) ? (cycle_num) : (1))))));
            double xfer = (sv * (1.0 + (novelty * 0.1)));
            double xfer_sc = evo_verify(xfer);
            if ((xfer_sc > best_conf)) {
                best_conf = xfer_sc;
                best_mut = xfer;
            }
            if ((best_conf > best_score)) {
                best_score = best_conf;
            }
            if ((best_conf >= 0.3)) {
                discoveries = (discoveries + 1);
            }
            if ((hexa_str_len(out_vals) > 0)) {
                out_vals = hexa_concat(out_vals, "|");
            }
            out_vals = hexa_concat(out_vals, hexa_float_to_str((double)(best_mut)));
        }
        si = (si + 1);
    }
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_float_to_str((double)(best_score)), "|"), hexa_int_to_str((long)(discoveries))), "|"), out_vals);
}

const char* evo_check_convergence(const char* history_csv) {
    hexa_arr parts = hexa_split(history_csv, "|");
    long hlen = parts.n;
    if ((hlen < 3)) {
        return "Exploring";
    }
    long all_zero = 1;
    long ci = (hlen - 3);
    if ((ci < 0)) {
        ci = 0;
    }
    while ((ci < hlen)) {
        long v = 0;
        v = hexa_to_int_str(((const char*)parts.d[ci]));
        if ((v > 0)) {
            all_zero = 0;
        }
        ci = (ci + 1);
    }
    if (all_zero) {
        return "Saturated";
    }
    if ((hlen < 4)) {
        return "Exploring";
    }
    long mid = (hlen / 2);
    long early_sum = 0;
    ci = 0;
    while ((ci < mid)) {
        early_sum = (early_sum + hexa_to_int_str(((const char*)parts.d[ci])));
        ci = (ci + 1);
    }
    long late_sum = 0;
    ci = mid;
    while ((ci < hlen)) {
        late_sum = (late_sum + hexa_to_int_str(((const char*)parts.d[ci])));
        ci = (ci + 1);
    }
    double early_avg = (((double)(early_sum)) / ((double)(mid)));
    long late_len = (hlen - mid);
    double late_avg = (((double)(late_sum)) / ((double)(late_len)));
    if ((early_avg > 0.0)) {
        double ratio = (late_avg / early_avg);
        if ((ratio < 0.5)) {
            return "Converging";
        }
        if ((ratio > 1.5)) {
            return "Divergent";
        }
    }
    return "Exploring";
}

double lens_consciousness(const char* vals) {
    hexa_arr parts = hexa_split(vals, "|");
    long n = parts.n;
    if ((n < 2)) {
        return 0.0;
    }
    long self_ref = 0;
    long i = 0;
    while (((i < n) && (i < 30))) {
        double vi = 0.0;
        vi = hexa_to_float(((const char*)parts.d[i]));
        long j = (i + 1);
        while (((j < n) && (j < 30))) {
            double vj = 0.0;
            vj = hexa_to_float(((const char*)parts.d[j]));
            if ((abs_f(vj) > 0.0000000001)) {
                double ratio = (vi / vj);
                if ((abs_f((ratio - 6.0)) < 0.01)) {
                    self_ref = (self_ref + 1);
                }
                if ((abs_f((ratio - 12.0)) < 0.01)) {
                    self_ref = (self_ref + 1);
                }
                if ((abs_f((ratio - 2.0)) < 0.01)) {
                    self_ref = (self_ref + 1);
                }
                if ((abs_f((ratio - 4.0)) < 0.01)) {
                    self_ref = (self_ref + 1);
                }
                if ((abs_f((ratio - 24.0)) < 0.01)) {
                    self_ref = (self_ref + 1);
                }
                if ((abs_f((ratio - 5.0)) < 0.01)) {
                    self_ref = (self_ref + 1);
                }
                if ((abs_f((ratio - 3.0)) < 0.01)) {
                    self_ref = (self_ref + 1);
                }
            }
            j = (j + 1);
        }
        i = (i + 1);
    }
    double max_pairs = ((double)(((n * (n - 1)) / 2)));
    if ((max_pairs < 1.0)) {
        return 0.0;
    }
    return clamp01((((double)(self_ref)) / max_pairs));
}

double lens_topology(const char* vals) {
    hexa_arr parts = hexa_split(vals, "|");
    long n = parts.n;
    if ((n < 3)) {
        return 0.0;
    }
    long limit = (((n < 40)) ? (n) : (40));
    const char* sorted = "";
    long si = 0;
    while ((si < limit)) {
        double v = 0.0;
        v = hexa_to_float(((const char*)parts.d[si]));
        if ((hexa_str_len(sorted) > 0)) {
            sorted = hexa_concat(sorted, "|");
        }
        sorted = hexa_concat(sorted, hexa_float_to_str((double)(v)));
        si = (si + 1);
    }
    hexa_arr sparts = hexa_split(sorted, "|");
    long sn = sparts.n;
    double sum = 0.0;
    si = 0;
    while ((si < sn)) {
        sum = (sum + hexa_to_float(((const char*)sparts.d[si])));
        si = (si + 1);
    }
    double mean = (((sn > 0)) ? ((sum / ((double)(sn)))) : (1.0));
    double threshold = (((abs_f(mean) > 0.0)) ? ((abs_f(mean) * 0.5)) : (1.0));
    long gaps = 0;
    si = 0;
    while ((si < (sn - 1))) {
        double v1 = 0.0;
        double v2 = 0.0;
        v1 = hexa_to_float(((const char*)sparts.d[si]));
        v2 = hexa_to_float(((const char*)sparts.d[(si + 1)]));
        if ((abs_f((v2 - v1)) > threshold)) {
            gaps = (gaps + 1);
        }
        si = (si + 1);
    }
    long betti0 = (gaps + 1);
    long cycles = 0;
    si = 0;
    while (((si < sn) && (si < 20))) {
        long sj = (si + 2);
        while (((sj < sn) && (sj < 20))) {
            double v1 = 0.0;
            double v2 = 0.0;
            v1 = hexa_to_float(((const char*)sparts.d[si]));
            v2 = hexa_to_float(((const char*)sparts.d[sj]));
            if ((abs_f((v1 - v2)) < (threshold * 0.1))) {
                cycles = (cycles + 1);
            }
            sj = (sj + 1);
        }
        si = (si + 1);
    }
    double sig = ((((double)((betti0 - 1))) * 0.3) + (((double)(cycles)) * 0.1));
    return clamp01(sig);
}

double lens_causal(const char* vals) {
    hexa_arr parts = hexa_split(vals, "|");
    long n = parts.n;
    if ((n < 4)) {
        return 0.0;
    }
    long limit = (((n < 50)) ? (n) : (50));
    double sum = 0.0;
    long ci = 0;
    while ((ci < limit)) {
        sum = (sum + hexa_to_float(((const char*)parts.d[ci])));
        ci = (ci + 1);
    }
    double mean = (sum / ((double)(limit)));
    double cov = 0.0;
    double var = 0.0;
    ci = 0;
    while ((ci < (limit - 1))) {
        double v0 = 0.0;
        double v1 = 0.0;
        v0 = hexa_to_float(((const char*)parts.d[ci]));
        v1 = hexa_to_float(((const char*)parts.d[(ci + 1)]));
        double d0 = (v0 - mean);
        double d1 = (v1 - mean);
        cov = (cov + (d0 * d1));
        var = (var + (d0 * d0));
        ci = (ci + 1);
    }
    double autocorr = (((var > 0.0)) ? ((cov / var)) : (0.0));
    long mid = (limit / 2);
    double first_sum = 0.0;
    ci = 0;
    while ((ci < mid)) {
        first_sum = (first_sum + hexa_to_float(((const char*)parts.d[ci])));
        ci = (ci + 1);
    }
    double second_sum = 0.0;
    ci = mid;
    while ((ci < limit)) {
        second_sum = (second_sum + hexa_to_float(((const char*)parts.d[ci])));
        ci = (ci + 1);
    }
    double first_mean = (first_sum / ((double)(mid)));
    double second_mean = (second_sum / ((double)((limit - mid))));
    double direction = abs_f((second_mean - first_mean));
    double sig = ((abs_f(autocorr) * 0.6) + (clamp01((direction * 0.1)) * 0.4));
    return clamp01(sig);
}

double lens_gravity(const char* vals) {
    hexa_arr parts = hexa_split(vals, "|");
    long n = parts.n;
    if ((n < 3)) {
        return 0.0;
    }
    long limit = (((n < 50)) ? (n) : (50));
    long attracted = 0;
    long ci = 0;
    while ((ci < limit)) {
        double v = 0.0;
        v = hexa_to_float(((const char*)parts.d[ci]));
        if (((abs_f((v - 6.0)) / 6.0) < 0.05)) {
            attracted = (attracted + 1);
        }
        if (((abs_f((v - 12.0)) / 12.0) < 0.05)) {
            attracted = (attracted + 1);
        }
        if (((abs_f((v - 2.0)) / 2.0) < 0.05)) {
            attracted = (attracted + 1);
        }
        if (((abs_f((v - 4.0)) / 4.0) < 0.05)) {
            attracted = (attracted + 1);
        }
        if (((abs_f((v - 24.0)) / 24.0) < 0.05)) {
            attracted = (attracted + 1);
        }
        if (((abs_f((v - 5.0)) / 5.0) < 0.05)) {
            attracted = (attracted + 1);
        }
        if (((abs_f((v - 137.036)) / 137.036) < 0.01)) {
            attracted = (attracted + 1);
        }
        if (((abs_f((v - 0.3153)) / 0.3153) < 0.05)) {
            attracted = (attracted + 1);
        }
        ci = (ci + 1);
    }
    return clamp01((((double)(attracted)) / ((double)(limit))));
}

double lens_thermo(const char* vals) {
    hexa_arr parts = hexa_split(vals, "|");
    long n = parts.n;
    if ((n < 2)) {
        return 0.0;
    }
    long limit = (((n < 50)) ? (n) : (50));
    long bin_map = 0;
}

const char* telescope_verify(const char* vals, double threshold) {
    double s1 = lens_consciousness(vals);
    double s2 = lens_topology(vals);
    double s3 = lens_causal(vals);
    double s4 = lens_gravity(vals);
    double s5 = lens_thermo(vals);
    long agree = 0;
    if ((s1 >= threshold)) {
        agree = (agree + 1);
    }
    if ((s2 >= threshold)) {
        agree = (agree + 1);
    }
    if ((s3 >= threshold)) {
        agree = (agree + 1);
    }
    if ((s4 >= threshold)) {
        agree = (agree + 1);
    }
    if ((s5 >= threshold)) {
        agree = (agree + 1);
    }
    const char* level = (((agree >= 4)) ? ("High") : (0));
    double boost = (((agree >= 5)) ? (0.2) : (0));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(agree)), "|"), level), "|"), hexa_float_to_str((double)(boost))), "|"), hexa_float_to_str((double)(s1))), "|"), hexa_float_to_str((double)(s2))), "|"), hexa_float_to_str((double)(s3))), "|"), hexa_float_to_str((double)(s4))), "|"), hexa_float_to_str((double)(s5)));
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    _n6_dyn = load_n6_consts();
    _n6_dyn_names = _n6_dyn.d[0];
    _n6_dyn_values = _n6_dyn.d[1];
    _n6_dyn_count = hexa_str_len(_n6_dyn_values);
    vals = /* unsupported method .values */ 0;
    cnt_vals = /* unsupported method .values */ 0;
    num_bins = hexa_str_len(cnt_vals);
    total = ((double)(limit));
    max_ent = ln_f(((double)((((num_bins > 1)) ? (num_bins) : (2)))));
    a = hexa_args();
    domain = (((a.n >= 3)) ? (((const char*)a.d[2])) : ("math"));
    max_depth = (((a.n >= 4)) ? (hexa_to_int_str(((const char*)a.d[3]))) : (6));
    mc = m_count(domain);
    evo_max_cycles = (((max_depth < 8)) ? (max_depth) : (8));
    current_seeds = seed_csv;
    closure = (((double)(frozen)) / ((double)(mc)));
    ax_count = (((frozen > 0)) ? (frozen) : (1));
    compression = (((double)(mc)) / ((double)(ax_count)));
    evo_boost = (evo_best_score * 0.1);
    effective_closure = min_f((closure + evo_boost), 1.0);
    buf_lines = hexa_arr_new();
    val_arr = hexa_arr_new();
    ax_name_arr = hexa_arr_new();
    ax_val_arr = hexa_arr_new();
    tele_result = telescope_verify(val_csv, 0.15);
    tparts = hexa_split(tele_result, "|");
    boosted_lines = hexa_arr_new();
    recurse_all_corollaries = hexa_arr_new();
    recurse_pool_size = buf_lines.n;
    hexa_bin_home = hexa_trim(hexa_exec("echo $HOME"));
    absorb_hexa_bin = hexa_concat(hexa_bin_home, "/Dev/hexa-lang/target/release/hexa");
    rline_count = buf_lines.n;
    show_limit = (((rline_count < 30)) ? (rline_count) : (30));
    rho = (((total > 0)) ? ((((double)((exact_count + near_count))) / ((double)(total)))) : (0.0));
    while ((li < lines.n)) {
        const char* line = ((const char*)lines.d[li]);
        if ((hexa_str_len(line) > 5)) {
            const char* t = graph_extract_field(line, "type");
            if ((strcmp(t, "edge") == 0)) {
                const char* f = graph_extract_field(line, "from");
                const char* to = graph_extract_field(line, "to");
                if ((hexa_str_len(f) > 0)) {
                    long d = 0;
                    d = deg_map[f];
                    deg_map[f] = (d + 1);
                }
                if ((hexa_str_len(to) > 0)) {
                    long d = 0;
                    d = deg_map[to];
                    deg_map[to] = (d + 1);
                }
            }
        }
        li = (li + 1);
    }
    while ((hi < hexa_str_len(vals))) {
        long d = vals[hi];
        if ((d >= min_deg)) {
            hub_count = (hub_count + 1);
        }
        hi = (hi + 1);
    }
    return hub_count;
    0;
    while ((ci < limit)) {
        double v = 0.0;
        v = hexa_to_float(((const char*)parts.d[ci]));
        const char* bin_key = hexa_int_to_str((long)((long)((v * 10.0))));
        long old_c = 0;
        old_c = bin_map[bin_key];
        bin_map[bin_key] = (old_c + 1);
        ci = (ci + 1);
    }
    ci = 0;
    while ((ci < num_bins)) {
        double c = ((double)(cnt_vals[ci]));
        double p = (c / total);
        if ((p > 0.0)) {
            entropy = (entropy - (p * ln_f(p)));
        }
        ci = (ci + 1);
    }
    if ((max_ent > 0.0)) {
        return clamp01((entropy / max_ent));
    }
    return 0.0;
    0;
    while ((ai < a.n)) {
        if ((strcmp(((const char*)a.d[ai]), "--no-graph") == 0)) {
            skip_graph = 1;
        }
        if ((strcmp(((const char*)a.d[ai]), "--fast") == 0)) {
            fast_mode = 1;
        }
        if (((strcmp(((const char*)a.d[ai]), "--seeds") == 0) && ((ai + 1) < a.n))) {
            ext_seeds = ((const char*)a.d[(ai + 1)]);
            ai = (ai + 1);
        }
        if (((strcmp(((const char*)a.d[ai]), "--pool-cap") == 0) && ((ai + 1) < a.n))) {
            ext_pool_cap = hexa_to_int_str(((const char*)a.d[(ai + 1)]));
            ai = (ai + 1);
        }
        ai = (ai + 1);
    }
    printf("%s\n", "");
    printf("%s\n", "======================================================");
    printf("%s\n", "        mk2 blowup engine (hexa-native)               ");
    printf("%s\n", "======================================================");
    printf("%s %s\n", "  domain  :", domain);
    printf("%s %ld\n", "  depth   :", (long)(max_depth));
    printf("%s\n", "");
    if ((!skip_graph)) {
        printf("%s\n", "--- Phase 1: Graph Load ---");
        graph_content = graph_load();
        graph_nodes_before = graph_count_type(graph_content, "node");
        graph_edges_before = graph_count_type(graph_content, "edge");
        graph_hubs_before = graph_hub_count(graph_content, 3);
        printf("%s\n", "  file             : shared/discovery_graph.json");
        printf("%s %ld\n", "  nodes (before)   :", (long)(graph_nodes_before));
        printf("%s %ld\n", "  edges (before)   :", (long)(graph_edges_before));
        printf("%s %ld\n", "  hubs (deg>=3)    :", (long)(graph_hubs_before));
    } else {
        printf("%s\n", "--- Phase 1: Graph Load (skipped --no-graph) ---");
    }
    printf("%s\n", "");
    printf("%s\n", "--- Phase 2: OUROBOROS Evolution ---");
    while ((si < mc)) {
        if ((hexa_str_len(seed_csv) > 0)) {
            seed_csv = hexa_concat(seed_csv, "|");
        }
        seed_csv = hexa_concat(seed_csv, hexa_float_to_str((double)(m_val(si, domain))));
        si = (si + 1);
    }
    while ((ec < evo_max_cycles)) {
        const char* result = evo_cycle(current_seeds, (ec + 1));
        hexa_arr rparts = hexa_split(result, "|");
        double cycle_score = 0.0;
        long cycle_disc = 0;
        cycle_score = hexa_to_float(((const char*)rparts.d[0]));
        cycle_disc = hexa_to_int_str(((const char*)rparts.d[1]));
        if ((cycle_score > evo_best_score)) {
            evo_best_score = cycle_score;
        }
        evo_total_disc = (evo_total_disc + cycle_disc);
        if ((hexa_str_len(evo_history) > 0)) {
            evo_history = hexa_concat(evo_history, "|");
        }
        evo_history = hexa_concat(evo_history, hexa_int_to_str((long)(cycle_disc)));
        const char* new_seeds = "";
        long ri = 2;
        while ((ri < rparts.n)) {
            if ((hexa_str_len(new_seeds) > 0)) {
                new_seeds = hexa_concat(new_seeds, "|");
            }
            new_seeds = hexa_concat(new_seeds, ((const char*)rparts.d[ri]));
            ri = (ri + 1);
        }
        if ((hexa_str_len(new_seeds) > 0)) {
            current_seeds = new_seeds;
        }
        evo_status = evo_check_convergence(evo_history);
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  cycle ", hexa_int_to_str((long)((ec + 1)))), ": score="), hexa_float_to_str((double)(cycle_score))), " disc="), hexa_int_to_str((long)(cycle_disc))), " status="), evo_status));
        if ((strcmp(evo_status, "Saturated") == 0)) {
            printf("%s\n", hexa_concat("  -> SATURATED at cycle ", hexa_int_to_str((long)((ec + 1)))));
            ec = evo_max_cycles;
        }
        ec = (ec + 1);
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  evolution total  : ", hexa_int_to_str((long)(evo_total_disc))), " discoveries, best="), hexa_float_to_str((double)(evo_best_score))));
    printf("%s\n", "");
    printf("%s\n", "--- Phase 3: Singularity Detection ---");
    si = 0;
    while ((si < mc)) {
        if ((n6_distance(m_val(si, domain)) < 0.01)) {
            frozen = (frozen + 1);
        }
        si = (si + 1);
    }
    printf("%s %g\n", "  closure (raw)    :", (double)(closure));
    printf("%s\n", hexa_concat("  evo boost        : +", hexa_float_to_str((double)(evo_boost))));
    printf("%s %g\n", "  closure (eff)    :", (double)(effective_closure));
    printf("%s %g\n", "  compression      :", (double)(compression));
    printf("%s %ld\n", "  axiom count      :", (long)(ax_count));
    if ((effective_closure >= 0.5)) {
        printf("%s\n", "  * SINGULARITY DETECTED -- closed system, blowup engaged");
    } else {
        printf("%s\n", "  o open system -- closure < 0.5, weak blowup");
    }
    printf("%s\n", "");
    printf("%s\n", "--- Phase 4: Recursive Blowup Corollary Generation ---");
    printf("%s %ld\n", "  max_depth :", (long)(max_depth));
    printf("%s\n", "");
    if ((hexa_str_len(ext_seeds) > 0)) {
        const char* _sep = ((hexa_contains(ext_seeds, ",")) ? (",") : ("|"));
        hexa_arr seed_parts = hexa_split(ext_seeds, _sep);
        long si = 0;
        while ((si < seed_parts.n)) {
            const char* sp_trimmed = hexa_trim(((const char*)seed_parts.d[si]));
            if ((strcmp(sp_trimmed, "") != 0)) {
                double sv = safe_float(sp_trimmed);
                if ((((sv != 0.0) || (strcmp(sp_trimmed, "0") == 0)) || (strcmp(sp_trimmed, "0.0") == 0))) {
                    ax_val_arr = hexa_arr_push(ax_val_arr, sv);
                    const char* nm = match_name(sv);
                    if ((strcmp(nm, "") != 0)) {
                        ax_name_arr = hexa_arr_push(ax_name_arr, (long)(nm));
                    } else {
                        ax_name_arr = hexa_arr_push(ax_name_arr, (long)(hexa_concat("s", hexa_int_to_str((long)(si)))));
                    }
                    ax_n = (ax_n + 1);
                }
            }
            si = (si + 1);
        }
        printf("%s\n", hexa_concat(hexa_concat("  seed source: DYNAMIC (", hexa_int_to_str((long)(ax_n))), " seeds from --seeds)"));
    } else {
        long init_i = 0;
        while ((init_i < mc)) {
            ax_name_arr = hexa_arr_push(ax_name_arr, (long)(m_name(init_i, domain)));
            ax_val_arr = hexa_arr_push(ax_val_arr, m_val(init_i, domain));
            ax_n = (ax_n + 1);
            init_i = (init_i + 1);
        }
        printf("%s\n", hexa_concat(hexa_concat("  seed source: STATIC (", hexa_int_to_str((long)(ax_n))), " domain metrics)"));
    }
    while ((depth < max_depth)) {
        long pool_before = ax_n;
        long total_before = total;
        long axiom_before = axiom_count;
        hexa_arr new_ax_name_arr = hexa_arr_new();
        hexa_arr new_ax_val_arr = hexa_arr_new();
        long new_ax_n = 0;
        long cur_n = ax_n;
        double phi_g = 1.618033988749895;
        long i = 0;
        while ((i < cur_n)) {
            long j = (i + 1);
            while ((j < cur_n)) {
                long va = ax_val_arr.d[i];
                long vb = ax_val_arr.d[j];
                long na = ax_name_arr.d[i];
                long nb = ax_name_arr.d[j];
                long product = (va * vb);
                double ratio = (((abs_f(vb) > 0.0000000001)) ? ((va / vb)) : (0.0));
                long sum = (va + vb);
                double dp = n6_distance(product);
                double dr = n6_distance(ratio);
                double ds = n6_distance(sum);
                long bv = product;
                double bd = dp;
                const char* bo = "*";
                if ((dr < bd)) {
                    bv = ratio;
                    bd = dr;
                    bo = "/";
                }
                if ((ds < bd)) {
                    bv = sum;
                    bd = ds;
                    bo = "+";
                }
                double conf = max_f((1.0 - bd), 0.0);
                if ((conf >= 0.1)) {
                    const char* nm = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("d", hexa_int_to_str((long)(depth))), "_ded_"), hexa_int_to_str((long)(na))), "_"), hexa_int_to_str((long)(nb)));
                    const char* ex = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(na)), " "), bo), " "), hexa_int_to_str((long)(nb))), " = "), hexa_int_to_str((long)(bv)));
                    double is_ax = (conf > 0.8);
                    buf_lines = hexa_arr_push(buf_lines, (long)(emit(nm, "deduction", ex, conf, bv, is_ax, na)));
                    total = (total + 1);
                    val_arr = hexa_arr_push(val_arr, (long)(hexa_int_to_str((long)(bv))));
                    const char* gr = match_grade(bv);
                    if ((strcmp(gr, "EXACT") == 0)) {
                        exact_count = (exact_count + 1);
                    }
                    if ((strcmp(gr, "NEAR") == 0)) {
                        near_count = (near_count + 1);
                    }
                    if ((conf >= 0.7)) {
                        high_conf_count = (high_conf_count + 1);
                    }
                    if (is_ax) {
                        axiom_count = (axiom_count + 1);
                        const char* cand_name = match_name(bv);
                        const char* cand_label = (((strcmp(cand_name, "") != 0)) ? (cand_name) : (nm));
                        if (((!hexa_contains(ax_name_arr, cand_label)) && (!hexa_contains(new_ax_name_arr, cand_label)))) {
                            new_ax_name_arr = hexa_arr_push(new_ax_name_arr, (long)(cand_label));
                            new_ax_val_arr = hexa_arr_push(new_ax_val_arr, bv);
                            new_ax_n = (new_ax_n + 1);
                        }
                    }
                    if (((strcmp(gr, "EXACT") == 0) && (!is_ax))) {
                        const char* cand_name2 = match_name(bv);
                        if ((((strcmp(cand_name2, "") != 0) && (!hexa_contains(ax_name_arr, cand_name2))) && (!hexa_contains(new_ax_name_arr, cand_name2)))) {
                            new_ax_name_arr = hexa_arr_push(new_ax_name_arr, (long)(cand_name2));
                            new_ax_val_arr = hexa_arr_push(new_ax_val_arr, bv);
                            new_ax_n = (new_ax_n + 1);
                        }
                    }
                }
                j = (j + 1);
            }
            i = (i + 1);
        }
        i = 0;
        while ((i < cur_n)) {
            long di = 0;
            while ((di < 6)) {
                long v = ax_val_arr.d[i];
                long na = ax_name_arr.d[i];
                double conf = (n6_conf(v) * 0.7);
                if ((conf >= 0.15)) {
                    const char* nm = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("d", hexa_int_to_str((long)(depth))), "_xfer_"), hexa_int_to_str((long)(na))), "_"), xfer_domain(di));
                    const char* ex = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(na)), "="), hexa_int_to_str((long)(v))), " in "), xfer_domain(di));
                    buf_lines = hexa_arr_push(buf_lines, (long)(emit(nm, "transfer", ex, conf, v, 0, na)));
                    total = (total + 1);
                    val_arr = hexa_arr_push(val_arr, (long)(hexa_int_to_str((long)(v))));
                    const char* gr = match_grade(v);
                    if ((strcmp(gr, "EXACT") == 0)) {
                        exact_count = (exact_count + 1);
                    }
                    if ((strcmp(gr, "NEAR") == 0)) {
                        near_count = (near_count + 1);
                    }
                    if ((conf >= 0.7)) {
                        high_conf_count = (high_conf_count + 1);
                    }
                }
                di = (di + 1);
            }
            i = (i + 1);
        }
        i = 0;
        while ((i < cur_n)) {
            long v = ax_val_arr.d[i];
            long na = ax_name_arr.d[i];
            double bp = (v + phi_g);
            double bm = (v - phi_g);
            double dp = n6_distance(bp);
            double dm = n6_distance(bm);
            double best = bp;
            double best_d = dp;
            const char* sign = "+";
            if ((dm < dp)) {
                best = bm;
                best_d = dm;
                sign = "-";
            }
            double conf = max_f((1.0 - best_d), 0.0);
            if ((conf >= 0.2)) {
                const char* nm = hexa_concat(hexa_concat(hexa_concat("d", hexa_int_to_str((long)(depth))), "_sym_"), hexa_int_to_str((long)(na)));
                const char* ex = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(na)), " "), sign), " phi = "), hexa_float_to_str((double)(best)));
                double is_ax = (conf > 0.7);
                buf_lines = hexa_arr_push(buf_lines, (long)(emit(nm, "symbreak", ex, conf, best, is_ax, na)));
                total = (total + 1);
                val_arr = hexa_arr_push(val_arr, (long)(hexa_float_to_str((double)(best))));
                const char* gr = match_grade(best);
                if ((strcmp(gr, "EXACT") == 0)) {
                    exact_count = (exact_count + 1);
                }
                if ((strcmp(gr, "NEAR") == 0)) {
                    near_count = (near_count + 1);
                }
                if ((conf >= 0.7)) {
                    high_conf_count = (high_conf_count + 1);
                }
                if (is_ax) {
                    axiom_count = (axiom_count + 1);
                    const char* cand_name = match_name(best);
                    const char* cand_label = (((strcmp(cand_name, "") != 0)) ? (cand_name) : (nm));
                    if (((!hexa_contains(ax_name_arr, cand_label)) && (!hexa_contains(new_ax_name_arr, cand_label)))) {
                        new_ax_name_arr = hexa_arr_push(new_ax_name_arr, (long)(cand_label));
                        new_ax_val_arr = hexa_arr_push(new_ax_val_arr, best);
                        new_ax_n = (new_ax_n + 1);
                    }
                }
                if (((strcmp(gr, "EXACT") == 0) && (!is_ax))) {
                    const char* cand_name2 = match_name(best);
                    if ((((strcmp(cand_name2, "") != 0) && (!hexa_contains(ax_name_arr, cand_name2))) && (!hexa_contains(new_ax_name_arr, cand_name2)))) {
                        new_ax_name_arr = hexa_arr_push(new_ax_name_arr, (long)(cand_name2));
                        new_ax_val_arr = hexa_arr_push(new_ax_val_arr, best);
                        new_ax_n = (new_ax_n + 1);
                    }
                }
            }
            i = (i + 1);
        }
        i = 0;
        while ((i < cur_n)) {
            long k = 0;
            while ((k < 6)) {
                double shifted = (ax_val_arr.d[i] + shift_val(k));
                long na = ax_name_arr.d[i];
                double conf = (max_f((1.0 - n6_distance(shifted)), 0.0) * 0.6);
                if ((conf >= 0.2)) {
                    const char* nm = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("d", hexa_int_to_str((long)(depth))), "_bif_"), hexa_int_to_str((long)(na))), "_"), shift_name(k));
                    const char* ex = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(na)), "+"), shift_name(k)), "="), hexa_float_to_str((double)(shifted)));
                    buf_lines = hexa_arr_push(buf_lines, (long)(emit(nm, "bifurc", ex, conf, shifted, 0, na)));
                    total = (total + 1);
                    val_arr = hexa_arr_push(val_arr, (long)(hexa_float_to_str((double)(shifted))));
                    const char* gr = match_grade(shifted);
                    if ((strcmp(gr, "EXACT") == 0)) {
                        exact_count = (exact_count + 1);
                    }
                    if ((strcmp(gr, "NEAR") == 0)) {
                        near_count = (near_count + 1);
                    }
                    if ((conf >= 0.7)) {
                        high_conf_count = (high_conf_count + 1);
                    }
                }
                k = (k + 1);
            }
            i = (i + 1);
        }
        i = 0;
        while ((i < cur_n)) {
            long j = (i + 1);
            while ((j < cur_n)) {
                long va = ax_val_arr.d[i];
                long vb = ax_val_arr.d[j];
                long na = ax_name_arr.d[i];
                long nb = ax_name_arr.d[j];
                double geom = sqrt_f((abs_f(va) * abs_f(vb)));
                double conf = (max_f((1.0 - n6_distance(geom)), 0.0) * 0.8);
                if ((conf >= 0.15)) {
                    const char* nm = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("d", hexa_int_to_str((long)(depth))), "_proj_"), hexa_int_to_str((long)(na))), "_"), hexa_int_to_str((long)(nb)));
                    const char* ex = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("sqrt(", hexa_int_to_str((long)(na))), "*"), hexa_int_to_str((long)(nb))), ")="), hexa_float_to_str((double)(geom)));
                    buf_lines = hexa_arr_push(buf_lines, (long)(emit(nm, "projection", ex, conf, geom, 0, na)));
                    total = (total + 1);
                    val_arr = hexa_arr_push(val_arr, (long)(hexa_float_to_str((double)(geom))));
                    const char* gr = match_grade(geom);
                    if ((strcmp(gr, "EXACT") == 0)) {
                        exact_count = (exact_count + 1);
                    }
                    if ((strcmp(gr, "NEAR") == 0)) {
                        near_count = (near_count + 1);
                    }
                    if ((conf >= 0.7)) {
                        high_conf_count = (high_conf_count + 1);
                    }
                }
                j = (j + 1);
            }
            i = (i + 1);
        }
        i = 0;
        while ((i < cur_n)) {
            long v = ax_val_arr.d[i];
            long na = ax_name_arr.d[i];
            if ((abs_f(v) > 0.0000000001)) {
                double dual = (1.0 / v);
                double conf = (max_f((1.0 - n6_distance(dual)), 0.0) * 0.6);
                if ((conf >= 0.15)) {
                    const char* nm = hexa_concat(hexa_concat(hexa_concat("d", hexa_int_to_str((long)(depth))), "_dual_"), hexa_int_to_str((long)(na)));
                    const char* ex = hexa_concat(hexa_concat(hexa_concat("1/", hexa_int_to_str((long)(na))), "="), hexa_float_to_str((double)(dual)));
                    buf_lines = hexa_arr_push(buf_lines, (long)(emit(nm, "dual", ex, conf, dual, 0, na)));
                    total = (total + 1);
                    val_arr = hexa_arr_push(val_arr, (long)(hexa_float_to_str((double)(dual))));
                    const char* gr = match_grade(dual);
                    if ((strcmp(gr, "EXACT") == 0)) {
                        exact_count = (exact_count + 1);
                    }
                    if ((strcmp(gr, "NEAR") == 0)) {
                        near_count = (near_count + 1);
                    }
                    if ((conf >= 0.7)) {
                        high_conf_count = (high_conf_count + 1);
                    }
                }
            }
            i = (i + 1);
        }
        long comp_limit = (((cur_n < 20)) ? (cur_n) : (20));
        i = 0;
        while ((i < comp_limit)) {
            long j = (i + 1);
            while ((j < comp_limit)) {
                long k = (j + 1);
                while ((k < comp_limit)) {
                    long va = ax_val_arr.d[i];
                    long vb = ax_val_arr.d[j];
                    long vc = ax_val_arr.d[k];
                    long na = ax_name_arr.d[i];
                    long nb = ax_name_arr.d[j];
                    long nc = ax_name_arr.d[k];
                    long prod1 = (va * vb);
                    long prod2 = (vb * vc);
                    long composed = (prod1 * prod2);
                    double c1 = n6_conf(prod1);
                    double c2 = n6_conf(prod2);
                    double conf = ((c1 * c2) * n6_conf(composed));
                    if ((conf >= 0.1)) {
                        const char* nm = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("d", hexa_int_to_str((long)(depth))), "_comp_"), hexa_int_to_str((long)(na))), "_"), hexa_int_to_str((long)(nb))), "_"), hexa_int_to_str((long)(nc)));
                        const char* ex = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("(", hexa_int_to_str((long)(na))), "*"), hexa_int_to_str((long)(nb))), ")*("), hexa_int_to_str((long)(nb))), "*"), hexa_int_to_str((long)(nc))), ")="), hexa_int_to_str((long)(composed)));
                        double is_ax = (conf > 0.7);
                        buf_lines = hexa_arr_push(buf_lines, (long)(emit(nm, "compose", ex, conf, composed, is_ax, na)));
                        total = (total + 1);
                        val_arr = hexa_arr_push(val_arr, (long)(hexa_int_to_str((long)(composed))));
                        const char* gr = match_grade(composed);
                        if ((strcmp(gr, "EXACT") == 0)) {
                            exact_count = (exact_count + 1);
                        }
                        if ((strcmp(gr, "NEAR") == 0)) {
                            near_count = (near_count + 1);
                        }
                        if ((conf >= 0.7)) {
                            high_conf_count = (high_conf_count + 1);
                        }
                        if (is_ax) {
                            axiom_count = (axiom_count + 1);
                            const char* cand_name = match_name(composed);
                            const char* cand_label = (((strcmp(cand_name, "") != 0)) ? (cand_name) : (nm));
                            if (((!hexa_contains(ax_name_arr, cand_label)) && (!hexa_contains(new_ax_name_arr, cand_label)))) {
                                new_ax_name_arr = hexa_arr_push(new_ax_name_arr, (long)(cand_label));
                                new_ax_val_arr = hexa_arr_push(new_ax_val_arr, composed);
                                new_ax_n = (new_ax_n + 1);
                            }
                        }
                        if (((strcmp(gr, "EXACT") == 0) && (!is_ax))) {
                            const char* cand_name2 = match_name(composed);
                            if ((((strcmp(cand_name2, "") != 0) && (!hexa_contains(ax_name_arr, cand_name2))) && (!hexa_contains(new_ax_name_arr, cand_name2)))) {
                                new_ax_name_arr = hexa_arr_push(new_ax_name_arr, (long)(cand_name2));
                                new_ax_val_arr = hexa_arr_push(new_ax_val_arr, composed);
                                new_ax_n = (new_ax_n + 1);
                            }
                        }
                    }
                    k = (k + 1);
                }
                j = (j + 1);
            }
            i = (i + 1);
        }
        long base_cap = (((ext_pool_cap > 0)) ? (ext_pool_cap) : (36));
        long pool_cap = (((ax_n > 30)) ? (0) : (base_cap));
        if (((new_ax_n > 0) && (new_ax_name_arr.n > 0))) {
            long room = (pool_cap - ax_n);
            if ((room > 0)) {
                long add_n = (((new_ax_n < room)) ? (new_ax_n) : (room));
                long ai = 0;
                while ((ai < add_n)) {
                    ax_name_arr = hexa_arr_push(ax_name_arr, new_ax_name_arr.d[ai]);
                    ax_val_arr = hexa_arr_push(ax_val_arr, new_ax_val_arr.d[ai]);
                    ax_n = (ax_n + 1);
                    ai = (ai + 1);
                }
            }
        }
        long depth_corollaries = (total - total_before);
        long depth_axioms = (axiom_count - axiom_before);
        if ((hexa_str_len(depth_stats) > 0)) {
            depth_stats = hexa_concat(depth_stats, "\n");
        }
        depth_stats = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(depth_stats, "  depth "), hexa_int_to_str((long)(depth))), ": "), hexa_int_to_str((long)(depth_corollaries))), " corollaries, "), hexa_int_to_str((long)(depth_axioms))), " axiom candidates (pool: "), hexa_int_to_str((long)(pool_before))), " -> "), hexa_int_to_str((long)(ax_n))), ")");
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  depth ", hexa_int_to_str((long)(depth))), ": "), hexa_int_to_str((long)(depth_corollaries))), " corollaries, "), hexa_int_to_str((long)(depth_axioms))), " axiom candidates (pool: "), hexa_int_to_str((long)(pool_before))), " -> "), hexa_int_to_str((long)(ax_n))), ")"));
        if (((ax_n == pool_before) && (depth > 0))) {
            printf("%s\n", hexa_concat(hexa_concat("  -> pool saturated at depth ", hexa_int_to_str((long)(depth))), ", no new axiom candidates"));
            depth = max_depth;
        }
        depth = (depth + 1);
    }
    printf("%s\n", "");
    printf("%s\n", "  --- depth breakdown ---");
    printf("%s\n", depth_stats);
    printf("%s\n", "");
    printf("%s %ld\n", "  total corollaries :", (long)(total));
    printf("%s %ld\n", "  EXACT match       :", (long)(exact_count));
    printf("%s %ld\n", "  NEAR match        :", (long)(near_count));
    printf("%s %ld\n", "  high conf >= 0.7  :", (long)(high_conf_count));
    printf("%s %ld\n", "  axiom candidates  :", (long)(axiom_count));
    printf("%s %ld\n", "  final pool size   :", (long)(ax_n));
    printf("%s\n", "");
    printf("%s\n", "--- Phase 5: Telescope Verification ---");
    0;
    join(val_arr);
    tele_agree = hexa_to_int_str(((const char*)tparts.d[0]));
    if ((tparts.n >= 2)) {
        tele_level = ((const char*)tparts.d[1]);
    }
    tele_boost = hexa_to_float(((const char*)tparts.d[2]));
    tele_s1 = hexa_to_float(((const char*)tparts.d[3]));
    tele_s2 = hexa_to_float(((const char*)tparts.d[4]));
    tele_s3 = hexa_to_float(((const char*)tparts.d[5]));
    tele_s4 = hexa_to_float(((const char*)tparts.d[6]));
    tele_s5 = hexa_to_float(((const char*)tparts.d[7]));
    printf("%s %g %s\n", "  consciousness    :", (double)(tele_s1), (((tele_s1 >= 0.15)) ? (" SIGNAL") : ("")));
    printf("%s %g %s\n", "  topology         :", (double)(tele_s2), (((tele_s2 >= 0.15)) ? (" SIGNAL") : ("")));
    printf("%s %g %s\n", "  causal           :", (double)(tele_s3), (((tele_s3 >= 0.15)) ? (" SIGNAL") : ("")));
    printf("%s %g %s\n", "  gravity          :", (double)(tele_s4), (((tele_s4 >= 0.15)) ? (" SIGNAL") : ("")));
    printf("%s %g %s\n", "  thermo           :", (double)(tele_s5), (((tele_s5 >= 0.15)) ? (" SIGNAL") : ("")));
    printf("%s %ld %s %s\n", "  consensus        :", (long)(tele_agree), "/ 5 ->", tele_level);
    printf("%s\n", hexa_concat("  confidence boost : +", hexa_float_to_str((double)(tele_boost))));
    printf("%s\n", "");
    while ((li < buf_lines.n)) {
        long line = buf_lines.d[li];
        if ((hexa_str_len(line) > 5)) {
            hexa_arr parts = hexa_split(line, "\t");
            if ((parts.n >= 7)) {
                double orig_conf = 0.0;
                orig_conf = hexa_to_float(((const char*)parts.d[3]));
                double new_conf = min_f((orig_conf + tele_boost), 1.0);
                boosted_lines = hexa_arr_push(boosted_lines, (long)(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(((const char*)parts.d[0]), "\t"), ((const char*)parts.d[1])), "\t"), ((const char*)parts.d[2])), "\t"), hexa_float_to_str((double)(new_conf))), "\t"), ((const char*)parts.d[4])), "\t"), ((const char*)parts.d[5])), "\t"), ((const char*)parts.d[6]))));
            }
        }
        li = (li + 1);
    }
    buf_lines = boosted_lines;
    li = 0;
    while ((li < buf_lines.n)) {
        long line = buf_lines.d[li];
        if ((hexa_str_len(line) > 5)) {
            hexa_arr parts = hexa_split(line, "\t");
            if ((parts.n >= 6)) {
                double c = 0.0;
                c = hexa_to_float(((const char*)parts.d[3]));
                if ((c >= 0.7)) {
                    boosted_high = (boosted_high + 1);
                }
                if ((c > 0.8)) {
                    boosted_axiom = (boosted_axiom + 1);
                }
            }
        }
        li = (li + 1);
    }
    if ((tele_boost > 0.0)) {
        printf("%s %ld %s %ld %s\n", "  post-boost high conf :", (long)(boosted_high), "(was", (long)(high_conf_count), ")");
        printf("%s %ld %s %ld %s\n", "  post-boost axioms    :", (long)(boosted_axiom), "(was", (long)(axiom_count), ")");
        high_conf_count = boosted_high;
        axiom_count = boosted_axiom;
        printf("%s\n", "");
    }
    if (skip_graph) {
        printf("%s\n", "--- Phase 6: Graph Update (skipped --no-graph) ---");
    } else {
        printf("%s\n", "--- Phase 6: Graph Update ---");
        double hub_bonus = (((graph_hubs_before > 0)) ? (0.1) : (0.0));
        if ((hub_bonus > 0.0)) {
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  hub bonus active : +", hexa_float_to_str((double)(hub_bonus))), " (from "), hexa_int_to_str((long)(graph_hubs_before))), " existing hubs)"));
        }
        li = 0;
        while ((li < buf_lines.n)) {
            long line = buf_lines.d[li];
            if ((hexa_str_len(line) > 5)) {
                hexa_arr parts = hexa_split(line, "\t");
                if ((parts.n >= 7)) {
                    double conf = 0.0;
                    conf = hexa_to_float(((const char*)parts.d[3]));
                    if ((conf >= 0.5)) {
                        const char* node_id = hexa_concat("blowup-", ((const char*)parts.d[0]));
                        if ((!graph_node_exists(graph_content, node_id))) {
                            double g_conf = min_f((conf + hub_bonus), 1.0);
                            const char* node_json = graph_append_node(node_id, "Discovery", domain, ((const char*)parts.d[2]), g_conf);
                            if ((hexa_str_len(graph_new_entries) > 0)) {
                                graph_new_entries = hexa_concat(graph_new_entries, "\n");
                            }
                            graph_new_entries = hexa_concat(graph_new_entries, node_json);
                            graph_added_nodes = (graph_added_nodes + 1);
                            const char* src_id = hexa_concat("n6-", ((const char*)parts.d[6]));
                            const char* edge_json = graph_append_edge(src_id, node_id, "Derives", conf);
                            graph_new_entries = hexa_concat(hexa_concat(graph_new_entries, "\n"), edge_json);
                            graph_added_edges = (graph_added_edges + 1);
                        }
                    }
                }
            }
            li = (li + 1);
        }
        if ((graph_added_nodes > 0)) {
            if ((hexa_str_len(graph_content) > 0)) {
                hexa_append_file(graph_path, hexa_concat("\n", graph_new_entries));
            } else {
                hexa_write_file(graph_path, graph_new_entries);
            }
        }
        long graph_nodes_after = (graph_nodes_before + graph_added_nodes);
        long graph_edges_after = (graph_edges_before + graph_added_edges);
        if ((!skip_graph)) {
            printf("%s %ld\n", "  nodes added      :", (long)(graph_added_nodes));
            printf("%s %ld\n", "  edges added      :", (long)(graph_added_edges));
            printf("%s %ld\n", "  nodes (after)    :", (long)(graph_nodes_after));
            printf("%s %ld\n", "  edges (after)    :", (long)(graph_edges_after));
        }
    }
    printf("%s\n", "");
    printf("%s\n", "--- Phase 6.5: OUROBOROS Dynamic Recursive Growth ---");
    li = 0;
    while (((li < buf_lines.n) && (axiom_fed < 12))) {
        long line = buf_lines.d[li];
        if ((hexa_str_len(line) > 5)) {
            hexa_arr parts = hexa_split(line, "\t");
            if ((parts.n >= 6)) {
                if ((strcmp(((const char*)parts.d[5]), "1") == 0)) {
                    double v = 0.0;
                    v = hexa_to_float(((const char*)parts.d[4]));
                    if ((abs_f(v) > 0.0000000001)) {
                        if ((hexa_str_len(axiom_seeds) > 0)) {
                            axiom_seeds = hexa_concat(axiom_seeds, "|");
                        }
                        axiom_seeds = hexa_concat(axiom_seeds, hexa_float_to_str((double)(v)));
                        axiom_fed = (axiom_fed + 1);
                    }
                }
            }
        }
        li = (li + 1);
    }
    if ((hexa_str_len(axiom_seeds) > 0)) {
        printf("%s\n", hexa_concat("  initial axiom seeds: ", hexa_int_to_str((long)(axiom_fed))));
        while (((recurse_round < recurse_max_rounds) && recurse_continue)) {
            recurse_round = (recurse_round + 1);
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  [round ", hexa_int_to_str((long)(recurse_round))), "/"), hexa_int_to_str((long)(recurse_max_rounds))), "] evolving "), hexa_int_to_str((long)(axiom_fed))), " seeds ..."));
            const char* r_result = evo_cycle(axiom_seeds, (evo_max_cycles + recurse_round));
            hexa_arr rp = hexa_split(r_result, "|");
            double round_score = 0.0;
            long round_disc = 0;
            round_score = hexa_to_float(((const char*)rp.d[0]));
            round_disc = hexa_to_int_str(((const char*)rp.d[1]));
            recurse_disc_total = (recurse_disc_total + round_disc);
            if ((round_score > recurse_score)) {
                recurse_score = round_score;
            }
            const char* next_seeds = "";
            long next_count = 0;
            long ri = 2;
            while (((ri < rp.n) && (next_count < 12))) {
                double rv = 0.0;
                rv = hexa_to_float(((const char*)rp.d[ri]));
                if ((abs_f(rv) > 0.0000000001)) {
                    double rv_conf = n6_conf(rv);
                    if ((rv_conf >= 0.3)) {
                        if ((hexa_str_len(next_seeds) > 0)) {
                            next_seeds = hexa_concat(next_seeds, "|");
                        }
                        next_seeds = hexa_concat(next_seeds, hexa_float_to_str((double)(rv)));
                        next_count = (next_count + 1);
                        const char* rv_grade = match_grade(rv);
                        const char* rv_name = match_name(rv);
                        const char* cor_line = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("R", hexa_int_to_str((long)(recurse_round))), "-"), hexa_int_to_str((long)(next_count))), "\trecursive\t"), domain), "\t"), hexa_float_to_str((double)(rv_conf))), "\t"), hexa_float_to_str((double)(rv))), "\t"), (((rv_conf >= 0.8)) ? ("1") : ("0"))), "\trecurse-r"), hexa_int_to_str((long)(recurse_round)));
                        recurse_all_corollaries = hexa_arr_concat(recurse_all_corollaries, hexa_arr_lit((long[]){(long)(cor_line)}, 1));
                    }
                }
                ri = (ri + 1);
            }
            long new_pool = (recurse_pool_size + round_disc);
            double closure_ratio = (((new_pool > 0)) ? ((((double)(round_disc)) / ((double)(new_pool)))) : (0.0));
            recurse_pool_size = new_pool;
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("    score=", hexa_float_to_str((double)(round_score))), " disc="), hexa_int_to_str((long)(round_disc))), " closure="), hexa_float_to_str((double)(closure_ratio))), " pool="), hexa_int_to_str((long)(recurse_pool_size))));
            if ((closure_ratio >= 0.5)) {
                printf("%s\n", "    -> closure saturated (>= 0.5), stopping");
                recurse_continue = 0;
            }
            if ((round_disc < 2)) {
                printf("%s\n", "    -> too few new discoveries (< 2), stopping");
                recurse_continue = 0;
            }
            if (recurse_continue) {
                if ((hexa_str_len(next_seeds) > 0)) {
                    axiom_seeds = next_seeds;
                    axiom_fed = next_count;
                } else {
                    printf("%s\n", "    -> no viable seeds for next round, stopping");
                    recurse_continue = 0;
                }
            }
        }
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  recursive growth complete: ", hexa_int_to_str((long)(recurse_round))), " rounds, "), hexa_int_to_str((long)(recurse_disc_total))), " total disc, best_score="), hexa_float_to_str((double)(recurse_score))));
        printf("%s\n", hexa_concat("  corollaries collected: ", hexa_int_to_str((long)(recurse_all_corollaries.n))));
    } else {
        printf("%s\n", "  no axiom candidates to feed back");
    }
    printf("%s\n", "");
    printf("%s\n", "--- Phase 6.7: Auto-Absorb ---");
    const char* _tail_result = hexa_exec(hexa_concat(hexa_concat("tail -150 ", log_path), " 2>/dev/null"));
    absorb_existing_log = _tail_result;
    if (((!skip_graph) && hexa_file_exists(graph_path))) {
        absorb_graph_content = hexa_read_file(graph_path);
    }
    while ((ci < recurse_all_corollaries.n)) {
        const char* cor_line = hexa_int_to_str((long)(recurse_all_corollaries.d[ci]));
        hexa_arr cor_parts = hexa_split(cor_line, "\t");
        if ((cor_parts.n >= 7)) {
            const char* cor_id = ((const char*)cor_parts.d[0]);
            const char* cor_type = ((const char*)cor_parts.d[1]);
            const char* cor_domain = ((const char*)cor_parts.d[2]);
            double cor_conf = 0.0;
            double cor_value = 0.0;
            cor_conf = hexa_to_float(((const char*)cor_parts.d[3]));
            cor_value = hexa_to_float(((const char*)cor_parts.d[4]));
            const char* cor_is_axiom = ((const char*)cor_parts.d[5]);
            const char* cor_src = ((const char*)cor_parts.d[6]);
            const char* cor_grade = match_grade(cor_value);
            const char* cor_name = match_name(cor_value);
            if ((strcmp(cor_grade, "EXACT") == 0)) {
                const char* dup_needle = hexa_concat(hexa_concat("\"blowup-", cor_id), "\"");
                if ((!hexa_contains(absorb_existing_log, dup_needle))) {
                    long ai_d = 0;
                    long ai_r = 0;
                    if ((cor_conf >= 0.8)) {
                        ai_d = 1;
                    }
                    if ((cor_conf < 0.8)) {
                        ai_r = 8;
                    }
                    const char* sector = sector_of(cor_domain);
                    const char* dn = (((strcmp(cor_name, "") != 0)) ? (cor_name) : (cor_id));
                    const char* log_entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"constant\":\"blowup-", dn), "\",\"value\":\""), hexa_float_to_str((double)(cor_value))), "\",\"source\":\"hexa-blowup-mk2-recurse\",\"timestamp\":\"2026-04-06\",\"grade\":\""), cor_grade), "\",\"processed\":true,\"alien_index\":{\"d\":"), hexa_int_to_str((long)(ai_d))), ",\"r\":"), hexa_int_to_str((long)(ai_r))), "},\"mk2\":{\"sector\":\""), sector), "\",\"confidence\":"), hexa_float_to_str((double)(cor_conf))), ",\"paths\":1,\"prime_set\":[],\"layer\":"), hexa_int_to_str((long)(ai_d))), "},\"blowup\":{\"type\":\""), cor_type), "\",\"src\":\""), cor_src), "\"},\"phase\":\"6.7-auto-absorb\",\"recurse_round\":\""), cor_id), "\"}");
                    absorb_log_entries = hexa_concat(hexa_concat(absorb_log_entries, log_entry), "\n");
                    absorb_logged = (absorb_logged + 1);
                    absorb_existing_log = hexa_concat(absorb_existing_log, dup_needle);
                }
            }
            if (((!skip_graph) && (cor_conf >= 0.5))) {
                const char* g_node_id = hexa_concat("recurse-", cor_id);
                if ((!graph_node_exists(absorb_graph_content, g_node_id))) {
                    double g_conf = min_f(cor_conf, 1.0);
                    const char* node_json = graph_append_node_d(g_node_id, "RecursiveDiscovery", cor_domain, cor_type, g_conf, 1);
                    if ((hexa_str_len(absorb_graph_entries) > 0)) {
                        absorb_graph_entries = hexa_concat(absorb_graph_entries, "\n");
                    }
                    absorb_graph_entries = hexa_concat(absorb_graph_entries, node_json);
                    absorb_graph_nodes = (absorb_graph_nodes + 1);
                    const char* src_node = hexa_concat("n6-", cor_src);
                    const char* edge_json = graph_append_edge(src_node, g_node_id, "RecursiveDerivation", cor_conf);
                    absorb_graph_entries = hexa_concat(hexa_concat(absorb_graph_entries, "\n"), edge_json);
                    absorb_graph_edges = (absorb_graph_edges + 1);
                    if (hexa_contains(absorb_graph_content, "\"BT-")) {
                        const char* bt_needle = hexa_concat(hexa_concat("\"domain\":\"", cor_domain), "\"");
                        if (hexa_contains(absorb_graph_content, bt_needle)) {
                            const char* xref_edge = graph_append_edge(g_node_id, hexa_concat("bt-", cor_domain), "CrossRef", (cor_conf * 0.8));
                            absorb_graph_entries = hexa_concat(hexa_concat(absorb_graph_entries, "\n"), xref_edge);
                            absorb_graph_edges = (absorb_graph_edges + 1);
                        }
                    }
                    absorb_graph_content = hexa_concat(absorb_graph_content, g_node_id);
                }
            }
            const char* bus_event = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"type\":\"absorb\",\"phase\":\"6.7\",\"id\":\"", cor_id), "\",\"value\":"), hexa_float_to_str((double)(cor_value))), ",\"grade\":\""), cor_grade), "\",\"confidence\":"), hexa_float_to_str((double)(cor_conf))), ",\"domain\":\""), cor_domain), "\",\"is_axiom\":"), cor_is_axiom), ",\"source\":\"blowup-recurse\",\"timestamp\":\"2026-04-06\"}");
            absorb_bus_entries = hexa_concat(hexa_concat(absorb_bus_entries, bus_event), "\n");
            absorb_bus_events = (absorb_bus_events + 1);
        }
        ci = (ci + 1);
    }
    if ((absorb_logged > 0)) {
        hexa_append_file(log_path, absorb_log_entries);
        printf("%s\n", hexa_concat(hexa_concat("  discovery_log  : +", hexa_int_to_str((long)(absorb_logged))), " EXACT entries"));
    }
    if (((!skip_graph) && (absorb_graph_nodes > 0))) {
        if (hexa_file_exists(graph_path)) {
            hexa_append_file(graph_path, hexa_concat("\n", absorb_graph_entries));
        } else {
            hexa_write_file(graph_path, absorb_graph_entries);
        }
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  graph          : +", hexa_int_to_str((long)(absorb_graph_nodes))), " nodes, +"), hexa_int_to_str((long)(absorb_graph_edges))), " edges"));
    }
    if ((absorb_bus_events > 0)) {
        hexa_append_file(growth_bus_path, absorb_bus_entries);
        printf("%s\n", hexa_concat(hexa_concat("  growth_bus     : +", hexa_int_to_str((long)(absorb_bus_events))), " absorb events"));
    }
    if (hexa_file_exists(absorb_hexa_path)) {
        printf("%s\n", "  invoking absorb.hexa distribution ...");
        const char* absorb_result = "";
        absorb_result = hexa_exec(absorb_hexa_bin);
        if ((hexa_str_len(absorb_result) > 0)) {
            printf("%s %s\n", "  ", ((const char*)(hexa_split(absorb_result, "\n")).d[0]));
        }
    }
    if (((absorb_logged == 0) && (absorb_graph_nodes == 0))) {
        printf("%s\n", "  no new absorptions (all duplicates or below threshold)");
    }
    printf("%s\n", "");
    printf("%s\n", "--- Phase 7: Comprehensive Report ---");
    printf("%s\n", "");
    printf("%s\n", "  --- corollaries (top 30, boosted confidence) ---");
    li = 0;
    while (((li < rline_count) && (shown < show_limit))) {
        long line = buf_lines.d[li];
        if ((hexa_str_len(line) > 5)) {
            hexa_arr parts = hexa_split(line, "\t");
            if ((parts.n >= 5)) {
                const char* tag = ((((parts.n >= 6) && (strcmp(((const char*)parts.d[5]), "1") == 0))) ? (" [AXIOM]") : (""));
                double pval = 0.0;
                pval = hexa_to_float(((const char*)parts.d[4]));
                const char* gr = match_grade(pval);
                printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  [", hexa_int_to_str((long)((shown + 1)))), "] "), ((const char*)parts.d[1])), " | "), ((const char*)parts.d[2])), " | conf="), ((const char*)parts.d[3])), " "), gr), tag));
                shown = (shown + 1);
            }
        }
        li = (li + 1);
    }
    printf("%s\n", "");
    printf("%s\n", "  ========== SUMMARY ==========");
    printf("%s\n", "");
    printf("%s\n", "  Pipeline Phase         Result");
    printf("%s\n", "  ---------------------- ----------------------------");
    if (skip_graph) {
        printf("%s\n", "  1. Graph Load          (skipped)");
    } else {
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  1. Graph Load          ", hexa_int_to_str((long)(graph_nodes_before))), " nodes, "), hexa_int_to_str((long)(graph_edges_before))), " edges"));
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  2. OUROBOROS Evo       ", hexa_int_to_str((long)(evo_total_disc))), " disc, score="), hexa_float_to_str((double)(evo_best_score))), ", "), evo_status));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  3. Singularity         closure=", hexa_float_to_str((double)(effective_closure))), " compression="), hexa_float_to_str((double)(compression))));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  4. Corollaries         ", hexa_int_to_str((long)(total))), " total, "), hexa_int_to_str((long)(exact_count))), " EXACT, "), hexa_int_to_str((long)(near_count))), " NEAR, pool="), hexa_int_to_str((long)(ax_n))));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  5. Telescope           ", hexa_int_to_str((long)(tele_agree))), "/5 consensus ("), tele_level), "), boost=+"), hexa_float_to_str((double)(tele_boost))));
    if (skip_graph) {
        printf("%s\n", "  6. Graph Update        (skipped)");
    } else {
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  6. Graph Update        +", hexa_int_to_str((long)(graph_added_nodes))), " nodes, +"), hexa_int_to_str((long)(graph_added_edges))), " edges -> "), hexa_int_to_str((long)(graph_nodes_after))), "/"), hexa_int_to_str((long)(graph_edges_after))));
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  6.5 Recursive Growth   ", hexa_int_to_str((long)(recurse_disc_total))), " disc ("), hexa_int_to_str((long)(recurse_round))), " rounds), score="), hexa_float_to_str((double)(recurse_score))));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  6.7 Auto-Absorb       +", hexa_int_to_str((long)(absorb_logged))), " log, +"), hexa_int_to_str((long)(absorb_graph_nodes))), " graph, +"), hexa_int_to_str((long)(absorb_bus_events))), " bus"));
    printf("%s\n", "");
    printf("%s %ld\n", "  axiom candidates  :", (long)(axiom_count));
    printf("%s %ld\n", "  high confidence   :", (long)(high_conf_count));
    printf("%s %g\n", "  breakthrough rho  :", (double)(rho));
    printf("%s\n", "  meta fixed point  : 0.333... (target)");
    printf("%s\n", "");
    li = 0;
    while ((li < buf_lines.n)) {
        long line = buf_lines.d[li];
        if ((hexa_str_len(line) > 5)) {
            hexa_arr parts = hexa_split(line, "\t");
            if ((parts.n >= 7)) {
                double conf = 0.0;
                double value = 0.0;
                conf = hexa_to_float(((const char*)parts.d[3]));
                value = hexa_to_float(((const char*)parts.d[4]));
                if ((conf >= 0.5)) {
                    const char* grade = match_grade(value);
                    const char* name = match_name(value);
                    const char* sector = sector_of(((const char*)parts.d[2]));
                    long ai_d = 0;
                    long ai_r = 0;
                    if (((strcmp(grade, "EXACT") == 0) && (conf >= 0.8))) {
                        ai_d = 1;
                        ai_r = 0;
                    }
                    if (((strcmp(grade, "EXACT") == 0) && (conf < 0.8))) {
                        ai_r = 8;
                    }
                    if (((strcmp(grade, "EXACT") != 0) && (conf >= 0.5))) {
                        ai_r = 6;
                    }
                    const char* dn = (((strcmp(name, "") != 0)) ? (name) : (((const char*)parts.d[0])));
                    const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"constant\":\"blowup-", dn), "\",\"value\":\""), hexa_float_to_str((double)(value))), "\",\"source\":\"hexa-blowup-mk2\",\"timestamp\":\"2026-04-06\",\"grade\":\""), grade), "\",\"processed\":true,\"alien_index\":{\"d\":"), hexa_int_to_str((long)(ai_d))), ",\"r\":"), hexa_int_to_str((long)(ai_r))), "},\"mk2\":{\"sector\":\""), sector), "\",\"confidence\":"), hexa_float_to_str((double)(conf))), ",\"paths\":1,\"prime_set\":[],\"layer\":"), hexa_int_to_str((long)(ai_d))), "},\"blowup\":{\"type\":\""), ((const char*)parts.d[1])), "\",\"src\":\""), ((const char*)parts.d[6])), "\"},\"telescope\":{\"consensus\":"), hexa_int_to_str((long)(tele_agree))), ",\"level\":\""), tele_level), "\",\"boost\":"), hexa_float_to_str((double)(tele_boost))), "},\"evolution\":{\"cycles\":"), hexa_int_to_str((long)(evo_max_cycles))), ",\"best_score\":"), hexa_float_to_str((double)(evo_best_score))), ",\"status\":\""), evo_status), "\"}}");
                    new_entries = hexa_concat(hexa_concat(new_entries, entry), "\n");
                    logged = (logged + 1);
                }
            }
        }
        li = (li + 1);
    }
    if ((logged > 0)) {
        hexa_append_file(log_path, new_entries);
    }
    if ((logged > 0)) {
        printf("%s %ld %s %s\n", "  discovery_log:", (long)(logged), "entries ->", log_path);
    }
    if (((!skip_graph) && (graph_added_nodes > 0))) {
        printf("%s %ld %s %s\n", "  graph updated:", (long)(graph_added_nodes), "nodes ->", graph_path);
    }
    printf("%s\n", "");
    if (fast_mode) {
        printf("%s\n", "═══ Phase 7.1: (skipped --fast) ═══");
        printf("%s\n", "═══ Phase 7.5: (skipped --fast) ═══");
        printf("%s\n", "");
        printf("%s\n", "done.");
    } else {
        printf("%s\n", "═══ Phase 7.1: Next Breakthrough Directions ═══");
        printf("%s\n", "");
        const char* bt_directions = "";
        long bt_dir_count = 0;
        li = 0;
        while ((li < buf_lines.n)) {
            long line = buf_lines.d[li];
            if ((hexa_str_len(line) > 5)) {
                hexa_arr parts = hexa_split(line, "\t");
                if ((parts.n >= 5)) {
                    double pval = 0.0;
                    pval = hexa_to_float(((const char*)parts.d[4]));
                    const char* gr = match_grade(pval);
                    const char* nm = match_name(pval);
                    double pconf = 0.0;
                    pconf = hexa_to_float(((const char*)parts.d[3]));
                    if ((((strcmp(gr, "EXACT") == 0) && (pconf < 0.9)) && (pconf >= 0.3))) {
                        const char* src = (((parts.n >= 7)) ? (((const char*)parts.d[6])) : ("?"));
                        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  🎯 [EXACT 미확정] ", nm), "="), hexa_float_to_str((double)(pval))), " conf="), hexa_float_to_str((double)(pconf))), " ← 추가 검증 시 돌파"));
                        bt_dir_count = (bt_dir_count + 1);
                    }
                    if (((strcmp(gr, "NEAR") == 0) && (pconf >= 0.4))) {
                        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  🔍 [NEAR→EXACT] ", nm), "≈"), hexa_float_to_str((double)(pval))), " conf="), hexa_float_to_str((double)(pconf))), " ← 정밀 seed 주입 시 돌파"));
                        bt_dir_count = (bt_dir_count + 1);
                    }
                }
            }
            li = (li + 1);
        }
        hexa_arr _n6_data = load_n6_consts();
        hexa_arr n6_bases = n6_values(_n6_data);
        hexa_arr n6_names = n6_names_list(_n6_data);
        long bi = 0;
        while ((bi < 10)) {
            long found_this = 0;
            li = 0;
            while ((li < buf_lines.n)) {
                long line = buf_lines.d[li];
                if ((hexa_str_len(line) > 5)) {
                    hexa_arr parts = hexa_split(line, "\t");
                    if ((parts.n >= 5)) {
                        double pval = 0.0;
                        pval = hexa_to_float(((const char*)parts.d[4]));
                        if ((abs_f((pval - n6_bases.d[bi])) < 0.01)) {
                            found_this = 1;
                        }
                    }
                }
                li = (li + 1);
            }
            if ((!found_this)) {
                printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  ⬡ [미출현] ", hexa_int_to_str((long)(n6_names.d[bi]))), "="), hexa_int_to_str((long)(n6_bases.d[bi]))), " ← 이 도메인에서 seed \""), hexa_int_to_str((long)(n6_names.d[bi]))), "\" 주입 시 돌파 가능"));
                bt_dir_count = (bt_dir_count + 1);
            }
            bi = (bi + 1);
        }
        hexa_arr cross_domains = hexa_arr_new();
        const char* dse_raw = hexa_read_file(hexa_concat(hexa_int_to_str((long)(nexus_dir)), "/shared/dse_domains.jsonl"));
        hexa_arr dse_lines = hexa_split(dse_raw, "\n");
        long __fi_dse_line = 0;
        while ((__fi_dse_line < dse_lines.n)) {
            const char* dse_line = ((const char*)dse_lines.d[__fi_dse_line]);
            const char* dl = hexa_trim(dse_line);
            if ((strcmp(dl, "") == 0)) {
                continue;
            }
            if (hexa_contains(dl, "\"domain\":\"")) {
                hexa_arr dp = hexa_split(dl, "\"domain\":\"");
                if ((dp.n >= 2)) {
                    cross_domains = hexa_arr_concat(cross_domains, hexa_arr_lit((long[]){(long)(((const char*)(hexa_split(((const char*)dp.d[1]), "\"")).d[0]))}, 1));
                }
            }
            __fi_dse_line = (__fi_dse_line + 1);
        }
        long ci = 0;
        while ((ci < cross_domains.n)) {
            if ((cross_domains.d[ci] != domain)) {
                const char* cross_count = "";
                cross_count = hexa_trim(hexa_exec(hexa_concat(hexa_concat("grep -c '\"", hexa_int_to_str((long)(cross_domains.d[ci]))), "\"' shared/discovery_log.jsonl 2>/dev/null")));
                long cc = 0;
                cc = (long)(hexa_to_float(cross_count));
                if ((cc > 10)) {
                    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  🔀 [교차수분] ", hexa_int_to_str((long)(cross_domains.d[ci]))), " ("), hexa_int_to_str((long)(cc))), "건 발견) × "), domain), " → 교차 돌파 가능"));
                    bt_dir_count = (bt_dir_count + 1);
                }
            }
            ci = (ci + 1);
        }
        printf("%s\n", "");
        printf("%s\n", "  ─── D. gap_finder 빈공간 교차 분석 ───");
        const char* gap_finder_path = "mk2_hexa/native/gap_finder.hexa";
        const char* gap_result = hexa_exec(absorb_hexa_bin);
        const char* buf_concat = "";
        long bci = 0;
        while (((bci < buf_lines.n) && (bci < 100))) {
            buf_concat = hexa_concat(hexa_concat(buf_concat, " "), hexa_int_to_str((long)(buf_lines.d[bci])));
            bci = (bci + 1);
        }
        hexa_arr gap_lines = hexa_split(gap_result, "\n");
        long gap_shown = 0;
        long gli = 0;
        while ((gli < gap_lines.n)) {
            const char* gl = ((const char*)gap_lines.d[gli]);
            if ((hexa_str_len(gl) > 3)) {
                hexa_arr gparts = hexa_split(hexa_trim(gl), "=");
                if ((gparts.n >= 2)) {
                    const char* gname = hexa_trim(((const char*)gparts.d[0]));
                    if (((hexa_str_len(gname) > 2) && hexa_contains(buf_concat, gname))) {
                        if ((gap_shown < 10)) {
                            printf("%s\n", hexa_concat("    🔗 [교차] ", hexa_trim(gl)));
                            bt_dir_count = (bt_dir_count + 1);
                            gap_shown = (gap_shown + 1);
                        }
                    }
                }
            }
            gli = (gli + 1);
        }
        if ((gap_shown == 0)) {
            printf("%s\n", "    (이번 돌파와 빈공간의 직접 교차 없음)");
        } else {
            printf("%s\n", hexa_concat(hexa_concat("    → ", hexa_int_to_str((long)(gap_shown))), "건 빈공간-돌파 교차 발견"));
        }
        printf("%s\n", "");
        if ((bt_dir_count == 0)) {
            printf("%s\n", "  (돌파 가능 방향 없음 — 이 도메인은 포화 상태)");
        }
        printf("%s\n", "");
        printf("%s\n", hexa_concat(hexa_concat("  총 ", hexa_int_to_str((long)(bt_dir_count))), "개 돌파 가능 방향 탐지"));
        printf("%s\n", "");
        const char* directions_path = "mk2_hexa/native/directions.hexa";
        hexa_exec(absorb_hexa_bin);
        printf("%s\n", "═══ Phase 7.5: Auto-Link (parallel) ═══");
        const char* engine_dir = "mk2_hexa/native";
        const char* hexa_bin = hexa_concat(hexa_trim(hexa_exec("echo $HOME")), "/Dev/hexa-lang/target/release/hexa");
        const char* autolink_log = "shared/.autolink_out.log";
        const char* forge_path = hexa_concat(engine_dir, "/lens_forge.hexa");
        const char* ar_path = hexa_concat(engine_dir, "/auto_register.hexa");
        const char* gf_path = hexa_concat(engine_dir, "/gap_finder.hexa");
        const char* ai_path = hexa_concat(engine_dir, "/alien_index.hexa");
        const char* par_cmd = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_bin, " "), forge_path), " forge > "), autolink_log), ".a 2>&1 & ");
        par_cmd = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(par_cmd, hexa_bin), " "), ar_path), " sync "), log_path), " > "), autolink_log), ".b 2>&1 & ");
        par_cmd = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(par_cmd, hexa_bin), " "), gf_path), " target > "), autolink_log), ".c 2>&1 & ");
        if ((logged > 0)) {
            par_cmd = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(par_cmd, hexa_bin), " "), ai_path), " alien_index.hexa assess 6.0 > "), autolink_log), ".d 2>&1 & ");
        }
        par_cmd = hexa_concat(par_cmd, "wait");
        printf("%s\n", "  [parallel] lens_forge + auto_register + gap_finder + alien_index ...");
        hexa_exec("bash");
        const char* al_a = "";
        const char* al_b = "";
        const char* al_c = "";
        const char* al_d = "";
        al_a = hexa_exec(hexa_concat(hexa_concat("head -1 ", autolink_log), ".a 2>/dev/null"));
        al_b = hexa_exec(hexa_concat(hexa_concat("head -1 ", autolink_log), ".b 2>/dev/null"));
        al_c = hexa_exec(hexa_concat(hexa_concat("head -1 ", autolink_log), ".c 2>/dev/null"));
        if ((logged > 0)) {
            al_d = hexa_exec(hexa_concat(hexa_concat("head -1 ", autolink_log), ".d 2>/dev/null"));
        }
        printf("%s %s\n", "  [7.5a] ", hexa_trim(al_a));
        printf("%s %s\n", "  [7.5b] ", hexa_trim(al_b));
        printf("%s %s\n", "  [7.5c] ", hexa_trim(al_c));
        if ((logged > 0)) {
            printf("%s %s\n", "  [7.5d] ", hexa_trim(al_d));
        }
        printf("%s\n", "═══ Phase 7.5 complete ═══");
        printf("%s\n", "");
        printf("%s\n", "done.");
    }
    return 0;
}
