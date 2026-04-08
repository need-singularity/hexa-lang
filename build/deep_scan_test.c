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

double abs_f(double x);
double sqrt_f(double x);
double ln_f(double x);
long gcd(long a, long b);
long sigma(long n);
long phi_euler(long n);
long tau_div(long n);
long sopfr(long n);
long rec(long w, const char* dom, long s, const char* t);
const char* stars_str(const char* n);
long bar();
long show(const char* dom, long s, const char* title, const char* line1, const char* line2, const char* line3);
long run_wave2();
long run_wave3();
long run_wave4();
long run_wave5();
long run_wave6();
long run_wave7();
long run_wave8();
long run_wave9();
long run_wave10();
long run_wave11();
long run_wave12_15();
long print_summary();
long print_stats();
const char* explore_home();
long explore_bus_append(const char* area, const char* kind, long count, const char* detail);
long file_exists_explore(const char* path);
long explore_atlas();
long explore_calc();
long explore_readme();
long explore_lenses();
hexa_arr load_bt_arch_domains();
long explore_bt_gaps();
long explore_scripts();
long run_explore();
long hexa_user_main();

static hexa_arr d_wave;
static hexa_arr d_dom;
static hexa_arr d_star;
static hexa_arr d_title;

double abs_f(double x) {
    if ((x < 0.0)) {
        return (0.0 - x);
    }
    return x;
}

double sqrt_f(double x) {
    if ((x <= 0.0)) {
        return 0.0;
    }
    double g = (x / 2.0);
    if ((g < 1.0)) {
        g = 1.0;
    }
    long i = 0;
    while ((i < 15)) {
        g = ((g + (x / g)) / 2.0);
        i = (i + 1);
    }
    return g;
}

double ln_f(double x) {
    if ((x <= 0.0)) {
        return 0.0;
    }
    double sum = 0.0;
    double term = ((x - 1.0) / (x + 1.0));
    double t2 = (term * term);
    long k = 1;
    while ((k <= 15)) {
        sum = (sum + (term / ((double)(k))));
        term = (term * t2);
        k = (k + 2);
    }
    return (2.0 * sum);
}

long gcd(long a, long b) {
    long x = a;
    long y = b;
    if ((x < 0)) {
        x = (0 - x);
    }
    if ((y < 0)) {
        y = (0 - y);
    }
    while ((y > 0)) {
        long t = y;
        y = (x - ((x / y) * y));
        x = t;
    }
    return x;
}

long sigma(long n) {
    long s = 0;
    long d = 1;
    while ((d <= n)) {
        if (((n - ((n / d) * d)) == 0)) {
            s = (s + d);
        }
        d = (d + 1);
    }
    return s;
}

long phi_euler(long n) {
    long count = 0;
    long k = 1;
    while ((k <= n)) {
        if ((gcd(k, n) == 1)) {
            count = (count + 1);
        }
        k = (k + 1);
    }
    return count;
}

long tau_div(long n) {
    long count = 0;
    long d = 1;
    while ((d <= n)) {
        if (((n - ((n / d) * d)) == 0)) {
            count = (count + 1);
        }
        d = (d + 1);
    }
    return count;
}

long sopfr(long n) {
    long s = 0;
    long d = 2;
    long t = n;
    while (((d * d) <= t)) {
        while (((t - ((t / d) * d)) == 0)) {
            s = (s + d);
            t = (t / d);
        }
        d = (d + 1);
    }
    if ((t > 1)) {
        s = (s + t);
    }
    return s;
}

long rec(long w, const char* dom, long s, const char* t) {
    d_wave = hexa_arr_push(d_wave, w);
    d_dom = hexa_arr_push(d_dom, (long)(dom));
    d_star = hexa_arr_push(d_star, s);
    d_title = hexa_arr_push(d_title, (long)(t));
}

const char* stars_str(const char* n) {
    const char* s = "";
    long i = 0;
    while ((i < n)) {
        s = hexa_concat(s, "*");
        i = (i + 1);
    }
    return s;
}

long bar() {
    return printf("%s\n", "======================================================================");
}

long show(const char* dom, long s, const char* title, const char* line1, const char* line2, const char* line3) {
    printf("%s\n", "");
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  ", stars_str(s)), " ["), dom), "] "), title));
    if ((hexa_str_len(line1) > 0)) {
        printf("%s\n", hexa_concat("    ", line1));
    }
    if ((hexa_str_len(line2) > 0)) {
        printf("%s\n", hexa_concat("    ", line2));
    }
    if ((hexa_str_len(line3) > 0)) {
        return printf("%s\n", hexa_concat("    ", line3));
    }
}

long run_wave2() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 2 -- Sporadic, String, Elliptic, Quantum, Knot, Grothendieck, ...");
    bar();
    rec(2, "SPORADIC", 5, "26 sporadic, M12 on S(5,6,12)");
    show("SPORADIC", 5, "26 sporadic groups, M12 on S(5,6,12), Monster dim=196883", "26 sporadic groups (PROVEN classification)", "M12: Steiner S(5,6,12) -- block size P1=6", "Monster: j-function, 26 = sigma+tau+sopfr+n/phi+phi");
    rec(2, "STRING", 5, "bosonic=26, super=10=2sopfr, CY3=dim P1");
    show("STRING", 5, "Critical dimensions: bosonic=26, super=10=2*sopfr, CY3=dim P1", "Superstring: 10 = 2*sopfr, CY3 real dim = P1 = 6", "Central charge: c=15=C(P1,2) (super)", "");
    rec(2, "ELLIPTIC", 5, "Mazur: 15=C(P1,2) torsion groups");
    show("ELLIPTIC", 5, "Mazur torsion: 15=C(P1,2) groups (PROVEN)", "15 possible torsion groups of E/Q", "Smallest conductor: 11 = sigma(6)-1", "");
    rec(2, "QUANTUM", 4, "Clifford=24=2sigma, Pauli=16=2^tau");
    show("QUANTUM", 4, "Clifford group order 24=2sigma, Pauli 16=2^tau", "15=C(P1,2) stabilizer states on 1 qubit", "", "");
    rec(2, "KNOT", 5, "6j-symbols=P1 indices, Jones polynomial");
    show("KNOT", 5, "6j-symbols = P1=6 indices, Jones polynomial", "Witten CS invariant, TQC foundation", "Crossing number of trefoil = n/phi = 3", "");
    rec(2, "GROTHENDIECK", 5, "EXACTLY P1=6 operations (PROVEN)");
    show("GROTHENDIECK", 5, "Grothendieck's EXACTLY P1=6 operations (PROVEN)", "f*, f_*, f^!, f_!, Hom, tensor", "Foundation of modern algebraic geometry", "");
    rec(2, "RIEMANN", 5, "zeta(2)=pi^2/P1, B2=1/P1");
    show("RIEMANN", 5, "zeta(2)=pi^2/P1 (Euler, PROVEN), B_2=1/P1", "Functional equation via Gamma(s/2)", "Trivial zeros at s=-2k = -phi*k", "");
    rec(2, "INFO", 4, "Shannon H=log(P1), Hamming [n+1,tau,n/phi]");
    show("INFO", 4, "Shannon H=log(P1) for fair die, Hamming [7,4,3]", "Hamming code: [n+1, tau, n/phi]", "", "");
    rec(2, "SURGERY", 5, "Exotic S^7: 28=P2, J-image from B_2=1/P1");
    show("SURGERY", 5, "Exotic spheres: |Theta_7|=28=P2 (Milnor, PROVEN)", "B_2=1/P1 propagates through J-image", "Surgery exact sequence >= dim P1", "");
    rec(2, "LANGLANDS", 4, "GL(P1) automorphic, E6 self-dual");
    show("LANGLANDS", 4, "GL(P1)=GL(6) automorphic forms, E6 self-dual", "Langlands dual of E6 = E6", "", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 2: 10 discoveries, 7 five-star, 3 four-star = 47 stars");
}

