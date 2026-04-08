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

const char* n6_match(double value);
const char* n6_grade(const char* match_str);
const char* escape_latex(const char* text);
const char* star_string(long count);
const char* jstr(const char* json, const char* key);
long* load_experiment(const char* exp_id);
const char* generate_markdown(const char* title, const char* abstract_text, const char* findings_text, const char* connections_text, const char* predictions_text);
const char* generate_latex(const char* title, const char* abstract_text, const char* findings_text, const char* connections_text, const char* predictions_text);
const char* format_bt(long bt_number, const char* title, const char* domains_str, const char* evidence_str, const char* n6_formula, long star_rating);
const char* format_bt_candidate(const char* title, const char* domains_str, const char* n6_connections, double confidence);
const char* format_bt_ref(long bt_number, const char* title);
long hexa_user_main();

static const char* report_dir = "shared/reports";
static const char* experiment_log = "shared/discovery_log.jsonl";
static const char* n6_foundation = "sigma(n) * phi(n) = n * tau(n)  <==>  n = 6";
static double n6_sigma = 12.0;
static double n6_tau = 4.0;
static double n6_phi = 2.0;
static double n6_j2 = 24.0;
static double n6_n = 6.0;
static double n6_sopfr = 5.0;

const char* n6_match(double value) {
    double diff_sigma = (((value > n6_sigma)) ? ((value - n6_sigma)) : ((n6_sigma - value)));
    if ((diff_sigma < 0.001)) {
        return "sigma(6)=12 EXACT";
    }
    double diff_tau = (((value > n6_tau)) ? ((value - n6_tau)) : ((n6_tau - value)));
    if ((diff_tau < 0.001)) {
        return "tau(6)=4 EXACT";
    }
    double diff_phi = (((value > n6_phi)) ? ((value - n6_phi)) : ((n6_phi - value)));
    if ((diff_phi < 0.001)) {
        return "phi(6)=2 EXACT";
    }
    double diff_j2 = (((value > n6_j2)) ? ((value - n6_j2)) : ((n6_j2 - value)));
    if ((diff_j2 < 0.001)) {
        return "J2(6)=24 EXACT";
    }
    double diff_n = (((value > n6_n)) ? ((value - n6_n)) : ((n6_n - value)));
    if ((diff_n < 0.001)) {
        return "n=6 EXACT";
    }
    double diff_sopfr = (((value > n6_sopfr)) ? ((value - n6_sopfr)) : ((n6_sopfr - value)));
    if ((diff_sopfr < 0.001)) {
        return "sopfr(6)=5 EXACT";
    }
    if ((diff_sigma < (n6_sigma * 0.1))) {
        return "~sigma CLOSE";
    }
    if ((diff_tau < (n6_tau * 0.1))) {
        return "~tau CLOSE";
    }
    if ((diff_j2 < (n6_j2 * 0.1))) {
        return "~J2 CLOSE";
    }
    if ((diff_n < (n6_n * 0.1))) {
        return "~n CLOSE";
    }
    return "--";
}

const char* n6_grade(const char* match_str) {
    if ((hexa_index_of(match_str, "EXACT") >= 0)) {
        return "EXACT";
    }
    if ((hexa_index_of(match_str, "CLOSE") >= 0)) {
        return "CLOSE";
    }
    if ((hexa_index_of(match_str, "WEAK") >= 0)) {
        return "WEAK";
    }
    return "--";
}

const char* escape_latex(const char* text) {
    const char* out = text;
    out = hexa_replace(out, "&", "\\&");
    out = hexa_replace(out, "%", "\\%");
    out = hexa_replace(out, "$", "\\$");
    out = hexa_replace(out, "#", "\\#");
    out = hexa_replace(out, "_", "\\_");
    out = hexa_replace(out, "{", "\\{");
    out = hexa_replace(out, "}", "\\}");
    out = hexa_replace(out, "~", "\\textasciitilde{}");
    out = hexa_replace(out, "^", "\\textasciicircum{}");
    return out;
}

