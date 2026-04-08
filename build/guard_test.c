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

long is_l0_file(const char* file_path);
const char* get_l0_desc(const char* file_path);
long cmd_check(const char* file_path);
long cmd_checklist();
long cmd_recurrence(const char* category, const char* project);
long cmd_root_cause();
long cmd_verify();

static const char* _home;
static const char* _shared;
static const char* _lockdown_path;
static hexa_arr _args;
static const char* _cmd;

long is_l0_file(const char* file_path) {
    const char* cmd = hexa_concat(hexa_concat(hexa_concat("python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); paths=[f[\"path\"] for p in d[\"projects\"].values() for f in p.get(\"L0\",[])] ; fp=sys.argv[2]; print(\"YES\" if any(fp.endswith(p) or p in fp for p in paths) else \"NO\")' ", _lockdown_path), " "), file_path);
    const char* result = "";
    result = hexa_trim(hexa_exec(hexa_concat(cmd, " 2>/dev/null")));
    return (strcmp(result, "YES") == 0);
}

const char* get_l0_desc(const char* file_path) {
    const char* cmd = hexa_concat(hexa_concat(hexa_concat("python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); [print(f[\"desc\"]) for p in d[\"projects\"].values() for f in p.get(\"L0\",[]) if sys.argv[2].endswith(f[\"path\"]) or f[\"path\"] in sys.argv[2]]' ", _lockdown_path), " "), file_path);
    const char* result = "";
    result = hexa_trim(hexa_exec(hexa_concat(cmd, " 2>/dev/null")));
    return result;
}

long cmd_check(const char* file_path) {
    if ((!is_l0_file(file_path))) {
        return 0;
    }
    const char* desc = get_l0_desc(file_path);
    printf("%s\n", hexa_concat("⛔ L0 코어 보호 파일 감지: ", file_path));
    printf("%s\n", hexa_concat("   설명: ", desc));
    printf("%s\n", "");
    printf("%s\n", "이 파일은 L0 코어 보호 파일입니다. 수정 승인하시겠습니까?");
    printf("%s\n", "");
    printf("%s\n", "승인 후 필수 절차:");
    printf("%s\n", "  1. 커밋 메시지에 'L0-수정: [파일명] — [이유]' 명시");
    printf("%s\n", "  2. 파일 상단 불변식 주석에 변경 사항 반영");
    printf("%s\n", "  3. 해당 프로젝트 테스트 통과 확인");
    return printf("%s\n", "  4. convergence JSON 업데이트");
}

long cmd_checklist() {
    const char* changed = "";
    changed = hexa_trim(hexa_exec("git diff --cached --name-only 2>/dev/null"));
    if ((strcmp(changed, "") == 0)) {
        changed = hexa_trim(hexa_exec("git diff --name-only 2>/dev/null"));
    }
    if ((strcmp(changed, "") == 0)) {
        printf("%s\n", "변경 파일 없음");
        return 0;
    }
    hexa_arr files = hexa_split(changed, "\n");
    long has_l0 = 0;
    long has_code = 0;
    long has_doc = 0;
    long has_convergence = 0;
    long has_todo = 0;
    long __fi_f_1 = 0;
    while ((__fi_f_1 < files.n)) {
        const char* f = ((const char*)files.d[__fi_f_1]);
        const char* ft = hexa_trim(f);
        if ((strcmp(ft, "") == 0)) {
            continue;
        }
        if (is_l0_file(ft)) {
            has_l0 = 1;
        }
        if ((((hexa_contains(ft, ".hexa") || hexa_contains(ft, ".rs")) || hexa_contains(ft, ".ts")) || hexa_contains(ft, ".py"))) {
            has_code = 1;
        }
        if ((hexa_contains(ft, ".md") || hexa_contains(ft, "README"))) {
            has_doc = 1;
        }
        if (hexa_contains(ft, "convergence")) {
            has_convergence = 1;
        }
        if (hexa_contains(ft, "todo")) {
            has_todo = 1;
        }
        __fi_f_1 = (__fi_f_1 + 1);
    }
    printf("%s\n", "═══ 커밋 전 체크리스트 ═══");
    printf("%s\n", "");
    if (has_l0) {
        printf("%s\n", "⛔ L0 파일 변경 감지!");
        long __fi_f_2 = 0;
        while ((__fi_f_2 < files.n)) {
            const char* f = ((const char*)files.d[__fi_f_2]);
            if (((strcmp(hexa_trim(f), "") != 0) && is_l0_file(hexa_trim(f)))) {
                printf("%s\n", hexa_concat(hexa_concat(hexa_concat("  🔴 ", hexa_trim(f)), " — "), get_l0_desc(hexa_trim(f))));
            }
            __fi_f_2 = (__fi_f_2 + 1);
        }
        printf("%s\n", "  → 커밋 메시지에 'L0-수정:' 접두사 필수");
        printf("%s\n", "");
    }
    long checks = 0;
    long total = 0;
    if (has_code) {
        total = (total + 1);
        printf("%s\n", "□ 테스트 통과 확인 (cargo test / pnpm test)");
    }
    total = (total + 1);
    if (has_convergence) {
        checks = (checks + 1);
        printf("%s\n", "☑ convergence JSON 업데이트 됨");
    } else {
        printf("%s\n", "□ convergence JSON 업데이트 필요? (골화/안정/실패 변경 시)");
    }
    if ((has_doc && (!has_code))) {
        printf("%s\n", "⚠️ 문서만 변경 — 근본원인 코드 수정이 동반되었는가?");
        printf("%s\n", "  문서화만으로 완료 처리 금지. 코드 수정이 필요하면 함께 커밋.");
    }
    total = (total + 1);
    printf("%s\n", "□ NEXUS-6 변경 전후 스캔 완료? (R3)");
    total = (total + 1);
    printf("%s\n", "□ 발견/결과 파일에 영구 기록? (R6)");
    total = (total + 1);
    printf("%s\n", "□ 하드코딩 없음? shared/*.jsonl 동적 로드? (R2)");
    printf("%s\n", "");
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat("체크: ", hexa_int_to_str((long)(checks))), "/"), hexa_int_to_str((long)(total))), " 자동 확인됨"));
    return printf("%s\n", "나머지 수동 확인 후 커밋하세요.");
}

