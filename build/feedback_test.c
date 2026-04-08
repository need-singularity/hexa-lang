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

double feedback_score(const char* type_tag);
long validate_type_tag(const char* tag);
const char* serialize_feedback(const char* ts, const char* type_tag, const char* discovery_id, const char* reason);
const char* parse_feedback_line(const char* line);
const char* load_feedbacks(const char* path);
long append_feedback(const char* path, const char* type_tag, const char* discovery_id, const char* reason);
const char* compute_stats(const char* path);
const char* compute_net_scores(const char* path);
const char* compute_weight_updates(const char* path);
double clamp(double val, double lo, double hi);
long hexa_user_main();

static long N = 6;
static double learning_rate = 0.166666667;
static double weight_min = 0.0;
static double weight_max = 2.0;
static const char* default_feedback_file = "shared/feedback.tsv";

double feedback_score(const char* type_tag) {
    if ((strcmp(type_tag, "good") == 0)) {
        return 1.0;
    }
    if ((strcmp(type_tag, "interesting") == 0)) {
        return 0.5;
    }
    if ((strcmp(type_tag, "irrelevant") == 0)) {
        return (-0.5);
    }
    if ((strcmp(type_tag, "bad") == 0)) {
        return (-1.0);
    }
    return 0.0;
}

long validate_type_tag(const char* tag) {
    if ((strcmp(tag, "good") == 0)) {
        return 1;
    }
    if ((strcmp(tag, "bad") == 0)) {
        return 1;
    }
    if ((strcmp(tag, "interesting") == 0)) {
        return 1;
    }
    if ((strcmp(tag, "irrelevant") == 0)) {
        return 1;
    }
    return 0;
}

const char* serialize_feedback(const char* ts, const char* type_tag, const char* discovery_id, const char* reason) {
    if ((hexa_str_len(reason) > 0)) {
        return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(ts, "\t"), type_tag), "\t"), discovery_id), "\t"), reason);
    }
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(ts, "\t"), type_tag), "\t"), discovery_id);
}

const char* parse_feedback_line(const char* line) {
    long parts = split(line, "\t");
    if ((hexa_str_len(parts) < 3)) {
        return "";
    }
    long ts = parts[0];
    long typ = parts[1];
    long did = parts[2];
    const char* reason = "";
    if ((hexa_str_len(parts) >= 4)) {
        reason = parts[3];
    }
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(ts)), "|"), hexa_int_to_str((long)(typ))), "|"), hexa_int_to_str((long)(did))), "|"), reason);
}

const char* load_feedbacks(const char* path) {
    return hexa_read_file(path);
}

long append_feedback(const char* path, const char* type_tag, const char* discovery_id, const char* reason) {
    const char* existing = "";
    existing = hexa_read_file(path);
    long lines = split(existing, "\n");
    long count = 0;
    long li = 0;
    while ((li < hexa_str_len(lines))) {
        if ((hexa_str_len(hexa_trim(lines[li])) > 0)) {
            count = (count + 1);
        }
        li = (li + 1);
    }
    const char* ts = hexa_int_to_str((long)((count + 1)));
    const char* new_line = serialize_feedback(ts, type_tag, discovery_id, reason);
    const char* content = existing;
    if ((hexa_str_len(content) > 0)) {
        if ((!hexa_ends_with(content, "\n"))) {
            content = hexa_concat(content, "\n");
        }
    }
    content = hexa_concat(hexa_concat(content, new_line), "\n");
    hexa_write_file(path, content);
    return 1;
}

const char* compute_stats(const char* path) {
    const char* content = load_feedbacks(path);
    long lines = split(content, "\n");
    long total = 0;
    long good = 0;
    long bad = 0;
    long interesting = 0;
    long irrelevant = 0;
    long li = 0;
    while ((li < hexa_str_len(lines))) {
        const char* line = hexa_trim(lines[li]);
        if ((hexa_str_len(line) == 0)) {
            li = (li + 1);
            continue;
        }
        const char* parsed = parse_feedback_line(line);
        if ((hexa_str_len(parsed) == 0)) {
            li = (li + 1);
            continue;
        }
        long parts = split(parsed, "|");
        long typ = parts[1];
        total = (total + 1);
        if ((typ == "good")) {
            good = (good + 1);
        } else {
            if ((typ == "bad")) {
                bad = (bad + 1);
            } else {
                if ((typ == "interesting")) {
                    interesting = (interesting + 1);
                } else {
                    if ((typ == "irrelevant")) {
                        irrelevant = (irrelevant + 1);
                    }
                }
            }
        }
        li = (li + 1);
    }
    double good_rate = 0.0;
    if ((total > 0)) {
        good_rate = (((double)(good)) / ((double)(total)));
    }
    const char* out = "=== Feedback Statistics ===\n";
    out = hexa_concat(hexa_concat(hexa_concat(out, "Total:       "), hexa_int_to_str((long)(total))), "\n");
    out = hexa_concat(hexa_concat(hexa_concat(out, "Good:        "), hexa_int_to_str((long)(good))), "\n");
    out = hexa_concat(hexa_concat(hexa_concat(out, "Bad:         "), hexa_int_to_str((long)(bad))), "\n");
    out = hexa_concat(hexa_concat(hexa_concat(out, "Interesting: "), hexa_int_to_str((long)(interesting))), "\n");
    out = hexa_concat(hexa_concat(hexa_concat(out, "Irrelevant:  "), hexa_int_to_str((long)(irrelevant))), "\n");
    out = hexa_concat(hexa_concat(hexa_concat(out, "Good rate:   "), hexa_float_to_str((double)(good_rate))), "\n");
    return out;
}

