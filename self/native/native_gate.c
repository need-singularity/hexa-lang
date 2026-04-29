// self/native/native_gate.c — LD_PRELOAD (Linux) / DYLD_INSERT_LIBRARIES (macOS)
// shim that blocks writes to .py / .rs / .sh / .toml at the VFS syscall
// boundary. Kernel returns EPERM before any byte hits disk.
//
// Mirrors the effect of macOS sandbox-exec + self/sbpl/native.sb on Linux,
// where sandbox-exec isn't available. Same allowlist — bench/rust_baseline,
// tool/emergency, self/native, scripts/safe_hexa_launchd.sh, /tmp.
//
// BUILD (Ubuntu / Linux):
//   cc -shared -fPIC -O2 -D_GNU_SOURCE \
//      -o $WS/hexa-lang/self/native/native_gate.so \
//      $WS/hexa-lang/self/native/native_gate.c -ldl
//
// INSTALL (per-user, no root):
//   # in ~/.bashrc or ~/.zshrc:
//   export LD_PRELOAD="$WS/hexa-lang/self/native/native_gate.so${LD_PRELOAD:+:$LD_PRELOAD}"
//
// OPT-OUT (emergency):
//   CLAUDX_NO_SANDBOX=1 <cmd>   — env gate (checked per-call, zero-cost
//                                 when set on invocation: shim early-returns).
//
// ALLOWED to compile as .c (self/native/ is on raw#8 allowlist — FFI shim tree).
// SSOT: airgenome/rules/airgenome.json#AG10 + self/sbpl/native.sb.
//
// NOTES:
//   - Intercepts `open`, `openat`, `creat`, `rename`, `renameat`, `renameat2`.
//   - LFS aliases: `open64`, `__open64`, `openat64`, `__openat64` also
//     hooked (Phase 11 fix — curl/wget go fopen → __open64 on glibc and
//     bypassed the plain `open` shim). All four route through the same
//     `refuse()` policy; LFS path semantics are identical (off64_t only
//     affects pread/pwrite, not the gate decision).
//   - `fopen` family (fopen/fopen64/freopen/freopen64) is ALSO hooked
//     directly (Phase 13 fix — verification in Phase 12 caught curl
//     calling `fopen` whose internal `__open64` resolves through libc
//     hidden alias / PLT-bypass, sidestepping our `__open64` interposer
//     entirely). Hooking `fopen` itself closes the libc-internal gap;
//     `fdopen` (already-open fd) and `tmpfile` (anonymous) need no shim.
//   - macOS `DYLD_INSERT_LIBRARIES` is blocked on SIP-protected binaries;
//     for Mac, sandbox-exec is the primary path (self/sbpl/native.sb).
//     This shim is for unprivileged Linux (and unsigned mac executables).
//
// LIMITATION: hexa CLI 자체는 static musl link (build/hexa_linux_*: ELF …
// statically linked, stripped) 라 LD_PRELOAD 무영향.  본 .so 는 hexa 가
// spawn 하는 *dynamic 자식 프로세스* (curl, wget, busybox sh -c 서브셸,
// popen 으로 띄운 외부 툴 etc.) 만 enforce 한다.  hexa 자신의 직접 file
// IO (raw_loader 의 .raw open, codegen 산출물 write 등) 는 본 레이어를
// **우회** — 그 경로는 다음 두 레이어로 cover:
//   - L0 agent-layer pre_tool_guard: 모든 hexa subprocess 진입 직전에
//     pretool hook 이 정책 검사 (cross-platform, hexa 자체 static 여부와
//     무관).
//   - L0' macOS sandbox-exec + self/sbpl/native.sb: darwin host 에서 hexa
//     binary 자체를 sandbox profile 로 감싼다 (LD_PRELOAD 등가물).
// 따라서 enforcement layer ladder 는:
//   L0  agent-layer pre_tool_guard      — 모든 hexa subprocess (cross-platform)
//   L0' sandbox-exec native.sb (macOS)  — hexa 자체 file IO (darwin)
//   L1  본 .so + LD_PRELOAD (Linux)     — dynamic 자식 process (이 파일)
//   L2  chflags uchg / launchd sweep    — filesystem-level (raw#1 os-lock)
// hexa CLI 자체는 L0 + L0' (macOS) 또는 L0 + 자체 raw_loader assertion
// (Linux container) 으로 cover; 본 .so 의 책임 범위가 아님을 명시한다.
// 자세한 ladder rationale: hive .raw raw#7 entry + Mac→docker hard-landing
// policy 2026-04-25 + doc/security/os-level-enforcement-limits.md §11.
//
// MACOS HOST L1 GAP (2026-04-26, doc §12): macOS 의 DYLD_INSERT_LIBRARIES
// 는 SIP-protected binary (curl/wget/sh 등) 에 inject 차단 → host 의 L1
// 등가 enforce 가 부재. 본 cycle 결정: Mac→docker hard-landing 정책으로
// 정책 레벨 mitigate (macOS host 에서 hexa run 자체가 컨테이너로 강제됨).
// 향후 옵션: (A) sandbox-exec wrapper for spawn (transitional, deprecated
// API), (B) EndpointSecurity Framework (signing+entitlement, 장기). 상세
// trade-off 표는 doc/security/os-level-enforcement-limits.md §12 참조.

// NOTE: build with -D_GNU_SOURCE on the command line (see BUILD above).
// Compiling without that define exposes a POSIX-only signature; dlsym
// RTLD_NEXT would still work, but openat/renameat extern decls wouldn't
// be visible from the glibc headers.

#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static const char *BANNED_SUFFIXES[] = { ".py", ".rs", ".sh", ".toml", NULL };

// raw#8 filename taxonomy (F1 version / F2 stage / F3 temp + no-backup)
static const char *RAW8_SUFFIXES[] = {
    ".bak", ".orig", NULL
};
static const char *RAW8_CONTAINS[] = {
    "_v2.", "_v3.", "_v4.", "_v5.", "_v6.", "_v7.", "_v8.", "_v9.", "_v10.",
    "_mk2.", "_mk3.", "_mk4.", "_mk5.",
    "_new.", "_copy.", "_old.", "_backup.",
    "_phase", "_stage", "_Phase",
    ".bak.", ".legacy-", ".pre-",
    "-backup", "/TMP-", "/TIMESTAMP-",
    NULL
};

// raw#13 ai/git config paths (path-substring match).
// raw#13 expansion 2026-04-22 — per-repo .claude/settings*.json + agents/ + commands/
// added (AG10 purge 후 project-local 금지 확정).
// user-global ~/.claude/ is whitelisted via is_user_global_claude() below.
static const char *RAW13_PATHS[] = {
    "/.github/workflows/",
    "/.githooks/",
    "/.husky/",
    "/husky.config.",
    "/.pre-commit-config.yaml",
    "/.pre-commit-config.yml",
    "/lefthook.yaml",
    "/lefthook.yml",
    "/.cursorrules",
    "/.continue/",
    "/.aider.conf.yaml",
    "/.aider.conf.yml",
    "/.windsurfrules",
    "/CLAUDE.md",
    "/.claude/hooks/",
    "/.claude/skills/",
    "/.claude/settings.json",
    "/.claude/settings.local.json",
    "/.claude/agents/",
    "/.claude/commands/",
    NULL
};

static int ends_with(const char *s, const char *suf) {
    if (!s || !suf) return 0;
    size_t ls = strlen(s), lf = strlen(suf);
    if (ls < lf) return 0;
    return strcmp(s + ls - lf, suf) == 0;
}

static int on_allowlist(const char *path) {
    static const char *allow_substrings[] = {
        "/bench/rust_baseline/",
        "/tool/emergency/",
        "/self/native/",
        "/scripts/safe_hexa_launchd.sh",
        "/.hexa-cache/",
        "/.claude-cache/",
        "/.git/",
        "/.hx/",
        "/tmp/",
        "/private/tmp/",
        "/var/folders/",
        NULL
    };
    for (int i = 0; allow_substrings[i]; i++) {
        if (strstr(path, allow_substrings[i])) return 1;
    }
    return 0;
}

static int opt_out(void) {
    const char *v = getenv("CLAUDX_NO_SANDBOX");
    return v && v[0] && strcmp(v, "0") != 0;
}