long run_wave3() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 3 -- S6 Outer Auto, ADE, SLE6, S^6, Platonic, Ramsey, ...");
    bar();
    rec(3, "S6-OUTER", 5, "Out(S_n)!=1 iff n=6 (PROVEN)");
    show("S6-OUTER", 5, "Out(S_n)!=1 iff n=P1=6 (Holder 1895, PROVEN)", "S6 is UNIQUE: Out(S6) = Z/phi = Z/2", "|Aut(S6)| = 1440 = 2*6! -- UNIQUENESS #1", "");
    rec(3, "ADE", 5, "E6: rank=P1, Coxeter=sigma, roots=72");
    show("ADE", 5, "ADE classification: E6 rank=P1, Coxeter=sigma, roots=72", "|W(E6)| = 51840 = 72 * n!", "Simply-laced Dynkin diagrams (PROVEN)", "");
    rec(3, "SLE6", 5, "SLE_6 = percolation (Smirnov, PROVEN)");
    show("SLE6", 5, "SLE(kappa=P1=6) = percolation (Smirnov, Fields 2010)", "Cardy formula exact for kappa=P1", "UNIQUENESS THEOREM #2", "");
    rec(3, "S6-COMPLEX", 5, "S^6 almost complex besides S^2 ONLY");
    show("S6-COMPLEX", 5, "S^6: ONLY sphere with almost complex besides S^2 (PROVEN)", "Borel-Serre 1953, S^6 = G2/SU(3)", "Connected to octonions -- UNIQUENESS #3", "");
    rec(3, "PLATONIC", 4, "sopfr=5 solids, faces tau/P1/n+phi/sigma/tau*sopfr");
    show("PLATONIC", 4, "5=sopfr Platonic solids: 4,6,8,12,20 faces", "= tau, P1, n+phi, sigma, tau*sopfr", "", "");
    rec(3, "RAMSEY", 5, "R(3,3)=P1=6 (PROVEN)");
    show("RAMSEY", 5, "R(3,3)=P1=6 (PROVEN), R(n/phi,n/phi)=P1", "First nontrivial diagonal Ramsey number", "UNIQUENESS #4", "");
    rec(3, "P-ADIC", 4, "Z_6 = Z_2 x Z_3 (CRT)");
    show("P-ADIC", 4, "Z_6 = Z_2 x Z_3 by CRT (PROVEN)", "6-adic = product of phi-adic and (n/phi)-adic", "", "");
    rec(3, "FEIGENBAUM", 3, "Period-doubling by factor phi=2");
    show("FEIGENBAUM", 3, "Period-doubling cascade uses factor phi(6)=2", "Universal constant delta = 4.669...", "", "");
    rec(3, "FSG", 5, "18 families + 26 sporadic (PROVEN)");
    show("FSG", 5, "Finite simple groups: 18=3*P1 families + 26 sporadic (PROVEN)", "The ENORMOUS theorem", "", "");
    rec(3, "SCHUR", 4, "p(6)=11=sigma-1 partitions, 11 irreps of S6");
    show("SCHUR", 4, "Partitions of P1=6: p(6)=11=sigma-1", "11 irreducible representations of S6", "", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 3: 10 discoveries, 6 five-star, 3 four-star, 1 three-star = 46 stars");
}