long cmd_recurrence(const char* category, const char* project) {
    const char* conv_path = hexa_concat(hexa_concat(hexa_concat(_shared, "/convergence/"), project), ".json");
    const char* cmd = hexa_concat(hexa_concat(hexa_concat("python3 -c 'import json,sys\nf=sys.argv[1]; cat=sys.argv[2]\nd=json.load(open(f))\nfor section in [\"ossified\",\"stable\",\"failed\"]:\n    if cat in d.get(section,{}):\n        item=d[section][cat]\n        rc=item.get(\"recurrence\",0)+1\n        item[\"recurrence\"]=rc\n        print(f\"recurrence={rc} ({section}.{cat})\")\n        if rc>=3:\n            print(f\"⚠️ 3회 반복! 설계 개선 필수: {cat}\")\n        json.dump(d,open(f,\"w\"),indent=2,ensure_ascii=False)\n        sys.exit(0)\nprint(f\"카테고리 미발견: {cat}\")' ", conv_path), " "), category);
    const char* result = hexa_exec(hexa_concat(cmd, " 2>/dev/null"));
    return printf("%s\n", hexa_trim(result));
}

long cmd_root_cause() {
    const char* changed = "";
    changed = hexa_trim(hexa_exec("git diff --cached --name-only 2>/dev/null"));
    if ((strcmp(changed, "") == 0)) {
        changed = hexa_trim(hexa_exec("git diff --name-only 2>/dev/null"));
    }
    hexa_arr files = hexa_split(changed, "\n");
    long has_code = 0;
    long has_doc = 0;
    long __fi_f_3 = 0;
    while ((__fi_f_3 < files.n)) {
        const char* f = ((const char*)files.d[__fi_f_3]);
        const char* ft = hexa_trim(f);
        if ((strcmp(ft, "") == 0)) {
            continue;
        }
        if (((hexa_contains(ft, ".hexa") || hexa_contains(ft, ".rs")) || hexa_contains(ft, ".ts"))) {
            has_code = 1;
        }
        if ((hexa_contains(ft, ".md") || hexa_contains(ft, ".json"))) {
            has_doc = 1;
        }
        __fi_f_3 = (__fi_f_3 + 1);
    }
    if ((has_doc && (!has_code))) {
        printf("%s\n", "⚠️ 경고: 문서/JSON만 변경되고 코드 수정 없음");
        printf("%s\n", "   근본원인 코드 수정이 필요하면 함께 커밋하세요.");
        return printf("%s\n", "   문서화만으로 완료 처리 = 수렴 아님 (prism P-029 교훈)");
    } else {
        if (has_code) {
            return printf("%s\n", "✅ 코드 수정 포함됨 — 근본원인 해결 확인");
        }
    }
}