static int is_write_flag(int flags) {
    return (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0;
}

static int refuse_raw8(const char *path) {
    for (int i = 0; RAW8_SUFFIXES[i]; i++) {
        if (ends_with(path, RAW8_SUFFIXES[i])) return 1;
    }
    for (int i = 0; RAW8_CONTAINS[i]; i++) {
        if (strstr(path, RAW8_CONTAINS[i])) return 1;
    }
    return 0;
}

// user-global ~/.claude/ is always allowed (raw#13 exception — AG10 purge 후
// user-global AI 도구 설정은 유지). Detect "/Users/<user>/.claude/" (macOS) or
// "/home/<user>/.claude/" (Linux) by finding "/.claude/" and looking for a
// $HOME-prefix match.
static int is_user_global_claude(const char *path) {
    if (!path) return 0;
    const char *hit = strstr(path, "/.claude/");
    if (!hit) return 0;
    const char *home = getenv("HOME");
    if (home && home[0]) {
        size_t hn = strlen(home);
        if (strncmp(path, home, hn) == 0 && path[hn] == '/') {
            // Only the top-level $HOME/.claude/ counts as user-global;
            // $HOME/core/hexa-lang/.claude/ is per-repo (still banned).
            if (strncmp(path + hn, "/.claude/", 9) == 0) return 1;
        }
    }
    return 0;
}

static int refuse_raw13(const char *path) {
    if (is_user_global_claude(path)) return 0;
    for (int i = 0; RAW13_PATHS[i]; i++) {
        if (strstr(path, RAW13_PATHS[i])) return 1;
    }
    return 0;
}

// raw#6 — folder-name F4 (banned basenames). Hive-only initial deployment
// 2026-04-25; other repos still pending raw_6.list expansion. Allowlist
// mirrors self/sbpl/native.sb raw#6 block + on_allowlist short-circuits.
static const char *RAW6_BANNED[] = {
    "/utils/", "/helpers/", "/common/", "/lib/",
    "/misc/", "/stuff/", "/base/",
    NULL
};
static const char *RAW6_ALLOW[] = {
    "/node_modules/",
    "/packages/",
    "/infrastructure/",
    "/libs/",
    "/archive/",
    NULL
};

static int is_under_hive_repo(const char *path) {
    if (!path) return 0;
    return strstr(path, "/core/hive/") != NULL;
}

static int refuse_raw6(const char *path) {
    if (!is_under_hive_repo(path)) return 0;
    for (int i = 0; RAW6_ALLOW[i]; i++) {
        if (strstr(path, RAW6_ALLOW[i])) return 0;
    }
    for (int i = 0; RAW6_BANNED[i]; i++) {
        if (strstr(path, RAW6_BANNED[i])) return 1;
    }
    return 0;
}

// raw#7 — tree-structure F5 (loner-dir) + F6 (parent-child stem repeat).
//
// Promoted 2026-04-26 from agent-layer (tool/ai_native_scan.hexa) to
// os-level on Linux LD_PRELOAD. macOS SBPL still cannot enforce — kernel
// regex lacks dir-content introspection (F5) and backreferences (F6
// fail-opens on backref patterns). On Linux the shim sees full paths +
// has FS access, so both F5 + F6 are straightforward C.
//
// Both helpers gate behind on_allowlist() (in refuse() flow) plus an
// is_under_managed_repo() check (mirrors refuse_raw6's is_under_hive_repo)
// so cross-repo expansion follows the raw_6.list precedent.

// F6 entry-point stem whitelist — mirrors ai_native_scan.hexa#L612.
// `index/mod/main/lib/init` are conventional entry-point names; never flagged.
static const char *RAW7_F6_ENTRY_STEMS[] = {
    "index", "mod", "main", "lib", "init", NULL
};

// raw#7 path-substring exemptions (vendor / test-fixtures / generated trees).
// Mirrors raw_7.list intent: tests/integration/*/test.sh is the canonical
// F5-loner exemption; vendor + node_modules are universal grandfather paths.
//
// This is the STATIC FALLBACK — always consulted first (cheap), and used
// when the per-repo `.raw-exemptions/raw_7.list` file is missing or empty.
// The dynamic, file-based reader below augments this list (cross-port from
// `hive/tool/ai_native_scan.hexa#load_raw_7_exemptions`).
static const char *RAW7_ALLOW[] = {
    "/node_modules/",
    "/vendor/",
    "/archive/",
    "/.git/",
    "/.hexa-cache/",
    "/.claude-cache/",
    "/.claude/worktrees/",
    "/tests/integration/",   // raw_7.list: per-test isolation by design
    "/tests/fixtures/",
    "/test/fixtures/",
    "/testdata/",
    "/.raw-exemptions/",
    NULL
};

static int is_under_managed_repo(const char *path) {
    if (!path) return 0;
    return strstr(path, "/core/hive/") != NULL
        || strstr(path, "/core/hexa-lang/") != NULL;
}

// ─── raw_7.list file-based reader ────────────────────────────────────
//
// Cross-port from `hive/tool/ai_native_scan.hexa` (commit 1f54f8dfc).
// Reads `<repo_root>/.raw-exemptions/raw_7.list` once per process (lazy
// load on first refuse_raw7_* call), parses each non-comment line, and
// substring-matches candidate paths against the assembled pattern array.
//
// Two file formats are auto-detected per line:
//   (a) pipe-separated  — hexa-lang style:
//       <pattern> | <line_range> | <reason> | <expiry YYYY-MM-DD>
//   (b) bare pattern    — hive style; metadata supplied by preceding
//                          `#` comment block (reason/expiry/owner/jurisdiction).
//
// Both paths populate `raw7_exemption_t` rows uniformly; downstream match
// logic only consults `pattern` + `expiry_unix` (debug-build expiry warner).
//
// Self-recursion safety: this reader uses fopen/fgets which will hit our
// own `open()` shim. That call passes `O_RDONLY`, so `is_write_flag(flags)
// == 0` short-circuits `refuse()` to allow before any raw#7 logic runs.
// The static `g_raw7_loading` guard adds a second layer (paranoia: if a
// future code path triggered raw7 check on read, the guard prevents
// infinite recursion during the load itself).

#define RAW7_PATTERN_MAX     256
#define RAW7_META_MAX         64
#define RAW7_MAX_ENTRIES     128
#define RAW7_LINE_MAX       1024

typedef struct {
    char pattern[RAW7_PATTERN_MAX];
    long expiry_unix;                       // 0 = no expiry parsed
    char expiry_iso[16];                    // YYYY-MM-DD (cached, for warner)
    char owner[RAW7_META_MAX];
    char jurisdiction[RAW7_META_MAX];
    char reason[RAW7_PATTERN_MAX];
} raw7_exemption_t;

// Per-repo cache. Each repo we encounter gets one cache slot. In practice
// we only ever hit hive + hexa-lang here, but the fixed array is cheap.
typedef struct {
    char repo_root[256];                    // "/Users/<u>/core/<repo>" or ""
    int  loaded;                            // 0 = unloaded, 1 = loaded, -1 = file missing
    int  count;
    raw7_exemption_t entries[RAW7_MAX_ENTRIES];
} raw7_cache_t;

#define RAW7_CACHE_SLOTS 4
static raw7_cache_t g_raw7_cache[RAW7_CACHE_SLOTS];
static int g_raw7_loading = 0;              // re-entrancy guard

// Strip leading + trailing whitespace in-place; returns the (mutated) buffer.
static char *raw7_trim(char *s) {
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' ||
                     s[n-1] == '\r' || s[n-1] == '\n')) {
        s[--n] = '\0';
    }
    return s;
}

// Parse YYYY-MM-DD into unix-ts (UTC, 00:00:00). Returns 0 on parse failure.
static long raw7_parse_iso_date(const char *s) {
    if (!s || strlen(s) < 10) return 0;
    int y = 0, m = 0, d = 0;
    if (sscanf(s, "%4d-%2d-%2d", &y, &m, &d) != 3) return 0;
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = d;
    return (long)timegm(&tm);
}

// Determine the repo-root prefix for a path.
// Returns 1 + writes prefix into out (caller-sized) on hit, 0 if path is
// outside known managed repos.
static int raw7_repo_root_for(const char *path, char *out, size_t out_sz) {
    if (!path || !out || out_sz < 32) return 0;
    static const char *repos[] = {
        "/core/hive/",
        "/core/hexa-lang/",
        NULL
    };
    for (int i = 0; repos[i]; i++) {
        const char *hit = strstr(path, repos[i]);
        if (!hit) continue;
        // Root = path[0..end-of-repo-segment] without trailing slash.
        size_t end = (size_t)(hit - path) + strlen(repos[i]) - 1; // omit '/'
        if (end >= out_sz) return 0;
        memcpy(out, path, end);
        out[end] = '\0';
        return 1;
    }
    return 0;
}

