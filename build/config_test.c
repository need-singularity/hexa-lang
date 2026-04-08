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

const char* config_path();
const char* parse_toml(const char* content);
const char* get_value(const char* parsed, const char* key);
const char* load_config();
const char* config_get(const char* key);
long config_set(const char* key, const char* value);
long config_list();
long config_section(const char* section_name);
long config_init();
long hexa_user_main();

static const char* home_env = "HOME";
static const char* config_subpath = "/.nexus/config.toml";
static const char* default_loop_domain = "number_theory";
static long default_loop_cycles = 1;
static long default_loop_max_ouroboros = 6;
static long default_loop_max_meta = 6;
static long default_loop_forge_after = 3;
static long default_daemon_interval = 30;
static const char* default_blowup_domain = "number_theory";
static long default_blowup_depth = 6;
static long default_blowup_max_corollaries = 216;
static double default_blowup_min_confidence = 0.15;
static double default_evolution_serendipity = 0.3;
static double default_evolution_min_verify = 0.5;
static long default_evolution_max_mutations = 20;
static long default_forge_max_candidates = 20;
static double default_forge_min_confidence = 0.2;
static double default_forge_similarity = 0.8;
static long default_log_max_bytes = 1048576;
static long default_log_max_files = 5;

const char* config_path() {
    const char* home_candidates = hexa_exec("printenv HOME");
    return hexa_concat(home_candidates, config_subpath);
}

const char* parse_toml(const char* content) {
    hexa_arr lines = hexa_split(content, "\n");
    const char* current_section = "";
    const char* result = "";
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") == 0)) {
            i = (i + 1);
            continue;
        }
        if ((strcmp(hexa_substr(line, 0, 1), "#") == 0)) {
            i = (i + 1);
            continue;
        }
        if ((strcmp(hexa_substr(line, 0, 1), "[") == 0)) {
            long end = hexa_index_of(line, "]");
            if ((end > 0)) {
                current_section = hexa_trim(hexa_substr(line, 1, end));
            }
            i = (i + 1);
            continue;
        }
        long eq = hexa_index_of(line, "=");
        if ((eq > 0)) {
            const char* key = hexa_trim(hexa_substr(line, 0, eq));
            const char* val = hexa_trim(hexa_substr(line, (eq + 1), hexa_str_len(line)));
            long hash = hexa_index_of(val, "#");
            if ((hash > 0)) {
                val = hexa_trim(hexa_substr(val, 0, hash));
            }
            if ((hexa_str_len(val) >= 2)) {
                if ((strcmp(hexa_substr(val, 0, 1), "\"") == 0)) {
                    const char* last = hexa_substr(val, (hexa_str_len(val) - 1), hexa_str_len(val));
                    if ((strcmp(last, "\"") == 0)) {
                        val = hexa_substr(val, 1, (hexa_str_len(val) - 1));
                    }
                }
            }
            const char* full_key = (((strcmp(current_section, "") != 0)) ? (hexa_concat(hexa_concat(current_section, "."), key)) : (key));
            result = hexa_concat(hexa_concat(hexa_concat(hexa_concat(result, full_key), "="), val), "\n");
        }
        i = (i + 1);
    }
    return result;
}

const char* get_value(const char* parsed, const char* key) {
    const char* needle = hexa_concat(key, "=");
    hexa_arr lines = hexa_split(parsed, "\n");
    long i = 0;
    while ((i < lines.n)) {
        const char* line = ((const char*)lines.d[i]);
        if ((hexa_index_of(line, needle) == 0)) {
            return hexa_substr(line, hexa_str_len(needle), hexa_str_len(line));
        }
        i = (i + 1);
    }
    return "";
}

const char* load_config() {
    const char* path = config_path();
    const char* content = hexa_read_file(path);
    if ((strcmp(content, "") == 0)) {
        return "";
    }
    return parse_toml(content);
}