const char* compute_net_scores(const char* path) {
    const char* content = load_feedbacks(path);
    long lines = split(content, "\n");
    hexa_arr ids = hexa_arr_new();
    hexa_arr scores = hexa_arr_new();
    long li = 0;
    while ((li < hexa_str_len(lines))) {
        const char* line = hexa_trim(lines[li]);
        if ((hexa_str_len(line) == 0)) {
            li = (li + 1);
            continue;
        }
        const char* parsed = parse_feedback_line(line);
        if ((hexa_str_len(parsed) == 0)) {
            li = (li + 1);
            continue;
        }
        long parts = split(parsed, "|");
        long typ = parts[1];
        long did = parts[2];
        double sc = feedback_score(typ);
        long found = 0;
        long si = 0;
        while ((si < ids.n)) {
            if ((ids.d[si] == did)) {
                scores.d[si] = (scores.d[si] + sc);
                found = 1;
            }
            si = (si + 1);
        }
        if ((!found)) {
            ids = hexa_arr_push(ids, did);
            scores = hexa_arr_push(scores, sc);
        }
        li = (li + 1);
    }
    long ni = 0;
    while ((ni < scores.n)) {
        long nj = (ni + 1);
        while ((nj < scores.n)) {
            if ((scores.d[nj] > scores.d[ni])) {
                long tmp_s = scores.d[ni];
                scores.d[ni] = scores.d[nj];
                scores.d[nj] = tmp_s;
                long tmp_id = ids.d[ni];
                ids.d[ni] = ids.d[nj];
                ids.d[nj] = tmp_id;
            }
            nj = (nj + 1);
        }
        ni = (ni + 1);
    }
    const char* out = "";
    long oi = 0;
    while ((oi < ids.n)) {
        out = hexa_concat(hexa_concat(hexa_concat(hexa_concat(out, hexa_int_to_str((long)(ids.d[oi]))), ":"), hexa_int_to_str((long)(scores.d[oi]))), "\n");
        oi = (oi + 1);
    }
    return out;
}

const char* compute_weight_updates(const char* path) {
    const char* content = load_feedbacks(path);
    long lines = split(content, "\n");
    hexa_arr lens_names = hexa_arr_new();
    hexa_arr lens_totals = hexa_arr_new();
    hexa_arr lens_counts = hexa_arr_new();
    long li = 0;
    while ((li < hexa_str_len(lines))) {
        const char* line = hexa_trim(lines[li]);
        if ((hexa_str_len(line) == 0)) {
            li = (li + 1);
            continue;
        }
        const char* parsed = parse_feedback_line(line);
        if ((hexa_str_len(parsed) == 0)) {
            li = (li + 1);
            continue;
        }
        long parts = split(parsed, "|");
        long typ = parts[1];
        long did = parts[2];
        double sc = feedback_score(typ);
        long id_parts = split(did, "-");
        const char* lens_name = "unknown";
        if ((hexa_str_len(id_parts) >= 2)) {
            lens_name = id_parts[0];
        }
        long found = 0;
        long si = 0;
        while ((si < lens_names.n)) {
            if ((strcmp(((const char*)lens_names.d[si]), lens_name) == 0)) {
                lens_totals.d[si] = (lens_totals.d[si] + sc);
                lens_counts.d[si] = (lens_counts.d[si] + 1);
                found = 1;
            }
            si = (si + 1);
        }
        if ((!found)) {
            lens_names = hexa_arr_push(lens_names, (long)(lens_name));
            lens_totals = hexa_arr_push(lens_totals, sc);
            lens_counts = hexa_arr_push(lens_counts, 1);
        }
        li = (li + 1);
    }
    const char* out = "=== Lens Weight Updates (lr=1/6) ===\n";
    long wi = 0;
    while ((wi < lens_names.n)) {
        double avg = (lens_totals.d[wi] / ((double)(lens_counts.d[wi])));
        double delta = (avg * learning_rate);
        const char* sign = "+";
        if ((delta < 0.0)) {
            sign = "";
        }
        out = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(out, "  "), ((const char*)lens_names.d[wi])), ": "), sign), hexa_float_to_str((double)(delta))), "\n");
        wi = (wi + 1);
    }
    return out;
}