// Build "<root>/.raw-exemptions/raw_7.list" into out. Returns 0 on overflow.
static int raw7_list_path(const char *repo_root, char *out, size_t out_sz) {
    static const char *suffix = "/.raw-exemptions/raw_7.list";
    size_t rl = strlen(repo_root), sl = strlen(suffix);
    if (rl + sl + 1 > out_sz) return 0;
    memcpy(out, repo_root, rl);
    memcpy(out + rl, suffix, sl);
    out[rl + sl] = '\0';
    return 1;
}

// Parse a pipe-separated entry "<pat> | <range> | <reason> | <expiry>".
// Returns 1 on parse-as-pipe, 0 if line lacks pipes (caller treats as bare).
static int raw7_parse_pipe_entry(char *line, raw7_exemption_t *out) {
    char *first = strchr(line, '|');
    if (!first) return 0;
    *first = '\0';
    char *pat = raw7_trim(line);
    if (!*pat) return 0;
    strncpy(out->pattern, pat, RAW7_PATTERN_MAX - 1);
    out->pattern[RAW7_PATTERN_MAX - 1] = '\0';

    // Skip line_range field.
    char *cur = first + 1;
    char *p2 = strchr(cur, '|');
    if (p2) { *p2 = '\0'; cur = p2 + 1; } else { return 1; }

    // reason
    char *p3 = strchr(cur, '|');
    if (p3) {
        *p3 = '\0';
        char *r = raw7_trim(cur);
        strncpy(out->reason, r, RAW7_PATTERN_MAX - 1);
        out->reason[RAW7_PATTERN_MAX - 1] = '\0';
        cur = p3 + 1;
    } else {
        return 1;
    }

    // expiry
    char *exp = raw7_trim(cur);
    strncpy(out->expiry_iso, exp, sizeof(out->expiry_iso) - 1);
    out->expiry_iso[sizeof(out->expiry_iso) - 1] = '\0';
    out->expiry_unix = raw7_parse_iso_date(exp);
    return 1;
}

// Apply a metadata field captured from a preceding `# key: value` line.
static void raw7_apply_meta(raw7_exemption_t *e,
                             const char *key,
                             const char *val) {
    if (strcmp(key, "reason") == 0) {
        strncpy(e->reason, val, RAW7_PATTERN_MAX - 1);
        e->reason[RAW7_PATTERN_MAX - 1] = '\0';
    } else if (strcmp(key, "expiry") == 0) {
        strncpy(e->expiry_iso, val, sizeof(e->expiry_iso) - 1);
        e->expiry_iso[sizeof(e->expiry_iso) - 1] = '\0';
        e->expiry_unix = raw7_parse_iso_date(val);
    } else if (strcmp(key, "owner") == 0) {
        strncpy(e->owner, val, RAW7_META_MAX - 1);
        e->owner[RAW7_META_MAX - 1] = '\0';
    } else if (strcmp(key, "jurisdiction") == 0) {
        strncpy(e->jurisdiction, val, RAW7_META_MAX - 1);
        e->jurisdiction[RAW7_META_MAX - 1] = '\0';
    }
}

// Parse a comment-block accumulator. Mirrors hive's reader:
// each blank line resets the pending-meta buffer; each non-comment line
// becomes a new entry inheriting the accumulator's metadata.
static int load_raw_7_exemptions(const char *repo_root,
                                  raw7_exemption_t *out_arr,
                                  int max_entries) {
    if (!repo_root || !out_arr || max_entries <= 0) return 0;

    char list_path[512];
    if (!raw7_list_path(repo_root, list_path, sizeof(list_path))) return 0;

    FILE *f = fopen(list_path, "r");
    if (!f) return -1;                          // file missing → caller falls back to static

    raw7_exemption_t pending;                   // accumulator for comment-block metadata
    memset(&pending, 0, sizeof(pending));

    int count = 0;
    char line[RAW7_LINE_MAX];
    while (fgets(line, sizeof(line), f) != NULL && count < max_entries) {
        char *t = raw7_trim(line);
        if (!*t) {
            // blank → reset pending metadata block
            memset(&pending, 0, sizeof(pending));
            continue;
        }
        if (t[0] == '#') {
            // strip leading '#' and any whitespace, then look for "key: value"
            char *body = raw7_trim(t + 1);
            char *colon = strchr(body, ':');
            if (colon) {
                *colon = '\0';
                char *key = raw7_trim(body);
                char *val = raw7_trim(colon + 1);
                raw7_apply_meta(&pending, key, val);
            }
            continue;
        }
        // Pattern line. Defensive: refuse double-quote (matches hive reader).
        if (strchr(t, '"')) continue;

        raw7_exemption_t entry = pending;       // inherit comment-block metadata
        if (raw7_parse_pipe_entry(t, &entry) == 0) {
            // bare-pattern form (hive style)
            strncpy(entry.pattern, t, RAW7_PATTERN_MAX - 1);
            entry.pattern[RAW7_PATTERN_MAX - 1] = '\0';
        }
        if (entry.pattern[0] == '\0') continue;
        out_arr[count++] = entry;
    }
    fclose(f);
    return count;
}

// Get-or-load the cache slot for `repo_root`. Returns slot ptr, or NULL if
// the cache is full (degenerate case — only 2 repos in practice).
static raw7_cache_t *raw7_get_cache(const char *repo_root) {
    if (g_raw7_loading) return NULL;            // re-entrancy: fail closed (use static)
    for (int i = 0; i < RAW7_CACHE_SLOTS; i++) {
        if (g_raw7_cache[i].loaded != 0
            && strcmp(g_raw7_cache[i].repo_root, repo_root) == 0) {
            return &g_raw7_cache[i];
        }
    }
    // Find empty slot.
    for (int i = 0; i < RAW7_CACHE_SLOTS; i++) {
        if (g_raw7_cache[i].loaded == 0) {
            g_raw7_loading = 1;
            strncpy(g_raw7_cache[i].repo_root, repo_root,
                    sizeof(g_raw7_cache[i].repo_root) - 1);
            g_raw7_cache[i].repo_root[
                sizeof(g_raw7_cache[i].repo_root) - 1] = '\0';
            int n = load_raw_7_exemptions(repo_root,
                                          g_raw7_cache[i].entries,
                                          RAW7_MAX_ENTRIES);
            if (n < 0) {
                g_raw7_cache[i].count = 0;
                g_raw7_cache[i].loaded = -1;     // file missing
            } else {
                g_raw7_cache[i].count = n;
                g_raw7_cache[i].loaded = 1;
            }
            g_raw7_loading = 0;
            return &g_raw7_cache[i];
        }
    }
    return NULL;                                // cache full
}

// Substring + dir-prefix glob match (mirrors hive `_path_matches_exemption`).
// Pattern semantics:
//   - empty pattern: never matches.
//   - all other patterns: free substring match (covers both `bin/hive`
//     exact-file form AND `packages/foo/test/fixtures/` dir-prefix form;
//     the trailing `/` distinguishes intent at authoring time, not match time).
static int path_matches_raw7_exemption(const char *path,
                                        const raw7_exemption_t *arr,
                                        int count) {
    if (!path || !arr || count <= 0) return 0;
    for (int i = 0; i < count; i++) {
        const char *pat = arr[i].pattern;
        if (!pat || !*pat) continue;
        if (strstr(path, pat)) return 1;
    }
    return 0;
}

// Combined check: static RAW7_ALLOW (cheap path), then dynamic file list.
static int raw7_path_exempt(const char *path) {
    if (!path) return 0;
    // Cheap static-list pass first.
    for (int i = 0; RAW7_ALLOW[i]; i++) {
        if (strstr(path, RAW7_ALLOW[i])) return 1;
    }
    // Dynamic file-based list (per-repo).
    char repo_root[256];
    if (!raw7_repo_root_for(path, repo_root, sizeof(repo_root))) return 0;
    raw7_cache_t *c = raw7_get_cache(repo_root);
    if (!c || c->loaded != 1 || c->count == 0) return 0;
    return path_matches_raw7_exemption(path, c->entries, c->count);
}