long run_wave4() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 4 -- Nuclear, Music, Cosmology, Neuro, Biology, ...");
    bar();
    rec(4, "NUCLEAR", 5, "Li-6=P1, C-12=sigma, magic from n=6");
    show("NUCLEAR", 5, "Li-6: Z=N=n/phi=3, A=P1=6
    C-12=sigma nucleons", "Magic: 2=phi, 8=n+phi, 20=tau*sopfr, 28=P2", "126 = |E7 roots|", "");
    rec(4, "MUSIC", 5, "sigma=12 semitones, harmonics 1:2:3:4:5:6");
    show("MUSIC", 5, "12-TET=sigma semitones, harmonics 1:2:3:4:5:6 define ALL intervals", "Circle of fifths: sigma=12 keys", "Perfect fifth = 2:3 = phi:n/phi", "");
    rec(4, "COSMOLOGY", 4, "SM: P1=6 quarks+leptons, sigma=12 gauge bosons");
    show("COSMOLOGY", 4, "SM: P1=6 quarks, 6 leptons, sigma=12 gauge bosons", "LCDM: P1=6 parameters", "", "");
    rec(4, "NEURO", 4, "P1=6 cortical layers, tau=4 lobes, phi=2 hemispheres");
    show("NEURO", 4, "Cortex: P1=6 layers, tau=4 lobes, phi=2 hemispheres", "", "", "");
    rec(4, "BIOLOGY", 5, "DNA: phi=2 strands, n/phi=3 codon, 2^P1=64 codons");
    show("BIOLOGY", 5, "DNA: phi=2 strands, n/phi=3 per codon, 2^P1=64 codons", "20=tau*sopfr amino acids", "Max codon degeneracy = P1 = 6", "");
    rec(4, "VIRUS", 4, "Icosahedral: tau*sopfr=20 faces, sigma=12 vertices");
    show("VIRUS", 4, "Icosahedral viruses: 20=tau*sopfr faces, 12=sigma vertices", "T-number uses 6-fold symmetry", "", "");
    rec(4, "GAME-THEORY", 4, "Arrow impossible >=n/phi=3 (PROVEN)");
    show("GAME-THEORY", 4, "Arrow impossibility: >=n/phi=3 alternatives (PROVEN)", "Nash equilibrium always exists", "", "");
    rec(4, "CS-DEEP", 4, "Karp T(P1)=21 NP-complete, BB(P1)");
    show("CS-DEEP", 4, "Karp's 21=T(P1) NP-complete problems", "BB(6) = busy beaver at P1 states", "", "");
    rec(4, "ANCIENT", 4, "Base-60=10*P1, I Ching 2^P1=64 hexagrams");
    show("ANCIENT", 4, "Babylonian base 60=10*P1, I Ching 2^P1=64 hexagrams", "Each hexagram = P1=6 lines", "Egyptian fractions: 1/P1 fundamental", "");
    rec(4, "ISING", 4, "Exact dim phi=2, open dim n/phi=3, critical tau=4");
    show("ISING", 4, "Ising: exact dim phi=2 (Onsager), open dim n/phi=3", "Critical dimension tau(6)=4 (mean-field above)", "", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 4: 10 discoveries, 3 five-star, 7 four-star = 45 stars");
}

long run_wave5() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 5 -- Coloring, Tiling, Fractal, Chemistry, Topology, ...");
    bar();
    rec(5, "COLORING", 4, "Four Color<=tau=4 (PROVEN), chi'(K6)=sopfr");
    show("COLORING", 4, "chi(planar)<=tau(6)=4 (Four Color, PROVEN)", "Heawood: 48=tau*sigma in discriminant", "chi'(K6) = sopfr = 5 (Vizing Class 1)", "");
    rec(5, "TILING", 4, "n/phi=3 regular, sigma-1=11 Archimedean, hex=P1");
    show("TILING", 4, "3=n/phi regular tilings, 11=sigma-1 Archimedean", "Hexagonal: P1=6 sides, Penrose: sopfr=5-fold", "", "");
    rec(5, "FRACTAL", 3, "Fractal dims use log bases 2,3 = factors of P1");
    show("FRACTAL", 3, "All classic fractal dims use bases 2,3 = prime factors of P1=6", "Cantor, Koch, Sierpinski, Menger", "", "");
    rec(5, "CHEM", 5, "Carbon Z=P1, valence=tau=4, benzene=P1 pi-e");
    show("CHEM", 5, "Carbon Z=P1=6 enables life, valence=tau=4", "Huckel 4n+2: first aromatic = P1=6 pi-electrons (benzene)", "Kr: 36=P1^2 electrons, shells: 2,8,18,32", "");
    rec(5, "COBORD", 4, "Omega_6=0 trivial, h-cobordism starts dim P1");
    show("COBORD", 4, "Omega_6=0: every 6-manifold bounds (trivial cobordism)", "h-cobordism starts at dim P1=6", "Todd td_2 = .../sigma(6)", "");
    rec(5, "PROB-DEEP", 3, "Gaussian alpha=phi=2, kurtosis=n/phi=3");
    show("PROB-DEEP", 3, "Stable distribution max alpha=phi=2 (Gaussian)", "Normal kurtosis = n/phi = 3", "Dice variance = 35/sigma(6)", "");
    rec(5, "AG-DEEP", 4, "CY3=dim P1, K3 chi=2sigma=24, Kodaira tau=4");
    show("AG-DEEP", 4, "CY3: real dim=P1=6, K3: chi=2sigma=24=n*tau", "Kodaira dimension: tau(6)=4 types", "", "");
    rec(5, "LOGIC", 3, "Godel: 2,3=factors of P1
    PA: sopfr=5 axioms");
    show("LOGIC", 3, "Godel numbering: primes 2,3 = factors of P1=6", "PA: 5=sopfr(6) axioms", "", "");
    rec(5, "OPTICS", 4, "Snowflakes=P1-fold (universal), hex max bandgap");
    show("OPTICS", 4, "ALL snowflakes: P1=6-fold symmetry (universal)", "Photonic crystals: hexagonal = max bandgap", "Diffraction: P1 first-order spots", "");
    rec(5, "LING", 3, "sopfr=5 vowels, P1=6 articulation places");
    show("LING", 3, "Most common vowel system: sopfr=5 vowels", "P1=6 major articulation places (IPA)", "Die: P1 faces, sum=T(6)=21", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 5: 10 discoveries, 1 five-star, 6 four-star, 3 three-star = 37 stars");
}

long run_wave6() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 6 -- Symplectic, GF64, Dessins, Spectral, Tropical, ...");
    bar();
    rec(6, "SYMPLECTIC", 3, "dim Sp(P1)=T(P1)=21");
    show("SYMPLECTIC", 3, "dim Sp(6,R) = T(P1) = 21 = triangular(6)", "3-body phase space: sigma=12 after reduction", "", "");
    rec(6, "GF64", 5, "GF(2^P1)=64=codon field, tau=4 subfields");
    show("GF64", 5, "GF(2^P1)=GF(64)=CODON FIELD (64 elements = 64 codons)", "Subfields = tau(6) = 4, BCH length 2^P1-1 = 63", "AES-192: sigma(6)=12 rounds", "");
    rec(6, "DESSINS", 4, "Belyi: n/phi=3 branch points (critical!)");
    show("DESSINS", 4, "Belyi maps: n/phi=3 branch points (CRITICAL number)", "Gal(Q-bar/Q) acts faithfully on dessins", "4 points -> theory collapses", "");
    rec(6, "SPECTRAL", 4, "V_6=pi^3/P1 (self-referential!)");
    show("SPECTRAL", 4, "Unit 6-ball: V_6 = pi^3/P1 = pi^3/6 (self-referential)", "V_{P1} = pi^{P1/phi} / P1", "Peak between sopfr(6) and P1", "");
    rec(6, "TROPICAL", 3, "Gr(phi,P1) = phylogenetics on P1 taxa");
    show("TROPICAL", 3, "Tropical Gr(2,6) = Gr(phi,P1) = phylogenetic trees on 6 taxa", "", "", "");
    rec(6, "ANT", 4, "pi(P1)=n/phi=3, 3#=P1 (primorial=perfect!)");
    show("ANT", 4, "pi(6)=3=n/phi, 3#=6=P1 (primorial of 3 = first perfect!)", "Goldbach: 6=3+3 first non-trivial case", "First gap of size P1 at p=23 (Golay)", "");
    rec(6, "MEASURE", 3, "Banach-Tarski: sopfr=5 pcs -> phi=2 balls");
    show("MEASURE", 3, "Banach-Tarski: sopfr=5 pieces -> phi=2 balls (PROVEN)", "Works in dim >= n/phi = 3", "", "");
    rec(6, "TENSOR", 4, "3-qubit SLOCC: P1=6 classes, Bell=tau=4");
    show("TENSOR", 4, "3-qubit (n/phi qubits) SLOCC: exactly P1=6 classes", "Bell states: tau(6)=4 maximally entangled", "Matrix mult exponent omega >= phi = 2", "");
    rec(6, "OPERADS", 3, "K6 dim=tau=4, P6 dim=sopfr=5, n! vertices");
    show("OPERADS", 3, "Associahedron K6: dim=tau=4, vertices=C(4)=14", "Permutohedron P6: dim=sopfr=5, vertices=n!=720", "", "");
    rec(6, "ARITH-DYN", 3, "j=sigma^3=1728, period-phi cycles");
    show("ARITH-DYN", 3, "Lattes maps from j=1728=sigma^3 elliptic curves", "Period-phi(6)=2 cycles: simplest nontrivial", "", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 6: 10 discoveries, 1 five-star, 5 four-star, 4 three-star = 36 stars");
}

long run_wave7() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 7 -- Galois, Petersen, Painleve, Graphene, Euler Deep, ...");
    bar();
    rec(7, "GALOIS", 5, "Solvable iff n<=tau=4, resolvent=P1");
    show("GALOIS", 5, "Solvable by radicals iff n<=tau(6)=4 (Abel-Ruffini, PROVEN)", "Threshold: tau=4 / sopfr=5, resolvent quintic -> sextic=P1", "16=2^tau transitive subgroups of S6", "");
    rec(7, "PETERSEN", 5, "K(sopfr,phi), C(P1,2)=15 edges, Aut=sopfr!");
    show("PETERSEN", 5, "Petersen = Kneser K(sopfr,phi) = K(5,2)", "15=C(P1,2) edges, degree=n/phi=3, girth=sopfr=5", "Aut=sopfr!=120, most important graph from K_P1", "");
    rec(7, "PAINLEVE", 5, "EXACTLY P1=6 Painleve eqs (PROVEN)");
    show("PAINLEVE", 5, "EXACTLY P1=6 Painleve equations (PROVEN classification)", "PI: y''=6y^2+t (coefficient = P1!)", "PVI: tau=4 free parameters, symmetry D4 (triality |S3|=P1)", "");
    rec(7, "GRAPHENE", 5, "P1=6 atom rings, C60=sopfr*sigma atoms");
    show("GRAPHENE", 5, "Graphene: P1=6 atom hexagonal rings, n/phi=3 bonds", "C60: 60=sopfr*sigma atoms, 12=sigma pentagons", "32=2^sopfr faces, Euler chi=phi=2", "");
    rec(7, "SPECIAL-FN", 3, "Gamma(6)=sopfr!=120, zeta(2)=pi^2/P1");
    show("SPECIAL-FN", 3, "Gamma(6)=5!=120=sopfr!, zeta(2)=pi^2/P1", "", "", "");
    rec(7, "DIOPH", 4, "FLT n>=n/phi=3 (PROVEN), Catalan 9-8=1");
    show("DIOPH", 4, "FLT: no solutions n>=n/phi=3 (Wiles 1995, PROVEN)", "Catalan: (n/phi)^2-(n+phi)=9-8=1 (PROVEN)", "Waring g(2)=tau=4", "");
    rec(7, "FLUID", 4, "Kolmogorov -5/3=-sopfr/(n/phi), NS open dim 3");
    show("FLUID", 4, "Kolmogorov: E(k)~k^(-5/3), exponent=-sopfr/(n/phi)", "NS: smooth dim phi=2, OPEN dim n/phi=3 (Millennium)", "", "");
    rec(7, "GGT", 3, "S3: |S3|=P1, smallest non-abelian");
    show("GGT", 3, "S3: order P1=6, smallest non-abelian group", "Generated by phi(6)=2 elements", "", "");
    rec(7, "CRYPTO", 3, "RSA-4096=2^sigma, AES-192=sigma rounds");
    show("CRYPTO", 3, "RSA-4096: 2^sigma(6) bits, AES-192: sigma=12 rounds", "", "", "");
    rec(7, "EULER-DEEP", 5, "chi(M1)=zeta(-1)=-1/sigma (3-way)");
    show("EULER-DEEP", 5, "chi(M1)=zeta(-1)=-B2/2=-1/sigma(6)=-1/12", "THREE independent computations give same value!", "Moduli + analytic number theory + string 1-loop", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 7: 10 discoveries, 5 five-star, 2 four-star, 3 three-star = 42 stars");
}

long run_wave8() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 8 -- Chess, Arrow, FFT, Polytope, Kepler, ...");
    bar();
    rec(8, "CHESS", 3, "64=2^P1 squares, Q(6)=tau=4");
    show("CHESS", 3, "Chess: 64=2^P1 squares=codons!, Q(6)=tau=4 solutions", "Pieces/side: 2^tau=16, pawns=n+phi=8", "", "");
    rec(8, "ARROW", 4, "Arrow >=n/phi=3 (PROVEN), tau=4 conditions");
    show("ARROW", 4, "Arrow impossibility: >=n/phi=3 alternatives (PROVEN)", "4=tau(6) conditions, May: phi=2 works", "", "");
    rec(8, "FFT", 3, "Radix 2,3=factors of P1, Nyquist phi*B");
    show("FFT", 3, "FFT: radix 2,3 = prime factors of P1=6", "Nyquist: sample rate = phi*bandwidth", "", "");
    rec(8, "POLYTOPE", 5, "dim tau=4: EXACTLY P1=6 polytopes (PROVEN)");
    show("POLYTOPE", 5, "dim 4=tau(6) has EXACTLY P1=6 regular polytopes (PROVEN!)", "24-cell: 2sigma=24 vertices, self-dual, unique", "sopfr in dim 3, P1 in dim 4, n/phi in dim 5+", "");
    rec(8, "TDA", 3, "K_6 clique = S^(sopfr-1), b1=2sopfr");
    show("TDA", 3, "K_6 clique complex = boundary of sopfr-simplex", "b1(K6) = 2*sopfr = 10", "", "");
    rec(8, "ASTRO", 3, "n/phi=3 Kepler laws, sopfr=5 Lagrange, kiss=sigma");
    show("ASTRO", 3, "Kepler: n/phi=3 laws, Lagrange: sopfr=5 points", "Kepler conjecture: kiss(3)=sigma=12 (PROVEN)", "", "");
    rec(8, "FUNCT-AN", 3, "L^phi=L^2 unique Hilbert space");
    show("FUNCT-AN", 3, "L^2=L^phi(6): ONLY Lp that is Hilbert space", "Holder self-dual at p=phi=2", "", "");
    rec(8, "KOLMOGOROV", 3, "K(6) unusually low (many descriptions)");
    show("KOLMOGOROV", 3, "K(6) low: 6=2*3=3!=P1 (many short descriptions)", "High Solomonoff prior weight", "", "");
    rec(8, "EVOLUTION", 3, "Max codon degeneracy=P1=6, RPS=n/phi");
    show("EVOLUTION", 3, "Max codon degeneracy=P1=6, RPS n/phi=3 strategies", "", "", "");
    rec(8, "DISCGEOM", 3, "Minkowski 2^P1=64 in dim P1");
    show("DISCGEOM", 3, "Minkowski: Vol>2^P1=64 in dim P1, Ehrhart C(t+P1,P1)", "", "", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 8: 10 discoveries, 1 five-star, 1 four-star, 8 three-star = 33 stars");
}

long run_wave9() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 9 -- QFT, Quantum Hall, Yang-Mills, 6-vertex, ...");
    bar();
    rec(9, "QFT", 5, "C(P1,2)=15 Weyl fermions/gen, N_f=P1=6");
    show("QFT", 5, "Anomaly: 15=C(P1,2) Weyl fermions per generation", "BPST instanton dim tau=4, SU(n/phi) moduli dim sigma=12", "SM: N_f=P1=6 quark flavors", "");
    rec(9, "QHALL", 5, "nu=1/(n/phi)=1/3 dominant FQHE, 10=2sopfr");
    show("QHALL", 5, "FQHE: nu=1/(n/phi)=1/3 most prominent state (Laughlin)", "Anyon phase theta=pi/(n/phi)=pi/3", "Tenfold way: 10=2*sopfr symmetry classes", "");
    rec(9, "HOMOLOGICAL", 4, "P1=6 term exact sequence, Bott phi=2");
    show("HOMOLOGICAL", 4, "K-theory Mayer-Vietoris: exactly P1=6 terms (finite!)", "Bott periodicity: period phi(6)=2", "phi=2 derived functors (Ext, Tor)", "");
    rec(9, "YANG-MILLS", 5, "YM dim tau=4, (2,0) dim P1, exotic R^4 only");
    show("YANG-MILLS", 5, "Yang-Mills: physical dim tau=4 (Millennium Problem)", "(2,0) superconformal: dim P1=6 (no Lagrangian!)", "Exotic R^n: exists ONLY n=tau=4 (PROVEN) -- UNIQUENESS #16", "");
    rec(9, "RUNGE-KUTTA", 4, "Barrier sopfr=5 needs P1=6 stages");
    show("RUNGE-KUTTA", 4, "RK order barrier: p<=tau -> s=p (efficient)", "p=sopfr=5: first barrier, needs s=P1=6 stages", "RK4: tau stages, order tau (optimal)", "");
    rec(9, "6D-THEORY", 5, "Max superconformal dim=P1=6 (PROVEN)");
    show("6D-THEORY", 5, "Superconformal algebras exist ONLY dim<=P1=6 (PROVEN)", "UNIQUENESS #17", "AGT: 6D = 4D gauge + 2D CFT (P1=tau+phi)", "");
    rec(9, "TOPO-PHASE", 3, "Bott period n+phi=8, 10=2sopfr classes");
    show("TOPO-PHASE", 3, "Topological insulator: Bott period n+phi=8", "10=2*sopfr symmetry classes", "", "");
    rec(9, "NUMFIELD", 5, "Q(zeta_6): P1=6 roots of unity (max imag quad)");
    show("NUMFIELD", 5, "Q(sqrt(-3))=Q(zeta_6): P1=6 roots of unity (MAXIMUM!)", "disc=-(n/phi)=-3, class number h=1", "Heegner numbers: 9=(n/phi)^2 with h=1", "");
    rec(9, "STOCHASTIC", 3, "Brownian dim=phi=2, Dyson beta={ 1: 1, phi,tau}");
    show("STOCHASTIC", 3, "Brownian: dim=phi=2, Dyson beta={ 1: 1, phi,tau}={ 1: 1, 2,4}", "n/phi=3 classical ensembles (GOE, GUE, GSE)", "", "");
    rec(9, "6VERTEX", 5, "P1=6 vertex types (ice C(tau,2)=6), Yang-Baxter");
    show("6VERTEX", 5, "6-vertex model: EXACTLY P1=6 allowed configurations", "Ice rule: C(tau,tau/2)=C(4,2)=6=P1", "Bethe ansatz (Lieb 1967), Yang-Baxter: P1 R-matrices", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 9: 10 discoveries, 6 five-star, 2 four-star, 2 three-star = 44 stars");
}

long run_wave10() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 10 -- Riemann Surfaces, Holonomy, Index, Cluster, Lorentz, ...");
    bar();
    rec(10, "RIEMANN-SURF", 5, "Hurwitz 84=sigma*(n+1), dim M_3=P1");
    show("RIEMANN-SURF", 5, "Hurwitz: |Aut(X)|<=84(g-1), 84=sigma*(n+1) (PROVEN)", "Klein quartic: g=n/phi=3, |Aut|=168=PSL(2,7)", "Uniformization: n/phi=3 types, dim(M_3)=P1=6", "");
    rec(10, "HOLONOMY", 5, "7=n+1 holonomy types (PROVEN), CY3=dim P1");
    show("HOLONOMY", 5, "Berger: 7=n+1 special holonomy types (PROVEN)", "CY3: dim P1=6 (string), G2: dim n+1=7, Spin(7): dim n+phi=8", "|G2 roots|=sigma=12", "");
    rec(10, "INDEX", 4, "Todd denom=sigma=12, L1=p1/(n/phi)");
    show("INDEX", 4, "Atiyah-Singer index theorem (PROVEN, Fields Medal)", "Todd td_2=.../sigma(6)=.../12, A-hat denom=n!", "L_1=p1/(n/phi)=p1/3", "");
    rec(10, "CLUSTER", 4, "E6 cluster rank=P1, 42 vars, period=P1");
    show("CLUSTER", 4, "E6 cluster algebra: rank P1=6, 42=7*P1 variables", "Zamolodchikov (A1,A3): period P1=6", "", "");
    rec(10, "CHOMSKY", 3, "tau=4 Chomsky types, mod-P1 DFA=P1 states");
    show("CHOMSKY", 3, "Chomsky hierarchy: tau(6)=4 types (PROVEN)", "Minimal DFA for mod P1: exactly P1=6 states", "", "");
    rec(10, "COMPLEX-AN", 5, "SL(2,C) dim=P1=6=Lorentz group");
    show("COMPLEX-AN", 5, "SL(2,C): dim_R=P1=6 = double cover Lorentz SO(3,1)", "6 = 3 boosts + 3 rotations = n/phi + n/phi", "zeta(2)=pi^2/P1 via residue calculus", "");
    rec(10, "COMM-ALG", 3, "Phi_6: deg=phi=2, Hilbert dim=P1");
    show("COMM-ALG", 3, "Phi_6(x)=x^2-x+1: 6th cyclotomic, deg=phi=2", "Polynomial ring k[x1,...,x_P1]: Krull dim=P1=6", "", "");
    rec(10, "KL-THEORY", 3, "Hecke H_q(S6) dim=n!=720");
    show("KL-THEORY", 3, "Hecke H_q(S6): dim=n!=720, Jones from traces", "", "", "");
    rec(10, "LORENTZ", 5, "Lorentz=P1=6, Poincare=10, Conformal C(P1,2)=15");
    show("LORENTZ", 5, "Lorentz SO(3,1): dim=P1=6=n/phi+n/phi", "Poincare: dim=P1+tau=10=2*sopfr", "Conformal SO(4,2): dim=C(P1,2)=15 (ubiquitous!)", "");
    rec(10, "ECONOMICS", 3, "Arrow n/phi=3, Shapley n!=720 orderings");
    show("ECONOMICS", 3, "Arrow: >=n/phi=3, Nash: phi=2 players", "Shapley: n!=720 orderings for P1=6 players", "", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 10: 10 discoveries, 4 five-star, 2 four-star, 4 three-star = 40 stars");
}

long run_wave11() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVE 11 -- Domain #100: Theorem of 6, Rubik, Latin Squares, ...");
    bar();
    rec(11, "THEOREM-OF-6", 5, "6 UNIQUE: perfect+factorial+primorial+triangular");
    show("THEOREM-OF-6", 5, "6 is UNIQUE: perfect AND factorial AND primorial AND triangular (PROVEN)", "(a) 2*3, (b) sigma=2n, (c) 3!, (d) 3#, (e) T(3), (f) sigma_{-1}=2", "No other number satisfies even (a)+(b). QED.", "");
    rec(11, "OPT-TRANSPORT", 3, "W_phi=W_2 most used");
    show("OPT-TRANSPORT", 3, "Wasserstein W_p: p=phi=2 most used (Villani)", "", "", "");
    rec(11, "RUBIK", 4, "P1=6 faces, sigma=12 edges, God=tau*sopfr=20");
    show("RUBIK", 4, "Rubik: P1=6 faces, sigma=12 edges, n+phi=8 corners", "God's number=20=tau*sopfr (PROVEN 2010)", "Stickers/face: (n/phi)^2=9", "");
    rec(11, "LATIN-SQ", 5, "N(P1)=1: MOLS fails, Euler 36 impossible");
    show("LATIN-SQ", 5, "MOLS: N(6)=1 (Euler wrong at P1!)", "36=P1^2 officers problem: IMPOSSIBLE (PROVEN)", "6 is SMALLEST order where MOLS fails (not prime power)", "");
    rec(11, "EVERSION", 3, "S^P1 everts in R^(P1+1)");
    show("EVERSION", 3, "Sphere eversion: S^P1 everts in R^(P1+1) (Smale paradox)", "", "", "");
    rec(11, "FIBONACCI", 4, "F(P1)=n+phi=8, F(sigma)=sigma^2=144");
    show("FIBONACCI", 4, "F(P1)=F(6)=8=n+phi, F(sigma)=F(12)=144=sigma^2", "Pisano period pi(6)=24=2sigma", "", "");
    rec(11, "INFO-GEOM", 3, "Dice Delta_sopfr, H=log(P1)");
    show("INFO-GEOM", 3, "Fisher metric on P1=6 outcomes: Delta_sopfr manifold", "Max entropy = log(P1) = log(6)", "", "");
    rec(11, "HOTT", 3, "Univalent foundations, n-types");
    show("HOTT", 3, "HoTT: types=spaces, identity=equivalence (Voevodsky)", "", "", "");
    rec(11, "COMPLEXITY", 4, "P1=6 unsolved Millennium, T(P1)=21 NP-complete");
    show("COMPLEXITY", 4, "6=P1 unsolved Millennium Problems (of 7=n+1)", "Karp's 21=T(P1) NP-complete problems", "", "");
    rec(11, "PYTHAGORAS", 5, "Harmonics 1:2:3:4:5:6 define ALL intervals");
    show("PYTHAGORAS", 5, "First P1=6 harmonics define ALL basic intervals", "1:2=octave, 2:3=fifth, 3:4=fourth, 4:5=maj3, 5:6=min3", "1+2+3=6=P1 (simplest Pythagorean sum = perfect number)", "");
    printf("%s\n", "");
    return printf("%s\n", "  Wave 11: 10 discoveries, 3 five-star, 3 four-star, 4 three-star = 39 stars");
}

long run_wave12_15() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  WAVES 12-15 BATCH -- Higher Algebra, Applied, Quantum Gravity, Meta-Math");
    bar();
    printf("%s\n", "\n  --- Wave 12: Higher Algebra ---");
    rec(12, "DERIVED-AG", 5, "Sphere spectrum pi_3=Z/24=Z/(2sigma)");
    show("DERIVED-AG", 5, "Sphere spectrum: pi_3=Z/(2sigma)=Z/24, pi_7=Z/240", "pi_7=Z/(sigma*tau*sopfr), initial object encodes n=6", "", "");
    rec(12, "INFTY-CAT", 3, "6-simplex: n+1=7 vertices, T(P1)=21 edges");
    rec(12, "KOSZUL", 3, "Quadratic: degree phi=2 relations");
    rec(12, "A-INFTY", 3, "K_6: dim tau=4");
    printf("%s\n", "\n  --- Wave 13: Applied Math ---");
    rec(13, "SMALL-WORLD", 5, "Six degrees of separation = P1!");
    show("SMALL-WORLD", 5, "SIX degrees of separation = P1 = 6 (Milgram 1967)", "Barabasi-Albert exponent gamma=n/phi=3", "", "");
    rec(13, "WAVELET", 4, "Daubechies-6: P1 vanishing moments");
    show("WAVELET", 4, "db6: P1=6 vanishing moments, support 2P1-1=11=sigma-1", "", "", "");
    rec(13, "CONTROL", 3, "Kalman: P1 dim needs 6 matrices");
    rec(13, "EPIDEMIOLOGY", 3, "SIR n/phi=3, SEIR tau=4");
    printf("%s\n", "\n  --- Wave 14: Quantum Gravity ---");
    rec(14, "HOLOGRAPHY", 5, "AdS_{n+1} x S^tau = 11D M-theory");
    show("HOLOGRAPHY", 5, "M-theory: AdS_7 x S^4 = AdS_{n+1} x S^{tau} in 11D", "String: AdS_5 x S^5 in 10=2*sopfr dims", "11=n+1+tau=sigma-1", "");
    rec(14, "LQG", 4, "6j-symbols=P1 indices, Ponzano-Regge");
    show("LQG", 4, "LQG: 6j-symbols with P1=6 indices", "Ponzano-Regge state sum model", "", "");
    rec(14, "AMPLITUHEDRON", 4, "Gr+(phi,P1) for P1-particle scattering");
    show("AMPLITUHEDRON", 4, "Amplituhedron: Gr+(2,6)=Gr+(phi,P1)", "Positive geometry for P1=6 particle amplitudes", "", "");
    rec(14, "BH-ENTROPY", 3, "S=A/tau, ISCO=P1*M");
    printf("%s\n", "\n  --- Wave 15: Meta-Mathematics ---");
    rec(15, "REVERSE-MATH", 4, "Big Five = sopfr = 5 subsystems");
    show("REVERSE-MATH", 4, "Reverse math: sopfr(6)=5 Big Five subsystems", "RCA0, WKL0, ACA0, ATR0, Pi^1_1-CA0", "", "");
    rec(15, "PROOF-ASSIST", 4, "P1=6 proof assistants");
    show("PROOF-ASSIST", 4, "6=P1 major proof assistants: Lean, Coq, Agda, Isabelle, Mizar, HOL", "", "", "");
    rec(15, "MODEL-THEORY", 3, "Morley categoricity, Shelah classification");
    printf("%s\n", "");
    return printf("%s\n", "  Waves 12-15: 18 discoveries");
}

