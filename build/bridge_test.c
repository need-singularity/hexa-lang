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

long* bridge_new(const char* anima_path);
double get_phi(long *bridge);
const char* get_laws(long *bridge);
const char* parse_laws_json(const char* content);
const char* suggest_experiment(double phi);
long print_status(long *bridge);

static const char* default_anima_path = "../anima";
static double phi_low = 0.5;
static double phi_mod = 1.0;
static double phi_high = 2.0;
static hexa_arr args;

long* bridge_new(const char* anima_path) {
    const char* test_path = hexa_concat(anima_path, "/");
    long connected = 0;
    const char* probe = hexa_read_file(hexa_concat(anima_path, "/consciousness_laws.json"));
    if ((strcmp(probe, "") != 0)) {
        connected = 1;
    }
    if ((!connected)) {
        const char* phi_probe = hexa_read_file(hexa_concat(anima_path, "/results/phi_latest.txt"));
        if ((strcmp(phi_probe, "") != 0)) {
            connected = 1;
        }
    }
    if ((!connected)) {
        const char* phi_probe2 = hexa_read_file(hexa_concat(anima_path, "/phi_latest.txt"));
        if ((strcmp(phi_probe2, "") != 0)) {
            connected = 1;
        }
    }
    return hexa_struct_alloc((long[]){(long)(anima_path), (long)(connected)}, 2);
}

double get_phi(long *bridge) {
    const char* candidates_0 = hexa_concat(hexa_int_to_str((long)(bridge[0])), "/results/phi_latest.txt");
    const char* candidates_1 = hexa_concat(hexa_int_to_str((long)(bridge[0])), "/results/phi.txt");
    const char* candidates_2 = hexa_concat(hexa_int_to_str((long)(bridge[0])), "/phi_latest.txt");
    const char* candidates_3 = hexa_concat(hexa_int_to_str((long)(bridge[0])), "/output/phi.txt");
    const char* c0 = hexa_read_file(candidates_0);
    if ((strcmp(c0, "") != 0)) {
        double v = hexa_to_float(hexa_trim(c0));
        if ((v != 0.0)) {
            return v;
        }
    }
    const char* c1 = hexa_read_file(candidates_1);
    if ((strcmp(c1, "") != 0)) {
        double v = hexa_to_float(hexa_trim(c1));
        if ((v != 0.0)) {
            return v;
        }
    }
    const char* c2 = hexa_read_file(candidates_2);
    if ((strcmp(c2, "") != 0)) {
        double v = hexa_to_float(hexa_trim(c2));
        if ((v != 0.0)) {
            return v;
        }
    }
    const char* c3 = hexa_read_file(candidates_3);
    if ((strcmp(c3, "") != 0)) {
        double v = hexa_to_float(hexa_trim(c3));
        if ((v != 0.0)) {
            return v;
        }
    }
    return (-1.0);
}

const char* get_laws(long *bridge) {
    const char* json_path = hexa_concat(hexa_int_to_str((long)(bridge[0])), "/consciousness_laws.json");
    const char* json_content = hexa_read_file(json_path);
    if ((strcmp(json_content, "") != 0)) {
        return parse_laws_json(json_content);
    }
    const char* txt_path = hexa_concat(hexa_int_to_str((long)(bridge[0])), "/consciousness_laws.txt");
    const char* txt_content = hexa_read_file(txt_path);
    if ((strcmp(txt_content, "") != 0)) {
        return txt_content;
    }
    return "";
}

const char* parse_laws_json(const char* content) {
    const char* result = "";
    long in_string = 0;
    long escape = 0;
    const char* current = "";
    long i = 0;
    while ((i < hexa_str_len(content))) {
        long ch = /* unsupported method .char_at */ 0;
        if (escape) {
            current = hexa_concat(current, hexa_int_to_str((long)(ch)));
            escape = 0;
            i = (i + 1);
        } else {
            if (((ch == "\\") && in_string)) {
                escape = 1;
                i = (i + 1);
            } else {
                if ((ch == "\"")) {
                    if (in_string) {
                        if ((hexa_str_len(current) > 3)) {
                            if ((strcmp(result, "") != 0)) {
                                result = hexa_concat(result, "\n");
                            }
                            result = hexa_concat(result, current);
                        }
                        current = "";
                        in_string = 0;
                    } else {
                        in_string = 1;
                    }
                    i = (i + 1);
                } else {
                    if (in_string) {
                        current = hexa_concat(current, hexa_int_to_str((long)(ch)));
                    }
                    i = (i + 1);
                }
            }
        }
    }
    return result;
}