// Debug-build expiry warner — emits stderr line for any expired or
// soon-to-expire entries on first cache load. Compile with
// `-DRAW7_DEBUG_EXPIRY` to enable; off by default to keep the shim quiet.
#ifdef RAW7_DEBUG_EXPIRY
__attribute__((unused))
static void expiry_warn_raw7(const raw7_cache_t *c) {
    if (!c || c->count == 0) return;
    long now = (long)time(NULL);
    long soon = now + (30L * 24L * 3600L);      // within 30 days
    for (int i = 0; i < c->count; i++) {
        const raw7_exemption_t *e = &c->entries[i];
        if (e->expiry_unix <= 0) continue;
        if (e->expiry_unix < now) {
            fprintf(stderr, "[raw_7.list expiry-warn] EXPIRED  %s  pattern=%s\n",
                    e->expiry_iso, e->pattern);
        } else if (e->expiry_unix < soon) {
            long days = (e->expiry_unix - now) / (24L * 3600L);
            fprintf(stderr, "[raw_7.list expiry-warn] %ldd  %s  pattern=%s\n",
                    days, e->expiry_iso, e->pattern);
        }
    }
}
#endif

// Find the last '/' in path. Returns NULL if none.
static const char *raw7_last_slash(const char *path) {
    return strrchr(path, '/');
}

// raw#7 F6: parent_dir_name == file_stem (or stem starts with "<parent>_").
// Pure pointer-math, zero syscalls. Returns 1 to refuse.
static int refuse_raw7_parent_child_stem(const char *path) {
    if (!path) return 0;
    if (!is_under_managed_repo(path)) return 0;
    if (raw7_path_exempt(path)) return 0;

    const char *slash1 = raw7_last_slash(path);
    if (!slash1 || slash1 == path) return 0;     // no parent component
    const char *basename = slash1 + 1;
    if (!*basename) return 0;

    // Locate parent-dir component: scan back for second-to-last slash.
    const char *p = slash1 - 1;
    while (p > path && *p != '/') p--;
    if (*p != '/') return 0;                      // path had only one '/'
    const char *pdir = p + 1;
    size_t pdir_len = (size_t)(slash1 - pdir);
    if (pdir_len == 0) return 0;

    // Compute stem length: basename up to last '.'.
    const char *dot = strrchr(basename, '.');
    size_t stem_len = dot ? (size_t)(dot - basename) : strlen(basename);
    if (stem_len == 0) return 0;

    // Whitelist conventional entry-point stems (index/mod/main/lib/init).
    for (int i = 0; RAW7_F6_ENTRY_STEMS[i]; i++) {
        size_t en = strlen(RAW7_F6_ENTRY_STEMS[i]);
        if (en == stem_len
            && strncmp(basename, RAW7_F6_ENTRY_STEMS[i], en) == 0) {
            return 0;
        }
    }

    // Exact-match: parent == stem.
    if (stem_len == pdir_len && strncmp(basename, pdir, pdir_len) == 0) {
        return 1;
    }
    // Prefix-match: stem starts with "<parent>_".
    if (stem_len > pdir_len + 1
        && strncmp(basename, pdir, pdir_len) == 0
        && basename[pdir_len] == '_') {
        return 1;
    }
    return 0;
}

// raw#7 F5: refuse if creating this file would leave the parent dir as a
// "loner" (this file is the sole regular-file entry). Gate behind O_CREAT
// + ENOENT(path) in caller so steady-state writes pay zero. Cost on first-
// write: one opendir + a bounded readdir scan (early-exit at 2 entries).
//
// Algorithm: open parent dir; iterate dirent until we either:
//   (a) see a second non-{".",".."} entry that's not the file we're about
//       to create — return 0 (allow, dir is not a loner)
//   (b) exhaust the iterator — return 1 (would-be loner, refuse)
// We accept that the create itself is racing dir contents; the lint-layer
// already provides eventual consistency. This shim's job is to catch the
// 95% "first file in fresh dir" pattern, not be perfectly atomic.
static int refuse_raw7_loner_dir(const char *path) {
    if (!path) return 0;
    if (!is_under_managed_repo(path)) return 0;
    if (raw7_path_exempt(path)) return 0;

    const char *slash = raw7_last_slash(path);
    if (!slash || slash == path) return 0;
    size_t plen = (size_t)(slash - path);
    if (plen == 0 || plen >= 4096) return 0;

    char parent[4096];
    memcpy(parent, path, plen);
    parent[plen] = '\0';

    DIR *d = opendir(parent);
    if (!d) return 0;                             // parent missing → let real open() set ENOENT

    const char *new_basename = slash + 1;
    int seen_other = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *n = de->d_name;
        if (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0'))) {
            continue;                             // skip "." and ".."
        }
        if (strcmp(n, new_basename) == 0) {
            // Already exists — not a fresh-create loner case; let real call decide.
            closedir(d);
            return 0;
        }
        // Any other entry → not a loner-to-be.
        seen_other = 1;
        break;
    }
    closedir(d);

    // Empty parent + we are about to add 1 entry → would-be loner. Refuse.
    return seen_other ? 0 : 1;
}

// raw#20 — .own append-only: O_TRUNC on path ending in /.own = EPERM.
static int refuse_raw20(const char *path, int flags) {
    if (!(flags & O_TRUNC)) return 0;
    if (!path) return 0;
    size_t n = strlen(path);
    if (n >= 5 && strcmp(path + n - 5, "/.own") == 0) return 1;
    return 0;
}

// ─── raw#1 — file-lock SSOT path-literal deny (F2 agent top priority) ──
//
// raw#1 mandate: locked SSOT files (~/core/*/.raw, .own, .roadmap, .guide,
// .ext, .workspace) are append-only-via-canonical-tools. ANY direct file
// IO via open(O_WRONLY|O_RDWR|O_TRUNC|O_APPEND), creat(), unlink(),
// rename() (as src or dst), chmod() targeting these path literals is
// rejected with EPERM at the syscall boundary.
//
// Match strategy: ends_with() against the literal suffix list. The path
// must end with exactly one of these segments (not contain). This keeps
// the pattern tight — `/some/dir/.raw-exemptions/foo` does NOT match
// because it doesn't end with `/.raw`.
//
// Interaction with raw#20: raw#20 was the partial precursor (only O_TRUNC
// on /.own). raw#1 supersedes — refuse_raw1 is checked FIRST and covers
// all write modes + the additional five SSOT files. raw#20 retained for
// defense-in-depth (free since refuse_raw1 already returned 1 in matching
// cases; refuse_raw20 only fires on the narrower /.own + O_TRUNC subset).
//
// FP risk audit (F2 agent):
//   - Tools that legitimately mutate locked SSOT (raw_lock_release.sh,
//     hexa raw-edit) bypass this shim because they're invoked under
//     CLAUDX_NO_SANDBOX=1. opt_out() is checked at refuse() entry.
//   - Hexa CLI itself is statically linked (musl) → LD_PRELOAD has no
//     effect on hexa's own raw_loader file IO. This shim guards
//     dynamically-linked subprocesses (sh, python, curl, etc.) only.
//   - SSOT path candidates: /.raw /.own /.roadmap /.guide /.ext /.workspace
//   - Free files inside the SSOT-named DIR (.raw/foo) are NOT matched
//     because ends_with(/.raw/foo, "/.raw") = false. Intentional — only
//     the literal lock files themselves are protected here.

static const char *RAW1_PATHS[] = {
    "/.raw",
    "/.own",
    "/.roadmap",
    "/.guide",
    "/.ext",
    "/.workspace",
    NULL
};

static int matches_raw1_path(const char *path) {
    if (!path) return 0;
    for (int i = 0; RAW1_PATHS[i]; i++) {
        if (ends_with(path, RAW1_PATHS[i])) return 1;
    }
    return 0;
}

// Write-side gate: open/openat/creat/fopen — any write-implying flag
// against a RAW1_PATHS literal = EPERM.
static int refuse_raw1_write(const char *path, int flags) {
    if (!path) return 0;
    if (!(flags & (O_WRONLY | O_RDWR | O_TRUNC | O_APPEND | O_CREAT))) return 0;
    return matches_raw1_path(path);
}

// Mutation-side gate: unlink/rename/chmod targeting the literals.
// Caller passes EITHER src or dst (or single arg for unlink/chmod).
static int refuse_raw1_mutate(const char *path) {
    return matches_raw1_path(path);
}

