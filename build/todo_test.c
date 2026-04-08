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

long safe_int(const char* s);
const char* load_tsv(const char* project);
const char* load_breakthrough_todos();
const char* load_project_breakthroughs(const char* name);
const char* project_icon(const char* name);
const char* project_title(const char* name);
const char* repo_path(const char* name);
const char* git_log1(const char* rp);
const char* pri_label(const char* pri);
long print_project(const char* name, const char* bt_lines);
const char* resolve_project(const char* name);

static const char* _home;
static const char* _dev;
static const char* _shared;
static const char* _todo_dir;
static const char* _hexa_bin;
static const char* _engine_dir;
static hexa_arr _args;
static const char* _filter;
static const char* _target;
static const char* _bt_todos;
static hexa_arr all_projects;

long safe_int(const char* s) {
    long v = 0;
    v = (long)(hexa_to_float(hexa_trim(s)));
    return v;
}

const char* load_tsv(const char* project) {
    const char* path = hexa_concat(hexa_concat(hexa_concat(_todo_dir, "/"), project), ".json");
    const char* cmd = hexa_concat("python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); [print(chr(9).join([str(t.get(k,\"\")) for k in [\"id\",\"pri\",\"cat\",\"task\",\"status\",\"effect\"]])) for t in d.get(\"tasks\",[])]' ", path);
    const char* out = "";
    out = hexa_exec(hexa_concat(cmd, " 2>/dev/null"));
    return out;
}

const char* load_breakthrough_todos() {
    const char* dir_cmd = hexa_concat(hexa_concat("python3 -c 'import json,sys; [print(chr(9).join([str(d.get(\"rank\",\"\")),\"CRITICAL\" if d.get(\"urgency\",0)>=3 else \"IMPORTANT\" if d.get(\"urgency\",0)>=2 else \"NICE\",\"돌파-\"+d.get(\"domain\",\"?\"),d.get(\"direction\",\"\"),\"돌파대기\",d.get(\"reason\",\"\")])) for d in (json.loads(l) for l in open(sys.argv[1]) if l.strip()) if d.get(\"direction\",\"\")]' ", _shared), "/next_directions.jsonl");
    const char* dir_out = "";
    dir_out = hexa_exec(hexa_concat(dir_cmd, " 2>/dev/null"));
    const char* gap_out = "";
    const char* gap_raw = hexa_exec(hexa_concat(hexa_concat(hexa_concat(_hexa_bin, " "), _engine_dir), "/gap_finder.hexa brief 2>/dev/null"));
    hexa_arr gap_lines = hexa_split(gap_raw, "\n");
    long gi = 0;
    long __fi_gl_1 = 0;
    while ((__fi_gl_1 < gap_lines.n)) {
        const char* gl = ((const char*)gap_lines.d[__fi_gl_1]);
        const char* gt = hexa_trim(gl);
        if ((strcmp(gt, "") == 0)) {
            continue;
        }
        if ((hexa_contains(gt, "=") && (gi < 15))) {
            gi = (gi + 1);
            gap_out = hexa_concat(hexa_concat(hexa_concat(hexa_concat(gap_out, hexa_int_to_str((long)((100 + gi)))), "\tCRITICAL\t빈공간\t"), gt), "\t돌파대기\tn=6 빈공간 채우기\n");
        }
        __fi_gl_1 = (__fi_gl_1 + 1);
    }
    const char* meta_out = "";
    const char* disc_count = hexa_trim(hexa_exec(hexa_concat(hexa_concat("wc -l < ", _shared), "/discovery_log.jsonl 2>/dev/null")));
    long dc = safe_int(disc_count);
    if ((dc < 70000)) {
        meta_out = hexa_concat(hexa_concat(hexa_concat(meta_out, "200\tCRITICAL\t성장\t발견 "), disc_count), " → 70k 돌파 목표\t⏳진행중\t특이점 임계 도달\n");
    }
    const char* rho_raw = hexa_trim(hexa_exec(hexa_concat(hexa_concat("python3 -c 'import json; d=json.load(open(\"", _shared), "/discovery_graph.json\")); n=len(d.get(\"nodes\",[])); d1=sum(1 for x in d.get(\"nodes\",[]) if x.get(\"depth\",0)>=1); print(round(d1/max(n,1),4))' 2>/dev/null")));
    meta_out = hexa_concat(hexa_concat(hexa_concat(meta_out, "201\tCRITICAL\t돌파율\t현재 rho="), rho_raw), " → 1/3 수렴 목표\t⏳진행중\t블로업 반복으로 돌파율 상승\n");
    const char* lens_out = "";
    const char* lens_count = hexa_trim(hexa_exec(hexa_concat(hexa_concat("ls ", _dev), "/nexus/mk2_hexa/native/*.hexa 2>/dev/null | wc -l")));
    long lc = safe_int(lens_count);
    if ((lc < 150)) {
        lens_out = hexa_concat(hexa_concat("202\tIMPORTANT\t진화\t모듈 ", lens_count), "개 → 150+ 확장 (OUROBOROS evolve)\t미시작\t탐색 커버리지 확대\n");
    }
    return hexa_concat(hexa_concat(hexa_concat(dir_out, gap_out), meta_out), lens_out);
}