const char* suggest_experiment(double phi) {
    if ((phi < phi_low)) {
        return "Low Phi detected. Suggest: increase integration via cross-domain lens scan (consciousness + topology + causal).";
    } else {
        if ((phi < phi_mod)) {
            return "Moderate Phi. Suggest: run OUROBOROS evolution with consciousness seed to amplify integration.";
        } else {
            if ((phi < phi_high)) {
                return "High Phi. Suggest: deep scan with quantum_microscope + recursion lenses to find hidden sub-structures.";
            } else {
                return hexa_concat(hexa_concat("Very high Phi (", hexa_float_to_str((double)(phi))), "). Suggest: full 22-lens scan to map the complete integration landscape.");
            }
        }
    }
}

long print_status(long *bridge) {
    printf("%s\n", "=== consciousness bridge (hexa-native) ===");
    printf("%s %ld\n", "  anima_path :", (long)(bridge[0]));
    printf("%s %ld\n", "  connected  :", (long)(bridge[1]));
    double phi = get_phi(bridge);
    if ((phi > 0.0)) {
        printf("%s %g\n", "  phi_latest :", (double)(phi));
        printf("%s %s\n", "  suggestion :", suggest_experiment(phi));
    } else {
        printf("%s\n", "  phi_latest : (not available)");
    }
    const char* laws = get_laws(bridge);
    if ((strcmp(laws, "") != 0)) {
        printf("%s\n", "  laws       :");
        return printf("%s\n", laws);
    } else {
        return printf("%s\n", "  laws       : (none loaded)");
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    args = hexa_args();
    if ((args.n < 3)) {
        printf("%s\n", "consciousness bridge (hexa-native)");
        printf("%s\n", "");
        printf("%s\n", "  hexa bridge.hexa status [anima_path]");
        printf("%s\n", "  hexa bridge.hexa phi [anima_path]");
        printf("%s\n", "  hexa bridge.hexa laws [anima_path]");
        printf("%s\n", "  hexa bridge.hexa suggest <phi_value>");
    } else {
        const char* cmd = ((const char*)args.d[2]);
        const char* path = (((args.n >= 4)) ? (((const char*)args.d[3])) : (default_anima_path));
        if ((strcmp(cmd, "status") == 0)) {
            long* bridge = bridge_new(path);
            print_status(bridge);
        } else {
            if ((strcmp(cmd, "phi") == 0)) {
                long* bridge = bridge_new(path);
                double phi = get_phi(bridge);
                if ((phi > 0.0)) {
                    printf("%s %g\n", "phi =", (double)(phi));
                } else {
                    printf("%s\n", "phi: not available (no phi_latest.txt found)");
                }
            } else {
                if ((strcmp(cmd, "laws") == 0)) {
                    long* bridge = bridge_new(path);
                    const char* laws = get_laws(bridge);
                    if ((strcmp(laws, "") != 0)) {
                        printf("%s\n", "=== consciousness laws ===");
                        printf("%s\n", laws);
                    } else {
                        printf("%s\n", "no laws file found");
                    }
                } else {
                    if ((strcmp(cmd, "suggest") == 0)) {
                        double phi_val = (((args.n >= 4)) ? (hexa_to_float(((const char*)args.d[3]))) : (0.0));
                        printf("%s %g\n", "phi =", (double)(phi_val));
                        printf("%s\n", suggest_experiment(phi_val));
                    } else {
                        printf("%s %s\n", "unknown command:", cmd);
                        printf("%s\n", "  usage: hexa bridge.hexa {status|phi|laws|suggest}");
                    }
                }
            }
        }
    }
    return 0;
}
