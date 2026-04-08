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

long* response_ok(const char* body);
long* response_not_found();
long* response_bad_request(const char* msg);
const char* extract_json_value(const char* json, const char* key);
const char* n6_match(double value);
long* handle_health();
long* handle_version();
long* handle_constants();
long* handle_lenses();
long* handle_lens_count();
long* handle_verify(const char* body);
long* handle_scan(const char* body);
long* handle_request(const char* method, const char* path, const char* body);
long process_file_request();
long hexa_user_main();

static long api_port = 8080;
static const char* api_version = "0.1.0";
static long n6_n = 6;
static long n6_sigma = 12;
static long n6_phi = 2;
static long n6_tau = 4;
static long n6_j2 = 24;
static long n6_sopfr = 5;
static long n6_mu = 1;
static long n6_sigma_phi = 10;
static long n6_sigma_tau = 8;

long* response_ok(const char* body) {
    return hexa_struct_alloc((long[]){(long)(200), (long)(body)}, 2);
}

long* response_not_found() {
    return hexa_struct_alloc((long[]){(long)(404), (long)("{\"error\":\"not found\"}")}, 2);
}

long* response_bad_request(const char* msg) {
    return hexa_struct_alloc((long[]){(long)(400), (long)(hexa_concat(hexa_concat("{\"error\":\"", msg), "\"}"))}, 2);
}

const char* extract_json_value(const char* json, const char* key) {
    const char* pattern = hexa_concat(hexa_concat("\"", key), "\"");
    if ((!hexa_contains(json, pattern))) {
        return "";
    }
    hexa_arr after_key = hexa_split(json, pattern);
    if ((after_key.n < 2)) {
        return "";
    }
    const char* rest = hexa_trim(((const char*)after_key.d[1]));
    if ((!hexa_starts_with(rest, ":"))) {
        return "";
    }
    const char* after_colon = hexa_trim(hexa_substr(rest, 1, hexa_str_len(rest)));
    if (hexa_starts_with(after_colon, "\"")) {
        const char* inner = hexa_substr(after_colon, 1, hexa_str_len(after_colon));
        long end = hexa_index_of(inner, "\"");
        if ((end < 0)) {
            return "";
        }
        return hexa_substr(inner, 0, end);
    }
    long end = hexa_index_of(after_colon, ",");
    if ((end < 0)) {
        end = hexa_index_of(after_colon, "}");
    }
    if ((end < 0)) {
        end = hexa_index_of(after_colon, "]");
    }
    if ((end < 0)) {
        end = hexa_str_len(after_colon);
    }
    return hexa_trim(hexa_substr(after_colon, 0, end));
}

const char* n6_match(double value) {
    hexa_arr targets = hexa_arr_lit((long[]){6.0, 12.0, 24.0, 48.0, 144.0}, 5);
    hexa_arr names = hexa_arr_lit((long[]){(long)("n(6)"), (long)("sigma(12)"), (long)("J2(24)"), (long)("sigma*tau(48)"), (long)("sigma^2(144)")}, 5);
    double v_abs = (((value < 0.0)) ? ((0.0 - value)) : (value));
    double min_dist = 999999.0;
    const char* closest = "";
    double closest_target = 6.0;
    long i = 0;
    while ((i < targets.n)) {
        double d = (v_abs - targets.d[i]);
        double abs_d = (((d < 0.0)) ? ((0.0 - d)) : (d));
        if ((abs_d < min_dist)) {
            min_dist = abs_d;
            closest = ((const char*)names.d[i]);
            closest_target = targets.d[i];
        }
        i = (i + 1);
    }
    double quality = (((closest_target > 0.0)) ? ((1.0 - (min_dist / closest_target))) : (0.0));
    double clamped = (((quality < 0.0)) ? (0.0) : (quality));
    return hexa_concat(hexa_concat(closest, "|"), hexa_float_to_str((double)(clamped)));
}

long* handle_health() {
    return response_ok(hexa_concat(hexa_concat("{\"status\":\"ok\",\"engine\":\"nexus\",\"port\":", hexa_int_to_str((long)(api_port))), "}"));
}

long* handle_version() {
    return response_ok(hexa_concat(hexa_concat("{\"version\":\"", api_version), "\",\"n\":6,\"sigma\":12,\"phi\":2,\"tau\":4}"));
}

long* handle_constants() {
    return response_ok("{\"n\":6,\"sigma\":12,\"phi\":2,\"tau\":4,\"J2\":24,\"sopfr\":5,\"mu\":1,\"sigma_phi\":10,\"sigma_tau\":8}");
}

long* handle_lenses() {
    hexa_arr lenses = hexa_arr_lit((long[]){(long)("consciousness"), (long)("topology"), (long)("causal"), (long)("quantum"), (long)("wave"), (long)("evolution"), (long)("thermo"), (long)("gravity"), (long)("symmetry"), (long)("resonance"), (long)("fractal"), (long)("emergence"), (long)("information"), (long)("entropy"), (long)("complexity"), (long)("network"), (long)("phase"), (long)("dimension"), (long)("tensor"), (long)("spectral"), (long)("algebraic"), (long)("meta_transcendence")}, 22);
    const char* json_list = "";
    long i = 0;
    while ((i < lenses.n)) {
        if ((strcmp(json_list, "") != 0)) {
            json_list = hexa_concat(json_list, ",");
        }
        json_list = hexa_concat(hexa_concat(hexa_concat(json_list, "\""), ((const char*)lenses.d[i])), "\"");
        i = (i + 1);
    }
    return response_ok(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"total\":", hexa_int_to_str((long)(lenses.n))), ",\"lenses\":["), json_list), "]}"));
}

