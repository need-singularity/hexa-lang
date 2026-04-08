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

long get_magic();
long increment();
long update_state(const char* new_state);
const char* get_state();
long apply_interest();
long log_event(const char* msg);
long add_to_acc(long n);
long add_range(long start, long end_val);
const char* classify(long x);
long tracked_add(long a, long b);
long area();
long perimeter();

static long MAGIC = 42;
static long counter = 0;
static long total = 0;
static long i = 1;
static const char* state = "init";
static double RATE = 0.1;
static double balance = 1000.0;
static hexa_arr hexa_v_log;
static long accumulator = 0;
static long THRESHOLD = 50;
static long sum_squares = 0;
static long fib_a = 0;
static long fib_b = 1;
static long fib_count = 0;
static long call_count = 0;
static long r1;
static long r2;
static long r3;
static long WIDTH = 6;
static long HEIGHT = 6;

long get_magic() {
    return MAGIC;
}

long increment() {
    counter = (counter + 1);
}

long update_state(const char* new_state) {
    state = new_state;
}

const char* get_state() {
    return state;
}

long apply_interest() {
    balance = (balance + (balance * RATE));
}

long log_event(const char* msg) {
    hexa_v_log = hexa_arr_push(hexa_v_log, (long)(msg));
}

long add_to_acc(long n) {
    accumulator = (accumulator + n);
}

long add_range(long start, long end_val) {
    long j = start;
    while ((j < end_val)) {
        add_to_acc(j);
        j = (j + 1);
    }
}

const char* classify(long x) {
    if ((x > THRESHOLD)) {
        return "high";
    }
    return "low";
}

long tracked_add(long a, long b) {
    call_count = (call_count + 1);
    return (a + b);
}

long area() {
    return (WIDTH * HEIGHT);
}

long perimeter() {
    return (2 * (WIDTH + HEIGHT));
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    printf("%s\n", "=== Global Scope Tests ===");
    printf("%s %ld\n", "global let:", (long)(get_magic()));
    if (!((get_magic() == 42))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    increment();
    increment();
    increment();
    printf("%s %ld\n", "counter after 3 increments:", (long)(counter));
    if (!((counter == 3))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    while ((i <= 10)) {
        total = (total + i);
        i = (i + 1);
    }
    printf("%s %ld\n", "sum 1..10:", (long)(total));
    if (!((total == 55))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    update_state("running");
    printf("%s %s\n", "state:", get_state());
    if (!((strcmp(get_state(), "running") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    update_state("done");
    printf("%s %s\n", "state:", get_state());
    if (!((strcmp(get_state(), "done") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    apply_interest();
    printf("%s %g\n", "balance after 1 interest:", (double)(balance));
    apply_interest();
    printf("%s %g\n", "balance after 2 interest:", (double)(balance));
    hexa_v_log = hexa_arr_new();
    log_event("start");
    log_event("process");
    log_event("end");
    printf("%s [arr len=%ld]\n", "log:", (long)((hexa_v_log).n));
    if (!((hexa_v_log.n == 3))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((hexa_v_log.d[0] == "start"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((hexa_v_log.d[2] == "end"))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    add_range(1, 6);
    printf("%s %ld\n", "accumulator 1..5:", (long)(accumulator));
    if (!((accumulator == 15))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s %s\n", "classify(30):", classify(30));
    printf("%s %s\n", "classify(80):", classify(80));
    if (!((strcmp(classify(30), "low") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((strcmp(classify(80), "high") == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    for (long k = 1; k < 11; k++) {
        sum_squares = (sum_squares + (k * k));
    }
    printf("%s %ld\n", "sum of squares 1..10:", (long)(sum_squares));
    if (!((sum_squares == 385))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    while ((fib_count < 10)) {
        long tmp = fib_b;
        fib_b = (fib_a + fib_b);
        fib_a = tmp;
        fib_count = (fib_count + 1);
    }
    printf("%s %ld\n", "fib(10) via globals:", (long)(fib_a));
    if (!((fib_a == 55))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    r1 = tracked_add(3, 4);
    r2 = tracked_add(10, 20);
    r3 = tracked_add(100, 200);
    printf("%s %ld %ld %ld %s %ld\n", "tracked results:", (long)(r1), (long)(r2), (long)(r3), "calls:", (long)(call_count));
    if (!((call_count == 3))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((r1 == 7))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((r2 == 30))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((r3 == 300))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s %ld\n", "area:", (long)(area()));
    printf("%s %ld\n", "perimeter:", (long)(perimeter()));
    if (!((area() == 36))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!((perimeter() == 24))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "");
    printf("%s\n", "=== All global scope tests passed! ===");
    return 0;
}
