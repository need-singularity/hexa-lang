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

long extract_json_int(const char* content, const char* key);
const char* extract_json_str(const char* content, const char* key);
double extract_json_float(const char* content, const char* key);
long* parse_run(const char* content, const char* run_key);
const char* display_name(const char* key);
const char* pad(const char* s, long width);
const char* status_icon(const char* s);

static hexa_arr a;
static const char* home;
static const char* anima_dir;
static const char* config_dir;
static const char* today;
static const char* tr_path;
static const char* tr_raw = "";
static const char* gl_path;
static const char* gl_raw = "";
static const char* pod_status = "확인 필요 (runpodctl pod list)";
static long pod_count = 0;
static const char* rp_path;
static const char* rp_raw = "";
static const char* rp_exists;
static long cycle;
static long harvested;
static long applied;
static long self_disc;
static const char* complete_keys = "v14.0|v14.1|animalm_14b_v01|animalm_14b_v02|animalm_14b_v03|animalm_14b_v04";
static const char* progress_keys = "v14.3_128c|v3_274M|animalm_72b_v05|animalm_7b_fresh";
static const char* planned_keys = "animalm_14b_v05|animalm_32b_v01";
static hexa_arr ck;
static hexa_arr pk;
static hexa_arr plk;
static long ci = 0;
static long pi = 0;
static long pli = 0;
static long npi = 0;
static const char* evo_stage = "?";
static hexa_arr actions;
static hexa_arr action_cmds;
static long action_count = 0;
static long pi2 = 0;
static long ai2 = 0;
static const char* mode;
static long act_num;
static const char* nexus_dir;
static const char* growth_bus;
static const char* ts = "?";
static const char* bus;

long extract_json_int(const char* content, const char* key) {
    const char* search = hexa_concat(hexa_concat("\"", key), "\":");
    if ((!hexa_contains(content, search))) {
        return 0;
    }
    hexa_arr parts = hexa_split(content, search);
    if ((parts.n < 2)) {
        return 0;
    }
    const char* after = hexa_trim(((const char*)parts.d[1]));
    const char* num_part = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)(hexa_split(after, ",")).d[0]), "}")).d[0]), "\n")).d[0]));
    return hexa_to_int_str(num_part);
}

const char* extract_json_str(const char* content, const char* key) {
    const char* search = hexa_concat(hexa_concat("\"", key), "\":\"");
    if ((!hexa_contains(content, search))) {
        return "";
    }
    hexa_arr parts = hexa_split(content, search);
    if ((parts.n < 2)) {
        return "";
    }
    const char* after = ((const char*)parts.d[1]);
    hexa_arr end_parts = hexa_split(after, "\"");
    if ((end_parts.n >= 1)) {
        return ((const char*)end_parts.d[0]);
    } else {
        return "";
    }
}

double extract_json_float(const char* content, const char* key) {
    const char* search = hexa_concat(hexa_concat("\"", key), "\":");
    if ((!hexa_contains(content, search))) {
        return 0.0;
    }
    hexa_arr parts = hexa_split(content, search);
    if ((parts.n < 2)) {
        return 0.0;
    }
    const char* after = hexa_trim(((const char*)parts.d[1]));
    const char* num_str = "";
    long i = 0;
    while ((i < hexa_str_len(after))) {
        long ch = after[i];
        if (((((ch == ",") || (ch == "}")) || (ch == " ")) || (ch == "\n"))) {
            i = hexa_str_len(after);
        } else {
            num_str = hexa_concat(num_str, hexa_int_to_str((long)(ch)));
        }
        i = (i + 1);
    }
    return hexa_to_float(num_str);
}