long* handle_lens_count() {
    return response_ok("{\"total\":22,\"core\":6,\"extended\":10,\"custom\":6}");
}

long* handle_verify(const char* body) {
    const char* value_str = extract_json_value(body, "value");
    const char* val_input = value_str;
    if ((strcmp(val_input, "") == 0)) {
        val_input = hexa_replace(hexa_trim(body), "\"", "");
    }
    double value = 0.0;
    value = hexa_to_float(val_input);
    const char* match_result = n6_match(value);
    hexa_arr parts = hexa_split(match_result, "|");
    const char* closest = ((const char*)parts.d[0]);
    double quality = 0.0;
    if ((parts.n > 1)) {
        quality = hexa_to_float(((const char*)parts.d[1]));
    }
    long n6_matches = (((quality >= 1.0)) ? (1) : (0));
    double distance = (((quality > 0.0)) ? ((1.0 - quality)) : (1.0));
    return response_ok(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"value\":", hexa_float_to_str((double)(value))), ",\"n6_matches\":"), hexa_int_to_str((long)(n6_matches))), ",\"closest\":\""), closest), "\",\"distance\":"), hexa_float_to_str((double)(distance))), "}"));
}

long* handle_scan(const char* body) {
    const char* domain = extract_json_value(body, "domain");
    const char* d = domain;
    if ((strcmp(d, "") == 0)) {
        d = hexa_replace(hexa_trim(body), "\"", "");
    }
    if ((strcmp(d, "") == 0)) {
        return response_bad_request("missing domain");
    }
    long hits = 6;
    long lens_count = 22;
    return response_ok(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"domain\":\"", d), "\",\"hits\":"), hexa_int_to_str((long)(hits))), ",\"lens_count\":"), hexa_int_to_str((long)(lens_count))), "}"));
}

long* handle_request(const char* method, const char* path, const char* body) {
    if ((strcmp(method, "GET") == 0)) {
        if ((strcmp(path, "/health") == 0)) {
            return handle_health();
        }
        if ((strcmp(path, "/version") == 0)) {
            return handle_version();
        }
        if ((strcmp(path, "/constants") == 0)) {
            return handle_constants();
        }
        if ((strcmp(path, "/lenses") == 0)) {
            return handle_lenses();
        }
        if ((strcmp(path, "/lenses/count") == 0)) {
            return handle_lens_count();
        }
    }
    if ((strcmp(method, "POST") == 0)) {
        if ((strcmp(path, "/verify") == 0)) {
            return handle_verify(body);
        }
        if ((strcmp(path, "/scan") == 0)) {
            return handle_scan(body);
        }
    }
    return response_not_found();
}

long process_file_request() {
    const char* raw = hexa_read_file("shared/api_request.json");
    if ((strcmp(raw, "") == 0)) {
        printf("%s", "[api] no request file found (shared/api_request.json)");
        return 0;
    }
    const char* method = extract_json_value(raw, "method");
    const char* path = extract_json_value(raw, "path");
    const char* body = extract_json_value(raw, "body");
    long* resp = handle_request(method, path, body);
    const char* resp_json = hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"status\":", hexa_int_to_str((long)(resp[0]))), ",\"body\":"), hexa_int_to_str((long)(resp[1]))), "}");
    hexa_write_file("shared/api_response.json", resp_json);
    return printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("[api] ", method), " "), path), " -> "), hexa_int_to_str((long)(resp[0]))));
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "api.hexa — RESTful API (파일 기반)");
        printf("%s", "Usage:");
        printf("%s", "  handle <method> <path> [body]   요청 처리");
        printf("%s", "  health                          헬스 체크");
        printf("%s", "  version                         버전 정보");
        printf("%s", "  constants                       n=6 상수");
        printf("%s", "  lenses                          렌즈 목록");
        printf("%s", "  verify <value>                  값 검증");
        printf("%s", "  scan <domain>                   스캔");
        printf("%s", "  process                         파일 기반 처리");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "handle") == 0)) {
        if ((a.n < 4)) {
            printf("%s", "Usage: handle <method> <path> [body]");
            return 0;
        }
        const char* method = ((const char*)a.d[2]);
        const char* path = ((const char*)a.d[3]);
        const char* body = (((a.n > 4)) ? (((const char*)a.d[4])) : (""));
        long* resp = handle_request(method, path, body);
        printf("%s", hexa_concat(hexa_concat(hexa_concat("[", hexa_int_to_str((long)(resp[0]))), "] "), hexa_int_to_str((long)(resp[1]))));
    }
    if ((strcmp(cmd, "health") == 0)) {
        long* resp = handle_health();
        printf("%ld", (long)(resp[1]));
    }
    if ((strcmp(cmd, "version") == 0)) {
        long* resp = handle_version();
        printf("%ld", (long)(resp[1]));
    }
    if ((strcmp(cmd, "constants") == 0)) {
        long* resp = handle_constants();
        printf("%ld", (long)(resp[1]));
    }
    if ((strcmp(cmd, "lenses") == 0)) {
        long* resp = handle_lenses();
        printf("%ld", (long)(resp[1]));
    }
    if ((strcmp(cmd, "verify") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: verify <value>");
            return 0;
        }
        long* resp = handle_verify(hexa_concat(hexa_concat("{\"value\":", ((const char*)a.d[2])), "}"));
        printf("%ld", (long)(resp[1]));
    }
    if ((strcmp(cmd, "scan") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "Usage: scan <domain>");
            return 0;
        }
        long* resp = handle_scan(hexa_concat(hexa_concat("{\"domain\":\"", ((const char*)a.d[2])), "\"}"));
        printf("%ld", (long)(resp[1]));
    }
    if ((strcmp(cmd, "process") == 0)) {
        return process_file_request();
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    return 0;
}
