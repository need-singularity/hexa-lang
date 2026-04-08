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

const char* next_snapshot_id();
const char* save_snapshot(const char* description);
const char* list_snapshots();
const char* get_snapshot(const char* snap_id);
const char* rollback_snapshot(const char* snap_id);
const char* create_branch(const char* snap_id, const char* branch_name, const char* reason);
const char* list_branches();
long hexa_user_main();

static const char* snapshot_dir = "shared/snapshots";
static const char* snapshot_index = "shared/snapshots/index.jsonl";
static const char* branch_index = "shared/snapshots/branches.jsonl";

const char* next_snapshot_id() {
    const char* raw = hexa_read_file(snapshot_index);
    if ((strcmp(raw, "") == 0)) {
        return "snap-0001";
    }
    hexa_arr lines = hexa_split(raw, "\n");
    long count = 0;
    long i = 0;
    while ((i < lines.n)) {
        if ((strcmp(hexa_trim(((const char*)lines.d[i])), "") != 0)) {
            count = (count + 1);
        }
        i = (i + 1);
    }
    long num = (count + 1);
    const char* id = "snap-";
    if ((num < 10)) {
        id = hexa_concat(hexa_concat(id, "000"), hexa_int_to_str((long)(num)));
    } else {
        if ((num < 100)) {
            id = hexa_concat(hexa_concat(id, "00"), hexa_int_to_str((long)(num)));
        } else {
            if ((num < 1000)) {
                id = hexa_concat(hexa_concat(id, "0"), hexa_int_to_str((long)(num)));
            } else {
                id = hexa_concat(id, hexa_int_to_str((long)(num)));
            }
        }
    }
    return id;
}

const char* save_snapshot(const char* description) {
    const char* id = next_snapshot_id();
    const char* graph_file = hexa_concat(hexa_concat(hexa_concat(snapshot_dir, "/"), id), ".json");
    const char* graph_data = hexa_read_file("shared/discovery_graph.json");
    if ((strcmp(graph_data, "") != 0)) {
        hexa_write_file(graph_file, graph_data);
    } else {
        hexa_write_file(graph_file, "{\"nodes\":[],\"edges\":[]}");
    }
    const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"id\":\"", id), "\",\"description\":\""), description), "\",\"graph_file\":\""), graph_file), "\"}");
    const char* raw = hexa_read_file(snapshot_index);
    if ((strcmp(raw, "") == 0)) {
        hexa_write_file(snapshot_index, hexa_concat(entry, "\n"));
    } else {
        hexa_write_file(snapshot_index, hexa_concat(hexa_concat(raw, entry), "\n"));
    }
    return id;
}

const char* list_snapshots() {
    const char* raw = hexa_read_file(snapshot_index);
    if ((strcmp(raw, "") == 0)) {
        return "(no snapshots)";
    }
    hexa_arr lines = hexa_split(raw, "\n");
    const char* result = "";
    long i = (lines.n - 1);
    while ((i >= 0)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") != 0)) {
            const char* id = "";
            const char* id_n = "\"id\":\"";
            if (hexa_contains(line, id_n)) {
                hexa_arr ip = hexa_split(line, id_n);
                if ((ip.n > 1)) {
                    id = ((const char*)(hexa_split(((const char*)ip.d[1]), "\"")).d[0]);
                }
            }
            const char* desc = "";
            const char* dn = "\"description\":\"";
            if (hexa_contains(line, dn)) {
                hexa_arr dp = hexa_split(line, dn);
                if ((dp.n > 1)) {
                    desc = ((const char*)(hexa_split(((const char*)dp.d[1]), "\"")).d[0]);
                }
            }
            if ((strcmp(result, "") != 0)) {
                result = hexa_concat(result, "\n");
            }
            result = hexa_concat(hexa_concat(hexa_concat(hexa_concat(result, "  "), id), "  "), desc);
        }
        i = (i - 1);
    }
    return result;
}

const char* get_snapshot(const char* snap_id) {
    const char* raw = hexa_read_file(snapshot_index);
    if ((strcmp(raw, "") == 0)) {
        return "";
    }
    hexa_arr lines = hexa_split(raw, "\n");
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if (hexa_contains(line, hexa_concat(hexa_concat("\"id\":\"", snap_id), "\""))) {
            return line;
        }
        i = (i + 1);
    }
    return "";
}

const char* rollback_snapshot(const char* snap_id) {
    const char* entry = get_snapshot(snap_id);
    if ((strcmp(entry, "") == 0)) {
        return hexa_concat("snapshot not found: ", snap_id);
    }
    const char* gn = "\"graph_file\":\"";
    if ((!hexa_contains(entry, gn))) {
        return "no graph_file in snapshot";
    }
    hexa_arr gp = hexa_split(entry, gn);
    if ((gp.n < 2)) {
        return "parse error";
    }
    const char* graph_file = ((const char*)(hexa_split(((const char*)gp.d[1]), "\"")).d[0]);
    const char* graph_data = hexa_read_file(graph_file);
    if ((strcmp(graph_data, "") == 0)) {
        return hexa_concat("graph file not found: ", graph_file);
    }
    hexa_write_file("shared/discovery_graph.json", graph_data);
    return hexa_concat("rolled back to ", snap_id);
}