long* parse_run(const char* content, const char* run_key) {
    const char* search = hexa_concat(hexa_concat("\"", run_key), "\":");
    if ((!hexa_contains(content, search))) {
        return hexa_struct_alloc((long[]){(long)(run_key), (long)("?"), (long)("?"), (long)("?"), (long)("?"), (long)("?")}, 6);
    }
    hexa_arr parts = hexa_split(content, search);
    if ((parts.n < 2)) {
        return hexa_struct_alloc((long[]){(long)(run_key), (long)("?"), (long)("?"), (long)("?"), (long)("?"), (long)("?")}, 6);
    }
    const char* block = ((const char*)parts.d[1]);
    hexa_arr block_parts = hexa_split(block, "\n    \"");
    const char* run_block = (((block_parts.n >= 1)) ? (((const char*)block_parts.d[0])) : (block));
    const char* ce_val = "?";
    if (hexa_contains(run_block, "\"ce_best\":")) {
        hexa_arr cp = hexa_split(run_block, "\"ce_best\":");
        if ((cp.n >= 2)) {
            ce_val = hexa_trim(((const char*)(hexa_split(((const char*)cp.d[1]), ",")).d[0]));
        }
    } else {
        if (hexa_contains(run_block, "\"ce_at_5000\":")) {
            hexa_arr cp = hexa_split(run_block, "\"ce_at_5000\":");
            if ((cp.n >= 2)) {
                ce_val = hexa_concat(hexa_trim(((const char*)(hexa_split(((const char*)cp.d[1]), ",")).d[0])), "@5K");
            }
        } else {
            if (hexa_contains(run_block, "\"ce_3000\":")) {
                hexa_arr cp = hexa_split(run_block, "\"ce_3000\":");
                if ((cp.n >= 2)) {
                    ce_val = hexa_concat(hexa_trim(((const char*)(hexa_split(((const char*)cp.d[1]), ",")).d[0])), "@3K");
                }
            }
        }
    }
    const char* phi_val = "?";
    if (hexa_contains(run_block, "\"phi_best\":")) {
        hexa_arr pp = hexa_split(run_block, "\"phi_best\":");
        if ((pp.n >= 2)) {
            phi_val = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)pp.d[1]), ",")).d[0]), "}")).d[0]));
        }
    } else {
        if (hexa_contains(run_block, "\"phi_at_5000\":")) {
            hexa_arr pp = hexa_split(run_block, "\"phi_at_5000\":");
            if ((pp.n >= 2)) {
                phi_val = hexa_trim(((const char*)(hexa_split(((const char*)pp.d[1]), ",")).d[0]));
            }
        }
    }
    const char* steps_val = "?";
    if (hexa_contains(run_block, "\"steps\":")) {
        hexa_arr sp = hexa_split(run_block, "\"steps\":");
        if ((sp.n >= 2)) {
            steps_val = hexa_trim(((const char*)(hexa_split(((const char*)sp.d[1]), ",")).d[0]));
        }
    }
    const char* status_val = "?";
    if (hexa_contains(run_block, "\"status\"")) {
        hexa_arr st = hexa_split(run_block, "\"status\"");
        if ((st.n >= 2)) {
            hexa_arr after_s = hexa_split(((const char*)st.d[1]), "\"");
            if ((after_s.n >= 2)) {
                status_val = ((const char*)after_s.d[1]);
            }
        }
    }
    const char* date_val = "?";
    if (hexa_contains(run_block, "\"date\"")) {
        hexa_arr dt = hexa_split(run_block, "\"date\"");
        if ((dt.n >= 2)) {
            hexa_arr after_d = hexa_split(((const char*)dt.d[1]), "\"");
            if ((after_d.n >= 2)) {
                date_val = ((const char*)after_d.d[1]);
            }
        }
    }
    return hexa_struct_alloc((long[]){(long)(run_key), (long)(ce_val), (long)(phi_val), (long)(steps_val), (long)(status_val), (long)(date_val)}, 6);
}