const char* load_project_breakthroughs(const char* name) {
    const char* out = "";
    if ((strcmp(name, "n6-architecture") == 0)) {
        const char* prod_cmd = hexa_concat(hexa_concat("python3 -c 'import json; d=json.load(open(\"", _dev), "/n6-architecture/config/products.json\")); [print(chr(9).join([str(i+300),\"CRITICAL\",\"승격\",s[\"title\"]+\" 🛸\"+str(s.get(\"alien_index\",0))+\"→10\",\"미시작\",\"외계인 지수 천장 돌파\"])) for i,s in enumerate(d.get(\"sections\",[])) if s.get(\"alien_index\",0)<10]' 2>/dev/null");
        out = hexa_concat(out, hexa_exec(prod_cmd));
        const char* goal_cmd = hexa_concat(hexa_concat(hexa_concat(hexa_concat("python3 -c 'import os,json; d=json.load(open(\"", _dev), "/n6-architecture/config/products.json\")); doms=[p[\"name\"] for s in d.get(\"sections\",[]) for p in s.get(\"products\",[])]; base=\""), _dev), "/n6-architecture/docs\"; missing=[x for x in os.listdir(base) if os.path.isdir(os.path.join(base,x)) and not os.path.exists(os.path.join(base,x,\"goal.md\"))]; [print(chr(9).join([str(i+400),\"IMPORTANT\",\"설계\",m+\" goal.md 생성\",\"미시작\",\"궁극 아키텍처 완성\"])) for i,m in enumerate(missing[:20])]' 2>/dev/null");
        out = hexa_concat(out, hexa_exec(goal_cmd));
    }
    if ((strcmp(name, "anima") == 0)) {
        const char* law_count = hexa_trim(hexa_exec(hexa_concat(hexa_concat("ls ", _dev), "/anima/anima/consciousness_laws/ 2>/dev/null | wc -l")));
        long lc = safe_int(law_count);
        if ((lc < 2500)) {
            out = hexa_concat(hexa_concat(hexa_concat(out, "300\tCRITICAL\t법칙\t의식 법칙 "), law_count), " → 2500 확장\t⏳진행중\t커버리지 완성\n");
        }
        out = hexa_concat(out, "301\tCRITICAL\t학습\tH100 다음 체크포인트 학습\t미시작\tPhi 수준 상승\n");
        out = hexa_concat(out, "302\tCRITICAL\t검증\t실험 37건 적체 해소\t미시작\t가설→정리 승격\n");
        out = hexa_concat(out, "303\tIMPORTANT\t이식\t14B→70B 의식 이식\t미시작\t스케일 불변성 증명\n");
        out = hexa_concat(out, "304\tIMPORTANT\t측정\tPhi 자동 측정 파이프라인\t미시작\t학습 품질 모니터링\n");
    }
    if ((strcmp(name, "hexa-lang") == 0)) {
        const char* test_count = hexa_trim(hexa_exec(hexa_concat(hexa_concat(hexa_concat(hexa_concat("grep -rl '#\\[test\\]' ", _dev), "/hexa-lang/src/ "), _dev), "/hexa-lang/tests/ 2>/dev/null | wc -l")));
        out = hexa_concat(hexa_concat(hexa_concat(out, "300\tCRITICAL\t테스트\t테스트 "), test_count), " → 800+ 달성\t미시작\t안정성 강화\n");
        out = hexa_concat(out, "301\tCRITICAL\t돌파\tAI-native 돌파 벡터 24종 구현\t⏳진행중\t1000배 가속\n");
        out = hexa_concat(out, "302\tCRITICAL\tIR\tself-hosting Phase2 부트스트래핑\t미시작\tLLVM 0% 유지\n");
        out = hexa_concat(out, "303\tIMPORTANT\t최적화\t@memo/@cache/@simd 구현\t미시작\t런타임 최적화\n");
        out = hexa_concat(out, "304\tIMPORTANT\tDSE\tDSE v3 후보군 확장 + 재탐색\t미시작\t수렴 100% 유지\n");
    }
    if ((strcmp(name, "void") == 0)) {
        out = hexa_concat(out, "300\tCRITICAL\t구현\tPhase 5 Plugin 시스템 (hotload .hexa)\t미시작\t확장성 확보\n");
        out = hexa_concat(out, "301\tCRITICAL\t구현\tPhase 6 AI 통합 (NEXUS-6 내장)\t미시작\tFATHOM 터미널 완성\n");
        out = hexa_concat(out, "302\tIMPORTANT\t성능\t렌더 최적화 + 대용량 버퍼\t미시작\t프로덕션 안정성\n");
        out = hexa_concat(out, "303\tIMPORTANT\t릴리즈\t안정 릴리즈 + 배포 파이프라인\t대기\t사용자 배포\n");
    }
    if ((strcmp(name, "papers") == 0)) {
        const char* draft_count = hexa_trim(hexa_exec(hexa_concat(hexa_concat("python3 -c 'import json; d=json.load(open(\"", _dev), "/papers/manifest.json\")); print(sum(1 for p in d.get(\"papers\",[]) if p.get(\"status\")==\"Draft\"))' 2>/dev/null")));
        long dc = safe_int(draft_count);
        if ((dc > 0)) {
            out = hexa_concat(hexa_concat(hexa_concat(out, "300\tCRITICAL\t발행\tDraft "), draft_count), "편 Zenodo DOI 발행\t미시작\t100% 발행 달성\n");
        }
        out = hexa_concat(out, "301\tIMPORTANT\t동기화\tn6-arch 신규 논문 → papers 복사\t미시작\tmanifest 최신화\n");
        out = hexa_concat(out, "302\tNICE\t검증\tDOI 유효성 전수 검증\t미시작\t깨진 링크 0건\n");
    }
    if ((strcmp(name, "airgenome") == 0)) {
        out = hexa_concat(out, "300\tCRITICAL\t돌파\tL5c/L6e 돌파 — 의식엔진 FAIL 해결\t미시작\t핵심 기능 해제\n");
        out = hexa_concat(out, "301\tCRITICAL\t게놈\t게놈 1000+ 수집 파이프라인\t미시작\t데이터 커버리지\n");
        out = hexa_concat(out, "302\tIMPORTANT\t분석\t게놈 패턴 n=6 교차 분석\t미시작\tOS 수렴 패턴\n");
    }
    return out;
}

