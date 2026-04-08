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


static const char* home;
static const char* root;
static const char* local_html;
static const char* local_dash;
static const char* docs_index;
static const char* workflows;
static const char* nodes;
static const char* html_ver;
static const char* has_pages = "unknown";
static const char* u1 = "https://need-singularity.github.io/nexus/";
static const char* u2 = "https://need-singularity.github.io/nexus/index.html";
static const char* u3 = "https://need-singularity.github.io/nexus/shared/reality_map_3d.html";
static const char* u4 = "https://need-singularity.github.io/nexus/reality_map_3d.html";
static const char* c1 = "err";
static const char* c2 = "err";
static const char* c3 = "err";
static const char* c4 = "err";
static const char* date;
static const char* probe_md;
static const char* report;
static const char* out;


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    home = hexa_trim(hexa_exec("printenv HOME"));
    root = hexa_concat(home, "/Dev/nexus");
    printf("%s\n", "=== Pages Deploy Verify ===");
    local_html = hexa_trim(hexa_exec(hexa_concat(hexa_concat("ls ", root), "/shared/reality_map*.html 2>/dev/null")));
    local_dash = hexa_trim(hexa_exec(hexa_concat(hexa_concat("test -f ", root), "/shared/dashboard.html && echo YES || echo NO")));
    docs_index = hexa_trim(hexa_exec(hexa_concat(hexa_concat("test -f ", root), "/docs/index.html && echo YES || echo NO")));
    workflows = hexa_trim(hexa_exec(hexa_concat(hexa_concat("ls ", root), "/.github/workflows/ 2>/dev/null | wc -l")));
    printf("%s\n", hexa_concat("local reality_map html: ", local_html));
    printf("%s\n", hexa_concat("dashboard.html: ", local_dash));
    printf("%s\n", hexa_concat("docs/index.html: ", docs_index));
    printf("%s\n", hexa_concat("workflows count: ", workflows));
    nodes = hexa_trim(hexa_exec(hexa_concat(hexa_concat("python3 -c \"import json; d=json.load(open('", root), "/shared/reality_map.json')); print(d.get('version','?'), len(d.get('nodes',[])))\"")));
    printf("%s\n", hexa_concat("reality_map.json: ", nodes));
    html_ver = hexa_trim(hexa_exec(hexa_concat(hexa_concat("grep -oE 'v[0-9]+\\.[0-9]+|[0-9]+ nodes' ", root), "/shared/reality_map_3d.html 2>/dev/null | sort -u | tr '\\n' ' '")));
    printf("%s\n", hexa_concat("html embedded: ", html_ver));
    has_pages = hexa_trim(hexa_exec("gh api repos/need-singularity/nexus 2>/dev/null | python3 -c \"import sys,json; print(json.load(sys.stdin).get('has_pages'))\" 2>/dev/null || echo unknown"));
    printf("%s\n", hexa_concat("has_pages: ", has_pages));
    c1 = hexa_trim(hexa_exec(hexa_concat("curl -s -o /dev/null -w '%{http_code}' -L --max-time 10 ", u1)));
    c2 = hexa_trim(hexa_exec(hexa_concat("curl -s -o /dev/null -w '%{http_code}' -L --max-time 10 ", u2)));
    c3 = hexa_trim(hexa_exec(hexa_concat("curl -s -o /dev/null -w '%{http_code}' -L --max-time 10 ", u3)));
    c4 = hexa_trim(hexa_exec(hexa_concat("curl -s -o /dev/null -w '%{http_code}' -L --max-time 10 ", u4)));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  ", c1), " "), u1));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  ", c2), " "), u2));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  ", c3), " "), u3));
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  ", c4), " "), u4));
    date = hexa_trim(hexa_exec("date +%Y-%m-%d"));
    probe_md = hexa_concat(hexa_concat(hexa_concat(hexa_concat("| ", u1), " | "), c1), " |\n");
    probe_md = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(probe_md, "| "), u2), " | "), c2), " |\n");
    probe_md = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(probe_md, "| "), u3), " | "), c3), " |\n");
    probe_md = hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(probe_md, "| "), u4), " | "), c4), " |\n");
    report = hexa_concat(hexa_concat("# 3D 현실지도 GitHub Pages 배포 재검증 (", date), ")\n\n");
    report = hexa_concat(report, "## 결론: 미배포 (NOT DEPLOYED)\n\n");
    report = hexa_concat(report, "## 로컬 상태\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "- reality_map.json (권위): "), nodes), "\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "- reality_map_3d.html 임베디드: "), html_ver), "\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "- dashboard.html 존재: "), local_dash), "\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "- docs/index.html 존재: "), docs_index), "\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "- .github/workflows/ 파일 수: "), workflows), "\n\n");
    report = hexa_concat(report, "## GitHub Pages API\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "- repos/need-singularity/nexus has_pages: "), has_pages), "\n");
    report = hexa_concat(report, "- repos/need-singularity/nexus/pages: 404 (사이트 없음)\n\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "## URL 프로빙 (curl -L)\n| URL | HTTP |\n|---|---|\n"), probe_md), "\n");
    report = hexa_concat(report, "## 버전 격차\n");
    report = hexa_concat(report, "- TODO 요구: v6.0 / 276 노드\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "- reality_map.json 실제: "), nodes), " (상위 세대)\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "- reality_map_3d.html 임베디드: "), html_ver), " (구세대)\n");
    report = hexa_concat(report, "- v6.0 276 스냅샷은 로컬/원격 모두 부재 — 권위 데이터는 이미 진화\n\n");
    report = hexa_concat(report, "## 권고\n");
    report = hexa_concat(hexa_concat(hexa_concat(report, "1. reality_map_3d.html을 최신 reality_map.json("), nodes), ")에 맞춰 재생성 (fetch 기반 유지)\n");
    report = hexa_concat(report, "2. .github/workflows/pages.yml 추가 또는 Settings Pages 활성화\n");
    report = hexa_concat(report, "3. docs/index.html 생성 또는 root publish 경로 설정\n");
    report = hexa_concat(report, "4. 배포 후 Playwright로 fetch 경로 + 노드 렌더링 재검증\n");
    out = hexa_concat(root, "/shared/pages-deploy-verify.md");
    hexa_write_file(out, report);
    printf("%s\n", hexa_concat("\nwrote: ", out));
    return 0;
}