const char* config_get(const char* key) {
    const char* parsed = load_config();
    const char* val = get_value(parsed, key);
    if ((strcmp(val, "") != 0)) {
        return val;
    }
    if ((strcmp(key, "loop.domain") == 0)) {
        return default_loop_domain;
    } else if ((strcmp(key, "loop.cycles") == 0)) {
        return hexa_int_to_str((long)(default_loop_cycles));
    } else if ((strcmp(key, "loop.max_ouroboros_cycles") == 0)) {
        return hexa_int_to_str((long)(default_loop_max_ouroboros));
    } else if ((strcmp(key, "loop.max_meta_cycles") == 0)) {
        return hexa_int_to_str((long)(default_loop_max_meta));
    } else if ((strcmp(key, "loop.forge_after_n_cycles") == 0)) {
        return hexa_int_to_str((long)(default_loop_forge_after));
    } else if ((strcmp(key, "daemon.interval_min") == 0)) {
        return hexa_int_to_str((long)(default_daemon_interval));
    } else if ((strcmp(key, "blowup.domain") == 0)) {
        return default_blowup_domain;
    } else if ((strcmp(key, "blowup.max_depth") == 0)) {
        return hexa_int_to_str((long)(default_blowup_depth));
    } else if ((strcmp(key, "blowup.max_corollaries") == 0)) {
        return hexa_int_to_str((long)(default_blowup_max_corollaries));
    } else if ((strcmp(key, "blowup.min_confidence") == 0)) {
        return hexa_float_to_str((double)(default_blowup_min_confidence));
    } else if ((strcmp(key, "evolution.serendipity_ratio") == 0)) {
        return hexa_float_to_str((double)(default_evolution_serendipity));
    } else if ((strcmp(key, "evolution.min_verification_score") == 0)) {
        return hexa_float_to_str((double)(default_evolution_min_verify));
    } else if ((strcmp(key, "evolution.max_mutations_per_cycle") == 0)) {
        return hexa_int_to_str((long)(default_evolution_max_mutations));
    } else if ((strcmp(key, "forge.max_candidates") == 0)) {
        return hexa_int_to_str((long)(default_forge_max_candidates));
    } else if ((strcmp(key, "forge.min_confidence") == 0)) {
        return hexa_float_to_str((double)(default_forge_min_confidence));
    } else if ((strcmp(key, "forge.similarity_threshold") == 0)) {
        return hexa_float_to_str((double)(default_forge_similarity));
    } else if ((strcmp(key, "log_rotation.max_bytes") == 0)) {
        return hexa_int_to_str((long)(default_log_max_bytes));
    } else if ((strcmp(key, "log_rotation.max_files") == 0)) {
        return hexa_int_to_str((long)(default_log_max_files));
    } else {
        return "";
    }

















}

long config_set(const char* key, const char* value) {
    const char* path = config_path();
    const char* content = hexa_read_file(path);
    long dot = hexa_index_of(key, ".");
    const char* section = "";
    const char* name = key;
    if ((dot > 0)) {
        section = hexa_substr(key, 0, dot);
        name = hexa_substr(key, (dot + 1), hexa_str_len(key));
    }
    const char* section_header = hexa_concat(hexa_concat("[", section), "]");
    const char* entry = hexa_concat(hexa_concat(name, " = "), value);
    if ((strcmp(content, "") == 0)) {
        const char* new_content = (((strcmp(section, "") != 0)) ? (hexa_concat(hexa_concat(hexa_concat(section_header, "\n"), entry), "\n")) : (hexa_concat(entry, "\n")));
        hexa_write_file(path, new_content);
        printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat("set ", key), " = "), value), " (new file)"));
        return 0;
    }
    hexa_arr lines = hexa_split(content, "\n");
    hexa_arr result = hexa_arr_new();
    long in_section = 0;
    long replaced = 0;
    long section_exists = 0;
    long i = 0;
    while ((i < lines.n)) {
        const char* line = ((const char*)lines.d[i]);
        const char* trimmed = hexa_trim(line);
        if ((hexa_str_len(trimmed) > 0)) {
            if ((strcmp(hexa_substr(trimmed, 0, 1), "[") == 0)) {
                if (in_section) {
                    if ((!replaced)) {
                        result = hexa_arr_push(result, (long)(entry));
                        replaced = 1;
                    }
                }
                if ((strcmp(trimmed, section_header) == 0)) {
                    in_section = 1;
                    section_exists = 1;
                } else {
                    in_section = 0;
                }
            }
        }
        if (in_section) {
            if ((strcmp(hexa_substr(trimmed, 0, 1), "#") != 0)) {
                if ((strcmp(hexa_substr(trimmed, 0, 1), "[") != 0)) {
                    long eq = hexa_index_of(trimmed, "=");
                    if ((eq > 0)) {
                        const char* k = hexa_trim(hexa_substr(trimmed, 0, eq));
                        if ((strcmp(k, name) == 0)) {
                            result = hexa_arr_push(result, (long)(entry));
                            replaced = 1;
                            i = (i + 1);
                            continue;
                        }
                    }
                }
            }
        }
        result = hexa_arr_push(result, (long)(line));
        i = (i + 1);
    }
    if (in_section) {
        if ((!replaced)) {
            result = hexa_arr_push(result, (long)(entry));
            replaced = 1;
        }
    }
    if ((!section_exists)) {
        if ((strcmp(section, "") != 0)) {
            result = hexa_arr_push(result, (long)(""));
            result = hexa_arr_push(result, (long)(section_header));
        }
        result = hexa_arr_push(result, (long)(entry));
    }
    const char* out = "";
    long j = 0;
    while ((j < result.n)) {
        out = hexa_concat(hexa_concat(out, ((const char*)result.d[j])), "\n");
        j = (j + 1);
    }
    hexa_write_file(path, out);
    return printf("%s", hexa_concat(hexa_concat(hexa_concat("set ", key), " = "), value));
}