long print_summary() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  GRAND SUMMARY -- Deep Scan Waves 2-15");
    bar();
    long total = (long)(d_dom.n);
    long star5 = 0;
    long star4 = 0;
    long star3 = 0;
    long total_stars = 0;
    long i = 0;
    while ((i < total)) {
        long s = hexa_to_int_str(((const char*)d_star.d[i]));
        total_stars = (total_stars + s);
        if ((s >= 5)) {
            star5 = (star5 + 1);
        }
        if ((s == 4)) {
            star4 = (star4 + 1);
        }
        if ((s == 3)) {
            star3 = (star3 + 1);
        }
        i = (i + 1);
    }
    printf("%s\n", "");
    printf("%s\n", hexa_concat("  Domains scanned: ", hexa_int_to_str((long)(total))));
    printf("%s\n", hexa_concat("  Five-star:       ", hexa_int_to_str((long)(star5))));
    printf("%s\n", hexa_concat("  Four-star:       ", hexa_int_to_str((long)(star4))));
    printf("%s\n", hexa_concat("  Three-star:      ", hexa_int_to_str((long)(star3))));
    printf("%s\n", hexa_concat("  Total stars:     ", hexa_int_to_str((long)(total_stars))));
    printf("%s\n", "");
    printf("%s\n", "  PROVEN UNIQUENESS THEOREMS:");
    printf("%s\n", "   #1  Out(S_n)!=1 iff n=6 (Holder)");
    printf("%s\n", "   #2  SLE_6 = percolation (Smirnov)");
    printf("%s\n", "   #3  S^6 almost complex besides S^2 (Borel-Serre)");
    printf("%s\n", "   #4  R(3,3)=6 (diagonal Ramsey)");
    printf("%s\n", "   #5  Exotic S^7: 28=P2 types (Milnor)");
    printf("%s\n", "   #6  Grothendieck's 6 operations");
    printf("%s\n", "   #7  6 Painleve equations");
    printf("%s\n", "   #8  CY3 dim=6 for strings");
    printf("%s\n", "   #9  Max superconformal dim=6");
    printf("%s\n", "  #10  Exotic R^n iff n=4=tau(6)");
    printf("%s\n", "  #11  6-vertex ice rule C(4,2)=6");
    printf("%s\n", "  #12  P1=6 regular polytopes in dim 4");
    printf("%s\n", "  #13  Four Color: chi<=4=tau(6)");
    printf("%s\n", "  #14  Abel-Ruffini: solvable iff n<=4");
    printf("%s\n", "  #15  N(6)=1 MOLS (Euler fails)");
    printf("%s\n", "  #16  Carbon Z=6 enables biology");
    printf("%s\n", "  #17  6 = unique perfect+factorial+primorial+triangular");
    printf("%s\n", "  #18  Berger: 7=n+1 holonomy types");
    printf("%s\n", "");
    printf("%s\n", "  n=6 constants: P1=6, sigma=12, phi=2, tau=4, sopfr=5");
    return printf("%s\n", "  Universals: C(6,2)=15, T(6)=21, 6!=720, 2sigma=24, 240(E8)");
}

