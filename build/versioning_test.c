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

long get_next_version(const char* id, const char* branch);
long do_commit(const char* id, const char* content);
const char* get_history(const char* id);
const char* get_content(const char* id, long version);
const char* compute_diff(const char* id, long v1, long v2);
const char* create_version_branch(const char* id, const char* branch_name);
const char* merge_branches(const char* branch1, const char* branch2);
long hexa_user_main();

static const char* version_file = "shared/version_store.jsonl";

long get_next_version(const char* id, const char* branch) {
    const char* raw = hexa_read_file(version_file);
    if ((strcmp(raw, "") == 0)) {
        return 1;
    }
    hexa_arr lines = hexa_split(raw, "\n");
    long count = 0;
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if (hexa_contains(line, hexa_concat(hexa_concat("\"id\":\"", id), "\""))) {
            if (hexa_contains(line, hexa_concat(hexa_concat("\"branch\":\"", branch), "\""))) {
                count = (count + 1);
            }
        }
        i = (i + 1);
    }
    return (count + 1);
}

long do_commit(const char* id, const char* content) {
    long version = get_next_version(id, "main");
    const char* parent = "";
    if ((version > 1)) {
        parent = hexa_concat(hexa_concat(id, "@v"), hexa_int_to_str((long)((version - 1))));
    }
    const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"id\":\"", id), "\",\"version\":"), hexa_int_to_str((long)(version))), ",\"branch\":\"main\",\"content\":\""), hexa_replace(content, "\"", "'")), "\",\"parent\":\""), parent), "\"}");
    const char* raw = hexa_read_file(version_file);
    if ((strcmp(raw, "") == 0)) {
        hexa_write_file(version_file, hexa_concat(entry, "\n"));
    } else {
        hexa_write_file(version_file, hexa_concat(hexa_concat(raw, entry), "\n"));
    }
    return version;
}

const char* get_history(const char* id) {
    const char* raw = hexa_read_file(version_file);
    if ((strcmp(raw, "") == 0)) {
        return "(no versions)";
    }
    hexa_arr lines = hexa_split(raw, "\n");
    const char* result = "";
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if (hexa_contains(line, hexa_concat(hexa_concat("\"id\":\"", id), "\""))) {
            const char* ver = "";
            const char* vn = "\"version\":";
            if (hexa_contains(line, vn)) {
                hexa_arr vp = hexa_split(line, vn);
                if ((vp.n > 1)) {
                    const char* vs = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)vp.d[1]), ",")).d[0]), "}")).d[0]));
                    ver = vs;
                }
            }
            const char* branch = "";
            const char* bn = "\"branch\":\"";
            if (hexa_contains(line, bn)) {
                hexa_arr bp = hexa_split(line, bn);
                if ((bp.n > 1)) {
                    branch = ((const char*)(hexa_split(((const char*)bp.d[1]), "\"")).d[0]);
                }
            }
            const char* content_preview = "";
            const char* cn = "\"content\":\"";
            if (hexa_contains(line, cn)) {
                hexa_arr cp = hexa_split(line, cn);
                if ((cp.n > 1)) {
                    const char* full = ((const char*)(hexa_split(((const char*)cp.d[1]), "\"")).d[0]);
                    if ((hexa_str_len(full) > 40)) {
                        content_preview = hexa_concat(hexa_substr(full, 0, 40), "...");
                    } else {
                        content_preview = full;
                    }
                }
            }
            if ((strcmp(result, "") != 0)) {
                result = hexa_concat(result, "\n");
            }
            result = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(result, "  v"), ver), " ["), branch), "] "), content_preview);
        }
        i = (i + 1);
    }
    if ((strcmp(result, "") == 0)) {
        return hexa_concat(hexa_concat("(no versions for ", id), ")");
    }
    return result;
}

const char* get_content(const char* id, long version) {
    const char* raw = hexa_read_file(version_file);
    if ((strcmp(raw, "") == 0)) {
        return "";
    }
    hexa_arr lines = hexa_split(raw, "\n");
    const char* ver_str = hexa_int_to_str((long)(version));
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if (hexa_contains(line, hexa_concat(hexa_concat("\"id\":\"", id), "\""))) {
            if (hexa_contains(line, hexa_concat("\"version\":", ver_str))) {
                const char* cn = "\"content\":\"";
                if (hexa_contains(line, cn)) {
                    hexa_arr cp = hexa_split(line, cn);
                    if ((cp.n > 1)) {
                        return ((const char*)(hexa_split(((const char*)cp.d[1]), "\"")).d[0]);
                    }
                }
            }
        }
        i = (i + 1);
    }
    return "";
}