long config_list() {
    const char* parsed = load_config();
    if ((strcmp(parsed, "") == 0)) {
        printf("%s", hexa_concat(hexa_concat("(no config file found at ", config_path()), ")"));
        printf("%s", "run 'hexa config.hexa init' to create default config");
        return 0;
    }
    hexa_arr lines = hexa_split(parsed, "\n");
    long count = 0;
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") != 0)) {
            printf("%s", line);
            count = (count + 1);
        }
        i = (i + 1);
    }
    return printf("%s", hexa_concat(hexa_concat("--- ", hexa_int_to_str((long)(count))), " setting(s)"));
}

long config_section(const char* section_name) {
    const char* parsed = load_config();
    const char* prefix = hexa_concat(section_name, ".");
    hexa_arr lines = hexa_split(parsed, "\n");
    long count = 0;
    long i = 0;
    while ((i < lines.n)) {
        const char* line = hexa_trim(((const char*)lines.d[i]));
        if ((strcmp(line, "") != 0)) {
            if ((hexa_index_of(line, prefix) == 0)) {
                printf("%s", line);
                count = (count + 1);
            }
        }
        i = (i + 1);
    }
    if ((count == 0)) {
        printf("%s", hexa_concat(hexa_concat("(no configured values for [", section_name), "], showing defaults)"));
        if ((strcmp(section_name, "loop") == 0)) {
            printf("%s", hexa_concat("loop.domain=", default_loop_domain));
            printf("%s", hexa_concat("loop.cycles=", hexa_int_to_str((long)(default_loop_cycles))));
            printf("%s", hexa_concat("loop.max_ouroboros_cycles=", hexa_int_to_str((long)(default_loop_max_ouroboros))));
            printf("%s", hexa_concat("loop.max_meta_cycles=", hexa_int_to_str((long)(default_loop_max_meta))));
            return printf("%s", hexa_concat("loop.forge_after_n_cycles=", hexa_int_to_str((long)(default_loop_forge_after))));
        } else if ((strcmp(section_name, "daemon") == 0)) {
            return printf("%s", hexa_concat("daemon.interval_min=", hexa_int_to_str((long)(default_daemon_interval))));
        } else if ((strcmp(section_name, "blowup") == 0)) {
            printf("%s", hexa_concat("blowup.domain=", default_blowup_domain));
            printf("%s", hexa_concat("blowup.max_depth=", hexa_int_to_str((long)(default_blowup_depth))));
            printf("%s", hexa_concat("blowup.max_corollaries=", hexa_int_to_str((long)(default_blowup_max_corollaries))));
            return printf("%s", hexa_concat("blowup.min_confidence=", hexa_float_to_str((double)(default_blowup_min_confidence))));
        } else if ((strcmp(section_name, "evolution") == 0)) {
            printf("%s", hexa_concat("evolution.serendipity_ratio=", hexa_float_to_str((double)(default_evolution_serendipity))));
            printf("%s", hexa_concat("evolution.min_verification_score=", hexa_float_to_str((double)(default_evolution_min_verify))));
            return printf("%s", hexa_concat("evolution.max_mutations_per_cycle=", hexa_int_to_str((long)(default_evolution_max_mutations))));
        } else if ((strcmp(section_name, "forge") == 0)) {
            printf("%s", hexa_concat("forge.max_candidates=", hexa_int_to_str((long)(default_forge_max_candidates))));
            printf("%s", hexa_concat("forge.min_confidence=", hexa_float_to_str((double)(default_forge_min_confidence))));
            return printf("%s", hexa_concat("forge.similarity_threshold=", hexa_float_to_str((double)(default_forge_similarity))));
        } else if ((strcmp(section_name, "log_rotation") == 0)) {
            printf("%s", hexa_concat("log_rotation.max_bytes=", hexa_int_to_str((long)(default_log_max_bytes))));
            return printf("%s", hexa_concat("log_rotation.max_files=", hexa_int_to_str((long)(default_log_max_files))));
        } else {
            return printf("%s", hexa_concat("unknown section: ", section_name));
        }





    } else {
        return printf("%s", hexa_concat(hexa_concat(hexa_concat(hexa_concat("--- ", hexa_int_to_str((long)(count))), " setting(s) in ["), section_name), "]"));
    }
}

