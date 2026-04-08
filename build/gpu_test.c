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

long* bufpool_new(long cap);
hexa_arr bufpool_get(long *bp);
long bufpool_available(long *bp);
hexa_arr distance_matrix(hexa_arr data, long n, long d);
double hexa_user_sqrt(double x);
hexa_arr mutual_info(hexa_arr data, long n, long d, long n_bins);
double ln(double x);
hexa_arr knn(long dist, long n, long k);
long verify_n6_architecture(long n_points, long dims);
long hexa_user_main();

static long N = 6;
static long sigma = 12;
static long phi = 2;
static long tau = 4;
static long j2 = 24;
static long sigma_sq = 144;
static hexa_arr test_data;
static hexa_arr test_dist;
static double dist_err;
static double abs_err;
static double s25;
static double s_err;
static double abs_s;
static long* bp;

long* bufpool_new(long cap) {
    return hexa_struct_alloc((long[]){(long)(hexa_arr_new()), (long)(cap)}, 2);
}

hexa_arr bufpool_get(long *bp) {
    if ((hexa_str_len(bp[0]) > 0)) {
        return hexa_arr_new();
    } else {
        return hexa_arr_new();
    }
}

long bufpool_available(long *bp) {
    return hexa_str_len(bp[0]);
}

hexa_arr distance_matrix(hexa_arr data, long n, long d) {
    long pair_count = ((n * (n - 1)) / 2);
    hexa_arr result = hexa_arr_new();
    long i = 1;
    while ((i < n)) {
        long j = 0;
        while ((j < i)) {
            double sum_sq = 0.0;
            long k = 0;
            while ((k < d)) {
                long diff = (data.d[((i * d) + k)] - data.d[((j * d) + k)]);
                sum_sq = (sum_sq + (diff * diff));
                k = (k + 1);
            }
            result = hexa_arr_push(result, hexa_sqrt((double)(sum_sq)));
            j = (j + 1);
        }
        i = (i + 1);
    }
    return result;
}

double hexa_user_sqrt(double x) {
    if ((x <= 0.0)) {
        return 0.0;
    }
    double guess = (x / 2.0);
    if ((guess < 1.0)) {
        guess = 1.0;
    }
    long iter = 0;
    while ((iter < 20)) {
        double next = ((guess + (x / guess)) / 2.0);
        double diff = (next - guess);
        double abs_diff = (((diff < 0.0)) ? ((0.0 - diff)) : (diff));
        if ((abs_diff < 0.0000001)) {
            return next;
        }
        guess = next;
        iter = (iter + 1);
    }
    return guess;
}

hexa_arr mutual_info(hexa_arr data, long n, long d, long n_bins) {
    hexa_arr mins = hexa_arr_new();
    hexa_arr maxs = hexa_arr_new();
    long dim = 0;
    while ((dim < d)) {
        double mn = 999999999.0;
        double mx = (-999999999.0);
        long s = 0;
        while ((s < n)) {
            long v = data.d[((s * d) + dim)];
            if ((v < mn)) {
                mn = v;
            }
            if ((v > mx)) {
                mx = v;
            }
            s = (s + 1);
        }
        mins = hexa_arr_push(mins, mn);
        maxs = hexa_arr_push(maxs, mx);
        dim = (dim + 1);
    }
    hexa_arr bins = hexa_arr_new();
    long s = 0;
    while ((s < n)) {
        long dim2 = 0;
        while ((dim2 < d)) {
            long range = (maxs.d[dim2] - mins.d[dim2]);
            long b = (((range <= 0.0)) ? (0) : ((((raw >= n_bins)) ? ((n_bins - 1)) : (raw))));
            bins = hexa_arr_push(bins, b);
            dim2 = (dim2 + 1);
        }
        s = (s + 1);
    }
    long nb = n_bins;
    hexa_arr mi = hexa_arr_new();
    long di = 0;
    while ((di < d)) {
        long dj = 0;
        while ((dj < d)) {
            if ((di == dj)) {
                mi = hexa_arr_push(mi, 0.0);
                dj = (dj + 1);
                continue;
            }
            hexa_arr joint = hexa_arr_new();
            long init_k = 0;
            while ((init_k < (nb * nb))) {
                joint = hexa_arr_push(joint, 0);
                init_k = (init_k + 1);
            }
            long s2 = 0;
            while ((s2 < n)) {
                long bi = bins.d[((s2 * d) + di)];
                long bj = bins.d[((s2 * d) + dj)];
                joint.d[((bi * nb) + bj)] = (joint.d[((bi * nb) + bj)] + 1);
                s2 = (s2 + 1);
            }
            hexa_arr margin_i = hexa_arr_new();
            hexa_arr margin_j = hexa_arr_new();
            long m_init = 0;
            while ((m_init < nb)) {
                margin_i = hexa_arr_push(margin_i, 0);
                margin_j = hexa_arr_push(margin_j, 0);
                m_init = (m_init + 1);
            }
            long bi2 = 0;
            while ((bi2 < nb)) {
                long bj2 = 0;
                while ((bj2 < nb)) {
                    long cnt = joint.d[((bi2 * nb) + bj2)];
                    margin_i.d[bi2] = (margin_i.d[bi2] + cnt);
                    margin_j.d[bj2] = (margin_j.d[bj2] + cnt);
                    bj2 = (bj2 + 1);
                }
                bi2 = (bi2 + 1);
            }
            double n_f = ((double)(n));
            double mi_val = 0.0;
            long bi3 = 0;
            while ((bi3 < nb)) {
                double pi = (margin_i.d[bi3] / n_f);
                if ((pi > 0.0)) {
                    long bj3 = 0;
                    while ((bj3 < nb)) {
                        double pj = (margin_j.d[bj3] / n_f);
                        double pij = (joint.d[((bi3 * nb) + bj3)] / n_f);
                        if (((pij > 0.0) && (pj > 0.0))) {
                            mi_val = (mi_val + (pij * ln((pij / (pi * pj)))));
                        }
                        bj3 = (bj3 + 1);
                    }
                }
                bi3 = (bi3 + 1);
            }
            double clamped = (((mi_val < 0.0)) ? (0.0) : (mi_val));
            mi = hexa_arr_push(mi, clamped);
            dj = (dj + 1);
        }
        di = (di + 1);
    }
    return mi;
}

