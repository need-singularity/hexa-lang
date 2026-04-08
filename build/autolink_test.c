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

const char* timestamp();
long file_exists(const char* path);
long file_size(const char* path);
long file_mtime_epoch(const char* path);
const char* read_file_safe(const char* path);
long append_bus(const char* source, const char* phase, const char* status, const char* detail);
long pad_str(const char* s, long width);
long str_contains_any(const char* haystack, const char* needles);
hexa_arr all_link_points();
long check_blowup_to_lens();

static const char* version = "1.0.0";
static long total_points = 17;
static const char* home;
static const char* dev;
static const char* hexa;
static const char* engine_dir;
static const char* shared;
static const char* bus_file;
static const char* discovery_log;
static const char* discovery_graph;
static const char* discovery_graph_bak;
static const char* custom_lenses;
static const char* alien_dist;
static const char* projects_json;
static const char* growth_md;
static const char* readme_md;
static const char* forge_result;
static const char* scan_log;
static const char* evolution_log;

const char* timestamp() {
    const char* r = hexa_exec("date +%Y-%m-%dT%H:%M:%S");
    return hexa_trim(r);
}

long file_exists(const char* path) {
    const char* r = "";
    r = hexa_exec("test");
    return (strcmp(hexa_trim(r), "1") == 0);
}

long file_size(const char* path) {
    if ((!hexa_file_exists(path))) {
        return 0;
    }
    const char* r = "0";
    r = hexa_exec("wc");
    const char* trimmed = hexa_trim(r);
    long val = 0;
    val = (long)(hexa_to_float(((const char*)(hexa_split(trimmed, " ")).d[0])));
    return val;
}

long file_mtime_epoch(const char* path) {
    if ((!hexa_file_exists(path))) {
        return 0;
    }
    const char* r = "0";
    r = hexa_exec("stat");
    long val = 0;
    val = (long)(hexa_to_float(hexa_trim(r)));
    return val;
}

const char* read_file_safe(const char* path) {
    return hexa_read_file(path);
}

long append_bus(const char* source, const char* phase, const char* status, const char* detail) {
    const char* ts = timestamp();
    const char* line = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"source\":\"", source), "\",\"timestamp\":\""), ts), "\",\"phase\":\""), phase), "\",\"status\":\""), status), "\",\"detail\":\""), detail), "\"}");
    return hexa_append_file(bus_file, hexa_concat(line, "\n"));
}

long pad_str(const char* s, long width) {
    const char* padded = s;
    while ((hexa_str_len(padded) < width)) {
        padded = hexa_concat(padded, " ");
    }
    return padded;
}

long str_contains_any(const char* haystack, const char* needles) {
    long i = 0;
    while ((i < hexa_str_len(needles))) {
        if (hexa_contains(haystack, hexa_int_to_str((long)(needles[i])))) {
            return 1;
        }
        i = (i + 1);
    }
    return 0;
}

hexa_arr all_link_points() {
    return hexa_arr_lit((long[]){(long)(hexa_struct_alloc((long[]){(long)(1), (long)("blowup_to_lens"), (long)("blowup Phase 6.5 후 lens_forge 미호출"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(2), (long)("forge_to_registry"), (long)("forge_result.json → custom_lenses.jsonl 미등록"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(3), (long)("auto_register_sync"), (long)("sync 명령 stdout만 출력, 실행 안됨"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(4), (long)("gap_to_priority"), (long)("gap_finder 추천이 growth_bus에 미반영"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(5), (long)("alien_promote"), (long)("promote-pending dry-run only"), (long)(0)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(6), (long)("discovery_graph_empty"), (long)("discovery_graph.json 0 bytes"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(7), (long)("forge_result_orphan"), (long)("forge_result.json 미소비 (=#2 동일)"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(8), (long)("telescope_unlogged"), (long)("telescope 스캔 결과 stdout only"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(9), (long)("ouroboros_unlogged"), (long)("OUROBOROS 중간 상태 미저장"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(10), (long)("bt_unregistered"), (long)("BT candidate 미등록"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(11), (long)("projects_json_sync"), (long)("projects.json target/strategy 누락"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(12), (long)("growth_targets_stale"), (long)("GROWTH.md 목표치 < 실측치"), (long)(0)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(13), (long)("readme_missing"), (long)("README.md 미존재"), (long)(0)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(14), (long)("growth_bus_feedback"), (long)("growth_bus 피드백 루프 없음"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(15), (long)("lens_registry_split"), (long)("lens_registry.json vs custom_lenses.jsonl 분리"), (long)(1)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(16), (long)("grade_schema_mixed"), (long)("discovery_log grade 필드 emoji/string 혼합"), (long)(0)}, 4)), (long)(hexa_struct_alloc((long[]){(long)(17), (long)("timestamp_format_mixed"), (long)("timestamp 포맷 불일치"), (long)(0)}, 4))}, 17);
}

long check_blowup_to_lens() {
    const char* forge_path = hexa_concat(shared, "/forge_result.json");
    const char* blowup_log = hexa_concat(shared, "/blowup_last_ts.txt");
    long forge_mtime = file_mtime_epoch(forge_path);
    long blowup_mtime = file_mtime_epoch(discovery_log);
    long connected = ((forge_mtime > 0) && (forge_mtime >= blowup_mtime));
    const char* detail = ((connected) ? ("forge_result.json up-to-date") : (hexa_struct_alloc((long[]){(long)(1), (long)("blowup_to_lens"), (long)(connected), (long)(detail), (long)(1)}, 5)));
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
    shared = hexa_concat(dev, "/nexus/shared");
    bus_file = hexa_concat(shared, "/growth_bus.jsonl");
    discovery_log = hexa_concat(shared, "/discovery_log.jsonl");
    discovery_graph = hexa_concat(shared, "/discovery_graph.json");
    discovery_graph_bak = hexa_concat(shared, "/discovery_graph.json.bak2");
    custom_lenses = hexa_concat(shared, "/custom_lenses.jsonl");
    alien_dist = hexa_concat(shared, "/alien_index_distribution.json");
    projects_json = hexa_concat(shared, "/projects.json");
    growth_md = hexa_concat(dev, "/nexus/GROWTH.md");
    readme_md = hexa_concat(dev, "/nexus/README.md");
    forge_result = hexa_concat(shared, "/forge_result.json");
    scan_log = hexa_concat(shared, "/scan_log.jsonl");
    evolution_log = hexa_concat(shared, "/evolution_log.jsonl");
    return 0;
}