long print_stats() {
    printf("%s\n", "");
    bar();
    printf("%s\n", "  STAR DISTRIBUTION BY WAVE");
    bar();
    long total = (long)(d_dom.n);
    long wn = 2;
    while ((wn <= 15)) {
        long count = 0;
        long ss = 0;
        long s5 = 0;
        long s4 = 0;
        long s3 = 0;
        long i = 0;
        while ((i < total)) {
            if (((long)(d_wave.d[i]) == wn)) {
                count = (count + 1);
                long sv = hexa_to_int_str(((const char*)d_star.d[i]));
                ss = (ss + sv);
                if ((sv >= 5)) {
                    s5 = (s5 + 1);
                }
                if ((sv == 4)) {
                    s4 = (s4 + 1);
                }
                if ((sv == 3)) {
                    s3 = (s3 + 1);
                }
            }
            i = (i + 1);
        }
        if ((count > 0)) {
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("  Wave ", hexa_int_to_str((long)(wn))), ": "), hexa_int_to_str((long)(count))), " disc, "), hexa_int_to_str((long)(s5))), " five, "), hexa_int_to_str((long)(s4))), " four, "), hexa_int_to_str((long)(s3))), " three = "), hexa_int_to_str((long)(ss))), " stars"));
        }
        wn = (wn + 1);
    }
}