const char* create_branch(const char* snap_id, const char* branch_name, const char* reason) {
    const char* entry = get_snapshot(snap_id);
    if ((strcmp(entry, "") == 0)) {
        return hexa_concat("snapshot not found: ", snap_id);
    }
    const char* gn = "\"graph_file\":\"";
    if ((!hexa_contains(entry, gn))) {
        return "no graph_file in snapshot";
    }
    hexa_arr gp = hexa_split(entry, gn);
    if ((gp.n < 2)) {
        return "parse error";
    }
    const char* src_graph = ((const char*)(hexa_split(((const char*)gp.d[1]), "\"")).d[0]);
    const char* new_id = hexa_concat(hexa_concat(snap_id, "-branch-"), branch_name);
    const char* new_graph_file = hexa_concat(hexa_concat(hexa_concat(snapshot_dir, "/"), new_id), ".json");
    const char* graph_data = hexa_read_file(src_graph);
    if ((strcmp(graph_data, "") != 0)) {
        hexa_write_file(new_graph_file, graph_data);
    }
    const char* snap_entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"id\":\"", new_id), "\",\"description\":\"Branch '"), branch_name), "' from "), snap_id), "\",\"graph_file\":\""), new_graph_file), "\"}");
    const char* raw = hexa_read_file(snapshot_index);
    hexa_write_file(snapshot_index, hexa_concat(hexa_concat(raw, snap_entry), "\n"));
    const char* branch_entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"name\":\"", branch_name), "\",\"parent\":\""), snap_id), "\",\"head\":\""), new_id), "\",\"reason\":\""), reason), "\"}");
    const char* braw = hexa_read_file(branch_index);
    if ((strcmp(braw, "") == 0)) {
        hexa_write_file(branch_index, hexa_concat(branch_entry, "\n"));
    } else {
        hexa_write_file(branch_index, hexa_concat(hexa_concat(braw, branch_entry), "\n"));
    }
    return new_id;
}

const char* list_branches() {
    const char* raw = hexa_read_file(branch_index);
    if ((strcmp(raw, "") == 0)) {
        return "(no branches)";
    }
    hexa_arr lines = hexa_split(raw, "\n");
    const char* result = "";
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") == 0)) {
            i = (i + 1);
            continue;
        }
        const char* name = "";
        const char* nn = "\"name\":\"";
        if (hexa_contains(line, nn)) {
            hexa_arr np = hexa_split(line, nn);
            if ((np.n > 1)) {
                name = ((const char*)(hexa_split(((const char*)np.d[1]), "\"")).d[0]);
            }
        }
        const char* parent = "";
        const char* pn = "\"parent\":\"";
        if (hexa_contains(line, pn)) {
            hexa_arr pp = hexa_split(line, pn);
            if ((pp.n > 1)) {
                parent = ((const char*)(hexa_split(((const char*)pp.d[1]), "\"")).d[0]);
            }
        }
        const char* reason = "";
        const char* rn = "\"reason\":\"";
        if (hexa_contains(line, rn)) {
            hexa_arr rp = hexa_split(line, rn);
            if ((rp.n > 1)) {
                reason = ((const char*)(hexa_split(((const char*)rp.d[1]), "\"")).d[0]);
            }
        }
        if ((strcmp(result, "") != 0)) {
            result = hexa_concat(result, "\n");
        }
        result = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(result, "  "), name), "  (from "), parent), ")  "), reason);
        i = (i + 1);
    }
    return result;
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "time_travel.hexa — 스냅샷/롤백/브랜치");
        printf("%s", "Usage:");
        printf("%s", "  save <description>                   스냅샷 저장");
        printf("%s", "  list                                 스냅샷 목록 (최신순)");
        printf("%s", "  get <snap_id>                        스냅샷 조회");
        printf("%s", "  rollback <snap_id>                   상태 롤백");
        printf("%s", "  branch <snap_id> <name> <reason>     브랜치 생성");
        printf("%s", "  branches                             브랜치 목록");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "save") == 0)) {
        const char* desc = "manual snapshot";
        if ((a.n > 2)) {
            desc = "";
            long di = 2;
            while ((di < a.n)) {
                if ((strcmp(desc, "") != 0)) {
                    desc = hexa_concat(desc, " ");
                }
                desc = hexa_concat(desc, ((const char*)a.d[di]));
                di = (di + 1);
            }
        }
        const char* id = save_snapshot(desc);
        printf("%s", hexa_concat(hexa_concat(hexa_concat("[time_travel] saved ", id), ": "), desc));
    }
    if ((strcmp(cmd, "list") == 0)) {
        const char* result = list_snapshots();
        printf("%s", "[time_travel] snapshots (newest first):");
        printf("%s", result);
    }
    if ((strcmp(cmd, "get") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: get <snap_id>");
            return 0;
        }
        const char* entry = get_snapshot(((const char*)a.d[2]));
        if ((strcmp(entry, "") == 0)) {
            printf("%s", hexa_concat("[time_travel] not found: ", ((const char*)a.d[2])));
        } else {
            printf("%s", hexa_concat("[time_travel] ", entry));
        }
    }
    if ((strcmp(cmd, "rollback") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: rollback <snap_id>");
            return 0;
        }
        const char* result = rollback_snapshot(((const char*)a.d[2]));
        printf("%s", hexa_concat("[time_travel] ", result));
    }
    if ((strcmp(cmd, "branch") == 0)) {
        if ((a.n < 5)) {
            printf("%s", "Usage: branch <snap_id> <name> <reason>");
            return 0;
        }
        const char* reason = "";
        long ri = 4;
        while ((ri < a.n)) {
            if ((strcmp(reason, "") != 0)) {
                reason = hexa_concat(reason, " ");
            }
            reason = hexa_concat(reason, ((const char*)a.d[ri]));
            ri = (ri + 1);
        }
        const char* new_id = create_branch(((const char*)a.d[2]), ((const char*)a.d[3]), reason);
        printf("%s", hexa_concat("[time_travel] branch created: ", new_id));
    }
    if ((strcmp(cmd, "branches") == 0)) {
        const char* result = list_branches();
        printf("%s", "[time_travel] branches:");
        return printf("%s", result);
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    return 0;
}
