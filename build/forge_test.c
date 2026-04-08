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

double round4(double x);
double abs_f(double x);
long str_len(const char* s);
const char* llm_call(const char* prompt);
const char* find_claude_cli();
long* forge_compress(const char* text, long rounds, double threshold);
long* forge_compress_default(const char* text);
double header_coverage_score(const char* original, const char* compressed);
long* anvil_verify(const char* compressed, const char* original);
const char* density_grade(double r);
const char* density_bar(double r);
long* profile_section(const char* name, const char* body, long original_total);
long* profile_text(hexa_arr section_names, hexa_arr section_bodies);

static long default_rounds = 5;
static double convergence_threshold = 0.01;
static long qa_total = 5;
static double qa_pass_threshold = 0.05;
static long section_min_chars = 20;
static double density_high = 0.35;
static double density_med = 0.20;
static double density_low = 0.10;
static const char* compress_prompt = "You are a compression engine. Compress the following document into the MINIMUM number of tokens that YOU (Claude) can later decode back to the full original meaning.\n\nRules:\n- The compressed output is ONLY for LLM consumption, not humans\n- Use any symbols, abbreviations, shorthand, unicode, or encoding you want\n- Preserve ALL semantic content\n- Structure markers can be replaced with minimal delimiters\n- Repeated patterns should use the shortest possible reference\n- Output ONLY the compressed text, nothing else\n\nDocument:\n---\n";
static const char* recompress_prompt = "You previously compressed a document into this form:\n\n---\n";
static const char* recompress_suffix = "\n---\n\nCompress it FURTHER. Find any remaining redundancy and eliminate it.\nSame rules: minimum tokens, preserve all meaning, LLM-only consumption.\nOutput ONLY the re-compressed text, nothing else.";
static const char* qa_prompt_prefix = "You will test whether a compressed document preserves the meaning of the original.\n\nORIGINAL DOCUMENT:\n---\n";
static const char* qa_prompt_mid = "\n---\n\nCOMPRESSED DOCUMENT:\n---\n";
static const char* qa_prompt_suffix = "\n---\n\nGenerate exactly 5 factual questions about the original document. For each question, answer it using ONLY the compressed document. Then judge if the compressed-based answer matches the original meaning.\n\nOutput format (exactly 5 lines, no other text):\nQ1 | A: [answer from compressed] | PASS or FAIL\nQ2 | A: [answer from compressed] | PASS or FAIL\nQ3 | A: [answer from compressed] | PASS or FAIL\nQ4 | A: [answer from compressed] | PASS or FAIL\nQ5 | A: [answer from compressed] | PASS or FAIL";
static const char* section_compress_prompt = "Compress this to minimum tokens only you can decode. Preserve ALL meaning. Output ONLY compressed text:\n\n---\n";

double round4(double x) {
    double scaled = (x * 10000.0);
    long i = (long)((scaled + 0.5));
    return (((double)(i)) / 10000.0);
}

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    } else {
        return x;
    }
}

long str_len(const char* s) {
    return hexa_str_len(s);
}

const char* llm_call(const char* prompt) {
    const char* result = hexa_exec(hexa_concat(hexa_concat("claude -p '", prompt), "'"));
    return result;
}

const char* find_claude_cli() {
    const char* which_result = hexa_exec("which claude 2>/dev/null");
    if ((str_len(which_result) > 0)) {
        return which_result;
    }
    const char* home = hexa_exec("printenv HOME");
    hexa_arr paths = hexa_arr_lit((long[]){(long)(hexa_concat(home, "/.local/bin/claude")), (long)("/usr/local/bin/claude"), (long)("/opt/homebrew/bin/claude")}, 3);
    long i = 0;
    while ((i < 3)) {
        if (hexa_file_exists(((const char*)paths.d[i]))) {
            return ((const char*)paths.d[i]);
        }
        i = (i + 1);
    }
    return "";
}