// ── raw#22 per-file manifest (explicit RAW22_MANIFEST_PATHS) ─────────
//
// Minimum-viable per-file SSOT lock: an explicit array of path SUFFIXES
// that are write-locked unconditionally. Future cycle promotes this to
// a sentinel-file scan (`.raw22-locked` marker in the same/parent dir);
// the explicit array is the safe MVP — no extra syscalls per write.
//
// Match strategy: ends_with() against the suffix list. Use long-enough
// suffixes that cross-repo collisions are unlikely (e.g. include a
// distinctive parent segment). Add entries by editing this list.
//
// Bypass: opt_out() honored. dev-toggle HIVE_HEXA_LANG_IMPROVEMENT does
// NOT bypass — manifest paths are SSOT-canonical and never auto-edited
// even during hexa-lang dev sessions.
//
// Trade-off vs sentinel-file pattern:
//   explicit array (this) — zero per-write syscalls, requires recompile
//                            to add entries; safe + cheap.
//   sentinel-file scan    — fstatat(parent, ".raw22-locked") per write;
//                            data-driven (no recompile); ~1 syscall cost.
// We pick explicit for MVP; sentinel can layer on later via a separate
// refuse_raw22_sentinel() helper.

static const char *RAW22_MANIFEST_PATHS[] = {
    // SSOT manifest files locked across all repos. Suffix-match anchored
    // by leading '/' so unrelated paths can't accidentally match.
    "/airgenome/rules/airgenome.json",
    "/.raw-exemptions/raw_7.list",
    "/.raw-exemptions/raw_22.list",
    "/self/sbpl/native.sb",
    "/self/native/native_gate.c",
    NULL
};

static int matches_raw22_manifest(const char *path) {
    if (!path) return 0;
    for (int i = 0; RAW22_MANIFEST_PATHS[i]; i++) {
        if (ends_with(path, RAW22_MANIFEST_PATHS[i])) return 1;
    }
    return 0;
}

// Explicit-array layer (cheap, recompile-required). Renamed from earlier
// matches_raw22_manifest() callsites — kept as a thin alias so existing
// callsites (refuse_raw22_write / refuse_raw22_mutate / rename src) stay
// stable. Future entries: edit RAW22_MANIFEST_PATHS[] above.
static int refuse_raw22_explicit(const char *path) {
    return matches_raw22_manifest(path);
}

// ── raw#22 sentinel-file pattern (data-driven layer) ─────────────────
//
// Promotion of the explicit-array MVP to a data-driven lock: scan the
// path's parent directory for a sentinel file `.raw22-locked`. If
// present, the entire directory (write/unlink/chmod targeting any file
// directly inside it) is refused. No recompile needed — operators add
// or remove the sentinel on disk to flip the lock.
//
// Layered semantics:
//   refuse_raw22_explicit() — array-driven, zero per-write syscall, must
//                              recompile to add entries; SSOT-canonical.
//   refuse_raw22_sentinel() — sentinel-driven, ~1 fstatat per write of
//                              any path; data-driven, operator-flippable.
// Both are checked: explicit-array hits return 1 first (fast path);
// otherwise sentinel scan runs.
//
// Naming convention: `.raw22-locked` (active state, directly readable)
// rather than `.raw22-manifest` (which suggests a list of paths inside
// the file; the sentinel is purely presence-based, contents ignored).
//
// Scan scope (trade-off): parent-dir only. Walking every ancestor would
// catch repo-root sentinels but adds O(depth) syscalls per write — too
// expensive for the per-syscall hot path. Operators wanting subtree
// locks must place the sentinel in each leaf dir (or use the explicit
// array for canonical SSOT files).
//
// Performance: one fstatat() per write/mutate. Linux fstatat on dentry
// cache is ~100ns; sentinel absence is the common case so kernel caches
// the negative dentry. opt_out() short-circuits before the syscall.
// Per-process caching (e.g. cache parent-dir fd → present bit) is
// possible but skipped in this MVP: cache invalidation under
// concurrent rmdir/create races is more code than the syscall saves.
#define RAW22_SENTINEL_NAME ".raw22-locked"

static int refuse_raw22_sentinel(const char *path) {
    if (!path) return 0;
    // Split path into parent dir + basename. We need the parent so we
    // can fstatat(AT_FDCWD, "<parent>/.raw22-locked"). If path has no
    // slash, parent is "." (cwd). The sentinel itself must not lock
    // its own creation, so skip when basename == RAW22_SENTINEL_NAME.
    const char *slash = strrchr(path, '/');
    const char *base  = slash ? slash + 1 : path;
    if (strcmp(base, RAW22_SENTINEL_NAME) == 0) return 0;

    char sentinel[4096];
    if (slash) {
        size_t plen = (size_t)(slash - path);
        // "<parent>/.raw22-locked" — bound check with sentinel name + NUL.
        if (plen + 1 + sizeof(RAW22_SENTINEL_NAME) >= sizeof(sentinel)) return 0;
        memcpy(sentinel, path, plen);
        sentinel[plen] = '/';
        memcpy(sentinel + plen + 1, RAW22_SENTINEL_NAME, sizeof(RAW22_SENTINEL_NAME));
    } else {
        memcpy(sentinel, "./" RAW22_SENTINEL_NAME, sizeof("./" RAW22_SENTINEL_NAME));
    }

    struct stat st;
    if (fstatat(AT_FDCWD, sentinel, &st, AT_SYMLINK_NOFOLLOW) == 0) return 1;
    return 0;
}

static int refuse_raw22_write(const char *path, int flags) {
    if (opt_out()) return 0;
    if (!path) return 0;
    if (!(flags & (O_WRONLY | O_RDWR | O_TRUNC | O_APPEND | O_CREAT))) return 0;
    if (refuse_raw22_explicit(path)) return 1;
    return refuse_raw22_sentinel(path);
}

static int refuse_raw22_mutate(const char *path) {
    if (opt_out()) return 0;
    if (!path) return 0;
    if (refuse_raw22_explicit(path)) return 1;
    return refuse_raw22_sentinel(path);
}

// ─── hexa-lang self-edit allowance — raw.hexa_lang_improvement gate ──
//
// Background mirrors self/sbpl/native.sb#hexa-lang block. Universal
// raw#8 already blocks .py/.rs/.sh/.toml everywhere; the 19 grand-
// fathered foreign-ext files in /Users/<u>/core/hexa-lang/ would
// therefore be unwritable in production sessions. SBPL cannot read
// env so it relies on agent-layer pre_tool_guard.hexa as primary; on
// Linux LD_PRELOAD we CAN read env, so the dev toggle gates here
// directly:
//
//   HIVE_HEXA_LANG_IMPROVEMENT=1  → hexa-lang foreign-ext writes
//                                    permitted (full 19-file allow +
//                                    new files). Production sessions
//                                    never set this — hive/coding-agent
//                                    propagates only when the user
//                                    enables raw.hexa_lang_improvement
//                                    in /hive selector (defaultOn=false).
//
//   unset / 0                      → grandfather-list-only allow:
//                                    only the 19 audited files match
//                                    is_grandfathered_hexa_lang_path();
//                                    new foreign-ext writes get EPERM.
//
// Detection. We compare path against $HOME/core/hexa-lang/ prefix.
// $HOME may be unset (boot, daemons) — fall back to scanning the path
// for "/core/hexa-lang/" substring (matches refuse_raw6 style).
static int is_under_hexa_lang(const char *path) {
    if (!path) return 0;
    return strstr(path, "/core/hexa-lang/") != NULL;
}

static int dev_toggle_on(void) {
    const char *v = getenv("HIVE_HEXA_LANG_IMPROVEMENT");
    return v && v[0] && strcmp(v, "0") != 0;
}

// 19 grandfathered files (audited 2026-04-26). Match by suffix from
// the hexa-lang root segment so /Users/<any>/core/hexa-lang/<rel>
// works regardless of $HOME. The trailing $ in the SBPL regex is
// equivalent to ends_with() here.
static const char *HEXA_LANG_GRANDFATHER_SUFFIXES[] = {
    "/core/hexa-lang/hexa.toml",
    "/core/hexa-lang/tool/install_darwin_marker.sh",
    "/core/hexa-lang/tool/extract_runtime_hi.sh",
    "/core/hexa-lang/scripts/safe_hexa_launchd.sh",
    "/core/hexa-lang/stdlib/anima_audio_worker.py",
    "/core/hexa-lang/tool/emergency/raw_reset.sh",
    "/core/hexa-lang/tool/emergency/raw_sudo_unlock.sh",
    "/core/hexa-lang/tests/integration/run_all.sh",
    NULL
};