double ln(double x) {
    if ((x <= 0.0)) {
        return (-999999.0);
    }
    double ln2 = 0.6931471805599453;
    double val = x;
    long k = 0;
    while ((val > 2.0)) {
        val = (val / 2.0);
        k = (k + 1);
    }
    while ((val < 1.0)) {
        val = (val * 2.0);
        k = (k - 1);
    }
    double u = (val - 1.0);
    double result = 0.0;
    double term = u;
    double sign = 1.0;
    long i = 1;
    while ((i <= 20)) {
        result = (result + ((sign * term) / i));
        term = (term * u);
        sign = (0.0 - sign);
        i = (i + 1);
    }
    return (result + (k * ln2));
}

hexa_arr knn(long dist, long n, long k) {
    hexa_arr result = hexa_arr_new();
    long i = 0;
    while ((i < n)) {
        hexa_arr dists = hexa_arr_new();
        long j = 0;
        while ((j < n)) {
            if ((j == i)) {
                j = (j + 1);
                continue;
            }
            long big = (((i > j)) ? (i) : (j));
            long small = (((i > j)) ? (j) : (i));
            long flat_idx = (((big * (big - 1)) / 2) + small);
            dists = hexa_arr_push(dists, hexa_arr_lit((long[]){(long)(j), (long)(dist[flat_idx])}, 2));
            j = (j + 1);
        }
        long s = 0;
        while (((s < k) && (s < dists.n))) {
            long min_pos = s;
            long t = (s + 1);
            while ((t < dists.n)) {
                if ((/* unknown field d */ 0 < /* unknown field d */ 0)) {
                    min_pos = t;
                }
                t = (t + 1);
            }
            if ((min_pos != s)) {
                long tmp = dists.d[s];
                dists.d[s] = dists.d[min_pos];
                dists.d[min_pos] = tmp;
            }
            result = hexa_arr_push(result, /* unknown field idx */ 0);
            s = (s + 1);
        }
        i = (i + 1);
    }
    return result;
}

long verify_n6_architecture(long n_points, long dims) {
    hexa_arr data = hexa_arr_new();
    long i = 0;
    while ((i < (n_points * dims))) {
        double val = (((double)((((i * 7) + 3) % 100))) / 100.0);
        data = hexa_arr_push(data, val);
        i = (i + 1);
    }
    hexa_arr dist = distance_matrix(data, n_points, dims);
    long expected_pairs = ((n_points * (n_points - 1)) / 2);
    return (dist.n == expected_pairs);
}