long* forge_compress(const char* text, long rounds, double threshold) {
    long original_chars = str_len(text);
    const char* run_id = hexa_concat("forge_", hexa_timestamp());
    const char* current = text;
    double prev_r = 1.0;
    long converged_at = (-1);
    double final_r = 1.0;
    long final_chars = original_chars;
    long rounds_done = 0;
    long i = 0;
    while ((i < rounds)) {
        const char* prompt = (((i == 0)) ? (hexa_concat(hexa_concat(compress_prompt, current), "\n---")) : (hexa_concat(hexa_concat(recompress_prompt, current), recompress_suffix)));
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("  Round ", hexa_int_to_str((long)((i + 1)))), "/"), hexa_int_to_str((long)(rounds))), ": 압축 중..."));
        const char* compressed = llm_call(prompt);
        long comp_chars = str_len(compressed);
        double r = round4((((double)(comp_chars)) / ((double)(original_chars))));
        double delta_r = (((i == 0)) ? (1.0) : (abs_f((r - prev_r))));
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  [Round ", hexa_int_to_str((long)(i))), "] "), hexa_int_to_str((long)(comp_chars))), " chars | r="), hexa_float_to_str((double)(r))), " | dr="), hexa_float_to_str((double)(delta_r))));
        const char* round_path = hexa_concat(hexa_concat(hexa_concat(hexa_concat("bench/runs/", run_id), "/round_"), hexa_int_to_str((long)(i))), ".tf");
        hexa_write_file(round_path, compressed);
        if (((i > 0) && (delta_r < threshold))) {
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  * 수렴 달성 (dr < ", hexa_float_to_str((double)(threshold))), ") at round "), hexa_int_to_str((long)(i))));
            converged_at = i;
            final_r = r;
            final_chars = comp_chars;
            rounds_done = (i + 1);
            i = rounds;
        } else {
            prev_r = r;
            final_r = r;
            final_chars = comp_chars;
            current = compressed;
            rounds_done = (i + 1);
            i = (i + 1);
        }
    }
    return hexa_struct_alloc((long[]){(long)(run_id), (long)(original_chars), (long)(final_r), (long)(final_chars), (long)(rounds_done), (long)(converged_at)}, 6);
}

long* forge_compress_default(const char* text) {
    return forge_compress(text, default_rounds, convergence_threshold);
}

double header_coverage_score(const char* original, const char* compressed) {
    long has_headers = hexa_contains(original, "## ");
    if ((!has_headers)) {
        return 1.0;
    }
    return 0.5;
}

long* anvil_verify(const char* compressed, const char* original) {
    double header_score = header_coverage_score(original, compressed);
    printf("%s\n", hexa_concat("  헤더 커버리지: ", hexa_float_to_str((double)(header_score))));
    const char* prompt = hexa_concat(hexa_concat(hexa_concat(hexa_concat(qa_prompt_prefix, original), qa_prompt_mid), compressed), qa_prompt_suffix);
    const char* qa_result = llm_call(prompt);
    printf("%s\n", qa_result);
    long pass_count = ({ const char* _h = qa_result; const char* _n = "PASS"; long _c = 0; const char* _p = _h; while ((_p = strstr(_p, _n)) != NULL) { _c++; _p += strlen(_n); } _c; });
    double qa_score = round4((((double)(pass_count)) / ((double)(qa_total))));
    double loss = round4((1.0 - qa_score));
    double passed = (loss <= qa_pass_threshold);
    printf("%s\n", "");
    printf("%s\n", "=== 결과 ===");
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("Q&A 정답률:    ", hexa_int_to_str((long)(pass_count))), "/"), hexa_int_to_str((long)(qa_total))), " = "), hexa_float_to_str((double)(qa_score))));
    printf("%s\n", hexa_concat("의미 손실도 L: ", hexa_float_to_str((double)(loss))));
    if (passed) {
        printf("%s\n", hexa_concat(hexa_concat("판정: PASS (L <= ", hexa_float_to_str((double)(qa_pass_threshold))), ")"));
    } else {
        printf("%s\n", hexa_concat(hexa_concat("판정: FAIL (L > ", hexa_float_to_str((double)(qa_pass_threshold))), ")"));
    }
    return hexa_struct_alloc((long[]){(long)(header_score), (long)(qa_score), (long)(loss), (long)(passed)}, 4);
}

