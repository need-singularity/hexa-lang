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

const char* plugin_type_from_str(const char* s);
const char* parse_manifest(const char* content);
const char* load_registry();
long save_registry(const char* json);
const char* plugin_to_json(const char* name, const char* version, const char* ptype, const char* entry_point);
long add_plugin_to_registry(const char* name, const char* version, const char* ptype, const char* entry_point);
const char* find_plugin(const char* raw, const char* name);
const char* validate_registry(const char* raw);
long count_by_type(const char* raw, const char* ptype);
const char* make_summary(const char* raw);
long hexa_user_main();

static const char* default_plugin_dir = "shared/plugins";
static const char* registry_file = "shared/plugin_registry.json";

const char* plugin_type_from_str(const char* s) {
    long lower = /* unsupported method .lower */ 0;
    if ((lower == "lens")) {
        return "lens";
    }
    if ((lower == "experiment")) {
        return "experiment";
    }
    if ((lower == "analyzer")) {
        return "analyzer";
    }
    if ((lower == "transformer")) {
        return "transformer";
    }
    return "unknown";
}

const char* parse_manifest(const char* content) {
    hexa_arr lines = hexa_split(content, "\n");
    const char* name = "";
    const char* version = "";
    const char* ptype = "";
    const char* entry_point = "";
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        i = (i + 1);
        if ((strcmp(line, "") == 0)) {
            continue;
        }
        if (hexa_starts_with(line, "#")) {
            continue;
        }
        if (hexa_starts_with(line, "[")) {
            continue;
        }
        long eq_pos = hexa_index_of(line, "=");
        if ((eq_pos < 0)) {
            continue;
        }
        const char* key = hexa_trim(hexa_substr(line, 0, eq_pos));
        const char* val = hexa_trim(hexa_substr(line, (eq_pos + 1), hexa_str_len(line)));
        val = hexa_replace(hexa_replace(val, "\"", ""), "'", "");
        if ((strcmp(key, "name") == 0)) {
            name = val;
        }
        if ((strcmp(key, "version") == 0)) {
            version = val;
        }
        if ((strcmp(key, "type") == 0)) {
            ptype = plugin_type_from_str(val);
        }
        if ((strcmp(key, "entry_point") == 0)) {
            entry_point = val;
        }
    }
    if ((strcmp(name, "") == 0)) {
        return "";
    }
    if ((strcmp(ptype, "unknown") == 0)) {
        return "";
    }
    if ((strcmp(entry_point, "") == 0)) {
        return "";
    }
    if ((strcmp(version, "") == 0)) {
        version = "0.0.0";
    }
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(name, "|"), version), "|"), ptype), "|"), entry_point);
}

const char* load_registry() {
    const char* raw = hexa_read_file(registry_file);
    if ((strcmp(raw, "") == 0)) {
        return "[]";
    }
    return raw;
}

long save_registry(const char* json) {
    return hexa_write_file(registry_file, json);
}

const char* plugin_to_json(const char* name, const char* version, const char* ptype, const char* entry_point) {
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"name\":\"", name), "\",\"version\":\""), version), "\",\"type\":\""), ptype), "\",\"entry_point\":\""), entry_point), "\"}");
}