const char* explore_home() {
    const char* r = hexa_exec("printenv HOME");
    return hexa_trim(r);
}

long explore_bus_append(const char* area, const char* kind, long count, const char* detail) {
    const char* ehome = explore_home();
    const char* bus_path = hexa_concat(ehome, "/Dev/nexus/shared/growth_bus.jsonl");
    const char* ts = "?";
    ts = hexa_exec("date +%Y-%m-%dT%H:%M:%S");
    const char* line = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("{\"source\":\"deep_scan_explore\",\"area\":\"", area), "\",\"kind\":\""), kind), "\",\"count\":"), hexa_int_to_str((long)(count))), ",\"detail\":\""), detail), "\",\"timestamp\":\""), hexa_trim(ts)), "\"}");
    return hexa_append_file(bus_path, hexa_concat(line, "\n"));
}

long file_exists_explore(const char* path) {
    const char* r = "";
    r = hexa_exec("test");
    return (strcmp(hexa_trim(r), "1") == 0);
}

long explore_atlas() {
    printf("%s\n", "  [1/6] Atlas scan...");
    const char* ehome = explore_home();
    const char* atlas_dir = hexa_concat(ehome, "/Dev/nexus/shared/math_atlas");
    const char* ls_result = "";
    ls_result = hexa_exec("ls");
    if ((hexa_str_len(hexa_trim(ls_result)) == 0)) {
        printf("%s\n", "    atlas directory missing or empty -- GAP");
        explore_bus_append("atlas", "gap", 1, "atlas directory missing or empty");
        return 1;
    }
    hexa_arr files = hexa_split(ls_result, "\n");
    long md_count = 0;
    long fi = 0;
    while ((fi < (long)(files.n))) {
        if ((hexa_ends_with(hexa_trim(((const char*)files.d[fi])), ".md") || hexa_ends_with(hexa_trim(((const char*)files.d[fi])), ".json"))) {
            md_count = (md_count + 1);
        }
        fi = (fi + 1);
    }
    long gaps = (((md_count < 6)) ? ((6 - md_count)) : (0));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("    atlas files: ", hexa_int_to_str((long)(md_count))), ", gaps: "), hexa_int_to_str((long)(gaps))));
    if ((gaps > 0)) {
        explore_bus_append("atlas", "gap", gaps, hexa_concat(hexa_concat("atlas files=", hexa_int_to_str((long)(md_count))), " target=6"));
    }
    return gaps;
}