long config_init() {
    const char* path = config_path();
    const char* existing = hexa_read_file(path);
    if ((strcmp(existing, "") != 0)) {
        printf("%s", hexa_concat("config already exists at ", path));
        printf("%s", "use 'set' to modify individual values");
        return 0;
    }
    const char* template = "# NEXUS-6 Configuration\n# Place this file at ~/.nexus/config.toml\n# All values are optional — hardcoded defaults are shown.\n\n[loop]\n# domain = \"number_theory\"\n# cycles = 1\n# max_ouroboros_cycles = 6\n# max_meta_cycles = 6\n# forge_after_n_cycles = 3\n\n[daemon]\n# domain = \"number_theory\"\n# interval_min = 30\n# max_loops = 0          # 0 = infinite\n\n[blowup]\n# domain = \"number_theory\"\n# max_depth = 6\n# max_corollaries = 216\n# min_confidence = 0.15\n# transfer_domains = [\"physics\", \"mathematics\", \"information\", \"biology\", \"consciousness\", \"architecture\"]\n\n[evolution]\n# domain = \"general\"\n# serendipity_ratio = 0.3\n# min_verification_score = 0.5\n# max_mutations_per_cycle = 20\n\n[forge]\n# max_candidates = 20\n# min_confidence = 0.2\n# similarity_threshold = 0.8\n\n[materials]\n# api_key = \"your-materials-project-api-key\"\n\n[log_rotation]\n# max_bytes = 1048576   # 1 MB\n# max_files = 5\n";
    hexa_write_file(path, template);
    return printf("%s", hexa_concat("created default config at ", path));
}

long hexa_user_main() {
    hexa_arr a = hexa_args();
    if ((a.n < 2)) {
        printf("%s", "config.hexa — configuration file management");
        printf("%s", "usage:");
        printf("%s", "  hexa config.hexa get <key>           — get value (section.key format)");
        printf("%s", "  hexa config.hexa set <key> <value>   — set value");
        printf("%s", "  hexa config.hexa list                — show all settings");
        printf("%s", "  hexa config.hexa section <name>      — show section (loop/daemon/blowup/...)");
        printf("%s", "  hexa config.hexa init                — create default config.toml");
        printf("%s", "");
        printf("%s", "sections: loop, daemon, blowup, evolution, forge, materials, log_rotation");
        return 0;
    }
    const char* cmd = ((const char*)a.d[1]);
    if ((strcmp(cmd, "get") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "usage: hexa config.hexa get <key>");
            return 0;
        }
        const char* key = ((const char*)a.d[2]);
        const char* val = config_get(key);
        if ((strcmp(val, "") == 0)) {
            return printf("%s", hexa_concat(key, " = (not set)"));
        } else {
            return printf("%s", hexa_concat(hexa_concat(key, " = "), val));
        }
    } else if ((strcmp(cmd, "set") == 0)) {
        if ((a.n < 4)) {
            printf("%s", "usage: hexa config.hexa set <key> <value>");
            return 0;
        }
        return config_set(((const char*)a.d[2]), ((const char*)a.d[3]));
    } else if ((strcmp(cmd, "list") == 0)) {
        return config_list();
    } else if ((strcmp(cmd, "section") == 0)) {
        if ((a.n < 3)) {
            printf("%s", "usage: hexa config.hexa section <name>");
            return 0;
        }
        return config_section(((const char*)a.d[2]));
    } else if ((strcmp(cmd, "init") == 0)) {
        return config_init();
    } else {
        printf("%s", hexa_concat("unknown command: ", cmd));
        return printf("%s", "commands: get, set, list, section, init");
    }




}


int main(int argc, char** argv) {
    hexa_main_argc = argc; hexa_main_argv = argv;
    hexa_user_main();
    hexa_user_main();
    return 0;
}
