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

double lattice_constant(const char* el);
double approx_mass(const char* el);
double estimate_lattice(hexa_arr elements);
const char* infer_structure(const char* sg);
hexa_arr filter_candidates(hexa_arr candidates);
hexa_arr rank_candidates(hexa_arr candidates);
const char* generate_poscar(long *c);
const char* poscar_perovskite(long *c, double a, const char* el_line);
const char* poscar_spinel(long *c, double a, const char* el_line);
const char* poscar_rocksalt(long *c, double a, const char* el_line);
const char* poscar_generic(long *c, double a, const char* el_line);
const char* generate_qe_input(long *c);
const char* repeat_str(const char* s, long n);
const char* join(hexa_arr arr, const char* sep);
const char* lowercase(const char* s);
long* parse_candidate_json(const char* json_str);
const char* json_extract_string(const char* json, const char* key);
hexa_arr json_extract_string_array(const char* json, const char* key);
long hexa_user_main();

static long N = 6;
static long sigma = 12;
static long phi = 2;
static long tau = 4;
static double ang_to_bohr = 1.8897259886;
static double default_ecutwfc = 60.0;
static double default_lattice = 4.0;
static long* test_c;
static double lat;
static const char* poscar;
static long* unstable;
static hexa_arr filtered;
static hexa_arr ranked;

double lattice_constant(const char* el) {
    if ((strcmp(el, "Li") == 0)) {
        return 3.51;
    } else if ((strcmp(el, "Na") == 0)) {
        return 4.29;
    } else if ((strcmp(el, "O") == 0)) {
        return 2.46;
    } else if ((strcmp(el, "S") == 0)) {
        return 3.13;
    } else if ((strcmp(el, "Fe") == 0)) {
        return 2.87;
    } else if ((strcmp(el, "Co") == 0)) {
        return 2.51;
    } else if ((strcmp(el, "Ni") == 0)) {
        return 3.52;
    } else if ((strcmp(el, "Mn") == 0)) {
        return 8.91;
    } else if ((strcmp(el, "Ti") == 0)) {
        return 2.95;
    } else if ((strcmp(el, "Zr") == 0)) {
        return 3.23;
    } else if ((strcmp(el, "Al") == 0)) {
        return 4.05;
    } else if ((strcmp(el, "Si") == 0)) {
        return 5.43;
    } else if ((strcmp(el, "Cu") == 0)) {
        return 3.61;
    } else if ((strcmp(el, "Zn") == 0)) {
        return 2.66;
    } else if ((strcmp(el, "Mg") == 0)) {
        return 3.21;
    } else if ((strcmp(el, "Ca") == 0)) {
        return 5.58;
    } else if ((strcmp(el, "Ba") == 0)) {
        return 5.02;
    } else if ((strcmp(el, "Sr") == 0)) {
        return 6.08;
    } else if ((strcmp(el, "La") == 0)) {
        return 5.30;
    } else if ((strcmp(el, "Ce") == 0)) {
        return 5.16;
    } else if ((strcmp(el, "P") == 0)) {
        return 3.31;
    } else if ((strcmp(el, "Se") == 0)) {
        return 4.36;
    } else if ((strcmp(el, "Te") == 0)) {
        return 4.45;
    } else {
        return 0.0;
    }






















}