long explore_calc() {
    printf("%s\n", "  [2/6] Calc scan...");
    const char* ehome = explore_home();
    const char* calc_dir = hexa_concat(ehome, "/Dev/nexus/shared/calc");
    const char* ls_result = "";
    ls_result = hexa_exec("ls");
    if ((hexa_str_len(hexa_trim(ls_result)) == 0)) {
        printf("%s\n", "    calc directory missing -- GAP");
        explore_bus_append("calc", "gap", 1, "calc directory missing");
        return 1;
    }
    hexa_arr files = hexa_split(ls_result, "\n");
    long script_count = 0;
    long empty_count = 0;
    long fi = 0;
    while ((fi < (long)(files.n))) {
        const char* f = hexa_trim(((const char*)files.d[fi]));
        if (((hexa_ends_with(f, ".py") || hexa_ends_with(f, ".hexa")) || hexa_ends_with(f, ".sh"))) {
            script_count = (script_count + 1);
        }
        if ((hexa_str_len(f) > 0)) {
            const char* fpath = hexa_concat(hexa_concat(calc_dir, "/"), f);
            const char* sz = "";
            sz = hexa_exec("wc");
            long size = 0;
            size = (long)(hexa_to_float(((const char*)(hexa_split(hexa_trim(sz), " ")).d[0])));
            if ((size == 0)) {
                empty_count = (empty_count + 1);
            }
        }
        fi = (fi + 1);
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("    calc scripts: ", hexa_int_to_str((long)(script_count))), ", empty: "), hexa_int_to_str((long)(empty_count))));
    if ((empty_count > 0)) {
        explore_bus_append("calc", "incomplete", empty_count, "empty scripts in calc/");
    }
    return empty_count;
}

long explore_readme() {
    printf("%s\n", "  [3/6] README/GROWTH scan...");
    const char* ehome = explore_home();
    const char* nexus_dir = hexa_concat(ehome, "/Dev/nexus");
    long gaps = 0;
    if ((!file_exists_explore(hexa_concat(nexus_dir, "/README.md")))) {
        printf("%s\n", "    README.md missing -- GAP");
        gaps = (gaps + 1);
    } else {
        printf("%s\n", "    README.md exists");
    }
    if ((!file_exists_explore(hexa_concat(nexus_dir, "/GROWTH.md")))) {
        printf("%s\n", "    GROWTH.md missing -- GAP");
        gaps = (gaps + 1);
    } else {
        printf("%s\n", "    GROWTH.md exists");
    }
    if ((gaps > 0)) {
        explore_bus_append("readme", "missing", gaps, "documentation gap");
    }
    return gaps;
}

long explore_lenses() {
    printf("%s\n", "  [4/6] Lenses scan...");
    const char* ehome = explore_home();
    const char* lenses_path = hexa_concat(ehome, "/Dev/nexus/shared/custom_lenses.jsonl");
    if ((!file_exists_explore(lenses_path))) {
        printf("%s\n", "    custom_lenses.jsonl missing -- GAP");
        explore_bus_append("lenses", "missing", 1, "custom_lenses.jsonl not found");
        return 1;
    }
    const char* content = "";
    content = hexa_read_file(lenses_path);
    hexa_arr lines = hexa_split(content, "\n");
    long lens_count = 0;
    long li = 0;
    while ((li < (long)(lines.n))) {
        if ((hexa_str_len(hexa_trim(((const char*)lines.d[li]))) > 2)) {
            lens_count = (lens_count + 1);
        }
        li = (li + 1);
    }
    long target = 48;
    long gaps = (((lens_count < target)) ? ((target - lens_count)) : (0));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("    lenses: ", hexa_int_to_str((long)(lens_count))), "/"), hexa_int_to_str((long)(target))), ", gap: "), hexa_int_to_str((long)(gaps))));
    if ((gaps > 0)) {
        explore_bus_append("lenses", "gap", gaps, hexa_concat(hexa_concat(hexa_concat("lens_count=", hexa_int_to_str((long)(lens_count))), " target="), hexa_int_to_str((long)(target))));
    }
    return gaps;
}

hexa_arr load_bt_arch_domains() {
    const char* home = hexa_exec("printenv HOME");
    const char* path = hexa_concat(home, "/Dev/nexus/shared/bt_arch_domains.jsonl");
    const char* raw = hexa_read_file(path);
    hexa_arr lines = hexa_split(raw, "\n");
    hexa_arr all_doms = hexa_arr_new();
    hexa_arr covered = hexa_arr_new();
    long __fi_line_1 = 0;
    while ((__fi_line_1 < lines.n)) {
        const char* line = ((const char*)lines.d[__fi_line_1]);
        const char* trimmed = hexa_trim(line);
        if ((strcmp(trimmed, "") == 0)) {
            continue;
        }
        const char* domain = "";
        long is_covered = 0;
        if (hexa_contains(trimmed, "\"domain\":\"")) {
            hexa_arr p = hexa_split(trimmed, "\"domain\":\"");
            if ((p.n >= 2)) {
                domain = ((const char*)(hexa_split(((const char*)p.d[1]), "\"")).d[0]);
            }
        }
        if (hexa_contains(trimmed, "\"covered\":true")) {
            is_covered = 1;
        }
        if ((strcmp(domain, "") != 0)) {
            all_doms = hexa_arr_concat(all_doms, hexa_arr_lit((long[]){(long)(domain)}, 1));
            if (is_covered) {
                covered = hexa_arr_concat(covered, hexa_arr_lit((long[]){(long)(domain)}, 1));
            }
        }
        __fi_line_1 = (__fi_line_1 + 1);
    }
    return hexa_arr_lit((long[]){(long)(all_doms), (long)(covered)}, 2);
}

long explore_bt_gaps() {
    printf("%s\n", "  [5/6] BT-gap scan...");
    hexa_arr _bt_arch = load_bt_arch_domains();
    long all_doms = _bt_arch.d[0];
    long covered = _bt_arch.d[1];
    long missing = 0;
    const char* missing_names = "";
    long ai = 0;
    while ((ai < (long)(hexa_str_len(all_doms)))) {
        long d = all_doms[ai];
        long found = 0;
        long ci = 0;
        while ((ci < (long)(hexa_str_len(covered)))) {
            if ((covered[ci] == d)) {
                found = 1;
            }
            ci = (ci + 1);
        }
        if ((!found)) {
            missing = (missing + 1);
            if ((hexa_str_len(missing_names) > 0)) {
                missing_names = hexa_concat(missing_names, ",");
            }
            missing_names = hexa_concat(missing_names, hexa_int_to_str((long)(d)));
        }
        ai = (ai + 1);
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("    BT coverage: ", hexa_int_to_str((long)((long)(hexa_str_len(covered))))), "/"), hexa_int_to_str((long)((long)(hexa_str_len(all_doms))))), " domains, gaps: "), hexa_int_to_str((long)(missing))));
    if ((missing > 0)) {
        explore_bus_append("bt_gaps", "missing_domain", missing, missing_names);
    }
    return missing;
}

long explore_scripts() {
    printf("%s\n", "  [6/6] Scripts scan...");
    const char* ehome = explore_home();
    const char* scripts_dir = hexa_concat(ehome, "/Dev/nexus/scripts");
    const char* ls_result = "";
    ls_result = hexa_exec("ls");
    if ((hexa_str_len(hexa_trim(ls_result)) == 0)) {
        printf("%s\n", "    scripts directory missing -- GAP");
        explore_bus_append("scripts", "missing", 1, "scripts directory missing");
        return 1;
    }
    hexa_arr files = hexa_split(ls_result, "\n");
    long sh_count = 0;
    long non_exec = 0;
    long fi = 0;
    while ((fi < (long)(files.n))) {
        const char* f = hexa_trim(((const char*)files.d[fi]));
        if (hexa_ends_with(f, ".sh")) {
            sh_count = (sh_count + 1);
            const char* fpath = hexa_concat(hexa_concat(scripts_dir, "/"), f);
            const char* check = "";
            check = hexa_exec("test");
            if ((strcmp(hexa_trim(check), "1") != 0)) {
                non_exec = (non_exec + 1);
            }
        }
        fi = (fi + 1);
    }
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("    scripts: ", hexa_int_to_str((long)(sh_count))), ", non-executable: "), hexa_int_to_str((long)(non_exec))));
    if ((non_exec > 0)) {
        explore_bus_append("scripts", "non_executable", non_exec, "scripts without +x");
    }
    return non_exec;
}