long hexa_user_main() {
    hexa_arr argv = hexa_args();
    if ((argv.n < 2)) {
        printf("%s\n", "Usage: hexa gpu.hexa <verify|distance|mi|knn> [args...]");
        return 0;
    }
    const char* cmd = ((const char*)argv.d[1]);
    if ((strcmp(cmd, "verify") == 0)) {
        long n_pts = N;
        long dims = phi;
        if ((argv.n >= 3)) {
            n_pts = hexa_to_int_str(((const char*)argv.d[2]));
        }
        if ((argv.n >= 4)) {
            dims = hexa_to_int_str(((const char*)argv.d[3]));
        }
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat("Verifying n=6 GPU architecture: n_points=", hexa_int_to_str((long)(n_pts))), " dims="), hexa_int_to_str((long)(dims))));
        long ok = verify_n6_architecture(n_pts, dims);
        printf("%s %ld\n", "Distance matrix pairs:", (long)(((n_pts * (n_pts - 1)) / 2)));
        printf("%s %s\n", "Verification:", ((ok) ? ("PASS") : ("FAIL")));
        hexa_arr test_d = hexa_arr_new();
        long idx = 0;
        while ((idx < (n_pts * dims))) {
            test_d = hexa_arr_push(test_d, (((double)((((idx * 7) + 3) % 100))) / 100.0));
            idx = (idx + 1);
        }
        hexa_arr mi_mat = mutual_info(test_d, n_pts, dims, tau);
        printf("%s %ld %s %ld %s\n", "MI matrix size:", (long)(mi_mat.n), "(expected:", (long)((dims * dims)), ")");
        hexa_arr dist_mat = distance_matrix(test_d, n_pts, dims);
        long k = (((n_pts > 3)) ? (3) : (1));
        hexa_arr knn_result = knn(dist_mat, n_pts, k);
        return printf("%s %ld %s %ld %s\n", "KNN indices:", (long)(knn_result.n), "(expected:", (long)((n_pts * k)), ")");
    } else if ((strcmp(cmd, "distance") == 0)) {
        printf("%s\n", "Distance matrix computation (CPU fallback)");
        printf("%s\n", "Provide data as space-separated floats, n, d");
        hexa_arr demo = hexa_arr_lit((long[]){0.0, 0.0, 3.0, 4.0, 1.0, 1.0}, 6);
        hexa_arr dm = distance_matrix(demo, 3, 2);
        printf("%s %ld %s\n", "Demo 3x2 distances:", (long)(dm.n), "pairs");
        long p = 0;
        while ((p < dm.n)) {
            printf("%s %ld %s %ld\n", "  pair", (long)(p), ":", (long)(dm.d[p]));
            p = (p + 1);
        }
    } else if ((strcmp(cmd, "mi") == 0)) {
        printf("%s\n", "Mutual information matrix (CPU fallback)");
        hexa_arr demo = hexa_arr_lit((long[]){1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0}, 12);
        hexa_arr mi_result = mutual_info(demo, N, phi, tau);
        printf("%s %ld %s %ld %s\n", "MI matrix", (long)(phi), "x", (long)(phi), ":");
        long r = 0;
        while ((r < mi_result.n)) {
            printf("%s %ld\n", hexa_concat(hexa_concat("  [", hexa_int_to_str((long)(r))), "]:"), (long)(mi_result.d[r]));
            r = (r + 1);
        }
    } else if ((strcmp(cmd, "knn") == 0)) {
        long n_pts = N;
        long k = 3;
        if ((argv.n >= 3)) {
            n_pts = hexa_to_int_str(((const char*)argv.d[2]));
        }
        if ((argv.n >= 4)) {
            k = hexa_to_int_str(((const char*)argv.d[3]));
        }
        printf("%s\n", hexa_concat(hexa_concat(hexa_concat("KNN: n=", hexa_int_to_str((long)(n_pts))), " k="), hexa_int_to_str((long)(k))));
        hexa_arr d = hexa_arr_new();
        long idx2 = 0;
        while ((idx2 < (n_pts * phi))) {
            d = hexa_arr_push(d, (((double)((((idx2 * 13) + 7) % 100))) / 100.0));
            idx2 = (idx2 + 1);
        }
        hexa_arr dm2 = distance_matrix(d, n_pts, phi);
        hexa_arr neighbors = knn(dm2, n_pts, k);
        long pt = 0;
        while ((pt < n_pts)) {
            const char* line = hexa_concat(hexa_concat("  point ", hexa_int_to_str((long)(pt))), " neighbors:");
            long ki = 0;
            while ((ki < k)) {
                line = hexa_concat(hexa_concat(line, " "), hexa_int_to_str((long)(neighbors.d[((pt * k) + ki)])));
                ki = (ki + 1);
            }
            printf("%s\n", line);
            pt = (pt + 1);
        }
    } else {
        printf("%s %s\n", "Unknown command:", cmd);
        return printf("%s\n", "Usage: hexa gpu.hexa <verify|distance|mi|knn> [args...]");
    }



}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    test_data = hexa_arr_lit((long[]){(long)(0.0), (long)(0.0), (long)(3.0), (long)(4.0)}, 4);
    test_dist = distance_matrix(test_data, 2, 2);
    if (!((test_dist.n == 1))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    dist_err = (test_dist.d[0] - 5.0);
    abs_err = (((dist_err < 0.0)) ? ((0.0 - dist_err)) : (dist_err));
    if (!((abs_err < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    s25 = hexa_sqrt((double)(25.0));
    s_err = (s25 - 5.0);
    abs_s = (((s_err < 0.0)) ? ((0.0 - s_err)) : (s_err));
    if (!((abs_s < 0.001))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    if (!(verify_n6_architecture(N, phi))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    bp = bufpool_new(1024);
    if (!((bufpool_available(bp) == 0))) { fprintf(stderr, "assertion failed\n"); exit(1); }
    printf("%s\n", "=== gpu verified ===");
    printf("%s %ld\n", "distance(0,0)-(3,4):", (long)(test_dist.d[0]));
    printf("%s %g\n", "sqrt(25):", (double)(s25));
    printf("%s %ld\n", "n6_arch_verify(6,2):", (long)(verify_n6_architecture(N, phi)));
    hexa_user_main();
    hexa_user_main();
    return 0;
}
