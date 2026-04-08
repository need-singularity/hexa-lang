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

double signal_multiplier(const char* signal);
const char* signal_label(const char* signal);
double effective_score(double score, const char* signal);
long record_entry(const char* signal, const char* lens, double score);
const char* compute_lens_scores(const char* raw);
const char* top_k(const char* scores_str, long k);
double novelty_score(double value, const char* history_csv);
double n6_alignment_reward(const char* values_csv);
long hexa_user_main();

static const char* reward_file = "shared/reward_tracker.jsonl";

double signal_multiplier(const char* signal) {
    if ((strcmp(signal, "pattern_found") == 0)) {
        return 1.0;
    }
    if ((strcmp(signal, "consensus") == 0)) {
        return 2.0;
    }
    if ((strcmp(signal, "novelty") == 0)) {
        return 3.0;
    }
    if ((strcmp(signal, "n6_alignment") == 0)) {
        return 6.0;
    }
    return 1.0;
}

const char* signal_label(const char* signal) {
    if ((strcmp(signal, "pattern_found") == 0)) {
        return "PatternFound";
    }
    if ((strcmp(signal, "consensus") == 0)) {
        return "ConsensusAchieved";
    }
    if ((strcmp(signal, "novelty") == 0)) {
        return "NoveltyDetected";
    }
    if ((strcmp(signal, "n6_alignment") == 0)) {
        return "N6Alignment";
    }
    return signal;
}

double effective_score(double score, const char* signal) {
    return (score * signal_multiplier(signal));
}

long record_entry(const char* signal, const char* lens, double score) {
    double eff = effective_score(score, signal);
    const char* line = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"signal\":\"", signal), "\",\"lens\":\""), lens), "\",\"score\":"), hexa_float_to_str((double)(score))), ",\"effective\":"), hexa_float_to_str((double)(eff))), "}");
    const char* raw = hexa_read_file(reward_file);
    if ((strcmp(raw, "") == 0)) {
        return hexa_write_file(reward_file, hexa_concat(line, "\n"));
    } else {
        return hexa_write_file(reward_file, hexa_concat(hexa_concat(raw, line), "\n"));
    }
}

const char* compute_lens_scores(const char* raw) {
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr lens_names = hexa_arr_new();
    hexa_arr lens_scores = hexa_arr_new();
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        i = (i + 1);
        if ((strcmp(line, "") == 0)) {
            continue;
        }
        const char* ln = "\"lens\":\"";
        if ((!hexa_contains(line, ln))) {
            continue;
        }
        hexa_arr lp = hexa_split(line, ln);
        if ((lp.n < 2)) {
            continue;
        }
        const char* lens = ((const char*)(hexa_split(((const char*)lp.d[1]), "\"")).d[0]);
        const char* en = "\"effective\":";
        if ((!hexa_contains(line, en))) {
            continue;
        }
        hexa_arr ep = hexa_split(line, en);
        if ((ep.n < 2)) {
            continue;
        }
        const char* eff_str = hexa_trim(((const char*)(hexa_split(((const char*)(hexa_split(((const char*)ep.d[1]), "}")).d[0]), ",")).d[0]));
        double eff = 0.0;
        eff = hexa_to_float(eff_str);
        long found = 0;
        long j = 0;
        while ((j < lens_names.n)) {
            if ((lens_names.d[j] == lens)) {
                lens_scores.d[j] = (lens_scores.d[j] + eff);
                found = 1;
            }
            j = (j + 1);
        }
        if ((!found)) {
            lens_names = hexa_arr_push(lens_names, (long)(lens));
            lens_scores = hexa_arr_push(lens_scores, eff);
        }
    }
    const char* result = "";
    long k = 0;
    while ((k < lens_names.n)) {
        if ((strcmp(result, "") != 0)) {
            result = hexa_concat(result, ",");
        }
        result = hexa_concat(hexa_concat(hexa_concat(result, hexa_int_to_str((long)(lens_names.d[k]))), ":"), hexa_int_to_str((long)(lens_scores.d[k])));
        k = (k + 1);
    }
    return result;
}

