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

long test_ring_symmetric();
long test_hypercube_degree();
long test_lorenz_not_fixed();
long test_chaos_perturb_changes();

static long ring4;
static long sw8;
static long edges = 0;
static long i = 0;
static long hc8;
static long hc_edges = 0;
static long j = 0;
static long auto4;
static long auto12;
static long auto32;
static double lx = 1.0;
static double ly = 1.0;
static double lz = 1.0;
static long step = 0;
static hexa_arr hidden;
static hexa_arr lorenz_state;
static long perturbed;
static long n = 4;
static long adj;
static long cells;
static hexa_arr chaos;
static long chaos_new;
static long cell0;
static long perturbed_cell;
static long signals;

long test_ring_symmetric() {
    long r = ring_topology(4);
    if (!((r[1] == r[4]))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "ring should be symmetric";
    return 0;
}

long test_hypercube_degree() {
    long h = hypercube_topology(4);
    long deg = ((h[1] + h[2]) + h[3]);
    if (!((deg == 2.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "hypercube(4) node degree should be 2";
    return 0;
}

long test_lorenz_not_fixed() {
    long s = lorenz_step(1.0, 1.0, 1.0, 0.01);
    if (!((s[0] != 1.0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "lorenz should evolve";
    return 0;
}

long test_chaos_perturb_changes() {
    hexa_arr v = hexa_arr_lit((long[]){1.0, 2.0, 3.0}, 3);
    long p = chaos_perturb(v, hexa_arr_lit((long[]){(long)(10.0), (long)(20.0), (long)(30.0)}, 3), 0.1);
    if (!((p[0] != v.d[0]))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    0;
    "chaos should change values";
    return 0;
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    ring4 = ring_topology(4);
    printf("%s\n", "[1] Ring(4) adjacency:");
    printf("%s %ld %ld %ld %ld\n", "    row0:", (long)(ring4[0]), (long)(ring4[1]), (long)(ring4[2]), (long)(ring4[3]));
    printf("%s %ld %ld %ld %ld\n", "    row1:", (long)(ring4[4]), (long)(ring4[5]), (long)(ring4[6]), (long)(ring4[7]));
    sw8 = small_world_topology(8, 2, 0.3);
    while ((i < 64)) {
        if ((sw8[i] > 0.5)) {
            edges = (edges + 1);
        }
        i = (i + 1);
    }
    printf("%s %ld\n", "[2] Small-world(8, k=2, p=0.3): edges =", (long)(edges));
    hc8 = hypercube_topology(8);
    while ((j < 64)) {
        if ((hc8[j] > 0.5)) {
            hc_edges = (hc_edges + 1);
        }
        j = (j + 1);
    }
    printf("%s %ld\n", "[3] Hypercube(8): edges =", (long)(hc_edges));
    auto4 = auto_topology(4);
    auto12 = auto_topology(12);
    auto32 = auto_topology(32);
    printf("%s %ld %s\n", "[4] auto_topology(4): len=", (long)(hexa_str_len(auto4)), "(ring 4×4=16)");
    printf("%s %ld %s\n", "    auto_topology(12): len=", (long)(hexa_str_len(auto12)), "(sw 12×12=144)");
    printf("%s %ld %s\n", "    auto_topology(32): len=", (long)(hexa_str_len(auto32)), "(hc 32×32=1024)");
    while ((step < 100)) {
        long state = lorenz_step(lx, ly, lz, 0.01);
        lx = state[0];
        ly = state[1];
        lz = state[2];
        step = (step + 1);
    }
    printf("%s %g %s %g %s %g\n", "[5] Lorenz after 100 steps: x=", (double)((hexa_round((double)((lx * 1000))) / 1000)), "y=", (double)((hexa_round((double)((ly * 1000))) / 1000)), "z=", (double)((hexa_round((double)((lz * 1000))) / 1000)));
    hidden = hexa_arr_lit((long[]){(long)(0.1), (long)(0.2), (long)((-0.1)), (long)(0.3)}, 4);
    lorenz_state = hexa_arr_lit((long[]){(long)(lx), (long)(ly), (long)(lz)}, 3);
    perturbed = chaos_perturb(hidden, lorenz_state, 0.01);
    printf("%s %ld\n", "[6] Chaos perturbed hidden:", (long)(perturbed));
    printf("%s [arr len=%ld]\n", "    original:", (long)((hidden).n));
    adj = auto_topology(n);
    cells = randn(16, 0.1);
    chaos = hexa_arr_lit((long[]){(long)(1.0), (long)(1.0), (long)(1.0)}, 3);
    chaos_new = lorenz_step(chaos.d[0], chaos.d[1], chaos.d[2], 0.01);
    cell0 = slice(cells, 0, 4);
    perturbed_cell = chaos_perturb(cell0, chaos_new, 0.001);
    signals = matvec(adj, cells, n, n);
    printf("%s %ld\n", "[7] Topology signals:", (long)(signals));
    printf("%s %ld\n", "    Chaos-perturbed cell0:", (long)(perturbed_cell));
    run_tests();
    printf("%s\n", "--- topology+chaos PASS ---");
    return 0;
}