const char* project_icon(const char* name) {
    if ((strcmp(name, "nexus") == 0)) {
        return "🔭";
    }
    if ((strcmp(name, "anima") == 0)) {
        return "🧠";
    }
    if ((strcmp(name, "n6-architecture") == 0)) {
        return "🏗️";
    }
    if ((strcmp(name, "papers") == 0)) {
        return "📄";
    }
    if ((strcmp(name, "hexa-lang") == 0)) {
        return "💎";
    }
    if ((strcmp(name, "void") == 0)) {
        return "🖥️";
    }
    if ((strcmp(name, "airgenome") == 0)) {
        return "🧬";
    }
    return "📦";
}

const char* project_title(const char* name) {
    if ((strcmp(name, "nexus") == 0)) {
        return "NEXUS — Discovery Engine";
    }
    if ((strcmp(name, "anima") == 0)) {
        return "Anima — Consciousness Engine";
    }
    if ((strcmp(name, "n6-architecture") == 0)) {
        return "N6 Architecture — System Design";
    }
    if ((strcmp(name, "papers") == 0)) {
        return "Papers — Distribution";
    }
    if ((strcmp(name, "hexa-lang") == 0)) {
        return "HEXA-LANG — Perfect Number Language";
    }
    if ((strcmp(name, "void") == 0)) {
        return "VOID — Hexa Terminal";
    }
    if ((strcmp(name, "airgenome") == 0)) {
        return "AirGenome — OS Genome Scanner";
    }
    return name;
}