double approx_mass(const char* el) {
    if ((strcmp(el, "H") == 0)) {
        return 1.008;
    } else if ((strcmp(el, "Li") == 0)) {
        return 6.941;
    } else if ((strcmp(el, "Be") == 0)) {
        return 9.012;
    } else if ((strcmp(el, "B") == 0)) {
        return 10.81;
    } else if ((strcmp(el, "C") == 0)) {
        return 12.011;
    } else if ((strcmp(el, "N") == 0)) {
        return 14.007;
    } else if ((strcmp(el, "O") == 0)) {
        return 15.999;
    } else if ((strcmp(el, "F") == 0)) {
        return 18.998;
    } else if ((strcmp(el, "Na") == 0)) {
        return 22.990;
    } else if ((strcmp(el, "Mg") == 0)) {
        return 24.305;
    } else if ((strcmp(el, "Al") == 0)) {
        return 26.982;
    } else if ((strcmp(el, "Si") == 0)) {
        return 28.086;
    } else if ((strcmp(el, "P") == 0)) {
        return 30.974;
    } else if ((strcmp(el, "S") == 0)) {
        return 32.065;
    } else if ((strcmp(el, "Cl") == 0)) {
        return 35.453;
    } else if ((strcmp(el, "K") == 0)) {
        return 39.098;
    } else if ((strcmp(el, "Ca") == 0)) {
        return 40.078;
    } else if ((strcmp(el, "Ti") == 0)) {
        return 47.867;
    } else if ((strcmp(el, "V") == 0)) {
        return 50.942;
    } else if ((strcmp(el, "Cr") == 0)) {
        return 51.996;
    } else if ((strcmp(el, "Mn") == 0)) {
        return 54.938;
    } else if ((strcmp(el, "Fe") == 0)) {
        return 55.845;
    } else if ((strcmp(el, "Co") == 0)) {
        return 58.933;
    } else if ((strcmp(el, "Ni") == 0)) {
        return 58.693;
    } else if ((strcmp(el, "Cu") == 0)) {
        return 63.546;
    } else if ((strcmp(el, "Zn") == 0)) {
        return 65.38;
    } else if ((strcmp(el, "Ga") == 0)) {
        return 69.723;
    } else if ((strcmp(el, "Ge") == 0)) {
        return 72.63;
    } else if ((strcmp(el, "Se") == 0)) {
        return 78.971;
    } else if ((strcmp(el, "Sr") == 0)) {
        return 87.62;
    } else if ((strcmp(el, "Zr") == 0)) {
        return 91.224;
    } else if ((strcmp(el, "Nb") == 0)) {
        return 92.906;
    } else if ((strcmp(el, "Mo") == 0)) {
        return 95.95;
    } else if ((strcmp(el, "Ba") == 0)) {
        return 137.327;
    } else if ((strcmp(el, "La") == 0)) {
        return 138.905;
    } else if ((strcmp(el, "Ce") == 0)) {
        return 140.116;
    } else if ((strcmp(el, "Te") == 0)) {
        return 127.60;
    } else {
        return 50.0;
    }




































}

double estimate_lattice(hexa_arr elements) {
    double sum = 0.0;
    long count = 0;
    long i = 0;
    while ((i < elements.n)) {
        double a = lattice_constant(elements.d[i]);
        if ((a > 0.0)) {
            sum = (sum + a);
            count = (count + 1);
        }
        i = (i + 1);
    }
    if ((count > 0)) {
        return (sum / count);
    } else {
        return default_lattice;
    }
}

const char* infer_structure(const char* sg) {
    if ((strcmp(sg, "Pm-3m") == 0)) {
        return "perovskite";
    } else if ((strcmp(sg, "221") == 0)) {
        return "perovskite";
    } else if ((strcmp(sg, "Fd-3m") == 0)) {
        return "spinel";
    } else if ((strcmp(sg, "227") == 0)) {
        return "spinel";
    } else if ((strcmp(sg, "Fm-3m") == 0)) {
        return "rocksalt";
    } else if ((strcmp(sg, "225") == 0)) {
        return "rocksalt";
    } else {
        return "generic";
    }





}

hexa_arr filter_candidates(hexa_arr candidates) {
    hexa_arr result = hexa_arr_new();
    long i = 0;
    while ((i < candidates.n)) {
        long c = candidates.d[i];
        if ((((/* unknown field predicted_formation_energy */ 0 < 0.0) && (/* unknown field predicted_bandgap */ 0 >= 0.0)) && (hexa_str_len(/* unknown field elements */ 0) > 0))) {
            result = hexa_arr_push(result, c);
        }
        i = (i + 1);
    }
    return result;
}

hexa_arr rank_candidates(hexa_arr candidates) {
    hexa_arr ranked = hexa_arr_new();
    long i = 0;
    while ((i < candidates.n)) {
        long c = candidates.d[i];
        double score = (/* unknown field predicted_formation_energy */ 0 - (0.5 * /* unknown field predicted_bandgap */ 0));
        ranked = hexa_arr_push(ranked, hexa_struct_alloc((long[]){(long)(i), (long)(score)}, 2));
        i = (i + 1);
    }
    long j = 0;
    while ((j < ranked.n)) {
        long min_idx = j;
        long k = (j + 1);
        while ((k < ranked.n)) {
            if ((/* unknown field score */ 0 < /* unknown field score */ 0)) {
                min_idx = k;
            }
            k = (k + 1);
        }
        if ((min_idx != j)) {
            long tmp = ranked.d[j];
            ranked.d[j] = ranked.d[min_idx];
            ranked.d[min_idx] = tmp;
        }
        j = (j + 1);
    }
    return ranked;
}