const char* compute_diff(const char* id, long v1, long v2) {
    const char* c1 = get_content(id, v1);
    const char* c2 = get_content(id, v2);
    if ((strcmp(c1, "") == 0)) {
        return hexa_concat(hexa_concat("(v", hexa_int_to_str((long)(v1))), " not found)");
    }
    if ((strcmp(c2, "") == 0)) {
        return hexa_concat(hexa_concat("(v", hexa_int_to_str((long)(v2))), " not found)");
    }
    hexa_arr lines1 = hexa_split(c1, "\\n");
    hexa_arr lines2 = hexa_split(c2, "\\n");
    const char* result = hexa_concat(hexa_concat(hexa_concat(hexa_concat("--- ", id), "@v"), hexa_int_to_str((long)(v1))), "\n");
    result = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(result, "+++ "), id), "@v"), hexa_int_to_str((long)(v2))), "\n");
    long max_len = (((lines1.n > lines2.n)) ? (lines1.n) : (lines2.n));
    long i = 0;
    while ((i < max_len)) {
        const char* l1 = (((i < lines1.n)) ? (((const char*)lines1.d[i])) : (""));
        const char* l2 = (((i < lines2.n)) ? (((const char*)lines2.d[i])) : (""));
        if ((strcmp(l1, l2) != 0)) {
            if ((strcmp(l1, "") != 0)) {
                result = hexa_concat(hexa_concat(hexa_concat(result, "- "), l1), "\n");
            }
            if ((strcmp(l2, "") != 0)) {
                result = hexa_concat(hexa_concat(hexa_concat(result, "+ "), l2), "\n");
            }
        } else {
            result = hexa_concat(hexa_concat(hexa_concat(result, "  "), l1), "\n");
        }
        i = (i + 1);
    }
    return result;
}

const char* create_version_branch(const char* id, const char* branch_name) {
    const char* raw = hexa_read_file(version_file);
    if ((strcmp(raw, "") == 0)) {
        return hexa_concat(hexa_concat("no versions found for '", id), "'");
    }
    hexa_arr lines = hexa_split(raw, "\n");
    const char* latest_content = "";
    long latest_version = 0;
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if (hexa_contains(line, hexa_concat(hexa_concat("\"id\":\"", id), "\""))) {
            if (hexa_contains(line, "\"branch\":\"main\"")) {
                const char* vn = "\"version\":";
                if (hexa_contains(line, vn)) {
                    hexa_arr vp = hexa_split(line, vn);
                    if ((vp.n > 1)) {
                        const char* vs = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)vp.d[1]), ",")).d[0]), "}")).d[0]));
                        long v = (long)(hexa_to_float(vs));
                        if ((v > latest_version)) {
                            latest_version = v;
                            const char* cn = "\"content\":\"";
                            if (hexa_contains(line, cn)) {
                                hexa_arr cp = hexa_split(line, cn);
                                if ((cp.n > 1)) {
                                    latest_content = ((const char*)(hexa_split(((const char*)cp.d[1]), "\"")).d[0]);
                                }
                            }
                        }
                    }
                }
            }
        }
        i = (i + 1);
    }
    if ((latest_version == 0)) {
        return hexa_concat(hexa_concat("no versions found for '", id), "'");
    }
    const char* parent = hexa_concat(hexa_concat(id, "@v"), hexa_int_to_str((long)(latest_version)));
    const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"id\":\"", id), "\",\"version\":"), hexa_int_to_str((long)(latest_version))), ",\"branch\":\""), branch_name), "\",\"content\":\""), latest_content), "\",\"parent\":\""), parent), "\"}");
    hexa_write_file(version_file, hexa_concat(hexa_concat(raw, entry), "\n"));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("branch '", branch_name), "' from "), id), "@v"), hexa_int_to_str((long)(latest_version)));
}