static int is_grandfathered_hexa_lang_path(const char *path) {
    if (!path) return 0;
    for (int i = 0; HEXA_LANG_GRANDFATHER_SUFFIXES[i]; i++) {
        if (ends_with(path, HEXA_LANG_GRANDFATHER_SUFFIXES[i])) return 1;
    }
    // 11 numbered integration test scripts: tests/integration/<NN>_<name>/test.sh
    // Match suffix /test.sh under /tests/integration/<digit-prefixed>_<...>/
    const char *hit = strstr(path, "/core/hexa-lang/tests/integration/");
    if (hit) {
        const char *rest = hit + strlen("/core/hexa-lang/tests/integration/");
        // first char must be digit (01_..11_)
        if (rest[0] >= '0' && rest[0] <= '9') {
            if (ends_with(path, "/test.sh")) return 1;
        }
    }
    return 0;
}

// Returns 1 if the path is a hexa-lang foreign-ext file we should
// gate. Returns 0 if the path is outside hexa-lang or not a foreign
// ext (let the universal rules decide).
static int hexa_lang_foreign_ext(const char *path) {
    if (!is_under_hexa_lang(path)) return 0;
    return ends_with(path, ".py") || ends_with(path, ".rs")
        || ends_with(path, ".sh") || ends_with(path, ".toml");
}

// Decision for hexa-lang scope:
//   dev toggle ON  → never refuse here (universal raw#8 still applies
//                    elsewhere; we let the toggle owner do all writes
//                    inside hexa-lang including new files).
//   toggle OFF     → grandfather-list-only allow; everything else under
//                    hexa-lang foreign-ext refuses with EPERM.
static int refuse_hexa_lang_scope(const char *path) {
    if (!hexa_lang_foreign_ext(path)) return 0;
    if (dev_toggle_on()) return 0;
    if (is_grandfathered_hexa_lang_path(path)) return 0;
    return 1;
}

static int refuse(const char *path, int flags) {
    if (opt_out()) return 0;
    if (!path || !is_write_flag(flags)) return 0;
    // raw#1 file-lock SSOT path-literal deny — checked BEFORE allowlist
    // so locked SSOT files in /tmp/ subdirs (or other allow-prefixes) are
    // still protected. Locked SSOT is repo-canonical, never a temp file.
    if (refuse_raw1_write(path, flags)) return 1;
    // raw#22 — manifest paths checked BEFORE allowlist (SSOT-canonical,
    // never legitimately under /tmp/ etc.).
    if (refuse_raw22_write(path, flags)) return 1;
    if (on_allowlist(path)) return 0;
    // hexa-lang scope check runs BEFORE universal raw#8 so the dev
    // toggle can rescue grandfathered files that would otherwise be
    // caught by the universal foreign-ext deny. When toggle is OFF
    // and the file is non-grandfathered, refuse here; when toggle is
    // ON, let the path through (skip raw#8 for hexa-lang scope).
    if (refuse_hexa_lang_scope(path)) return 1;
    if (is_under_hexa_lang(path) && hexa_lang_foreign_ext(path)) {
        // either dev_toggle_on or grandfathered — explicit allow.
        if (refuse_raw13(path)) return 1;        // raw#13 still applies
        if (refuse_raw20(path, flags)) return 1; // raw#20 .own gate
        if (refuse_raw6(path)) return 1;         // raw#6 folder-naming
        if (refuse_raw7_parent_child_stem(path)) return 1;       // raw#7 F6
        if ((flags & O_CREAT) && refuse_raw7_loner_dir(path)) return 1; // raw#7 F5
        return 0;
    }
    for (int i = 0; BANNED_SUFFIXES[i]; i++) {
        if (ends_with(path, BANNED_SUFFIXES[i])) return 1;
    }
    if (refuse_raw8(path)) return 1;
    if (refuse_raw13(path)) return 1;
    if (refuse_raw20(path, flags)) return 1;
    if (refuse_raw6(path)) return 1;
    if (refuse_raw7_parent_child_stem(path)) return 1;           // raw#7 F6
    if ((flags & O_CREAT) && refuse_raw7_loner_dir(path)) return 1;   // raw#7 F5
    return 0;
}

static int refuse_rename(const char *newpath) {
    if (opt_out()) return 0;
    if (!newpath) return 0;
    // raw#1 — checked BEFORE allowlist (locked SSOT is canonical, not temp).
    if (refuse_raw1_mutate(newpath)) return 1;
    if (refuse_raw22_mutate(newpath)) return 1;
    if (on_allowlist(newpath)) return 0;
    for (int i = 0; BANNED_SUFFIXES[i]; i++) {
        if (ends_with(newpath, BANNED_SUFFIXES[i])) return 1;
    }
    return 0;
}

// raw#1 oldpath-side rename guard — refuse renaming the locked SSOT itself
// (not just refusing it as a destination). Standalone helper because
// refuse_rename() handles dst-side; this handles src-side.
static int refuse_rename_src_raw1(const char *oldpath) {
    if (opt_out()) return 0;
    if (!oldpath) return 0;
    if (refuse_raw1_mutate(oldpath)) return 1;
    if (refuse_raw22_explicit(oldpath)) return 1;      // raw#22 src-side (array)
    if (refuse_raw22_sentinel(oldpath)) return 1;      // raw#22 src-side (sentinel)
    return 0;
}

// ── open / openat / creat ────────────────────────────────────────────

typedef int  (*open_fn)(const char *, int, ...);
typedef int  (*openat_fn)(int, const char *, int, ...);
typedef int  (*creat_fn)(const char *, mode_t);
typedef int  (*rename_fn)(const char *, const char *);
typedef int  (*renameat_fn)(int, const char *, int, const char *);

int open(const char *pathname, int flags, ...) {
    static open_fn real = NULL;
    if (!real) real = (open_fn)dlsym(RTLD_NEXT, "open");
    if (refuse(pathname, flags)) { errno = EPERM; return -1; }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
        return real(pathname, flags, mode);
    }
    return real(pathname, flags);
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    static openat_fn real = NULL;
    if (!real) real = (openat_fn)dlsym(RTLD_NEXT, "openat");
    if (refuse(pathname, flags)) { errno = EPERM; return -1; }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
        return real(dirfd, pathname, flags, mode);
    }
    return real(dirfd, pathname, flags);
}

int creat(const char *pathname, mode_t mode) {
    static creat_fn real = NULL;
    if (!real) real = (creat_fn)dlsym(RTLD_NEXT, "creat");
    if (refuse(pathname, O_WRONLY | O_CREAT | O_TRUNC)) { errno = EPERM; return -1; }
    return real(pathname, mode);
}

// ── LFS aliases: open64 / __open64 / openat64 / __openat64 ───────────
//
// glibc exposes Large-File-Support variants of the open family; on 64-bit
// Linux these are functionally identical to plain open/openat (off_t is
// already 64-bit), but stdio (fopen) and several dynamically-linked tools
// (curl, wget) call __open64 directly via the LFS-redirect macro. Without
// dedicated dlsym shims those calls bypass our `open` interposer entirely
// — Phase 11 verification caught curl writing through this gap.
//
// All four route through the same `refuse()` policy used by `open` so
// raw#8/13/20/6/7 + hexa-lang scope checks are identical. The signatures
// match glibc's prototypes (varargs `mode_t` only on O_CREAT).
typedef int (*open64_fn)(const char *, int, ...);
typedef int (*openat64_fn)(int, const char *, int, ...);

int open64(const char *pathname, int flags, ...) {
    static open64_fn real = NULL;
    if (!real) real = (open64_fn)dlsym(RTLD_NEXT, "open64");
    if (refuse(pathname, flags)) { errno = EPERM; return -1; }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
        return real(pathname, flags, mode);
    }
    return real(pathname, flags);
}

int __open64(const char *pathname, int flags, ...) {
    static open64_fn real = NULL;
    if (!real) real = (open64_fn)dlsym(RTLD_NEXT, "__open64");
    if (refuse(pathname, flags)) { errno = EPERM; return -1; }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
        return real(pathname, flags, mode);
    }
    return real(pathname, flags);
}