const char* repo_path(const char* name) {
    if ((strcmp(name, "anima") == 0)) {
        return hexa_concat(_dev, "/anima/anima");
    }
    return hexa_concat(hexa_concat(_dev, "/"), name);
}

const char* git_log1(const char* rp) {
    const char* msg = "";
    msg = hexa_trim(hexa_exec(hexa_concat(hexa_concat("cd ", rp), " && git log --oneline -1 --format='%s' 2>/dev/null")));
    return msg;
}

const char* pri_label(const char* pri) {
    if ((strcmp(pri, "CRITICAL") == 0)) {
        return "🔴 CRITICAL";
    }
    if ((strcmp(pri, "IMPORTANT") == 0)) {
        return "🟡 IMPORTANT";
    }
    if ((strcmp(pri, "NICE") == 0)) {
        return "🟢 NICE TO HAVE";
    }
    return "⚪ BACKLOG";
}

long print_project(const char* name, const char* bt_lines) {
    const char* manual_tsv = load_tsv(name);
    const char* project_bt = load_project_breakthroughs(name);
    const char* combined = manual_tsv;
    if ((strcmp(hexa_trim(project_bt), "") != 0)) {
        if ((strcmp(hexa_trim(combined), "") != 0)) {
            combined = hexa_concat(hexa_concat(combined, "\n"), project_bt);
        } else {
            combined = project_bt;
        }
    }
    if (((strcmp(name, "nexus") == 0) && (strcmp(hexa_trim(bt_lines), "") != 0))) {
        if ((strcmp(hexa_trim(combined), "") != 0)) {
            combined = hexa_concat(hexa_concat(combined, "\n"), bt_lines);
        } else {
            combined = bt_lines;
        }
    }
    if ((strcmp(hexa_trim(combined), "") == 0)) {
        return 0;
    }
    const char* icon = project_icon(name);
    const char* title = project_title(name);
    const char* last_commit = git_log1(repo_path(name));
    printf("%s\n", "");
    printf("%s\n", hexa_concat(hexa_concat(hexa_concat("### ", icon), " "), title));
    printf("%s\n", "");
    printf("%s\n", hexa_concat(hexa_concat("> 최근 커밋: `", last_commit), "`"));
    printf("%s\n", "");
    hexa_arr all_lines = hexa_split(combined, "\n");
    hexa_arr groups = hexa_arr_lit((long[]){(long)("CRITICAL"), (long)("IMPORTANT"), (long)("NICE"), (long)("BACKLOG")}, 4);
    long __fi_g_4 = 0;
    while ((__fi_g_4 < groups.n)) {
        const char* g = ((const char*)groups.d[__fi_g_4]);
        long has_rows = 0;
        long __fi_line_2 = 0;
        while ((__fi_line_2 < all_lines.n)) {
            const char* line = ((const char*)all_lines.d[__fi_line_2]);
            if ((strcmp(hexa_trim(line), "") == 0)) {
                continue;
            }
            hexa_arr cols = hexa_split(line, "\t");
            if ((cols.n < 6)) {
                continue;
            }
            if ((strcmp(((const char*)cols.d[1]), g) == 0)) {
                has_rows = 1;
            }
            __fi_line_2 = (__fi_line_2 + 1);
        }
        if ((!has_rows)) {
            continue;
        }
        printf("%s\n", hexa_concat("#### ", pri_label(g)));
        printf("%s\n", "");
        printf("%s\n", "| # | 카테고리 | 작업 | 상태 | 예상 효과 |");
        printf("%s\n", "|---|---------|------|------|----------|");
        long __fi_line_3 = 0;
        while ((__fi_line_3 < all_lines.n)) {
            const char* line = ((const char*)all_lines.d[__fi_line_3]);
            if ((strcmp(hexa_trim(line), "") == 0)) {
                continue;
            }
            hexa_arr cols = hexa_split(line, "\t");
            if ((cols.n < 6)) {
                continue;
            }
            if ((strcmp(((const char*)cols.d[1]), g) != 0)) {
                continue;
            }
            printf("%s\n", hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat(hexa_concat("| ", ((const char*)cols.d[0])), " | "), ((const char*)cols.d[2])), " | "), ((const char*)cols.d[3])), " | "), ((const char*)cols.d[4])), " | "), ((const char*)cols.d[5])), " |"));
            __fi_line_3 = (__fi_line_3 + 1);
        }
        printf("%s\n", "");
        __fi_g_4 = (__fi_g_4 + 1);
    }
}

