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

long* worker_new(const char* id, const char* address, const char* status);
long is_available(long *w);
const char* load_workers();
long save_workers(const char* json);
long parse_workers(const char* raw);
const char* extract_worker_field(const char* raw, long idx, const char* field);
const char* distribute_domains(const char* worker_ids, const char* domain_list);
const char* merge_results(const char* results_json);
const char* worker_to_json(const char* id, const char* address, const char* status);
long add_worker_to_file(const char* id, const char* address, const char* status);
const char* get_available_ids(const char* raw);
long hexa_user_main();

static const char* workers_file = "shared/distributed_workers.json";
static const char* results_file = "shared/distributed_results.json";

long* worker_new(const char* id, const char* address, const char* status) {
    return hexa_struct_alloc((long[]){(long)(id), (long)(address), (long)(status), (long)("")}, 4);
}

long is_available(long *w) {
    return (w[2] != "offline");
}

const char* load_workers() {
    const char* raw = hexa_read_file(workers_file);
    if ((strcmp(raw, "") == 0)) {
        return "[]";
    }
    return raw;
}

long save_workers(const char* json) {
    return hexa_write_file(workers_file, json);
}

long parse_workers(const char* raw) {
    hexa_arr lines = hexa_split(raw, "\n");
    long count = 0;
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if (hexa_contains(line, "\"id\"")) {
            count = (count + 1);
        }
        i = (i + 1);
    }
    return count;
}

const char* extract_worker_field(const char* raw, long idx, const char* field) {
    const char* needle = hexa_concat(hexa_concat("\"", field), "\":");
    hexa_arr parts = hexa_split(raw, needle);
    if ((parts.n <= (idx + 1))) {
        return "";
    }
    const char* after = hexa_trim(((const char*)parts.d[(idx + 1)]));
    if (hexa_starts_with(after, "\"")) {
        const char* inner = hexa_substr(after, 1, hexa_str_len(after));
        long end_q = hexa_index_of(inner, "\"");
        if ((end_q < 0)) {
            return "";
        }
        return hexa_substr(inner, 0, end_q);
    }
    long end = hexa_index_of(after, ",");
    if ((end < 0)) {
        long end2 = hexa_index_of(after, "}");
        if ((end2 < 0)) {
            return "";
        }
        return hexa_trim(hexa_substr(after, 0, end2));
    }
    return hexa_trim(hexa_substr(after, 0, end));
}

const char* distribute_domains(const char* worker_ids, const char* domain_list) {
    hexa_arr wids = hexa_split(worker_ids, ",");
    hexa_arr doms = hexa_split(domain_list, ",");
    long n = wids.n;
    if ((n == 0)) {
        return "{}";
    }
    const char* result = "[";
    hexa_arr buckets = hexa_arr_new();
    long bi = 0;
    while ((bi < n)) {
        buckets = hexa_arr_push(buckets, (long)(""));
        bi = (bi + 1);
    }
    long di = 0;
    while ((di < doms.n)) {
        long slot = (di % n);
        const char* cur = ((const char*)buckets.d[slot]);
        if ((strcmp(cur, "") == 0)) {
            buckets.d[slot] = (long)(hexa_trim(((const char*)doms.d[di])));
        } else {
            buckets.d[slot] = (long)(hexa_concat(hexa_concat(cur, "
            "), hexa_trim(((const char*)doms.d[di]))));
        }
        di = (di + 1);
    }
    long first = 1;
    long wi = 0;
    while ((wi < n)) {
        const char* b = ((const char*)buckets.d[wi]);
        if ((strcmp(b, "") != 0)) {
            if ((!first)) {
                result = hexa_concat(result, ",");
            }
            result = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(result, "{\"worker\":\""), hexa_trim(((const char*)wids.d[wi]))), "\",\"domains\":\""), b), "\"}");
            first = 0;
        }
        wi = (wi + 1);
    }
    result = hexa_concat(result, "]");
    return result;
}

const char* merge_results(const char* results_json) {
    hexa_arr parts = hexa_split(results_json, "\"results\":\"");
    const char* merged = "";
    long i = 1;
    while ((i < parts.n)) {
        const char* after = ((const char*)parts.d[i]);
        long end_q = hexa_index_of(after, "\"");
        if ((end_q > 0)) {
            const char* val = hexa_substr(after, 0, end_q);
            if ((strcmp(merged, "") == 0)) {
                merged = val;
            } else {
                merged = hexa_concat(hexa_concat(merged, "
                "), val);
            }
        }
        i = (i + 1);
    }
    return merged;
}

const char* worker_to_json(const char* id, const char* address, const char* status) {
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"id\":\"", id), "\",\"address\":\""), address), "\",\"status\":\""), status), "\"}");
}