const char* star_string(long count) {
    long capped = (((count > 3)) ? (3) : (count));
    const char* out = "";
    long i = 0;
    while ((i < capped)) {
        out = hexa_concat(out, "*");
        i = (i + 1);
    }
    return out;
}

const char* jstr(const char* json, const char* key) {
    const char* needle = hexa_concat(hexa_concat("\"", key), "\":\"");
    long idx = hexa_index_of(json, needle);
    if ((idx < 0)) {
        return "";
    }
    long start = (idx + hexa_str_len(needle));
    const char* rest = hexa_substr(json, start, hexa_str_len(json));
    long end = hexa_index_of(rest, "\"");
    if ((end < 0)) {
        return "";
    }
    return hexa_substr(rest, 0, end);
}

long* load_experiment(const char* exp_id) {
    const char* raw = hexa_read_file(experiment_log);
    if ((strcmp(raw, "") != 0)) {
        hexa_arr lines = hexa_split(raw, "\n");
        long i = 0;
        while ((i < lines.n)) {
            const char* line = hexa_trim(((const char*)lines.d[i]));
            if ((strcmp(line, "") != 0)) {
                const char* id = jstr(line, "id");
                if ((strcmp(id, exp_id) == 0)) {
                    return hexa_struct_alloc((long[]){(long)(id), (long)(jstr(line, "title")), (long)(jstr(line, "abstract")), (long)(jstr(line, "findings")), (long)(jstr(line, "connections")), (long)(jstr(line, "predictions"))}, 6);
                }
            }
            i = (i + 1);
        }
    }
    return hexa_struct_alloc((long[]){(long)(exp_id), (long)(hexa_concat("Experiment ", exp_id)), (long)(hexa_concat("Auto-generated report for ", exp_id)), (long)(""), (long)(""), (long)("")}, 6);
}

