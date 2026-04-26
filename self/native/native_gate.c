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
//   - `fopen` goes through `open` internally on glibc; no separate shim.
//   - macOS `DYLD_INSERT_LIBRARIES` is blocked on SIP-protected binaries;
//     for Mac, sandbox-exec is the primary path (self/sbpl/native.sb).
//     This shim is for unprivileged Linux (and unsigned mac executables).

// NOTE: build with -D_GNU_SOURCE on the command line (see BUILD above).
// Compiling without that define exposes a POSIX-only signature; dlsym
// RTLD_NEXT would still work, but openat/renameat extern decls wouldn't
// be visible from the glibc headers.

#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

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

static int raw7_path_exempt(const char *path) {
    if (!path) return 0;
    for (int i = 0; RAW7_ALLOW[i]; i++) {
        if (strstr(path, RAW7_ALLOW[i])) return 1;
    }
    return 0;
}

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
    if (on_allowlist(newpath)) return 0;
    for (int i = 0; BANNED_SUFFIXES[i]; i++) {
        if (ends_with(newpath, BANNED_SUFFIXES[i])) return 1;
    }
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

int rename(const char *oldpath, const char *newpath) {
    static rename_fn real = NULL;
    if (!real) real = (rename_fn)dlsym(RTLD_NEXT, "rename");
    if (refuse_rename(newpath)) { errno = EPERM; return -1; }
    return real(oldpath, newpath);
}

int renameat(int olddfd, const char *oldpath, int newdfd, const char *newpath) {
    static renameat_fn real = NULL;
    if (!real) real = (renameat_fn)dlsym(RTLD_NEXT, "renameat");
    if (refuse_rename(newpath)) { errno = EPERM; return -1; }
    return real(olddfd, oldpath, newdfd, newpath);
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
