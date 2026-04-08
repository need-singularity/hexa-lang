#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static long hexa_time_unix(void) { return (long)time(NULL); }
static double hexa_clock(void) { return (double)clock() / CLOCKS_PER_SEC; }
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

long verify_path(const char* path, const char* label);
long verify_all_paths();

static const char* home;
static const char* dev;
static const char* nexus_root;
static const char* hexa_bin;
static const char* engine_dir;
static const char* shared_dir;
static const char* scripts_dir;
static const char* docs_dir;
static const char* discovery_log;
static const char* discovery_graph;
static const char* growth_bus;
static const char* custom_lenses;
static const char* lens_registry;
static const char* math_atlas;
static const char* consciousness_laws;
static const char* sedi_grades;
static const char* calculators;
static const char* projects_json;
static const char* bridge_state;
static const char* alien_index_dist;
static const char* alien_index_frontier;
static const char* verified_constants;
static const char* forge_result;
static const char* scan_log;
static const char* evolution_log;
static const char* anima_root;
static const char* tecs_l_root;
static const char* sedi_root;
static const char* papers_root;
static const char* hexa_lang_root;
static const char* airgenome_root;
static const char* brainwire_root;
static const char* fathom_root;
static const char* token_forge_root;
static const char* air_rs_root;
static const char* n6_guard_config;
static long max_concurrent = 2;
static long burst_concurrent = 4;
static hexa_arr a;

long verify_path(const char* path, const char* label) {
    const char* r = "";
    r = hexa_exec("test");
    if ((strcmp(hexa_trim(r), "1") != 0)) {
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat("⚠ PATH MISSING: ", label), " → "), path));
        return 0;
    }
    return 1;
}

long verify_all_paths() {
    long missing = 0;
    hexa_arr checks = hexa_arr_lit((long[]){hexa_arr_lit((long[]){(long)(nexus_root), (long)("nexus_root")}, 2), hexa_arr_lit((long[]){(long)(engine_dir), (long)("engine_dir")}, 2), hexa_arr_lit((long[]){(long)(shared_dir), (long)("shared_dir")}, 2), hexa_arr_lit((long[]){(long)(discovery_log), (long)("discovery_log")}, 2), hexa_arr_lit((long[]){(long)(projects_json), (long)("projects_json")}, 2), hexa_arr_lit((long[]){(long)(hexa_bin), (long)("hexa_bin")}, 2)}, 6);
    long __fi_c_1 = 0;
    while ((__fi_c_1 < checks.n)) {
        long c = checks.d[__fi_c_1];
        if ((!verify_path(c[0], c[1]))) {
            missing = (missing + 1);
        }
        __fi_c_1 = (__fi_c_1 + 1);
    }
    if ((missing == 0)) {
        printf("%s\n", "✓ All critical paths verified");
    } else {
        printf("%s\n", hexa_concat(hexa_concat("⚠ ", hexa_int_to_str((long)(missing))), " paths missing"));
    }
    return missing;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    home = hexa_exec("printenv HOME");
    dev = hexa_concat(home, "/Dev");
    nexus_root = hexa_concat(dev, "/nexus");
    hexa_bin = hexa_concat(dev, "/hexa-lang/target/release/hexa");
    engine_dir = hexa_concat(nexus_root, "/mk2_hexa/native");
    shared_dir = hexa_concat(nexus_root, "/shared");
    scripts_dir = hexa_concat(nexus_root, "/scripts");
    docs_dir = hexa_concat(nexus_root, "/docs");
    discovery_log = hexa_concat(shared_dir, "/discovery_log.jsonl");
    discovery_graph = hexa_concat(shared_dir, "/discovery_graph.json");
    growth_bus = hexa_concat(shared_dir, "/growth_bus.jsonl");
    custom_lenses = hexa_concat(shared_dir, "/custom_lenses.jsonl");
    lens_registry = hexa_concat(shared_dir, "/lens_registry.json");
    math_atlas = hexa_concat(shared_dir, "/math_atlas.json");
    consciousness_laws = hexa_concat(shared_dir, "/consciousness_laws.json");
    sedi_grades = hexa_concat(shared_dir, "/sedi-grades.json");
    calculators = hexa_concat(shared_dir, "/calculators.json");
    projects_json = hexa_concat(shared_dir, "/projects.json");
    bridge_state = hexa_concat(shared_dir, "/bridge_state.json");
    alien_index_dist = hexa_concat(shared_dir, "/alien_index_distribution.json");
    alien_index_frontier = hexa_concat(shared_dir, "/alien_index_frontier.md");
    verified_constants = hexa_concat(shared_dir, "/verified_constants.jsonl");
    forge_result = hexa_concat(shared_dir, "/forge_result.json");
    scan_log = hexa_concat(shared_dir, "/scan_log.jsonl");
    evolution_log = hexa_concat(shared_dir, "/evolution_log.jsonl");
    anima_root = hexa_concat(dev, "/anima");
    tecs_l_root = hexa_concat(dev, "/TECS-L");
    sedi_root = hexa_concat(dev, "/sedi");
    papers_root = hexa_concat(dev, "/papers");
    hexa_lang_root = hexa_concat(dev, "/hexa-lang");
    airgenome_root = hexa_concat(dev, "/airgenome");
    brainwire_root = hexa_concat(dev, "/brainwire");
    fathom_root = hexa_concat(dev, "/fathom");
    token_forge_root = hexa_concat(dev, "/token-forge");
    air_rs_root = hexa_concat(dev, "/air_rs");
    n6_guard_config = hexa_concat(home, "/.config/n6-guard.toml");
    a = hexa_args();
    if (((a.n >= 2) && (strcmp(((const char*)a.d[1]), "verify") == 0))) {
        long m = verify_all_paths();
        printf("%s\n", "");
        printf("%s\n", hexa_concat(hexa_concat("=== paths.hexa verified (", hexa_int_to_str((long)(m))), " missing) ==="));
    }
    if (((a.n >= 2) && (strcmp(((const char*)a.d[1]), "list") == 0))) {
        printf("%s\n", hexa_concat("dev             = ", dev));
        printf("%s\n", hexa_concat("nexus_root     = ", nexus_root));
        printf("%s\n", hexa_concat("hexa_bin        = ", hexa_bin));
        printf("%s\n", hexa_concat("engine_dir      = ", engine_dir));
        printf("%s\n", hexa_concat("shared_dir      = ", shared_dir));
        printf("%s\n", hexa_concat("discovery_log   = ", discovery_log));
        printf("%s\n", hexa_concat("discovery_graph = ", discovery_graph));
        printf("%s\n", hexa_concat("growth_bus      = ", growth_bus));
        printf("%s\n", hexa_concat("custom_lenses   = ", custom_lenses));
        printf("%s\n", hexa_concat("math_atlas      = ", math_atlas));
        printf("%s\n", hexa_concat("projects_json   = ", projects_json));
        printf("%s\n", hexa_concat("alien_index_dist= ", alien_index_dist));
        printf("%s\n", "");
        printf("%s\n", "=== paths.hexa list ===");
    }
    if (((a.n >= 2) && (strcmp(((const char*)a.d[1]), "help") == 0))) {
        printf("%s\n", "paths.hexa — 공유 경로 상수 모듈");
        printf("%s\n", "");
        printf("%s\n", "사용법:");
        printf("%s\n", "  hexa paths.hexa verify   경로 존재 검증");
        printf("%s\n", "  hexa paths.hexa list     모든 경로 출력");
        printf("%s\n", "  hexa paths.hexa help     이 도움말");
    }
    return 0;
}