double clamp(double val, double lo, double hi) {
    if ((val < lo)) {
        return lo;
    }
    if ((val > hi)) {
        return hi;
    }
    return val;
}

long hexa_user_main() {
    hexa_arr argv = hexa_args();
    if ((argv.n < 2)) {
        printf("%s\n", "feedback.hexa — 발견 품질 피드백 루프 (n=6)");
        printf("%s\n", "");
        printf("%s\n", "Usage:");
        printf("%s\n", "  hexa feedback.hexa report [path]                  — 피드백 통계 리포트");
        printf("%s\n", "  hexa feedback.hexa rate <discovery_id> <score>    — 피드백 기록");
        printf("%s\n", "    score: good | bad | interesting | irrelevant");
        printf("%s\n", "  hexa feedback.hexa top [N]                        — 상위 발견 리더보드");
        printf("%s\n", "  hexa feedback.hexa weights [path]                 — 렌즈 가중치 업데이트");
        printf("%s\n", "");
        printf("%s\n", "점수 체계:");
        printf("%s\n", "  good         = +1.0");
        printf("%s\n", "  interesting  = +0.5");
        printf("%s\n", "  irrelevant   = -0.5");
        printf("%s\n", "  bad          = -1.0");
        printf("%s\n", "");
        printf("%s\n", hexa_concat("Default file: ", default_feedback_file));
        return 0;
    }
    const char* cmd = ((const char*)argv.d[1]);
    if ((strcmp(cmd, "report") == 0)) {
        const char* path = default_feedback_file;
        if ((argv.n >= 3)) {
            path = ((const char*)argv.d[2]);
        }
        printf("%s\n", compute_stats(path));
    } else {
        if ((strcmp(cmd, "rate") == 0)) {
            if ((argv.n < 4)) {
                printf("%s\n", "[ERROR] 사용법: hexa feedback.hexa rate <discovery_id> <score>");
                printf("%s\n", "  score: good | bad | interesting | irrelevant");
                return 1;
            }
            const char* did = ((const char*)argv.d[2]);
            const char* type_tag = ((const char*)argv.d[3]);
            if ((!validate_type_tag(type_tag))) {
                printf("%s\n", hexa_concat("[ERROR] 유효하지 않은 점수: ", type_tag));
                printf("%s\n", "  허용: good | bad | interesting | irrelevant");
                return 1;
            }
            const char* reason = "";
            if ((argv.n >= 5)) {
                reason = ((const char*)argv.d[4]);
            }
            const char* path = default_feedback_file;
            if ((argv.n >= 6)) {
                path = ((const char*)argv.d[5]);
            }
            long ok = append_feedback(path, type_tag, did, reason);
            if (ok) {
                double sc = feedback_score(type_tag);
                printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[OK] ", did), " <- "), type_tag), " ("), hexa_float_to_str((double)(sc))), ")"));
            }
        } else {
            if ((strcmp(cmd, "top") == 0)) {
                long n = 6;
                if ((argv.n >= 3)) {
                    n = hexa_to_int_str(((const char*)argv.d[2]));
                }
                const char* path = default_feedback_file;
                if ((argv.n >= 4)) {
                    path = ((const char*)argv.d[3]);
                }
                printf("%s\n", hexa_concat(hexa_concat("=== Top ", hexa_int_to_str((long)(n))), " Discoveries ==="));
                const char* scores_str = compute_net_scores(path);
                long score_lines = split(scores_str, "\n");
                long printed = 0;
                long si = 0;
                while ((si < hexa_str_len(score_lines))) {
                    if ((printed >= n)) {
                        break;
                    }
                    const char* sl = hexa_trim(score_lines[si]);
                    if ((hexa_str_len(sl) > 0)) {
                        long rank = (printed + 1);
                        printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  #", hexa_int_to_str((long)(rank))), " "), sl));
                        printed = (printed + 1);
                    }
                    si = (si + 1);
                }
                if ((printed == 0)) {
                    printf("%s\n", "  (피드백 없음)");
                }
            } else {
                if ((strcmp(cmd, "weights") == 0)) {
                    const char* path = default_feedback_file;
                    if ((argv.n >= 3)) {
                        path = ((const char*)argv.d[2]);
                    }
                    printf("%s\n", compute_weight_updates(path));
                } else {
                    printf("%s\n", hexa_concat("[ERROR] 알 수 없는 명령: ", cmd));
                    printf("%s\n", "  report | rate | top | weights");
                    return 1;
                }
            }
        }
    }
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    hexa_user_main();
    return 0;
}