const char* generate_poscar(long *c) {
    double a = estimate_lattice(c[1]);
    const char* stype = infer_structure(c[2]);
    const char* el_line = join(c[1], " ");
    if ((strcmp(stype, "perovskite") == 0)) {
        return poscar_perovskite(c, a, el_line);
    } else if ((strcmp(stype, "spinel") == 0)) {
        return poscar_spinel(c, a, el_line);
    } else if ((strcmp(stype, "rocksalt") == 0)) {
        return poscar_rocksalt(c, a, el_line);
    } else {
        return poscar_generic(c, a, el_line);
    }


}

const char* poscar_perovskite(long *c, double a, const char* el_line) {
    const char* counts = (((hexa_str_len(c[1]) >= 3)) ? ("1  1  3") : (repeat_str("1", hexa_str_len(c[1]))));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(c[0])), " -- perovskite (DFT candidate)\n"), hexa_float_to_str((double)(a))), "\n"), "1.000000  0.000000  0.000000\n"), "0.000000  1.000000  0.000000\n"), "0.000000  0.000000  1.000000\n"), el_line), "\n"), counts), "\n"), "Direct\n"), "0.000000  0.000000  0.000000\n"), "0.500000  0.500000  0.500000\n"), "0.500000  0.500000  0.000000\n"), "0.500000  0.000000  0.500000\n"), "0.000000  0.500000  0.500000\n");
}

const char* poscar_spinel(long *c, double a, const char* el_line) {
    const char* counts = (((hexa_str_len(c[1]) >= 3)) ? ("2  4  8") : (repeat_str("2", hexa_str_len(c[1]))));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(c[0])), " -- spinel (DFT candidate)\n"), hexa_float_to_str((double)(a))), "\n"), "0.500000  0.500000  0.000000\n"), "0.500000  0.000000  0.500000\n"), "0.000000  0.500000  0.500000\n"), el_line), "\n"), counts), "\n"), "Direct\n"), "0.000000  0.000000  0.000000\n"), "0.250000  0.250000  0.250000\n"), "0.625000  0.625000  0.625000\n"), "0.375000  0.375000  0.375000\n");
}

const char* poscar_rocksalt(long *c, double a, const char* el_line) {
    const char* counts = (((hexa_str_len(c[1]) >= 2)) ? ("1  1") : ("2"));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(c[0])), " -- rocksalt (DFT candidate)\n"), hexa_float_to_str((double)(a))), "\n"), "0.500000  0.500000  0.000000\n"), "0.500000  0.000000  0.500000\n"), "0.000000  0.500000  0.500000\n"), el_line), "\n"), counts), "\n"), "Direct\n"), "0.000000  0.000000  0.000000\n"), "0.500000  0.500000  0.500000\n");
}

const char* poscar_generic(long *c, double a, const char* el_line) {
    const char* counts = repeat_str("1", hexa_str_len(c[1]));
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_int_to_str((long)(c[0])), " -- generic (DFT candidate)\n"), hexa_float_to_str((double)(a))), "\n"), "1.000000  0.000000  0.000000\n"), "0.000000  1.000000  0.000000\n"), "0.000000  0.000000  1.000000\n"), el_line), "\n"), counts), "\n"), "Direct\n"), "0.000000  0.000000  0.000000\n");
}

