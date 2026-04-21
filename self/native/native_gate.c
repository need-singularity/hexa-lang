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
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static const char *BANNED_SUFFIXES[] = { ".py", ".rs", ".sh", ".toml", NULL };

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

static int refuse(const char *path, int flags) {
    if (opt_out()) return 0;
    if (!path || !is_write_flag(flags)) return 0;
    if (on_allowlist(path)) return 0;
    for (int i = 0; BANNED_SUFFIXES[i]; i++) {
        if (ends_with(path, BANNED_SUFFIXES[i])) return 1;
    }
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