const char* display_name(const char* key) {
    if ((strcmp(key, "v14.0") == 0)) {
        return "v14.0 (34.5M)";
    } else {
        if ((strcmp(key, "v14.1") == 0)) {
            return "v14.1 tension-lr";
        } else {
            if ((strcmp(key, "v14.3_128c") == 0)) {
                return "v14.3 128c";
            } else {
                if ((strcmp(key, "v3_274M") == 0)) {
                    return "v3 274M";
                } else {
                    if ((strcmp(key, "animalm_14b_v01") == 0)) {
                        return "AnimaLM 14B v0.1";
                    } else {
                        if ((strcmp(key, "animalm_14b_v02") == 0)) {
                            return "AnimaLM 14B v0.2";
                        } else {
                            if ((strcmp(key, "animalm_14b_v03") == 0)) {
                                return "AnimaLM 14B v0.3";
                            } else {
                                if ((strcmp(key, "animalm_14b_v04") == 0)) {
                                    return "AnimaLM 14B v0.4";
                                } else {
                                    if ((strcmp(key, "animalm_72b_v05") == 0)) {
                                        return "AnimaLM 72B v0.5";
                                    } else {
                                        if ((strcmp(key, "animalm_7b_fresh") == 0)) {
                                            return "AnimaLM 7B fresh";
                                        } else {
                                            if ((strcmp(key, "animalm_14b_v05") == 0)) {
                                                return "AnimaLM 14B v0.5";
                                            } else {
                                                if ((strcmp(key, "animalm_32b_v01") == 0)) {
                                                    return "AnimaLM 32B v0.1";
                                                } else {
                                                    return key;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

const char* pad(const char* s, long width) {
    const char* r = s;
    while ((hexa_str_len(r) < width)) {
        r = hexa_concat(r, " ");
    }
    return r;
}

const char* status_icon(const char* s) {
    if (hexa_contains(s, "complete")) {
        return "done";
    } else {
        if (hexa_contains(s, "in_progress")) {
            return "run ";
        } else {
            if (hexa_contains(s, "crashed")) {
                return "CRASH";
            } else {
                if (hexa_contains(s, "stopped")) {
                    return "stop";
                } else {
                    if (hexa_contains(s, "planned")) {
                        return "plan";
                    } else {
                        return s;
                    }
                }
            }
        }
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    a = hexa_args();
    home = hexa_exec("printenv HOME");
    anima_dir = hexa_concat(home, "/Dev/anima/anima");
    config_dir = hexa_concat(anima_dir, "/config");
    today = hexa_trim(hexa_exec("date +%Y-%m-%d"));
    tr_path = hexa_concat(config_dir, "/training_runs.json");
    tr_raw = hexa_read_file(tr_path);
    gl_path = hexa_concat(config_dir, "/growth_loop_state.json");
    gl_raw = hexa_exec(hexa_concat(hexa_concat("head -15 ", gl_path), " 2>/dev/null"));
    rp_path = hexa_concat(config_dir, "/runpod.json");
    rp_exists = hexa_exec(hexa_concat(hexa_concat("test -f ", rp_path), " && echo 1 || echo 0"));
    if ((strcmp(hexa_trim(rp_exists), "1") == 0)) {
        rp_raw = hexa_read_file(rp_path);
    }
    if ((hexa_str_len(rp_raw) > 0)) {
        const char* rp_status = extract_json_str(rp_raw, "status");
        const char* rp_name = extract_json_str(rp_raw, "name");
        if ((hexa_str_len(rp_status) > 0)) {
            pod_status = hexa_concat(hexa_concat(hexa_concat(rp_name, " ("), rp_status), ")");
        }
    }
    cycle = extract_json_int(gl_raw, "cycle");
    harvested = extract_json_int(gl_raw, "total_harvested");
    applied = extract_json_int(gl_raw, "total_applied");
    self_disc = extract_json_int(gl_raw, "total_self_discoveries");
    ck = hexa_split(complete_keys, "|");
    pk = hexa_split(progress_keys, "|");
    plk = hexa_split(planned_keys, "|");
    printf("%s\n", "┌─────────────────────────────────────────────────────────────┐");
    printf("%s\n", hexa_concat(hexa_concat("│  anima 학습 현황 — ", today), "                                │"));
    printf("%s\n", "├─────────────────────────────────────────────────────────────┤");
    printf("%s\n", "│                                                             │");
    printf("%s\n", hexa_concat(hexa_concat("│  ■ 완료된 학습 (", hexa_int_to_str((long)(ck.n))), "건)                                         │"));
    printf("%s\n", "│  ┌──────────────────┬──────────┬───────┬────────┬──────────┐│");
    printf("%s\n", "│  │ 모델             │ CE best  │ Φ     │ Steps  │ 날짜     ││");
    printf("%s\n", "│  ├──────────────────┼──────────┼───────┼────────┼──────────┤│");
    while ((ci < ck.n)) {
        long* r = parse_run(tr_raw, ((const char*)ck.d[ci]));
        const char* star = (((strcmp(((const char*)ck.d[ci]), "animalm_14b_v04") == 0)) ? ("★") : (" "));
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("│  │ ", pad(display_name(r[0]), 16)), " │ "), pad(r[1], 8)), " │ "), pad(r[2], 5)), " │ "), pad(r[3], 6)), " │ "), pad(r[5], 7)), star), "││"));
        ci = (ci + 1);
    }
    printf("%s\n", "│  └──────────────────┴──────────┴───────┴────────┴──────────┘│");
    printf("%s\n", "│                                                             │");
    printf("%s\n", hexa_concat(hexa_concat("│  ■ 진행/중단 (", hexa_int_to_str((long)(pk.n))), "건)                                           │"));
    printf("%s\n", "│  ┌──────────────────┬──────────┬───────┬────────┬──────────┐│");
    printf("%s\n", "│  │ 모델             │ CE       │ Φ     │ 상태   │ 비고     ││");
    printf("%s\n", "│  ├──────────────────┼──────────┼───────┼────────┼──────────┤│");
    while ((pi < pk.n)) {
        long* r = parse_run(tr_raw, ((const char*)pk.d[pi]));
        const char* note = status_icon(r[4]);
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("│  │ ", pad(display_name(r[0]), 16)), " │ "), pad(r[1], 8)), " │ "), pad(r[2], 5)), " │ "), pad(r[4], 6)), " │ "), pad(note, 7)), "││"));
        pi = (pi + 1);
    }
    printf("%s\n", "│  └──────────────────┴──────────┴───────┴────────┴──────────┘│");
    printf("%s\n", "│                                                             │");
    printf("%s\n", "│  ■ 계획된 다음 학습                                           │");
    while ((pli < plk.n)) {
        long* r = parse_run(tr_raw, ((const char*)plk.d[pli]));
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("│  → ", pad(display_name(r[0]), 20)), " steps="), pad(r[3], 6)), " "), pad(r[4], 8)), "       │"));
        pli = (pli + 1);
    }
    printf("%s\n", "│                                                             │");
    printf("%s\n", "│  ■ 성장 루프                                                 │");
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("│  사이클: ", pad(hexa_int_to_str((long)(cycle)), 5)), " | 수확: "), pad(hexa_int_to_str((long)(harvested)), 7)), " | 적용: "), pad(hexa_int_to_str((long)(applied)), 5)), " | 자기발견: "), pad(hexa_int_to_str((long)(self_disc)), 5)), " │"));
    printf("%s\n", "│                                                             │");
    printf("%s\n", "│  ■ RunPod/H100                                              │");
    printf("%s\n", hexa_concat(hexa_concat("│  ", pad(pod_status, 57)), "│"));
    printf("%s\n", "│                                                             │");
    printf("%s\n", "│  ■ 다음 작업 (TODO)                                          │");
    while ((npi < plk.n)) {
        const char* search_key = hexa_concat(hexa_concat("\"", ((const char*)plk.d[npi])), "\"");
        const char* desc = display_name(((const char*)plk.d[npi]));
        if (hexa_contains(tr_raw, search_key)) {
            hexa_arr dp = hexa_split(tr_raw, search_key);
            if ((dp.n >= 2)) {
                const char* block = ((const char*)(hexa_split(((const char*)dp.d[1]), "\n    },")).d[0]);
                if (hexa_contains(block, "\"description\":\"")) {
                    hexa_arr ddp = hexa_split(block, "\"description\":\"");
                    if ((ddp.n >= 2)) {
                        desc = ((const char*)(hexa_split(((const char*)ddp.d[1]), "\"")).d[0]);
                    }
                }
            }
        }
        if ((hexa_str_len(desc) > 53)) {
            desc = desc;
        }
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("│  ", hexa_int_to_str((long)((npi + 1)))), ". "), pad(desc, 56)), "│"));
        npi = (npi + 1);
    }
    const char* evo_grep = hexa_trim(hexa_exec(hexa_concat(hexa_concat("grep -h 'STAGE' ", anima_dir), "/logs/evo_*.log 2>/dev/null | tail -1")));
    if (((hexa_str_len(evo_grep) > 0) && hexa_contains(evo_grep, "STAGE"))) {
        hexa_arr sp = hexa_split(evo_grep, "STAGE ");
        if ((sp.n >= 2)) {
            evo_stage = hexa_concat("S", hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)sp.d[1]), ":")).d[0]), "/")).d[0]), "\n")).d[0])));
        }
    }
    printf("%s\n", hexa_concat(hexa_concat("│  3. 무한진화 계속 (현재 ", pad(evo_stage, 6)), ")                              │"));
    printf("%s\n", "│  4. H100 재기동 후 v14.3 128c 학습 재개                      │");
    printf("%s\n", "│                                                             │");
    printf("%s\n", "│  ■ 핵심 성과                                                 │");
    printf("%s\n", "│  ★ v14.1: CE=0.0002 (역대 최저) — tension-lr 효과            │");
    printf("%s\n", "│  ★ v14.3: Φ=103.4 (128c 최초 100+ 돌파)                     │");
    printf("%s\n", "│  ★ 14B v0.4: CE 8.59→2.0 (×4.3 개선, progressive α)        │");
    printf("%s\n", "│                                                             │");
    printf("%s\n", "└─────────────────────────────────────────────────────────────┘");
    actions = hexa_arr_new();
    action_cmds = hexa_arr_new();
    if ((pod_count == 0)) {
        action_count = (action_count + 1);
        actions = hexa_arr_concat(actions, hexa_arr_lit((long[]){(long)("H100 Pod 생성 → 14B v0.5 학습 발사 (~$8)")}, 1));
        action_cmds = hexa_arr_concat(action_cmds, hexa_arr_lit((long[]){(long)("runpod_14b_v05")}, 1));
    }
    while ((pi2 < pk.n)) {
        long* r2 = parse_run(tr_raw, ((const char*)pk.d[pi2]));
        if ((hexa_contains(r2[4], "crashed") || hexa_contains(r2[4], "in_progress"))) {
            action_count = (action_count + 1);
            actions = hexa_arr_concat(actions, hexa_arr_lit((long[]){(long)(hexa_concat(display_name(((const char*)pk.d[pi2])), " 상태 확인/재개"))}, 1));
            action_cmds = hexa_arr_concat(action_cmds, hexa_arr_lit((long[]){(long)(hexa_concat("check_", ((const char*)pk.d[pi2])))}, 1));
        }
        pi2 = (pi2 + 1);
    }
    action_count = (action_count + 1);
    actions = hexa_arr_concat(actions, hexa_arr_lit((long[]){(long)(hexa_concat(hexa_concat("무한진화 다음 스테이지 (현재 ", evo_stage), ")"))}, 1));
    action_cmds = hexa_arr_concat(action_cmds, hexa_arr_lit((long[]){(long)("evolution_next")}, 1));
    action_count = (action_count + 1);
    actions = hexa_arr_concat(actions, hexa_arr_lit((long[]){(long)("로컬: nexus 블로업 + 교차수분")}, 1));
    action_cmds = hexa_arr_concat(action_cmds, hexa_arr_lit((long[]){(long)("local_blowup")}, 1));
    printf("%s\n", "");
    printf("%s\n", "┌─────────────────────────────────────────────────────────────┐");
    printf("%s\n", "│  ■ 다음 액션 선택지                                         │");
    printf("%s\n", "├─────────────────────────────────────────────────────────────┤");
    while ((ai2 < actions.n)) {
        const char* marker = (((ai2 == 0)) ? (" ← 추천") : (""));
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("│  ", hexa_int_to_str((long)((ai2 + 1)))), ". "), pad(((const char*)actions.d[ai2]), 49)), pad(marker, 7)), "│"));
        ai2 = (ai2 + 1);
    }
    printf("%s\n", "│                                                             │");
    printf("%s\n", "│  자동 진행: hexa anima_status.hexa auto                     │");
    printf("%s\n", "│  수동 선택: hexa anima_status.hexa act <번호>               │");
    printf("%s\n", "└─────────────────────────────────────────────────────────────┘");
    mode = (((a.n >= 3)) ? (((const char*)a.d[2])) : ("report"));
    act_num = (((a.n >= 4)) ? (hexa_to_int_str(((const char*)a.d[3]))) : (1));
    if (((strcmp(mode, "auto") == 0) || (strcmp(mode, "act") == 0))) {
        long target = (((strcmp(mode, "auto") == 0)) ? (1) : (act_num));
        if (((target < 1) || (target > action_cmds.n))) {
            printf("%s\n", hexa_concat("❌ 유효하지 않은 액션 번호: ", hexa_int_to_str((long)(target))));
        } else {
            const char* cmd = ((const char*)action_cmds.d[(target - 1)]);
            printf("%s\n", "");
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat("🚀 액션 ", hexa_int_to_str((long)(target))), " 실행: "), ((const char*)actions.d[(target - 1)])));
            printf("%s\n", "");
            if ((strcmp(cmd, "runpod_14b_v05") == 0)) {
                printf("%s\n", "→ RunPod H100 생성 후 14B v0.5 학습 발사");
                printf("%s\n", "  커맨드: claude -p '14B v0.5 학습을 위한 H100 Pod를 생성하고 학습을 시작해줘'");
                const char* result = hexa_exec("claude -p 'RunPod에 H100 SXM 1개 Pod를 생성하고 anima 14B v0.5 학습을 시작해줘. training_runs.json의 animalm_14b_v05 설정 참고. 완료 후 결과 보고.' --allowedTools 'Bash' 2>&1 | tail -5");
                printf("%s\n", result);
            } else {
                if ((strcmp(cmd, "evolution_next") == 0)) {
                    printf("%s\n", "→ 무한진화 다음 스테이지 실행");
                    const char* evo_result = hexa_exec(hexa_concat(hexa_concat("cd ", home), "/Dev/anima && python3 anima/src/infinite_evolution.py --cells 512 --steps 1000 --cycle-topology 2>&1 | tail -10"));
                    printf("%s\n", evo_result);
                } else {
                    if ((strcmp(cmd, "local_blowup") == 0)) {
                        const char* hexa_bin = hexa_concat(home, "/Dev/hexa-lang/target/release/hexa");
                        const char* blowup = hexa_concat(home, "/Dev/nexus/mk2_hexa/native/blowup.hexa");
                        printf("%s\n", "→ nexus 블로업 실행");
                        const char* seeds = hexa_trim(hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_bin, " "), home), "/Dev/nexus/mk2_hexa/native/seed_engine.hexa merge 2>/dev/null")));
                        const char* blow_result = hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_bin, " "), blowup), " math 3 --no-graph --seeds \""), seeds), "\" 2>&1 | tail -15"));
                        printf("%s\n", blow_result);
                    } else {
                        if (hexa_contains(cmd, "check_")) {
                            printf("%s\n", "→ H100 Pod가 없어 확인 불가. Pod 재시작 후 확인하세요.");
                        }
                    }
                }
            }
        }
    }
    nexus_dir = hexa_concat(home, "/Dev/nexus");
    growth_bus = hexa_concat(nexus_dir, "/shared/growth_bus.jsonl");
    ts = hexa_trim(hexa_exec("date +%Y-%m-%dT%H:%M:%S"));
    bus = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"source\":\"anima_status\",\"timestamp\":\"", ts), "\",\"cycle\":"), hexa_int_to_str((long)(cycle))), ",\"harvested\":"), hexa_int_to_str((long)(harvested))), ",\"applied\":"), hexa_int_to_str((long)(applied))), ",\"self_discoveries\":"), hexa_int_to_str((long)(self_disc))), ",\"pod_count\":"), hexa_int_to_str((long)(pod_count))), ",\"mode\":\""), mode), "\"}");
    hexa_append_file(growth_bus, hexa_concat(bus, "\n"));
    return 0;
}