const char* generate_qe_input(long *c) {
    double a_bohr = (estimate_lattice(c[1]) * ang_to_bohr);
    long nat = hexa_str_len(c[1]);
    double ecutwfc = default_ecutwfc;
    double ecutrho = (ecutwfc * 8.0);
    const char* species_lines = "";
    const char* position_lines = "";
    long i = 0;
    while ((i < nat)) {
        long el = c[1][i];
        double mass = approx_mass(el);
        species_lines = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(species_lines, "  "), hexa_int_to_str((long)(el))), "  "), hexa_float_to_str((double)(mass))), "  "), hexa_int_to_str((long)(el))), ".UPF\n");
        double frac = (((nat > 0)) ? ((i / nat)) : (0.0));
        position_lines = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(position_lines, "  "), hexa_int_to_str((long)(el))), "  "), hexa_float_to_str((double)(frac))), "  "), hexa_float_to_str((double)(frac))), "  "), hexa_float_to_str((double)(frac))), "\n");
        i = (i + 1);
    }
    const char* prefix = lowercase(c[0]);
    return hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("&CONTROL\n", "  calculation = 'scf'\n"), "  prefix      = '"), prefix), "'\n"), "  outdir      = './tmp'\n"), "  pseudo_dir  = './pseudo'\n"), "/\n"), "&SYSTEM\n"), "  ibrav  = 0\n"), "  celldm(1) = "), hexa_float_to_str((double)(a_bohr))), "\n"), "  nat    = "), hexa_int_to_str((long)(nat))), "\n"), "  ntyp   = "), hexa_int_to_str((long)(nat))), "\n"), "  ecutwfc = "), hexa_float_to_str((double)(ecutwfc))), "\n"), "  ecutrho = "), hexa_float_to_str((double)(ecutrho))), "\n"), "/\n"), "&ELECTRONS\n"), "  mixing_beta = 0.3\n"), "  conv_thr    = 1.0e-8\n"), "/\n"), "ATOMIC_SPECIES\n"), species_lines), "ATOMIC_POSITIONS {crystal}\n"), position_lines), "K_POINTS {automatic}\n"), "  6 6 6  0 0 0\n"), "CELL_PARAMETERS {bohr}\n"), "  "), hexa_float_to_str((double)(a_bohr))), "  0.000000  0.000000\n"), "  0.000000  "), hexa_float_to_str((double)(a_bohr))), "  0.000000\n"), "  0.000000  0.000000  "), hexa_float_to_str((double)(a_bohr))), "\n");
}

const char* repeat_str(const char* s, long n) {
    const char* result = "";
    long i = 0;
    while ((i < n)) {
        if ((i > 0)) {
            result = hexa_concat(result, "  ");
        }
        result = hexa_concat(result, s);
        i = (i + 1);
    }
    return result;
}

const char* join(hexa_arr arr, const char* sep) {
    const char* result = "";
    long i = 0;
    while ((i < arr.n)) {
        if ((i > 0)) {
            result = hexa_concat(result, sep);
        }
        result = hexa_concat(result, ((const char*)arr.d[i]));
        i = (i + 1);
    }
    return result;
}

const char* lowercase(const char* s) {
    return "";
}

long* parse_candidate_json(const char* json_str) {
    const char* formula = json_extract_string(json_str, "formula");
    const char* spacegroup = json_extract_string(json_str, "spacegroup");
    hexa_arr elements = json_extract_string_array(json_str, "elements");
    double bandgap = 0.0;
    bandgap = hexa_to_float(json_extract_string(json_str, "bandgap"));
    double fe = 0.0;
    fe = hexa_to_float(json_extract_string(json_str, "formation_energy"));
    return hexa_struct_alloc((long[]){(long)(formula), (long)(elements), (long)(spacegroup), (long)(bandgap), (long)(fe)}, 5);
}

const char* json_extract_string(const char* json, const char* key) {
    const char* needle = hexa_concat(hexa_concat("\"", key), "\":");
    long idx = hexa_index_of(json, needle);
    if ((idx < 0)) {
        return "";
    }
    long after = substring(json, (idx + hexa_str_len(needle)), hexa_str_len(json));
    const char* trimmed = hexa_trim(after);
    if (hexa_starts_with(trimmed, "\"")) {
        long end = hexa_index_of(substring(trimmed, 1, hexa_str_len(trimmed)), "\"");
        if ((end >= 0)) {
            return "";
        }
    }
    long end = 0;
    while ((end < hexa_str_len(trimmed))) {
        long ch = char_at(trimmed, end);
        if ((((ch == ",") || (ch == "}")) || (ch == "]"))) {
            break;
        }
        end = (end + 1);
    }
    return hexa_trim(substring(trimmed, 0, end));
}

hexa_arr json_extract_string_array(const char* json, const char* key) {
    const char* needle = hexa_concat(hexa_concat("\"", key), "\":");
    long idx = hexa_index_of(json, needle);
    if ((idx < 0)) {
        return hexa_arr_new();
    }
    long after = substring(json, (idx + hexa_str_len(needle)), hexa_str_len(json));
    long start = hexa_index_of(after, "[");
    long end = hexa_index_of(after, "]");
    if (((start < 0) || (end < 0))) {
        return hexa_arr_new();
    }
    long inner = substring(after, (start + 1), end);
    long parts = split(inner, ",");
    hexa_arr result = hexa_arr_new();
    long i = 0;
    while ((i < hexa_str_len(parts))) {
        const char* s = hexa_trim(replace(replace(parts[i], "\"", ""), "'", ""));
        if ((hexa_str_len(s) > 0)) {
            result = hexa_arr_push(result, (long)(s));
        }
        i = (i + 1);
    }
    return result;
}