long add_worker_to_file(const char* id, const char* address, const char* status) {
    const char* raw = hexa_read_file(workers_file);
    const char* entry = worker_to_json(id, address, status);
    if ((strcmp(raw, "") == 0)) {
        return hexa_write_file(workers_file, hexa_concat(hexa_concat("[", entry), "]"));
    } else {
        const char* trimmed = hexa_trim(raw);
        if ((strcmp(trimmed, "[]") == 0)) {
            return hexa_write_file(workers_file, hexa_concat(hexa_concat("[", entry), "]"));
        } else {
            const char* without_bracket = hexa_substr(trimmed, 0, (hexa_str_len(trimmed) - 1));
            return hexa_write_file(workers_file, hexa_concat(hexa_concat(hexa_concat(without_bracket, ","), entry), "]"));
        }
    }
}

const char* get_available_ids(const char* raw) {
    hexa_arr lines = hexa_split(raw, "\n");
    const char* ids = "";
    const char* cur_id = "";
    const char* cur_status = "";
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if (hexa_contains(line, "\"id\"")) {
            hexa_arr p = hexa_split(line, "\"id\":");
            if ((p.n > 1)) {
                const char* v = hexa_replace(hexa_replace(hexa_trim(((const char*)p.d[1])), "\"", ""), ",", "");
                cur_id = v;
            }
        }
        if (hexa_contains(line, "\"status\"")) {
            hexa_arr p = hexa_split(line, "\"status\":");
            if ((p.n > 1)) {
                const char* v = hexa_replace(hexa_replace(hexa_replace(hexa_trim(((const char*)p.d[1])), "\"", ""), ",", ""), "}", "");
                cur_status = v;
                if ((strcmp(cur_status, "offline") != 0)) {
                    if ((strcmp(ids, "") == 0)) {
                        ids = cur_id;
                    } else {
                        ids = hexa_concat(hexa_concat(ids, ","), cur_id);
                    }
                }
                cur_id = "";
                cur_status = "";
            }
        }
        i = (i + 1);
    }
    return ids;
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "distributed.hexa — 분산 탐색 스케줄러");
        printf("%s", "Usage:");
        printf("%s", "  add <id> <address> [idle|busy|offline]");
        printf("%s", "  distribute <domain1> <domain2> ...");
        printf("%s", "  status");
        printf("%s", "  merge");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "add") == 0)) {
        if ((a.n < 4)) {
            printf("%s", "Usage: add <id> <address> [status=idle]");
            return 0;
        }
        const char* id = ((const char*)a.d[2]);
        const char* address = ((const char*)a.d[3]);
        const char* status = "idle";
        if ((a.n > 4)) {
            status = ((const char*)a.d[4]);
        }
        add_worker_to_file(id, address, status);
        printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[distributed] added worker ", id), " ("), address), ") status="), status));
    }
    if ((strcmp(cmd, "status") == 0)) {
        const char* raw = load_workers();
        long count = parse_workers(raw);
        const char* avail_ids = get_available_ids(raw);
        long avail_count = (((strcmp(avail_ids, "") == 0)) ? (0) : ((hexa_split(avail_ids, ",")).n));
        printf("%s", "=== distributed scheduler status ===");
        printf("%s", hexa_concat("  total workers : ", hexa_int_to_str((long)(count))));
        printf("%s", hexa_concat("  available     : ", hexa_int_to_str((long)(avail_count))));
        if ((strcmp(avail_ids, "") != 0)) {
            printf("%s", hexa_concat("  available ids : ", avail_ids));
        }
    }
    if ((strcmp(cmd, "distribute") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: distribute <domain1> <domain2> ...");
            return 0;
        }
        const char* domains = "";
        long di = 2;
        while ((di < a.n)) {
            if ((strcmp(domains, "") == 0)) {
                domains = ((const char*)a.d[di]);
            } else {
                domains = hexa_concat(hexa_concat(domains, ","), ((const char*)a.d[di]));
            }
            di = (di + 1);
        }
        const char* raw = load_workers();
        const char* avail_ids = get_available_ids(raw);
        if ((strcmp(avail_ids, "") == 0)) {
            printf("%s", "[distributed] no available workers");
            return 0;
        }
        const char* plan = distribute_domains(avail_ids, domains);
        hexa_write_file(results_file, plan);
        printf("%s", "[distributed] plan:");
        printf("%s", plan);
    }
    if ((strcmp(cmd, "merge") == 0)) {
        const char* raw = hexa_read_file(results_file);
        if ((strcmp(raw, "") == 0)) {
            printf("%s", "[distributed] no results to merge");
            return 0;
        }
        const char* merged = merge_results(raw);
        return printf("%s", hexa_concat("[distributed] merged results: ", merged));
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    return 0;
}