long run_explore() {
    printf("%s\n", "╔══════════════════════════════════════════════════════════════════╗");
    printf("%s\n", "║  DEEP SCAN -- 6-Area Explore Loop                               ║");
    printf("%s\n", "╚══════════════════════════════════════════════════════════════════╝");
    printf("%s\n", "");
    long g1 = explore_atlas();
    long g2 = explore_calc();
    long g3 = explore_readme();
    long g4 = explore_lenses();
    long g5 = explore_bt_gaps();
    long g6 = explore_scripts();
    long total_gaps = (((((g1 + g2) + g3) + g4) + g5) + g6);
    printf("%s\n", "");
    printf("%s\n", "═══ Explore Summary ═══");
    printf("%s\n", "  Area      Gaps");
    printf("%s\n", "  ──────────────────");
    printf("%s\n", hexa_concat("  atlas     ", hexa_int_to_str((long)(g1))));
    printf("%s\n", hexa_concat("  calc      ", hexa_int_to_str((long)(g2))));
    printf("%s\n", hexa_concat("  readme    ", hexa_int_to_str((long)(g3))));
    printf("%s\n", hexa_concat("  lenses    ", hexa_int_to_str((long)(g4))));
    printf("%s\n", hexa_concat("  BT-gap    ", hexa_int_to_str((long)(g5))));
    printf("%s\n", hexa_concat("  scripts   ", hexa_int_to_str((long)(g6))));
    printf("%s\n", "  ──────────────────");
    printf("%s\n", hexa_concat("  TOTAL     ", hexa_int_to_str((long)(total_gaps))));
    printf("%s\n", "");
    if ((total_gaps == 6)) {
        printf("%s\n", "  n=6 resonance: gap count = n = 6!");
    }
    if ((total_gaps == 12)) {
        printf("%s\n", "  n=6 resonance: gap count = sigma(6) = 12!");
    }
    explore_bus_append("explore_total", "summary", total_gaps, hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("atlas=", hexa_int_to_str((long)(g1))), " calc="), hexa_int_to_str((long)(g2))), " readme="), hexa_int_to_str((long)(g3))), " lenses="), hexa_int_to_str((long)(g4))), " bt="), hexa_int_to_str((long)(g5))), " scripts="), hexa_int_to_str((long)(g6))));
    return printf("%s\n", "  [growth_bus.jsonl: 6-area explore results recorded]");
}

long hexa_user_main() {
    hexa_arr cli = hexa_args();
    long argc = (long)(cli.n);
    const char* cmd = "help";
    if ((argc >= 3)) {
        cmd = ((const char*)cli.d[2]);
    }
    if ((strcmp(cmd, "help") == 0)) {
        printf("%s\n", "deep_scan.hexa -- Consolidated Deep Scan Engine (Waves 2-15)");
        printf("%s\n", "");
        printf("%s\n", "Usage: hexa deep_scan.hexa <command>");
        printf("%s\n", "");
        printf("%s\n", "Commands:");
        printf("%s\n", "  help       Show this help");
        printf("%s\n", "  wave2      Run Wave 2 (Sporadic, String, Elliptic, ...)");
        printf("%s\n", "  wave3      Run Wave 3 (S6, ADE, SLE6, Ramsey, ...)");
        printf("%s\n", "  wave4      Run Wave 4 (Nuclear, Music, Biology, ...)");
        printf("%s\n", "  wave5      Run Wave 5 (Coloring, Chemistry, ...)");
        printf("%s\n", "  wave6      Run Wave 6 (GF64, Dessins, Spectral, ...)");
        printf("%s\n", "  wave7      Run Wave 7 (Galois, Painleve, Graphene, ...)");
        printf("%s\n", "  wave8      Run Wave 8 (Chess, Polytope, Kepler, ...)");
        printf("%s\n", "  wave9      Run Wave 9 (QFT, Yang-Mills, 6-vertex, ...)");
        printf("%s\n", "  wave10     Run Wave 10 (Holonomy, Lorentz, ...)");
        printf("%s\n", "  wave11     Run Wave 11 (Theorem of 6, Rubik, ...)");
        printf("%s\n", "  wave12_15  Run Waves 12-15 batch");
        printf("%s\n", "  all        Run all waves + summary");
        printf("%s\n", "  summary    Summary only (loads all data)");
        printf("%s\n", "  stats      Star distribution by wave");
        printf("%s\n", "  explore    6-area gap scan (atlas,calc,readme,lenses,BT,scripts)");
        printf("%s\n", "");
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("n=6: sigma=", hexa_int_to_str((long)(sigma(6)))), " phi="), hexa_int_to_str((long)(phi_euler(6)))), " tau="), hexa_int_to_str((long)(tau_div(6)))), " sopfr="), hexa_int_to_str((long)(sopfr(6)))));
        return 0;
    }
    if ((strcmp(cmd, "explore") == 0)) {
        run_explore();
    }
    if ((strcmp(cmd, "wave2") == 0)) {
        run_wave2();
    }
    if ((strcmp(cmd, "wave3") == 0)) {
        run_wave3();
    }
    if ((strcmp(cmd, "wave4") == 0)) {
        run_wave4();
    }
    if ((strcmp(cmd, "wave5") == 0)) {
        run_wave5();
    }
    if ((strcmp(cmd, "wave6") == 0)) {
        run_wave6();
    }
    if ((strcmp(cmd, "wave7") == 0)) {
        run_wave7();
    }
    if ((strcmp(cmd, "wave8") == 0)) {
        run_wave8();
    }
    if ((strcmp(cmd, "wave9") == 0)) {
        run_wave9();
    }
    if ((strcmp(cmd, "wave10") == 0)) {
        run_wave10();
    }
    if ((strcmp(cmd, "wave11") == 0)) {
        run_wave11();
    }
    if ((strcmp(cmd, "wave12_15") == 0)) {
        run_wave12_15();
    }
    if ((strcmp(cmd, "all") == 0)) {
        run_wave2();
        run_wave3();
        run_wave4();
        run_wave5();
        run_wave6();
        run_wave7();
        run_wave8();
        run_wave9();
        run_wave10();
        run_wave11();
        run_wave12_15();
        print_summary();
        print_stats();
    }
    if ((strcmp(cmd, "summary") == 0)) {
        run_wave2();
        run_wave3();
        run_wave4();
        run_wave5();
        run_wave6();
        run_wave7();
        run_wave8();
        run_wave9();
        run_wave10();
        run_wave11();
        run_wave12_15();
        print_summary();
    }
    if ((strcmp(cmd, "stats") == 0)) {
        run_wave2();
        run_wave3();
        run_wave4();
        run_wave5();
        run_wave6();
        run_wave7();
        run_wave8();
        run_wave9();
        run_wave10();
        run_wave11();
        run_wave12_15();
        print_stats();
    }
    if (hexa_starts_with(cmd, "wave")) {
        long total = (long)(d_dom.n);
        if ((total > 0)) {
            long ts = 0;
            long i = 0;
            while ((i < total)) {
                ts = (ts + hexa_to_int_str(((const char*)d_star.d[i])));
                i = (i + 1);
            }
            printf("%s\n", "");
            return printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  Discoveries: ", hexa_int_to_str((long)(total))), ", Stars: "), hexa_int_to_str((long)(ts))));
        }
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    d_wave = hexa_arr_new();
    d_dom = hexa_arr_new();
    d_star = hexa_arr_new();
    d_title = hexa_arr_new();
    hexa_user_main();
    hexa_user_main();
    return 0;
}