long hexa_user_main() {
    hexa_arr argv = hexa_args();
    if ((argv.n < 2)) {
        printf("%s\n", "Usage: hexa materials.hexa <analyze|rank|poscar|qe> <material_json>");
        return 0;
    }
    const char* cmd = ((const char*)argv.d[1]);
    const char* data = (((argv.n >= 3)) ? (((const char*)argv.d[2])) : (""));
    if ((strcmp(cmd, "analyze") == 0)) {
        if ((hexa_str_len(data) == 0)) {
            printf("%s\n", "Error: provide material JSON");
            return 0;
        }
        long* c = parse_candidate_json(data);
        printf("%s %ld\n", "Formula:", (long)(c[0]));
        printf("%s %s\n", "Elements:", join(c[1], ", "));
        printf("%s %s\n", "Structure:", infer_structure(c[2]));
        printf("%s %g\n", "Lattice (A):", (double)(estimate_lattice(c[1])));
        printf("%s %ld\n", "Bandgap (eV):", (long)(c[3]));
        printf("%s %ld\n", "Formation E (eV/atom):", (long)(c[4]));
        double stable = ((c[4] < 0.0) && (c[3] >= 0.0));
        printf("%s %g\n", "Stable:", (double)(stable));
        printf("%s\n", "");
        printf("%s\n", "--- POSCAR ---");
        printf("%s\n", generate_poscar(c));
        printf("%s\n", "--- QE Input ---");
        return printf("%s\n", generate_qe_input(c));
    } else if ((strcmp(cmd, "rank") == 0)) {
        long* c = parse_candidate_json(data);
        double score = (c[4] - (0.5 * c[3]));
        printf("%s %ld\n", "Formula:", (long)(c[0]));
        return printf("%s %g\n", "Score:", (double)(score));
    } else if ((strcmp(cmd, "poscar") == 0)) {
        long* c = parse_candidate_json(data);
        return printf("%s\n", generate_poscar(c));
    } else if ((strcmp(cmd, "qe") == 0)) {
        long* c = parse_candidate_json(data);
        return printf("%s\n", generate_qe_input(c));
    } else {
        printf("%s %s\n", "Unknown command:", cmd);
        return printf("%s\n", "Usage: hexa materials.hexa <analyze|rank|poscar|qe> <material_json>");
    }



}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    test_c = hexa_struct_alloc((long[]){(long)("LiCoO2"), (long)(hexa_arr_lit((long[]){(long)("Li"), (long)("Co"), (long)("O")}, 3)), (long)("Fm-3m"), (long)(2.5), (long)((-1.2))}, 5);
    if (!((strcmp(infer_structure("Fm-3m"), "rocksalt") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(infer_structure("Pm-3m"), "perovskite") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(infer_structure("Fd-3m"), "spinel") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(infer_structure("Pa-3"), "generic") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    lat = estimate_lattice(hexa_arr_lit((long[]){(long)("Li"), (long)("Co"), (long)("O")}, 3));
    if (!((lat > 0.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    poscar = generate_poscar(test_c);
    if (!((hexa_index_of(poscar, "LiCoO2") >= 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((hexa_index_of(poscar, "rocksalt") >= 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "=== materials verified ===");
    printf("%s %s\n", "structure(Fm-3m):", infer_structure("Fm-3m"));
    printf("%s %g\n", "lattice(Li,Co,O):", (double)(lat));
    unstable = hexa_struct_alloc((long[]){(long)("FeS2"), (long)(hexa_arr_lit((long[]){(long)("Fe"), (long)("S")}, 2)), (long)("Pa-3"), (long)(0.9), (long)(0.3)}, 5);
    filtered = filter_candidates(hexa_arr_lit((long[]){(long)(test_c), (long)(unstable)}, 2));
    if (!((filtered.n == 1))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s %ld %s\n", "filter: 2 candidates -> ", (long)(filtered.n), " passed");
    ranked = rank_candidates(hexa_arr_lit((long[]){(long)(test_c)}, 1));
    if (!((ranked.n == 1))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s %ld\n", "rank score:", (long)(/* unknown field score */ 0));
    hexa_user_main();
    hexa_user_main();
    return 0;
}