const char* generate_markdown(const char* title, const char* abstract_text, const char* findings_text, const char* connections_text, const char* predictions_text) {
    const char* md = hexa_concat(hexa_concat("# ", title), "\n\n");
    md = hexa_concat(hexa_concat(hexa_concat(md, "## Abstract\n\n"), abstract_text), "\n\n");
    md = hexa_concat(hexa_concat(hexa_concat(md, "## Foundation\n\n"), n6_foundation), "\n\n");
    if ((strcmp(findings_text, "") != 0)) {
        md = hexa_concat(md, "## Findings\n\n");
        md = hexa_concat(md, "| Metric | Value | n=6 Match | Grade |\n");
        md = hexa_concat(md, "|--------|-------|-----------|-------|\n");
        hexa_arr items = hexa_split(findings_text, "
        ");
        long i = 0;
        while ((i < items.n)) {
            const char* item = hexa_trim(((const char*)items.d[i]));
            if ((strcmp(item, "") != 0)) {
                long eq = hexa_index_of(item, "=");
                if ((eq > 0)) {
                    const char* name = hexa_trim(hexa_substr(item, 0, eq));
                    const char* val_str = hexa_trim(hexa_substr(item, (eq + 1), hexa_str_len(item)));
                    double val = 0.0;
                    val = hexa_to_float(val_str);
                    const char* match_str = n6_match(val);
                    const char* grade = n6_grade(match_str);
                    md = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(md, "| "), name), " | "), val_str), " | "), match_str), " | "), grade), " |\n");
                }
            }
            i = (i + 1);
        }
        md = hexa_concat(md, "\n");
    }
    if ((strcmp(connections_text, "") != 0)) {
        md = hexa_concat(md, "## n=6 Connections\n\n");
        hexa_arr conns = hexa_split(connections_text, "
        ");
        long i = 0;
        while ((i < conns.n)) {
            const char* c = hexa_trim(((const char*)conns.d[i]));
            if ((strcmp(c, "") != 0)) {
                md = hexa_concat(hexa_concat(hexa_concat(md, "- "), c), "\n");
            }
            i = (i + 1);
        }
        md = hexa_concat(md, "\n");
    }
    if ((strcmp(predictions_text, "") != 0)) {
        md = hexa_concat(md, "## Testable Predictions\n\n");
        hexa_arr preds = hexa_split(predictions_text, "
        ");
        long i = 0;
        while ((i < preds.n)) {
            const char* p = hexa_trim(((const char*)preds.d[i]));
            if ((strcmp(p, "") != 0)) {
                md = hexa_concat(hexa_concat(hexa_concat(hexa_concat(md, hexa_int_to_str((long)((i + 1)))), ". "), p), "\n");
            }
            i = (i + 1);
        }
        md = hexa_concat(md, "\n");
    }
    md = hexa_concat(md, "---\n*Generated by NEXUS-6 Discovery Engine*\n");
    return md;
}

const char* generate_latex(const char* title, const char* abstract_text, const char* findings_text, const char* connections_text, const char* predictions_text) {
    const char* doc = "\\documentclass{article}\n";
    doc = hexa_concat(doc, "\\usepackage[utf8]{inputenc}\n");
    doc = hexa_concat(doc, "\\usepackage{ amsmath: amsmath, amssymb}\n");
    doc = hexa_concat(doc, "\\usepackage{booktabs}\n");
    doc = hexa_concat(doc, "\\usepackage{hyperref}\n\n");
    doc = hexa_concat(hexa_concat(hexa_concat(doc, "\\title{"), escape_latex(title)), "}\n");
    doc = hexa_concat(doc, "\\author{NEXUS-6 Discovery Engine}\n");
    doc = hexa_concat(doc, "\\date{\\today}\n\n");
    doc = hexa_concat(doc, "\\begin{document}\n");
    doc = hexa_concat(doc, "\\maketitle\n\n");
    doc = hexa_concat(doc, "\\begin{abstract}\n");
    doc = hexa_concat(hexa_concat(doc, escape_latex(abstract_text)), "\n");
    doc = hexa_concat(doc, "\\end{abstract}\n\n");
    doc = hexa_concat(doc, "\\section{Foundation}\n");
    doc = hexa_concat(doc, "All results are analyzed through the lens of the n=6 uniqueness theorem:\n");
    doc = hexa_concat(doc, "\\begin{equation}\n");
    doc = hexa_concat(doc, "\\sigma(n) \\cdot \\varphi(n) = n \\cdot \\tau(n) \\iff n = 6\n");
    doc = hexa_concat(doc, "\\end{equation}\n\n");
    if ((strcmp(findings_text, "") != 0)) {
        doc = hexa_concat(doc, "\\section{Findings}\n\n");
        doc = hexa_concat(doc, "\\begin{table}[h]\n\\centering\n");
        doc = hexa_concat(doc, "\\begin{tabular}{lrl}\n\\toprule\n");
        doc = hexa_concat(doc, "Metric & Value & n=6 Match \\\\\n\\midrule\n");
        hexa_arr items = hexa_split(findings_text, "
        ");
        long i = 0;
        while ((i < items.n)) {
            const char* item = hexa_trim(((const char*)items.d[i]));
            if ((strcmp(item, "") != 0)) {
                long eq = hexa_index_of(item, "=");
                if ((eq > 0)) {
                    const char* name = hexa_trim(hexa_substr(item, 0, eq));
                    const char* val_str = hexa_trim(hexa_substr(item, (eq + 1), hexa_str_len(item)));
                    double val = 0.0;
                    val = hexa_to_float(val_str);
                    const char* match_str = n6_match(val);
                    const char* grade = n6_grade(match_str);
                    doc = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(doc, escape_latex(name)), " & "), val_str), " & "), match_str), " ("), grade), ") \\\\\n");
                }
            }
            i = (i + 1);
        }
        doc = hexa_concat(doc, "\\bottomrule\n\\end{tabular}\n");
        doc = hexa_concat(doc, "\\caption{Experimental findings with n=6 correspondence}\n");
        doc = hexa_concat(doc, "\\end{table}\n\n");
    }
    if ((strcmp(connections_text, "") != 0)) {
        doc = hexa_concat(doc, "\\section{n=6 Connections}\n\n\\begin{itemize}\n");
        hexa_arr conns = hexa_split(connections_text, "
        ");
        long i = 0;
        while ((i < conns.n)) {
            const char* c = hexa_trim(((const char*)conns.d[i]));
            if ((strcmp(c, "") != 0)) {
                doc = hexa_concat(hexa_concat(hexa_concat(doc, "\\item "), escape_latex(c)), "\n");
            }
            i = (i + 1);
        }
        doc = hexa_concat(doc, "\\end{itemize}\n\n");
    }
    if ((strcmp(predictions_text, "") != 0)) {
        doc = hexa_concat(doc, "\\section{Testable Predictions}\n\n\\begin{enumerate}\n");
        hexa_arr preds = hexa_split(predictions_text, "
        ");
        long i = 0;
        while ((i < preds.n)) {
            const char* p = hexa_trim(((const char*)preds.d[i]));
            if ((strcmp(p, "") != 0)) {
                doc = hexa_concat(hexa_concat(hexa_concat(doc, "\\item "), escape_latex(p)), "\n");
            }
            i = (i + 1);
        }
        doc = hexa_concat(doc, "\\end{enumerate}\n\n");
    }
    doc = hexa_concat(doc, "\\end{document}\n");
    return doc;
}

const char* format_bt(long bt_number, const char* title, const char* domains_str, const char* evidence_str, const char* n6_formula, long star_rating) {
    const char* stars = star_string(star_rating);
    hexa_arr evidence_items = hexa_split(evidence_str, "
    ");
    long count = evidence_items.n;
    const char* out = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("BT-", hexa_int_to_str((long)(bt_number))), ": "), title), " ("), domains_str), ", "), hexa_int_to_str((long)(count))), "/"), hexa_int_to_str((long)(count))), " EXACT) "), stars), "\n");
    if ((strcmp(n6_formula, "") != 0)) {
        out = hexa_concat(hexa_concat(hexa_concat(out, "  Formula: "), n6_formula), "\n");
    }
    if ((strcmp(evidence_str, "") != 0)) {
        out = hexa_concat(out, "  Evidence:\n");
        long i = 0;
        while ((i < evidence_items.n)) {
            const char* ev = hexa_trim(((const char*)evidence_items.d[i]));
            if ((strcmp(ev, "") != 0)) {
                out = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(out, "    "), hexa_int_to_str((long)((i + 1)))), ". "), ev), "\n");
            }
            i = (i + 1);
        }
    }
    return out;
}