const char* merge_branches(const char* branch1, const char* branch2) {
    const char* raw = hexa_read_file(version_file);
    if ((strcmp(raw, "") == 0)) {
        return "cannot merge: no versions";
    }
    hexa_arr lines = hexa_split(raw, "\n");
    const char* b1_content = "";
    const char* b1_id = "";
    const char* b2_content = "";
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if (hexa_contains(line, hexa_concat(hexa_concat("\"branch\":\"", branch1), "\""))) {
            const char* cn = "\"content\":\"";
            if (hexa_contains(line, cn)) {
                hexa_arr cp = hexa_split(line, cn);
                if ((cp.n > 1)) {
                    b1_content = ((const char*)(hexa_split(((const char*)cp.d[1]), "\"")).d[0]);
                }
            }
            const char* idn = "\"id\":\"";
            if (hexa_contains(line, idn)) {
                hexa_arr ip = hexa_split(line, idn);
                if ((ip.n > 1)) {
                    b1_id = ((const char*)(hexa_split(((const char*)ip.d[1]), "\"")).d[0]);
                }
            }
        }
        if (hexa_contains(line, hexa_concat(hexa_concat("\"branch\":\"", branch2), "\""))) {
            const char* cn = "\"content\":\"";
            if (hexa_contains(line, cn)) {
                hexa_arr cp = hexa_split(line, cn);
                if ((cp.n > 1)) {
                    b2_content = ((const char*)(hexa_split(((const char*)cp.d[1]), "\"")).d[0]);
                }
            }
        }
        i = (i + 1);
    }
    if ((strcmp(b1_content, "") == 0)) {
        return hexa_concat(hexa_concat("cannot merge: missing branch '", branch1), "'");
    }
    if ((strcmp(b2_content, "") == 0)) {
        return hexa_concat(hexa_concat("cannot merge: missing branch '", branch2), "'");
    }
    const char* merged_content = hexa_concat(hexa_concat(b1_content, "\\n---merged---\\n"), b2_content);
    const char* id = b1_id;
    long version = get_next_version(id, "main");
    const char* parent = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("merge(", id), "@"), branch1), ","), id), "@"), branch2), ")");
    const char* entry = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"id\":\"", id), "\",\"version\":"), hexa_int_to_str((long)(version))), ",\"branch\":\"main\",\"content\":\""), merged_content), "\",\"parent\":\""), parent), "\"}");
    hexa_write_file(version_file, hexa_concat(hexa_concat(raw, entry), "\n"));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("merged ", branch1), " + "), branch2), " -> "), id), "@v"), hexa_int_to_str((long)(version)));
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "versioning.hexa — 버전 관리");
        printf("%s", "Usage:");
        printf("%s", "  commit <id> <content>            새 버전 커밋");
        printf("%s", "  history <id>                     버전 히스토리");
        printf("%s", "  diff <id> <v1> <v2>              줄 단위 diff");
        printf("%s", "  branch <id> <branch_name>        브랜치 생성");
        printf("%s", "  merge <branch1> <branch2>        병합");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "commit") == 0)) {
        if ((a.n < 4)) {
            printf("%s", "Usage: commit <id> <content>");
            return 0;
        }
        const char* id = ((const char*)a.d[2]);
        const char* content = "";
        long ci = 3;
        while ((ci < a.n)) {
            if ((strcmp(content, "") != 0)) {
                content = hexa_concat(content, " ");
            }
            content = hexa_concat(content, ((const char*)a.d[ci]));
            ci = (ci + 1);
        }
        long ver = do_commit(id, content);
        printf("%s", hexa_concat(hexa_concat(hexa_concat("[versioning] committed ", id), "@v"), hexa_int_to_str((long)(ver))));
    }
    if ((strcmp(cmd, "history") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: history <id>");
            return 0;
        }
        const char* result = get_history(((const char*)a.d[2]));
        printf("%s", hexa_concat(hexa_concat("[versioning] history for ", ((const char*)a.d[2])), ":"));
        printf("%s", result);
    }
    if ((strcmp(cmd, "diff") == 0)) {
        if ((a.n < 5)) {
            printf("%s", "Usage: diff <id> <v1> <v2>");
            return 0;
        }
        long v1 = 1;
        long v2 = 2;
        v1 = (long)(hexa_to_float(((const char*)a.d[3])));
        v2 = (long)(hexa_to_float(((const char*)a.d[4])));
        const char* result = compute_diff(((const char*)a.d[2]), v1, v2);
        printf("%s", result);
    }
    if ((strcmp(cmd, "branch") == 0)) {
        if ((a.n < 4)) {
            printf("%s", "Usage: branch <id> <branch_name>");
            return 0;
        }
        const char* result = create_version_branch(((const char*)a.d[2]), ((const char*)a.d[3]));
        printf("%s", hexa_concat("[versioning] ", result));
    }
    if ((strcmp(cmd, "merge") == 0)) {
        if ((a.n < 4)) {
            printf("%s", "Usage: merge <branch1> <branch2>");
            return 0;
        }
        const char* result = merge_branches(((const char*)a.d[2]), ((const char*)a.d[3]));
        return printf("%s", hexa_concat("[versioning] ", result));
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    return 0;
}