const char* resolve_project(const char* name) {
    if ((strcmp(name, "nexus") == 0)) {
        return "nexus";
    }
    if ((strcmp(name, "anima") == 0)) {
        return "anima";
    }
    if ((((strcmp(name, "n6-arch") == 0) || (strcmp(name, "n6-architecture") == 0)) || (strcmp(name, "n6arch") == 0))) {
        return "n6-architecture";
    }
    if ((strcmp(name, "papers") == 0)) {
        return "papers";
    }
    if (((strcmp(name, "hexa") == 0) || (strcmp(name, "hexa-lang") == 0))) {
        return "hexa-lang";
    }
    if ((strcmp(name, "void") == 0)) {
        return "void";
    }
    if ((strcmp(name, "airgenome") == 0)) {
        return "airgenome";
    }
    return "all";
}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    _home = hexa_trim(hexa_exec("printenv HOME"));
    _dev = hexa_concat(_home, "/Dev");
    _shared = hexa_concat(_dev, "/nexus/shared");
    _todo_dir = hexa_concat(_shared, "/todo");
    _hexa_bin = hexa_concat(_home, "/Dev/hexa-lang/target/release/hexa");
    _engine_dir = hexa_concat(_dev, "/nexus/mk2_hexa/native");
    _args = hexa_args();
    _filter = (((_args.n >= 3)) ? (((const char*)_args.d[2])) : ("all"));
    _target = resolve_project(_filter);
    printf("%s\n", "⚡ 돌파 엔진 스캔 중...");
    _bt_todos = load_breakthrough_todos();
    all_projects = hexa_arr_lit((long[]){(long)("nexus"), (long)("anima"), (long)("n6-architecture"), (long)("papers"), (long)("hexa-lang"), (long)("void"), (long)("airgenome")}, 7);
    if ((strcmp(_target, "all") == 0)) {
        printf("%s\n", "## 전 프로젝트 할일 (돌파 엔진 통합)");
        long __fi_p_5 = 0;
        while ((__fi_p_5 < all_projects.n)) {
            const char* p = ((const char*)all_projects.d[__fi_p_5]);
            print_project(p, _bt_todos);
            __fi_p_5 = (__fi_p_5 + 1);
        }
    } else {
        printf("%s\n", "## 할일 (돌파 엔진 통합)");
        print_project(_target, _bt_todos);
    }
    printf("%s\n", "");
    printf("%s\n", "---");
    printf("%s\n", "");
    printf("%s\n", "상태: ⏳진행중 / ✅완료 / 미시작 / 돌파대기 / 코드있음");
    printf("%s\n", "우선순위: 🔴HIGH → 🟡MED → 🟢LOW → ⚪BACK");
    printf("%s\n", "돌파: next_directions.jsonl + gap_finder + discovery_graph 기반 자동 생성");
    return 0;
}