int openat64(int dirfd, const char *pathname, int flags, ...) {
    static openat64_fn real = NULL;
    if (!real) real = (openat64_fn)dlsym(RTLD_NEXT, "openat64");
    if (refuse(pathname, flags)) { errno = EPERM; return -1; }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
        return real(dirfd, pathname, flags, mode);
    }
    return real(dirfd, pathname, flags);
}

int __openat64(int dirfd, const char *pathname, int flags, ...) {
    static openat64_fn real = NULL;
    if (!real) real = (openat64_fn)dlsym(RTLD_NEXT, "__openat64");
    if (refuse(pathname, flags)) { errno = EPERM; return -1; }
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
        return real(dirfd, pathname, flags, mode);
    }
    return real(dirfd, pathname, flags);
}

// ── fopen / fopen64 / freopen / freopen64 (Phase 13 PLT-bypass close) ──
//
// Phase 12 verification caught a libc-internal gap: glibc's `fopen` routes
// through `__open64` via a HIDDEN ALIAS / direct-call inside libc.so itself,
// not via the public PLT. LD_PRELOAD's symbol-interposition only redirects
// calls that go through the PLT/GOT lookup — calls inside libc to its own
// hidden aliases bind directly to the libc-internal implementation, bypassing
// our `__open64` shim entirely. curl/wget exhibited this: their `fopen` for
// the output file appeared to "succeed" (fopen returned non-NULL) even with
// the open shim in place, because the underlying syscall used libc's private
// `__open64` slot.
//
// Fix = hook `fopen` itself. dlsym(RTLD_NEXT, "fopen") finds glibc's public
// `fopen`, but we get to inspect path + mode and gate BEFORE the libc-
// internal `__open64` is invoked. Once we decide to allow, we hand off to
// real_fopen; libc's internal alias path is fine downstream because the
// policy decision already happened here.
//
// Mode parsing semantics (POSIX/C99 fopen mode):
//   "r"      — read-only.            writes=0 → enforce skipped.
//   "rb"     — read-only binary.     writes=0.
//   "w","wb" — write/truncate.       writes=1 → enforce.
//   "a","ab" — append.               writes=1 → enforce.
//   "r+"     — read + write.         writes=1 → enforce.
//   "w+"     — read + write + trunc. writes=1 → enforce.
//   "a+"     — read + append.        writes=1 → enforce.
//   "...e"   — O_CLOEXEC modifier.   ignored for policy.
//   "...x"   — O_EXCL on create.     write-implying ('w' or 'a' present).
// Detection: any of {'w','a','+'} in the mode string ⇒ a write may occur.
//
// Self-recursion safety: the raw7 reader (load_raw_7_exemptions) calls
// fopen(list_path, "r"). Mode "r" → writes=0 → enforce skipped → real_fopen
// runs without entering refuse(). The pre-existing `g_raw7_loading` flag
// remains as defense-in-depth (paranoia: a future code path that read-mode-
// opens a write-mode-cached file would still be guarded by re-entrancy).
//
// `fdopen(fd, mode)` and `tmpfile()` need no shims:
//   fdopen — fd was opened earlier; that open() was already gated.
//   tmpfile — anonymous + auto-deleted; outside policy scope.
//
// Refuse signal mode for fopen: synthesize O_WRONLY|O_CREAT to mirror what
// the underlying open would have used. refuse() consults is_write_flag()
// which only checks for any write bit, so this is sufficient.

typedef FILE *(*fopen_fn)(const char *, const char *);
typedef FILE *(*freopen_fn)(const char *, const char *, FILE *);

static int fopen_mode_writes(const char *mode) {
    if (!mode) return 0;
    // 'r' alone (with optional 'b','e','x' modifiers) is read-only;
    // any 'w', 'a', or '+' anywhere in the string implies write capability.
    return (strchr(mode, 'w') != NULL)
        || (strchr(mode, 'a') != NULL)
        || (strchr(mode, '+') != NULL);
}

FILE *fopen(const char *path, const char *mode) {
    static fopen_fn real = NULL;
    if (!real) real = (fopen_fn)dlsym(RTLD_NEXT, "fopen");
    if (path && fopen_mode_writes(mode)) {
        if (refuse(path, O_WRONLY | O_CREAT)) { errno = EPERM; return NULL; }
    }
    return real(path, mode);
}

FILE *fopen64(const char *path, const char *mode) {
    static fopen_fn real = NULL;
    if (!real) real = (fopen_fn)dlsym(RTLD_NEXT, "fopen64");
    if (path && fopen_mode_writes(mode)) {
        if (refuse(path, O_WRONLY | O_CREAT)) { errno = EPERM; return NULL; }
    }
    return real(path, mode);
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    static freopen_fn real = NULL;
    if (!real) real = (freopen_fn)dlsym(RTLD_NEXT, "freopen");
    // freopen with NULL path = mode-change on existing fd; no path to gate.
    if (path && fopen_mode_writes(mode)) {
        if (refuse(path, O_WRONLY | O_CREAT)) { errno = EPERM; return NULL; }
    }
    return real(path, mode, stream);
}

FILE *freopen64(const char *path, const char *mode, FILE *stream) {
    static freopen_fn real = NULL;
    if (!real) real = (freopen_fn)dlsym(RTLD_NEXT, "freopen64");
    if (path && fopen_mode_writes(mode)) {
        if (refuse(path, O_WRONLY | O_CREAT)) { errno = EPERM; return NULL; }
    }
    return real(path, mode, stream);
}

int rename(const char *oldpath, const char *newpath) {
    static rename_fn real = NULL;
    if (!real) real = (rename_fn)dlsym(RTLD_NEXT, "rename");
    if (refuse_rename_src_raw1(oldpath)) { errno = EPERM; return -1; }
    if (refuse_rename(newpath)) { errno = EPERM; return -1; }
    return real(oldpath, newpath);
}

int renameat(int olddfd, const char *oldpath, int newdfd, const char *newpath) {
    static renameat_fn real = NULL;
    if (!real) real = (renameat_fn)dlsym(RTLD_NEXT, "renameat");
    if (refuse_rename_src_raw1(oldpath)) { errno = EPERM; return -1; }
    if (refuse_rename(newpath)) { errno = EPERM; return -1; }
    return real(olddfd, oldpath, newdfd, newpath);
}

// renameat2 — Linux 3.15+ extension. coreutils `mv` uses this by default
// (RENAME_NOREPLACE flag) on modern glibc, completely bypassing rename()
// and renameat(). raw#1 verification on ubu1 (kernel 6.17, glibc 2.39)
// caught this gap. Hook signature matches glibc/Linux. Same policy as
// renameat(): src-side raw#1 mutate deny + dst-side refuse_rename().
typedef int (*renameat2_fn)(int, const char *, int, const char *, unsigned int);

int renameat2(int olddfd, const char *oldpath, int newdfd, const char *newpath,
              unsigned int flags) {
    static renameat2_fn real = NULL;
    if (!real) real = (renameat2_fn)dlsym(RTLD_NEXT, "renameat2");
    if (refuse_rename_src_raw1(oldpath)) { errno = EPERM; return -1; }
    if (refuse_rename(newpath)) { errno = EPERM; return -1; }
    if (!real) { errno = ENOSYS; return -1; }
    return real(olddfd, oldpath, newdfd, newpath, flags);
}

// ── unlink / unlinkat / chmod / fchmodat (raw#1 mutation gate) ───────
//
// Phase X (raw#1) — mutation surface beyond write+rename. unlink removes
// the file outright; chmod can flip W bits to enable a subsequent write
// that bypasses the open-time gate (e.g. chmod 0644 on a chmod-ed-down
// .raw, then open O_WRONLY). Both must refuse on RAW1_PATHS literals.
//
// opt_out() honored; allowlist intentionally NOT consulted (locked SSOT
// is repo-canonical and never legitimately under /tmp/ etc.).

typedef int (*unlink_fn)(const char *);
typedef int (*unlinkat_fn)(int, const char *, int);
typedef int (*chmod_fn)(const char *, mode_t);
typedef int (*fchmodat_fn)(int, const char *, mode_t, int);

int unlink(const char *pathname) {
    static unlink_fn real = NULL;
    if (!real) real = (unlink_fn)dlsym(RTLD_NEXT, "unlink");
    if (!opt_out() && (refuse_raw1_mutate(pathname) || refuse_raw22_mutate(pathname))) { errno = EPERM; return -1; }
    return real(pathname);
}