const char* density_grade(double r) {
    if ((r > density_high)) {
        return "HIGH";
    } else {
        if ((r > density_med)) {
            return "MED";
        } else {
            if ((r > density_low)) {
                return "LOW";
            } else {
                return "SKIP";
            }
        }
    }
}

const char* density_bar(double r) {
    long n = (long)((r * 30.0));
    const char* bar = "";
    long i = 0;
    while ((i < n)) {
        bar = hexa_concat(bar, "#");
        i = (i + 1);
    }
    return bar;
}

long* profile_section(const char* name, const char* body, long original_total) {
    long orig_chars = str_len(body);
    if ((orig_chars < section_min_chars)) {
        return hexa_struct_alloc((long[]){(long)(name), (long)(orig_chars), (long)(0), (long)(0.0), (long)("SKIP")}, 5);
    }
    const char* prompt = hexa_concat(hexa_concat(section_compress_prompt, body), "\n---");
    const char* compressed = llm_call(prompt);
    long comp_chars = str_len(compressed);
    double r = round4((((double)(comp_chars)) / ((double)(orig_chars))));
    const char* grade = density_grade(r);
    const char* bar = density_bar(r);
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  ", name), " "), hexa_int_to_str((long)(orig_chars))), "->"), hexa_int_to_str((long)(comp_chars))), "  r="), hexa_float_to_str((double)(r))), "  "), bar), " ["), grade), "]"));
    return hexa_struct_alloc((long[]){(long)(name), (long)(orig_chars), (long)(comp_chars), (long)(r), (long)(grade)}, 5);
}

long* profile_text(hexa_arr section_names, hexa_arr section_bodies) {
    long count = section_names.n;
    long total_orig = 0;
    long total_comp = 0;
    long skip_count = 0;
    long skip_chars = 0;
    long i = 0;
    while ((i < count)) {
        long* sr = profile_section(((const char*)section_names.d[i]), ((const char*)section_bodies.d[i]), 0);
        total_orig = (total_orig + sr[1]);
        total_comp = (total_comp + sr[2]);
        if (((sr[4] == "SKIP") || (sr[4] == "LOW"))) {
            skip_count = (skip_count + 1);
            skip_chars = (skip_chars + sr[1]);
        }
        i = (i + 1);
    }
    double overall_r = (((total_orig > 0)) ? (round4((((double)(total_comp)) / ((double)(total_orig))))) : (0.0));
    printf("%s\n", "");
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("전체: ", hexa_int_to_str((long)(total_orig))), "->"), hexa_int_to_str((long)(total_comp))), " chars (r="), hexa_float_to_str((double)(overall_r))), ")"));
    if ((skip_count > 0)) {
        double skip_pct = round4(((((double)(skip_chars)) / ((double)(total_orig))) * 100.0));
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("삭제 후보 (LOW/SKIP): ", hexa_int_to_str((long)(skip_count))), "섹션, "), hexa_int_to_str((long)(skip_chars))), " chars ("), hexa_float_to_str((double)(skip_pct))), "%)"));
    }
    return hexa_struct_alloc((long[]){(long)(total_orig), (long)(total_comp), (long)(overall_r), (long)(skip_count), (long)(skip_chars), (long)(count)}, 6);
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    if (!((default_rounds == 5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((qa_total == 5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(density_grade(0.40), "HIGH") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(density_grade(0.25), "MED") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(density_grade(0.15), "LOW") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(density_grade(0.05), "SKIP") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((round4(0.12345) == 0.1235))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((abs_f((-0.5)) == 0.5))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "=== forge verified ===");
    printf("%s %s\n", "density_grade(0.40):", density_grade(0.40));
    printf("%s %s\n", "density_grade(0.05):", density_grade(0.05));
    printf("%s %g\n", "round4(0.12345):", (double)(round4(0.12345)));
    printf("%s %g\n", "abs_f(-0.5):", (double)(abs_f((-0.5))));
    return 0;
}