const char* top_k(const char* scores_str, long k) {
    if ((strcmp(scores_str, "") == 0)) {
        return "(none)";
    }
    hexa_arr pairs = hexa_split(scores_str, ",");
    hexa_arr names = hexa_arr_new();
    hexa_arr vals = hexa_arr_new();
    long i = 0;
    while ((i < pairs.n)) {
        hexa_arr p = hexa_split(((const char*)pairs.d[i]), ":");
        if ((p.n >= 2)) {
            names = hexa_arr_push(names, (long)(((const char*)p.d[0])));
            vals = hexa_arr_push(vals, hexa_to_float(((const char*)p.d[1])));
        }
        i = (i + 1);
    }
    long si = 0;
    while ((si < vals.n)) {
        long sj = 0;
        while ((sj < ((vals.n - 1) - si))) {
            if ((vals.d[sj] < vals.d[(sj + 1)])) {
                long tmp_v = vals.d[sj];
                vals.d[sj] = vals.d[(sj + 1)];
                vals.d[(sj + 1)] = tmp_v;
                long tmp_n = names.d[sj];
                names.d[sj] = names.d[(sj + 1)];
                names.d[(sj + 1)] = tmp_n;
            }
            sj = (sj + 1);
        }
        si = (si + 1);
    }
    const char* result = "";
    long ri = 0;
    long limit = (((k < names.n)) ? (k) : (names.n));
    while ((ri < limit)) {
        if ((strcmp(result, "") != 0)) {
            result = hexa_concat(result, "\n");
        }
        result = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(result, "  "), hexa_int_to_str((long)((ri + 1)))), ". "), hexa_int_to_str((long)(names.d[ri]))), "  score="), hexa_int_to_str((long)(vals.d[ri])));
        ri = (ri + 1);
    }
    return result;
}

double novelty_score(double value, const char* history_csv) {
    if ((strcmp(history_csv, "") == 0)) {
        return 1.0;
    }
    hexa_arr parts = hexa_split(history_csv, ",");
    hexa_arr hist = hexa_arr_new();
    long i = 0;
    while ((i < parts.n)) {
        double v = hexa_to_float(hexa_trim(((const char*)parts.d[i])));
        hist = hexa_arr_push(hist, v);
        i = (i + 1);
    }
    if ((hist.n == 0)) {
        return 1.0;
    }
    long min_val = hist.d[0];
    long max_val = hist.d[0];
    long hi = 0;
    while ((hi < hist.n)) {
        if ((hist.d[hi] < min_val)) {
            min_val = hist.d[hi];
        }
        if ((hist.d[hi] > max_val)) {
            max_val = hist.d[hi];
        }
        hi = (hi + 1);
    }
    long range = (max_val - min_val);
    if ((range < 0.000000000001)) {
        double diff = (value - min_val);
        if ((diff < 0.0)) {
            return 1.0;
        }
        if ((diff < 0.000000000001)) {
            return 0.0;
        }
        return 1.0;
    }
    double min_dist = 999999999.0;
    long di = 0;
    while ((di < hist.n)) {
        double d = (value - hist.d[di]);
        double abs_d = (((d < 0.0)) ? ((0.0 - d)) : (d));
        if ((abs_d < min_dist)) {
            min_dist = abs_d;
        }
        di = (di + 1);
    }
    double score = (min_dist / range);
    if ((score > 1.0)) {
        return 1.0;
    }
    return score;
}