int unlinkat(int dirfd, const char *pathname, int flags) {
    static unlinkat_fn real = NULL;
    if (!real) real = (unlinkat_fn)dlsym(RTLD_NEXT, "unlinkat");
    if (!opt_out() && (refuse_raw1_mutate(pathname) || refuse_raw22_mutate(pathname))) { errno = EPERM; return -1; }
    return real(dirfd, pathname, flags);
}

int chmod(const char *pathname, mode_t mode) {
    static chmod_fn real = NULL;
    if (!real) real = (chmod_fn)dlsym(RTLD_NEXT, "chmod");
    if (!opt_out() && (refuse_raw1_mutate(pathname) || refuse_raw22_mutate(pathname))) { errno = EPERM; return -1; }
    return real(pathname, mode);
}

int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags) {
    static fchmodat_fn real = NULL;
    if (!real) real = (fchmodat_fn)dlsym(RTLD_NEXT, "fchmodat");
    if (!opt_out() && (refuse_raw1_mutate(pathname) || refuse_raw22_mutate(pathname))) { errno = EPERM; return -1; }
    return real(dirfd, pathname, mode, flags);
}

// ── raw#6 mkdir / mkdirat (folder-naming F4 — create-time deny) ──────
//
// Existing refuse_raw6() catches WRITES under banned-named dirs (utils/,
// helpers/, common/, lib/, misc/, stuff/, base/) but only fires when a
// file inside the dir is opened. This hook closes the gap at create-time
// so `mkdir ~/core/hive/utils` itself fails before any file is written.
//
// Scope: hive-only (matches refuse_raw6 is_under_hive_repo gate). Other
// repos pending raw_6.list cross-port follow-up cycle.
//
// Allowlist mirrors refuse_raw6: node_modules/, packages/, infrastructure/,
// libs/, archive/. Path-substring match is sufficient — hive layout never
// has a legit `/utils/` segment outside those prefixes.
//
// We refuse if the LAST path component (basename) matches a banned name.
// This avoids false-positive on parent path segments that happen to contain
// a banned token (the existing substring ban in refuse_raw6 already handles
// path-segment writes; mkdir gate fires on the component being created).

typedef int (*mkdir_fn)(const char *, mode_t);
typedef int (*mkdirat_fn)(int, const char *, mode_t);

static const char *RAW6_BANNED_BASENAMES[] = {
    "utils", "helpers", "common", "lib",
    "misc", "stuff", "base",
    NULL
};

// Strip trailing '/' (treat "foo/" same as "foo") then return last component.
static const char *raw6_basename(const char *path) {
    if (!path || !*path) return NULL;
    size_t n = strlen(path);
    while (n > 0 && path[n-1] == '/') n--;
    if (n == 0) return NULL;
    const char *end = path + n;
    const char *p = end - 1;
    while (p > path && *p != '/') p--;
    return (*p == '/') ? p + 1 : p;
}

static int raw6_basename_eq(const char *path, const char *name) {
    const char *b = raw6_basename(path);
    if (!b) return 0;
    size_t bl = strlen(path);
    while (bl > 0 && path[bl-1] == '/') bl--;
    size_t off = (size_t)(b - path);
    size_t blen = bl - off;
    size_t nl = strlen(name);
    return blen == nl && strncmp(b, name, nl) == 0;
}

static int refuse_raw6_mkdir(const char *path) {
    if (opt_out()) return 0;
    if (!path) return 0;
    if (!is_under_hive_repo(path)) return 0;
    for (int i = 0; RAW6_ALLOW[i]; i++) {
        if (strstr(path, RAW6_ALLOW[i])) return 0;
    }
    for (int i = 0; RAW6_BANNED_BASENAMES[i]; i++) {
        if (raw6_basename_eq(path, RAW6_BANNED_BASENAMES[i])) return 1;
    }
    return 0;
}

int mkdir(const char *pathname, mode_t mode) {
    static mkdir_fn real = NULL;
    if (!real) real = (mkdir_fn)dlsym(RTLD_NEXT, "mkdir");
    if (refuse_raw6_mkdir(pathname)) { errno = EPERM; return -1; }
    return real(pathname, mode);
}

int mkdirat(int dirfd, const char *pathname, mode_t mode) {
    static mkdirat_fn real = NULL;
    if (!real) real = (mkdirat_fn)dlsym(RTLD_NEXT, "mkdirat");
    if (refuse_raw6_mkdir(pathname)) { errno = EPERM; return -1; }
    return real(dirfd, pathname, mode);
}

// ── raw#144 execve / execvp — argv length guard ──────────────────────
//
// Linux kernel ARG_MAX = 2,097,152 bytes total for argv+envp+pointers.
// Massive argv (e.g. accidental `python -c "$X"*1000000`) wastes kernel
// memory + can cause E2BIG late in execve. We pre-validate argv summed
// length against a conservative threshold; over → EPERM (errno=E2BIG)
// before the kernel touches it.
//
// Conservative budget: ARG_MAX - 64KB envp_overhead - 4KB pointer table.
// This is intentionally pessimistic so envp drift (CI/CD env explosions)
// doesn't push us over kernel limit at exec time. Tools needing larger
// argv (rare; usually a misuse) bypass via CLAUDX_NO_SANDBOX=1.
//
// Short-cmd bypass: if argv total < SHORT_CMD_BYPASS (32KB) we return
// allow immediately — the vast majority of execs never approach the
// limit and we don't want extra walking cost.
//
// Linux-only. macOS execve goes through SBPL (sandbox-exec) when present
// and ARG_MAX is enforced by darwin kernel; we don't shim there.

typedef int (*execve_fn)(const char *, char *const[], char *const[]);
typedef int (*execvp_fn)(const char *, char *const[]);

#define RAW144_ARG_MAX           2097152L   // Linux kernel default
#define RAW144_ENVP_OVERHEAD       65536L   // 64KB reserved for envp+pointers
#define RAW144_BUDGET           (RAW144_ARG_MAX - RAW144_ENVP_OVERHEAD)
#define RAW144_SHORT_BYPASS        32768L   // <32KB → fast-path allow

static int refuse_raw144(char *const argv[]) {
    if (opt_out()) return 0;
    if (!argv) return 0;
    long total = 0;
    for (int i = 0; argv[i]; i++) {
        total += (long)strlen(argv[i]) + 1;     // +1 for NUL terminator
        if (total < RAW144_SHORT_BYPASS && argv[i+1] == NULL) return 0;
        if (total > RAW144_BUDGET) return 1;
    }
    return 0;
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
    static execve_fn real = NULL;
    if (!real) real = (execve_fn)dlsym(RTLD_NEXT, "execve");
    if (refuse_raw144(argv)) { errno = E2BIG; return -1; }
    return real(pathname, argv, envp);
}

int execvp(const char *file, char *const argv[]) {
    static execvp_fn real = NULL;
    if (!real) real = (execvp_fn)dlsym(RTLD_NEXT, "execvp");
    if (refuse_raw144(argv)) { errno = E2BIG; return -1; }
    return real(file, argv);
}

// ── symlink / symlinkat (raw#21) ─────────────────────────────────────

typedef int (*symlink_fn)(const char *, const char *);
typedef int (*symlinkat_fn)(const char *, int, const char *);

static int refuse_symlink(const char *linkpath) {
    if (opt_out()) return 0;
    if (!linkpath) return 0;
    if (strstr(linkpath, "/archive/deprecated/")) return 0;  // allowlist
    if (strstr(linkpath, "/tmp/")) return 0;
    // All other symlink creates are blocked (raw#21 conservative).
    // Allowlist can be widened later if false positives emerge.
    return 1;
}

int symlink(const char *target, const char *linkpath) {
    static symlink_fn real = NULL;
    if (!real) real = (symlink_fn)dlsym(RTLD_NEXT, "symlink");
    if (refuse_symlink(linkpath)) { errno = EPERM; return -1; }
    return real(target, linkpath);
}

int symlinkat(const char *target, int newdirfd, const char *linkpath) {
    static symlinkat_fn real = NULL;
    if (!real) real = (symlinkat_fn)dlsym(RTLD_NEXT, "symlinkat");
    if (refuse_symlink(linkpath)) { errno = EPERM; return -1; }
    return real(target, newdirfd, linkpath);
}