long add_plugin_to_registry(const char* name, const char* version, const char* ptype, const char* entry_point) {
    const char* raw = hexa_read_file(registry_file);
    const char* entry = plugin_to_json(name, version, ptype, entry_point);
    if ((strcmp(raw, "") == 0)) {
        hexa_write_file(registry_file, hexa_concat(hexa_concat("[", entry), "]"));
        return 0;
    }
    const char* trimmed = hexa_trim(raw);
    if ((strcmp(trimmed, "[]") == 0)) {
        hexa_write_file(registry_file, hexa_concat(hexa_concat("[", entry), "]"));
        return 0;
    }
    if (hexa_contains(trimmed, hexa_concat(hexa_concat("\"name\":\"", name), "\""))) {
        const char* new_json = "[";
        hexa_arr entries = hexa_split(trimmed, "},{");
        long first = 1;
        long ei = 0;
        while ((ei < entries.n)) {
            const char* e = ((const char*)entries.d[ei]);
            if ((!hexa_contains(e, hexa_concat(hexa_concat("\"name\":\"", name), "\"")))) {
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
        hexa_write_file(registry_file, new_json);
        return 0;
    }
    const char* without_bracket = hexa_substr(trimmed, 0, (hexa_str_len(trimmed) - 1));
    return hexa_write_file(registry_file, hexa_concat(hexa_concat(hexa_concat(without_bracket, ","), entry), "]"));
}

const char* find_plugin(const char* raw, const char* name) {
    const char* needle = hexa_concat(hexa_concat("\"name\":\"", name), "\"");
    if ((!hexa_contains(raw, needle))) {
        return "";
    }
    hexa_arr after = hexa_split(raw, needle);
    if ((after.n < 2)) {
        return "";
    }
    const char* block = ((const char*)(hexa_split(((const char*)after.d[1]), "}")).d[0]);
    const char* version = "";
    const char* ptype = "";
    const char* entry_point = "";
    const char* vn = "\"version\":\"";
    if (hexa_contains(block, vn)) {
        hexa_arr vp = hexa_split(block, vn);
        if ((vp.n > 1)) {
            version = ((const char*)(hexa_split(((const char*)vp.d[1]), "\"")).d[0]);
        }
    }
    const char* tn = "\"type\":\"";
    if (hexa_contains(block, tn)) {
        hexa_arr tp = hexa_split(block, tn);
        if ((tp.n > 1)) {
            ptype = ((const char*)(hexa_split(((const char*)tp.d[1]), "\"")).d[0]);
        }
    }
    const char* en = "\"entry_point\":\"";
    if (hexa_contains(block, en)) {
        hexa_arr ep = hexa_split(block, en);
        if ((ep.n > 1)) {
            entry_point = ((const char*)(hexa_split(((const char*)ep.d[1]), "\"")).d[0]);
        }
    }
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(name, "|"), version), "|"), ptype), "|"), entry_point);
}

const char* validate_registry(const char* raw) {
    const char* errors = "";
    hexa_arr entries = hexa_split(raw, "\"name\":\"");
    long i = 1;
    while ((i < entries.n)) {
        const char* name = ((const char*)(hexa_split(((const char*)entries.d[i]), "\"")).d[0]);
        const char* block = ((const char*)(hexa_split(((const char*)entries.d[i]), "}")).d[0]);
        const char* vn = "\"version\":\"";
        if (hexa_contains(block, vn)) {
            hexa_arr vp = hexa_split(block, vn);
            if ((vp.n > 1)) {
                const char* ver = ((const char*)(hexa_split(((const char*)vp.d[1]), "\"")).d[0]);
                if ((strcmp(ver, "") == 0)) {
                    errors = hexa_concat(hexa_concat(errors, name), ": empty version\n");
                }
            }
        }
        const char* en = "\"entry_point\":\"";
        if (hexa_contains(block, en)) {
            hexa_arr ep = hexa_split(block, en);
            if ((ep.n > 1)) {
                const char* ept = ((const char*)(hexa_split(((const char*)ep.d[1]), "\"")).d[0]);
                if ((strcmp(ept, "") == 0)) {
                    errors = hexa_concat(hexa_concat(errors, name), ": empty entry_point\n");
                }
            }
        }
        i = (i + 1);
    }
    return errors;
}

long count_by_type(const char* raw, const char* ptype) {
    const char* needle = hexa_concat(hexa_concat("\"type\":\"", ptype), "\"");
    hexa_arr parts = hexa_split(raw, needle);
    return (parts.n - 1);
}