double n6_alignment_reward(const char* values_csv) {
    if ((strcmp(values_csv, "") == 0)) {
        return 0.0;
    }
    hexa_arr parts = hexa_split(values_csv, ",");
    hexa_arr targets = hexa_arr_lit((long[]){6.0, 12.0, 24.0, 48.0, 144.0}, 5);
    double total = 0.0;
    long count = 0;
    long i = 0;
    while ((i < parts.n)) {
        double v = hexa_to_float(hexa_trim(((const char*)parts.d[i])));
        double v_abs = (((v < 0.0)) ? ((0.0 - v)) : (v));
        double min_dist = 999999999.0;
        double nearest = 6.0;
        long ti = 0;
        while ((ti < targets.n)) {
            double d = (v_abs - targets.d[ti]);
            double abs_d = (((d < 0.0)) ? ((0.0 - d)) : (d));
            if ((abs_d < min_dist)) {
                min_dist = abs_d;
                nearest = targets.d[ti];
            }
            ti = (ti + 1);
        }
        double ratio = (0.0 - (min_dist / nearest));
        double x = ratio;
        double exp_approx = (((1.0 + x) + ((x * x) / 2.0)) + (((x * x) * x) / 6.0));
        double clamped = (((exp_approx < 0.0)) ? (0.0) : (exp_approx));
        total = (total + clamped);
        count = (count + 1);
        i = (i + 1);
    }
    if ((count == 0)) {
        return 0.0;
    }
    return (total / hexa_to_float(hexa_int_to_str((long)(count))));
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "reward.hexa — 보상 추적 시스템");
        printf("%s", "Usage:");
        printf("%s", "  record <signal> <lens> <score>   기록");
        printf("%s", "    signals: pattern_found, consensus, novelty, n6_alignment");
        printf("%s", "  top [k=5]                        상위 렌즈");
        printf("%s", "  novelty <value> <history_csv>     신규성 점수");
        printf("%s", "  n6-align <values_csv>             n=6 정렬 보상");
        printf("%s", "  status                            현재 상태");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "record") == 0)) {
        if ((a.n < 5)) {
            printf("%s", "Usage: record <signal> <lens> <score>");
            return 0;
        }
        const char* signal = ((const char*)a.d[2]);
        const char* lens = ((const char*)a.d[3]);
        double score = 0.0;
        score = hexa_to_float(((const char*)a.d[4]));
        record_entry(signal, lens, score);
        double eff = effective_score(score, signal);
        printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[reward] recorded ", signal_label(signal)), " for "), lens), " score="), hexa_float_to_str((double)(score))), " effective="), hexa_float_to_str((double)(eff))));
    }
    if ((strcmp(cmd, "top") == 0)) {
        double k = (((a.n > 2)) ? (0) : (5.0));
        const char* raw = hexa_read_file(reward_file);
        if ((strcmp(raw, "") == 0)) {
            printf("%s", "[reward] no entries recorded");
            return 0;
        }
        const char* scores = compute_lens_scores(raw);
        const char* result = top_k(scores, (long)(k));
        printf("%s", "[reward] top performers:");
        printf("%s", result);
    }
    if ((strcmp(cmd, "novelty") == 0)) {
        if ((a.n < 4)) {
            printf("%s", "Usage: novelty <value> <history_csv>");
            return 0;
        }
        double val = 0.0;
        val = hexa_to_float(((const char*)a.d[2]));
        double score = novelty_score(val, ((const char*)a.d[3]));
        printf("%s", hexa_concat(hexa_concat(hexa_concat("[reward] novelty(", ((const char*)a.d[2])), ") = "), hexa_float_to_str((double)(score))));
    }
    if ((strcmp(cmd, "n6-align") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: n6-align <values_csv>");
            return 0;
        }
        double reward = n6_alignment_reward(((const char*)a.d[2]));
        printf("%s", hexa_concat("[reward] n6_alignment = ", hexa_float_to_str((double)(reward))));
    }
    if ((strcmp(cmd, "status") == 0)) {
        const char* raw = hexa_read_file(reward_file);
        if ((strcmp(raw, "") == 0)) {
            printf("%s", "[reward] no entries");
            return 0;
        }
        hexa_arr lines = hexa_split(raw, "\n");
        long count = 0;
        long li = 0;
        while ((li < lines.n)) {
            if ((strcmp(hexa_trim(((const char*)lines.d[li])), "") != 0)) {
                count = (count + 1);
            }
            li = (li + 1);
        }
        const char* scores = compute_lens_scores(raw);
        printf("%s", hexa_concat("[reward] entries: ", hexa_int_to_str((long)(count))));
        return printf("%s", hexa_concat("[reward] lens scores: ", scores));
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    return 0;
}