long cmd_verify() {
    printf("%s\n", "═══ L0 불변식 계약 테스트 ═══");
    printf("%s\n", "");
    long pass = 0;
    long fail = 0;
    const char* inv1_cmd = hexa_concat(hexa_concat("python3 -c 'import json; lines=[json.loads(l) for l in open(\"", _shared), "/n6_constants.jsonl\") if l.strip()]; names={x[\"name\"] for x in lines}; required={\"n\",\"sigma\",\"phi\",\"tau\",\"sopfr\",\"j2\",\"P2\"}; missing=required-names; print(\"PASS\" if not missing else \"FAIL:\"+str(missing))' 2>/dev/null");
    const char* inv1 = "";
    inv1 = hexa_trim(hexa_exec(inv1_cmd));
    if ((strcmp(inv1, "PASS") == 0)) {
        pass = (pass + 1);
        printf("%s\n", "  ✅ INV-001: n6_constants.jsonl 7개 기본상수 존재");
    } else {
        fail = (fail + 1);
        printf("%s\n", hexa_concat("  ❌ INV-001: n6_constants.jsonl — ", inv1));
    }
    const char* inv2_cmd = hexa_concat(hexa_concat(hexa_concat(hexa_concat("python3 -c 'import subprocess as sp; r=sp.run([\"grep\",\"-c\",\"node\",\"", _shared), "/discovery_graph.json\"],capture_output=True,text=True); n=int(r.stdout.strip() or \"0\"); e=int(sp.run([\"grep\",\"-c\",\"edge\",\""), _shared), "/discovery_graph.json\"],capture_output=True,text=True).stdout.strip() or \"0\"); print(\"PASS:\"+str(n)+\"n/\"+str(e)+\"e\" if n>=1000 else \"FAIL:n=\"+str(n))' 2>/dev/null");
    const char* inv2 = "";
    inv2 = hexa_trim(hexa_exec(inv2_cmd));
    if (hexa_contains(inv2, "PASS")) {
        pass = (pass + 1);
        printf("%s\n", hexa_concat(hexa_concat("  ✅ INV-002: discovery_graph.json 스키마 유효 (", ((const char*)(hexa_split(inv2, "PASS:")).d[1])), ")"));
    } else {
        fail = (fail + 1);
        printf("%s\n", hexa_concat("  ❌ INV-002: discovery_graph.json — ", inv2));
    }
    const char* inv3_cmd = hexa_concat(hexa_concat("python3 -c 'import json; d=json.load(open(\"", _lockdown_path), "\")); ok=all(k in d for k in [\"levels\",\"projects\",\"keywords\",\"ai_protocol\"]); print(\"PASS\" if ok else \"FAIL:missing keys\")' 2>/dev/null");
    const char* inv3 = "";
    inv3 = hexa_trim(hexa_exec(inv3_cmd));
    if ((strcmp(inv3, "PASS") == 0)) {
        pass = (pass + 1);
        printf("%s\n", "  ✅ INV-003: core-lockdown.json 스키마 유효");
    } else {
        fail = (fail + 1);
        printf("%s\n", hexa_concat("  ❌ INV-003: core-lockdown.json — ", inv3));
    }
    const char* inv4_cmd = hexa_concat(hexa_concat("python3 -c 'import json; d=json.load(open(\"", _shared), "/absolute_rules.json\")); nc=len(d.get(\"common\",[])); np=len(d.get(\"projects\",{})); ok=nc>=8 and np>=7; print(\"PASS:R\"+str(nc)+\"/P\"+str(np) if ok else \"FAIL:common=\"+str(nc)+\",projects=\"+str(np))' 2>/dev/null");
    const char* inv4 = "";
    inv4 = hexa_trim(hexa_exec(inv4_cmd));
    if (hexa_contains(inv4, "PASS")) {
        pass = (pass + 1);
        printf("%s\n", hexa_concat(hexa_concat("  ✅ INV-004: absolute_rules.json 구조 유효 (", ((const char*)(hexa_split(inv4, "PASS:")).d[1])), ")"));
    } else {
        fail = (fail + 1);
        printf("%s\n", hexa_concat("  ❌ INV-004: absolute_rules.json — ", inv4));
    }
    const char* inv5_cmd = hexa_concat(hexa_concat("python3 -c 'import json; d=json.load(open(\"", _shared), "/core.json\")); ok=all(k in d for k in [\"systems\",\"commands\",\"folders\"]); ns=len(d.get(\"systems\",{})); nc=len(d.get(\"commands\",{})); print(\"PASS:S\"+str(ns)+\"/C\"+str(nc) if ok else \"FAIL\")' 2>/dev/null");
    const char* inv5 = "";
    inv5 = hexa_trim(hexa_exec(inv5_cmd));
    if (hexa_contains(inv5, "PASS")) {
        pass = (pass + 1);
        printf("%s\n", hexa_concat(hexa_concat("  ✅ INV-005: core.json 구조 유효 (", ((const char*)(hexa_split(inv5, "PASS:")).d[1])), ")"));
    } else {
        fail = (fail + 1);
        printf("%s\n", hexa_concat("  ❌ INV-005: core.json — ", inv5));
    }
    const char* inv6_cmd = hexa_concat(hexa_concat("python3 -c 'import json,os; base=\"", _shared), "/convergence\"; projs=[\"nexus\",\"anima\",\"n6-architecture\",\"papers\",\"hexa-lang\",\"void\",\"airgenome\"]; ok=0; fail=[]\nfor p in projs:\n    f=os.path.join(base,p+\".json\")\n    if os.path.exists(f):\n        d=json.load(open(f))\n        if all(k in d for k in [\"ossified\",\"stable\",\"failed\"]): ok+=1\n        else: fail.append(p+\":missing keys\")\n    else: fail.append(p+\":not found\")\nprint(\"PASS:\"+str(ok)+\"/7\" if ok==7 else \"FAIL:\"+\",\".join(fail))' 2>/dev/null");
    const char* inv6 = "";
    inv6 = hexa_trim(hexa_exec(inv6_cmd));
    if (hexa_contains(inv6, "PASS")) {
        pass = (pass + 1);
        printf("%s\n", hexa_concat(hexa_concat("  ✅ INV-006: convergence/ 7개 프로젝트 유효 (", ((const char*)(hexa_split(inv6, "PASS:")).d[1])), ")"));
    } else {
        fail = (fail + 1);
        printf("%s\n", hexa_concat("  ❌ INV-006: convergence/ — ", inv6));
    }
    const char* inv7_cmd = hexa_concat(hexa_concat("python3 -c 'import json,os; base=\"", _shared), "/todo\"; projs=[\"nexus\",\"anima\",\"n6-architecture\",\"papers\",\"hexa-lang\",\"void\",\"airgenome\"]; ok=0; fail=[]\nfor p in projs:\n    f=os.path.join(base,p+\".json\")\n    if os.path.exists(f):\n        d=json.load(open(f))\n        if \"tasks\" in d and len(d[\"tasks\"])>0: ok+=1\n        else: fail.append(p+\":empty\")\n    else: fail.append(p+\":not found\")\nprint(\"PASS:\"+str(ok)+\"/7\" if ok==7 else \"FAIL:\"+\",\".join(fail))' 2>/dev/null");
    const char* inv7 = "";
    inv7 = hexa_trim(hexa_exec(inv7_cmd));
    if (hexa_contains(inv7, "PASS")) {
        pass = (pass + 1);
        printf("%s\n", hexa_concat(hexa_concat("  ✅ INV-007: todo/ 7개 프로젝트 유효 (", ((const char*)(hexa_split(inv7, "PASS:")).d[1])), ")"));
    } else {
        fail = (fail + 1);
        printf("%s\n", hexa_concat("  ❌ INV-007: todo/ — ", inv7));
    }
    const char* inv8_cmd = hexa_concat(hexa_concat("python3 -c 'import json; d=json.load(open(\"", _shared), "/reality_map.json\")); nodes=d.get(\"nodes\",[]); n=len(nodes); ex=sum(1 for x in nodes if x.get(\"grade\")==\"EXACT\"); pct=round(ex/max(n,1)*100,1); ok=n>=100 and pct>=80; print(\"PASS:\"+str(n)+\"nodes/\"+str(pct)+\"%EXACT\" if ok else \"FAIL:n=\"+str(n)+\",exact=\"+str(pct)+\"%\")' 2>/dev/null");
    const char* inv8 = "";
    inv8 = hexa_trim(hexa_exec(inv8_cmd));
    if (hexa_contains(inv8, "PASS")) {
        pass = (pass + 1);
        printf("%s\n", hexa_concat(hexa_concat("  ✅ INV-008: reality_map.json 유효 (", ((const char*)(hexa_split(inv8, "PASS:")).d[1])), ")"));
    } else {
        fail = (fail + 1);
        printf("%s\n", hexa_concat("  ❌ INV-008: reality_map.json — ", inv8));
    }
    const char* inv9_cmd = hexa_concat(hexa_concat("python3 -c 'import json,os; base=\"", _shared), "/loop\"; projs=[\"nexus\",\"anima\",\"n6-architecture\"]; ok=0\nfor p in projs:\n    f=os.path.join(base,p+\".json\")\n    if os.path.exists(f):\n        d=json.load(open(f))\n        if \"phases\" in d: ok+=1\nprint(\"PASS:\"+str(ok)+\"/3\" if ok==3 else \"FAIL:\"+str(ok)+\"/3\")' 2>/dev/null");
    const char* inv9 = "";
    inv9 = hexa_trim(hexa_exec(inv9_cmd));
    if (hexa_contains(inv9, "PASS")) {
        pass = (pass + 1);
        printf("%s\n", hexa_concat(hexa_concat("  ✅ INV-009: loop/ 3개 성장 루프 유효 (", ((const char*)(hexa_split(inv9, "PASS:")).d[1])), ")"));
    } else {
        fail = (fail + 1);
        printf("%s\n", hexa_concat("  ❌ INV-009: loop/ — ", inv9));
    }
    printf("%s\n", "");
    long total = (pass + fail);
    if ((fail == 0)) {
        return printf("%s\n", hexa_concat(hexa_concat(hexa_concat("✅ 전체 통과: ", hexa_int_to_str((long)(pass))), "/"), hexa_int_to_str((long)(total))));
    } else {
        return printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("❌ 실패 있음: ", hexa_int_to_str((long)(pass))), "/"), hexa_int_to_str((long)(total))), " ("), hexa_int_to_str((long)(fail))), "건 실패)"));
    }
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    _home = hexa_trim(hexa_exec("printenv HOME"));
    _shared = hexa_concat(_home, "/Dev/nexus/shared");
    _lockdown_path = hexa_concat(_shared, "/core-lockdown.json");
    _args = hexa_args();
    _cmd = (((_args.n >= 3)) ? (((const char*)_args.d[2])) : ("help"));
    if ((strcmp(_cmd, "check") == 0)) {
        const char* file = (((_args.n >= 4)) ? (((const char*)_args.d[3])) : (""));
        if ((strcmp(file, "") == 0)) {
            printf("%s\n", "사용: hexa guard.hexa check <file_path>");
        } else {
            cmd_check(file);
        }
    } else {
        if ((strcmp(_cmd, "checklist") == 0)) {
            cmd_checklist();
        } else {
            if ((strcmp(_cmd, "recurrence") == 0)) {
                const char* cat = (((_args.n >= 4)) ? (((const char*)_args.d[3])) : (""));
                const char* proj = (((_args.n >= 5)) ? (((const char*)_args.d[4])) : ("nexus"));
                if ((strcmp(cat, "") == 0)) {
                    printf("%s\n", "사용: hexa guard.hexa recurrence <category> [project]");
                } else {
                    cmd_recurrence(cat, proj);
                }
            } else {
                if ((strcmp(_cmd, "root-cause") == 0)) {
                    cmd_root_cause();
                } else {
                    if ((strcmp(_cmd, "verify") == 0)) {
                        cmd_verify();
                    } else {
                        printf("%s\n", "guard.hexa — prism 수준 L0/수렴 강제 가드");
                        printf("%s\n", "");
                        printf("%s\n", "사용법:");
                        printf("%s\n", "  hexa guard.hexa check <file>        — L0 수정 감지 + 승인 요청");
                        printf("%s\n", "  hexa guard.hexa checklist            — 커밋 전 체크리스트");
                        printf("%s\n", "  hexa guard.hexa recurrence <cat> [proj] — 반복 카운터 +1");
                        printf("%s\n", "  hexa guard.hexa root-cause           — 근본원인 코드 수정 확인");
                        printf("%s\n", "  hexa guard.hexa verify               — L0 불변식 계약 테스트 (9건)");
                    }
                }
            }
        }
    }
    return 0;
}