const char* make_summary(const char* raw) {
    hexa_arr total_parts = hexa_split(raw, "\"name\":");
    long total = (total_parts.n - 1);
    long lens = count_by_type(raw, "lens");
    long exp = count_by_type(raw, "experiment");
    long ana = count_by_type(raw, "analyzer");
    long tfm = count_by_type(raw, "transformer");
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(total)), " plugins ("), hexa_int_to_str((long)(lens))), " lens, "), hexa_int_to_str((long)(exp))), " experiment, "), hexa_int_to_str((long)(ana))), " analyzer, "), hexa_int_to_str((long)(tfm))), " transformer)");
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "plugin.hexa — 플러그인 로더/레지스트리");
        printf("%s", "Usage:");
        printf("%s", "  scan <dir>         디렉토리 .toml 매니페스트 스캔");
        printf("%s", "  register <name> <version> <type> <entry_point>");
        printf("%s", "  list               전체 플러그인 목록");
        printf("%s", "  load <name>        이름으로 플러그인 조회");
        printf("%s", "  validate           유효성 검증");
        printf("%s", "  summary            타입별 요약");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "register") == 0)) {
        if ((a.n < 6)) {
            printf("%s", "Usage: register <name> <version> <type> <entry_point>");
            return 0;
        }
        const char* name = ((const char*)a.d[2]);
        const char* version = ((const char*)a.d[3]);
        const char* ptype = plugin_type_from_str(((const char*)a.d[4]));
        const char* entry_point = ((const char*)a.d[5]);
        if ((strcmp(ptype, "unknown") == 0)) {
            printf("%s", hexa_concat(hexa_concat("[plugin] unknown type: ", ((const char*)a.d[4])), " (lens/experiment/analyzer/transformer)"));
            return 0;
        }
        add_plugin_to_registry(name, version, ptype, entry_point);
        printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[plugin] registered ", name), " v"), version), " ("), ptype), ")"));
    }
    if ((strcmp(cmd, "scan") == 0)) {
        const char* dir = (((a.n > 2)) ? (((const char*)a.d[2])) : (default_plugin_dir));
        printf("%s", hexa_concat(hexa_concat("[plugin] scanning ", dir), " for .toml manifests ..."));
        printf("%s", "[plugin] (manual scan: use 'register' for each plugin)");
    }
    if ((strcmp(cmd, "list") == 0)) {
        const char* raw = load_registry();
        hexa_arr entries = hexa_split(raw, "\"name\":\"");
        long total = (entries.n - 1);
        if ((total == 0)) {
            printf("%s", "[plugin] no plugins registered");
            return 0;
        }
        printf("%s", hexa_concat(hexa_concat("[plugin] ", hexa_int_to_str((long)(total))), " plugins:"));
        long i = 1;
        while ((i < entries.n)) {
            const char* name = ((const char*)(hexa_split(((const char*)entries.d[i]), "\"")).d[0]);
            const char* block = ((const char*)(hexa_split(((const char*)entries.d[i]), "}")).d[0]);
            const char* ptype = "";
            const char* tn = "\"type\":\"";
            if (hexa_contains(block, tn)) {
                hexa_arr tp = hexa_split(block, tn);
                if ((tp.n > 1)) {
                    ptype = ((const char*)(hexa_split(((const char*)tp.d[1]), "\"")).d[0]);
                }
            }
            const char* version = "";
            const char* vn = "\"version\":\"";
            if (hexa_contains(block, vn)) {
                hexa_arr vp = hexa_split(block, vn);
                if ((vp.n > 1)) {
                    version = ((const char*)(hexa_split(((const char*)vp.d[1]), "\"")).d[0]);
                }
            }
            printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  ", name), " v"), version), " ["), ptype), "]"));
            i = (i + 1);
        }
    }
    if ((strcmp(cmd, "load") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: load <name>");
            return 0;
        }
        const char* raw = load_registry();
        const char* result = find_plugin(raw, ((const char*)a.d[2]));
        if ((strcmp(result, "") == 0)) {
            printf("%s", hexa_concat("[plugin] not found: ", ((const char*)a.d[2])));
            return 0;
        }
        hexa_arr parts = hexa_split(result, "|");
        printf("%s", hexa_concat("[plugin] ", ((const char*)parts.d[0])));
        printf("%s", hexa_concat("  version     : ", ((const char*)parts.d[1])));
        printf("%s", hexa_concat("  type        : ", ((const char*)parts.d[2])));
        printf("%s", hexa_concat("  entry_point : ", ((const char*)parts.d[3])));
    }
    if ((strcmp(cmd, "validate") == 0)) {
        const char* raw = load_registry();
        const char* errors = validate_registry(raw);
        if ((strcmp(errors, "") == 0)) {
            printf("%s", "[plugin] all plugins valid");
        } else {
            printf("%s", "[plugin] validation errors:");
            printf("%s", errors);
        }
    }
    if ((strcmp(cmd, "summary") == 0)) {
        const char* raw = load_registry();
        const char* summary = make_summary(raw);
        return printf("%s", hexa_concat("[plugin] ", summary));
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    return 0;
}