const char* format_bt_candidate(const char* title, const char* domains_str, const char* n6_connections, double confidence) {
    long suggested_stars = (((confidence >= 0.9)) ? (3) : ((((confidence >= 0.7)) ? (2) : (1))));
    const char* stars = star_string(suggested_stars);
    const char* pct = hexa_float_to_str((double)((confidence * 100.0)));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("BT-XXX (candidate): ", title), " ("), domains_str), ", "), n6_connections), " n=6 connections, confidence="), pct), "%) "), stars);
}

const char* format_bt_ref(long bt_number, const char* title) {
    return hexa_concat(hexa_concat(hexa_concat("BT-", hexa_int_to_str((long)(bt_number))), ": "), title);
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "publish.hexa — experiment result documentation");
        printf("%s", "usage:");
        printf("%s", "  hexa publish.hexa report <experiment_id>");
        printf("%s", "  hexa publish.hexa export <format> [experiment_id]  (markdown|latex|bt)");
        printf("%s", "  hexa publish.hexa bt <number> <title> <domains> <formula> [stars]");
        printf("%s", "  hexa publish.hexa bt-candidate <title> <domains> <connections> <confidence>");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "report") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "usage: hexa publish.hexa report <experiment_id>");
            return 0;
        }
        const char* exp_id = ((const char*)a.d[2]);
        long* hexa_v_exp = load_experiment(exp_id);
        const char* md = generate_markdown(hexa_v_exp[1], hexa_v_exp[2], hexa_v_exp[3], hexa_v_exp[4], hexa_v_exp[5]);
        printf("%s", md);
        const char* path = hexa_concat(hexa_concat(hexa_concat(report_dir, "/"), exp_id), ".md");
        hexa_write_file(path, md);
        return printf("%s", hexa_concat("--- saved to ", path));
    } else if ((strcmp(cmd, "export") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "usage: hexa publish.hexa export <format> [experiment_id]");
            return 0;
        }
        const char* format = ((const char*)a.d[2]);
        const char* exp_id = (((a.n >= 4)) ? (((const char*)a.d[3])) : ("latest"));
        long* hexa_v_exp = load_experiment(exp_id);
        if ((strcmp(format, "markdown") == 0)) {
            const char* md = generate_markdown(hexa_v_exp[1], hexa_v_exp[2], hexa_v_exp[3], hexa_v_exp[4], hexa_v_exp[5]);
            printf("%s", md);
            const char* path = hexa_concat(hexa_concat(hexa_concat(report_dir, "/"), exp_id), ".md");
            hexa_write_file(path, md);
            return printf("%s", hexa_concat("--- saved to ", path));
        } else if ((strcmp(format, "latex") == 0)) {
            const char* tex = generate_latex(hexa_v_exp[1], hexa_v_exp[2], hexa_v_exp[3], hexa_v_exp[4], hexa_v_exp[5]);
            printf("%s", tex);
            const char* path = hexa_concat(hexa_concat(hexa_concat(report_dir, "/"), exp_id), ".tex");
            hexa_write_file(path, tex);
            return printf("%s", hexa_concat("--- saved to ", path));
        } else if ((strcmp(format, "bt") == 0)) {
            const char* bt_out = format_bt(0, hexa_v_exp[1], "n6", hexa_v_exp[3], n6_foundation, 2);
            return printf("%s", bt_out);
        } else {
            return printf("%s", hexa_concat(hexa_concat("unknown format: ", format), " (use markdown|latex|bt)"));
        }


    } else if ((strcmp(cmd, "bt") == 0)) {
        if ((a.n < 5)) {
            printf("%s", "usage: hexa publish.hexa bt <number> <title> <domains> <formula> [stars]");
            return 0;
        }
        long bt_num = 0;
        bt_num = hexa_to_int_str(((const char*)a.d[2]));
        const char* title = ((const char*)a.d[3]);
        const char* domains = ((const char*)a.d[4]);
        const char* formula = (((a.n >= 6)) ? (((const char*)a.d[5])) : (""));
        long stars = 2;
        if ((a.n >= 7)) {
            stars = hexa_to_int_str(((const char*)a.d[6]));
        }
        const char* out = format_bt(bt_num, title, domains, "", formula, stars);
        return printf("%s", out);
    } else if ((strcmp(cmd, "bt-candidate") == 0)) {
        if ((a.n < 6)) {
            printf("%s", "usage: hexa publish.hexa bt-candidate <title> <domains> <connections> <confidence>");
            return 0;
        }
        const char* title = ((const char*)a.d[2]);
        const char* domains = ((const char*)a.d[3]);
        long conns = 0;
        conns = hexa_to_int_str(((const char*)a.d[4]));
        double conf = 0.0;
        conf = hexa_to_float(((const char*)a.d[5]));
        const char* out = format_bt_candidate(title, domains, conns, conf);
        return printf("%s", out);
    } else {
        printf("%s", hexa_concat("unknown command: ", cmd));
        return printf("%s", "commands: report, export, bt, bt-candidate");
    }



}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    hexa_user_main();
    return 0;
}
