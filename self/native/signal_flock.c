/* self/native/signal_flock.c — stdlib/os signal + flock builtins
 *
 * Roadmap item 62 (2026-04-22). Included from self/runtime.c via
 *   #include "native/signal_flock.c"
 * analogous to native/net.c. Pure libc; portable macOS/Linux.
 *
 * Symbols exported (signal — 9 + flock — 2):
 *   hexa_os_sig_install(sig, name_str)  : install async-signal-safe
 *                                         trampoline; stash handler name
 *   hexa_os_sig_uninstall(sig)          : restore SIG_DFL
 *   hexa_os_sig_current(sig)            : installed name ("" if none)
 *   hexa_os_sig_raise(sig)              : kill(getpid, sig)
 *   hexa_os_sig_kill(pid, sig)          : kill(pid, sig)
 *   hexa_os_sig_pipe_fd()               : read-end of self-pipe
 *   hexa_os_sig_drain()                 : array<int> pending signals,
 *                                         drained from pipe
 *   hexa_os_sig_block(arr)              : sigprocmask SIG_BLOCK
 *   hexa_os_sig_unblock(arr)            : sigprocmask SIG_UNBLOCK
 *
 *   hexa_os_flock_open(path, mode_int)  : open O_RDWR|O_CREAT|O_CLOEXEC
 *                                         then flock. mode bits:
 *                                           0 = LOCK_EX  (blocking)
 *                                           1 = LOCK_SH  (blocking)
 *                                           2 = LOCK_EX|LOCK_NB
 *                                           3 = LOCK_SH|LOCK_NB
 *                                         Returns fd >= 0 on success,
 *                                         -errno on failure.
 *   hexa_os_flock_close(fd)             : LOCK_UN + close; returns 0 or
 *                                         -errno.
 *
 * Error model: negative TAG_INT (raw -errno) — matches net.c.
 *
 * Async-signal-safety: __hexa_sig_trampoline uses only write(2), which
 * POSIX lists as async-signal-safe (signal-safety(7)). No malloc, no
 * printf, no lock. The trampoline writes 1 byte == signal number to the
 * self-pipe; the scheduler (pure hexa code) drains via hexa_os_sig_drain.
 *
 * O_CLOEXEC: flock fd is opened CLOEXEC so execve(3) does not leak the
 * lock into child processes (locks are per-(pid,open-file-description)
 * on POSIX, and CLOEXEC avoids accidental lock promotion through exec).
 */

#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/file.h>   /* flock(2) */
#include <sys/types.h>

#ifndef NSIG
#define NSIG 64
#endif

/* Per-signal stored handler name. Written under normal program control
 * (hexa_os_sig_install) — not from inside the trampoline. */
static char     g_handler_names[NSIG][64];
static int      g_handler_set[NSIG];
static int      g_sig_pipe[2] = {-1, -1};

/* Async-signal-safe: only calls write(2). */
static void __hexa_sig_trampoline(int sig) {
    if (g_sig_pipe[1] >= 0 && sig > 0 && sig < NSIG) {
        unsigned char b = (unsigned char)sig;
        int saved = errno;
        (void)write(g_sig_pipe[1], &b, 1);
        errno = saved;
    }
}

/* Set O_NONBLOCK + FD_CLOEXEC on fd. Returns 0 / -errno. */
static int _hexa_fd_nonblock_cloexec(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -errno;
    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return -errno;
    int cf = fcntl(fd, F_GETFD, 0);
    if (cf < 0) return -errno;
    if (fcntl(fd, F_SETFD, cf | FD_CLOEXEC) < 0) return -errno;
    return 0;
}

/* Lazy self-pipe init. Thread-compat not guaranteed — single scheduler
 * thread expected (matches async_runtime.hexa). */
static int _hexa_sig_pipe_init(void) {
    if (g_sig_pipe[0] >= 0) return 0;
    int p[2];
    if (pipe(p) < 0) return -errno;
    int rc = _hexa_fd_nonblock_cloexec(p[0]);
    if (rc < 0) { close(p[0]); close(p[1]); return rc; }
    rc = _hexa_fd_nonblock_cloexec(p[1]);
    if (rc < 0) { close(p[0]); close(p[1]); return rc; }
    g_sig_pipe[0] = p[0];
    g_sig_pipe[1] = p[1];
    return 0;
}

HexaVal hexa_os_sig_pipe_fd(void) {
    int rc = _hexa_sig_pipe_init();
    if (rc < 0) return hexa_int(rc);
    return hexa_int(g_sig_pipe[0]);
}

HexaVal hexa_os_sig_install(HexaVal sig_val, HexaVal name_val) {
    if (!HX_IS_INT(sig_val)) return hexa_int(-EINVAL);
    int sig = (int)HX_INT(sig_val);
    if (sig <= 0 || sig >= NSIG) return hexa_int(-EINVAL);
    int rc = _hexa_sig_pipe_init();
    if (rc < 0) return hexa_int(rc);
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = __hexa_sig_trampoline;
    act.sa_flags = SA_RESTART;
    sigemptyset(&act.sa_mask);
    if (sigaction(sig, &act, NULL) < 0) return hexa_int(-errno);
    const char* name = HX_IS_STR(name_val) ? HX_STR(name_val) : "";
    size_t nlen = strlen(name);
    if (nlen >= sizeof(g_handler_names[sig])) nlen = sizeof(g_handler_names[sig]) - 1;
    memcpy(g_handler_names[sig], name, nlen);
    g_handler_names[sig][nlen] = '\0';
    g_handler_set[sig] = 1;
    return hexa_int(0);
}

HexaVal hexa_os_sig_uninstall(HexaVal sig_val) {
    if (!HX_IS_INT(sig_val)) return hexa_int(-EINVAL);
    int sig = (int)HX_INT(sig_val);
    if (sig <= 0 || sig >= NSIG) return hexa_int(-EINVAL);
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_DFL;
    sigemptyset(&act.sa_mask);
    if (sigaction(sig, &act, NULL) < 0) return hexa_int(-errno);
    g_handler_names[sig][0] = '\0';
    g_handler_set[sig] = 0;
    return hexa_int(0);
}

HexaVal hexa_os_sig_current(HexaVal sig_val) {
    if (!HX_IS_INT(sig_val)) return hexa_str("");
    int sig = (int)HX_INT(sig_val);
    if (sig <= 0 || sig >= NSIG) return hexa_str("");
    if (!g_handler_set[sig]) return hexa_str("");
    return hexa_str(g_handler_names[sig]);
}

HexaVal hexa_os_sig_raise(HexaVal sig_val) {
    if (!HX_IS_INT(sig_val)) return hexa_int(-EINVAL);
    int sig = (int)HX_INT(sig_val);
    if (kill(getpid(), sig) < 0) return hexa_int(-errno);
    return hexa_int(0);
}

HexaVal hexa_os_sig_kill(HexaVal pid_val, HexaVal sig_val) {
    if (!HX_IS_INT(pid_val) || !HX_IS_INT(sig_val)) return hexa_int(-EINVAL);
    int pid = (int)HX_INT(pid_val);
    int sig = (int)HX_INT(sig_val);
    if (kill(pid, sig) < 0) return hexa_int(-errno);
    return hexa_int(0);
}

/* Drain the self-pipe, returning an array of signal numbers that had
 * been delivered since last drain. Non-blocking. */
HexaVal hexa_os_sig_drain(void) {
    HexaVal arr = hexa_array_new();
    int rc = _hexa_sig_pipe_init();
    if (rc < 0) return arr;
    unsigned char buf[64];
    for (;;) {
        ssize_t n = read(g_sig_pipe[0], buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; i++) {
            arr = hexa_array_push(arr, hexa_int((int)buf[i]));
        }
        if ((size_t)n < sizeof(buf)) break;
    }
    return arr;
}

/* sigprocmask helper. which: SIG_BLOCK / SIG_UNBLOCK. arr_val must be
 * HexaVal array of ints. */
static HexaVal _hexa_sig_mask_op(int which, HexaVal arr_val) {
    if (!HX_IS_ARRAY(arr_val)) return hexa_int(-EINVAL);
    sigset_t set;
    sigemptyset(&set);
    int64_t n = (int64_t)HX_ARR_LEN(arr_val);
    for (int64_t i = 0; i < n; i++) {
        HexaVal el = hexa_array_get(arr_val, i);
        if (!HX_IS_INT(el)) continue;
        int sig = (int)HX_INT(el);
        if (sig > 0 && sig < NSIG) sigaddset(&set, sig);
    }
    if (sigprocmask(which, &set, NULL) < 0) return hexa_int(-errno);
    return hexa_int(0);
}

HexaVal hexa_os_sig_block(HexaVal arr_val) {
    return _hexa_sig_mask_op(SIG_BLOCK, arr_val);
}

HexaVal hexa_os_sig_unblock(HexaVal arr_val) {
    return _hexa_sig_mask_op(SIG_UNBLOCK, arr_val);
}

/* ── flock ────────────────────────────────────────────────────── */

/* mode_int:
 *   0 = LOCK_EX (blocking)
 *   1 = LOCK_SH (blocking)
 *   2 = LOCK_EX | LOCK_NB
 *   3 = LOCK_SH | LOCK_NB
 */
HexaVal hexa_os_flock_open(HexaVal path_val, HexaVal mode_val) {
    if (!HX_IS_STR(path_val) || !HX_IS_INT(mode_val)) return hexa_int(-EINVAL);
    const char* path = HX_STR(path_val);
    int mode_bits = (int)HX_INT(mode_val);

    int op;
    switch (mode_bits) {
        case 0: op = LOCK_EX; break;
        case 1: op = LOCK_SH; break;
        case 2: op = LOCK_EX | LOCK_NB; break;
        case 3: op = LOCK_SH | LOCK_NB; break;
        default: return hexa_int(-EINVAL);
    }

    /* O_CLOEXEC mandatory — exec'd children must not inherit the lock fd.
     * POSIX flock(2) semantics: lock is held per open-file-description; if
     * fd leaks via exec, the child would own the lock, surprising the
     * parent's unlock expectations. */
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return hexa_int(-errno);

    if (flock(fd, op) < 0) {
        int e = errno;
        close(fd);
        return hexa_int(-e);
    }
    return hexa_int(fd);
}

HexaVal hexa_os_flock_close(HexaVal fd_val) {
    if (!HX_IS_INT(fd_val)) return hexa_int(-EINVAL);
    int fd = (int)HX_INT(fd_val);
    if (fd < 0) return hexa_int(-EINVAL);
    /* Best-effort unlock; close always runs. */
    (void)flock(fd, LOCK_UN);
    if (close(fd) < 0) return hexa_int(-errno);
    return hexa_int(0);
}

/* getpid helper (used by .hexa wrappers for raise_signal fallback). */
HexaVal hexa_os_getpid(void) {
    return hexa_int((int64_t)getpid());
}

/* ── TAG_FN shim globals (bootstrap bridge) ──────────────────────
 * Analogous to net.c's _hexa_init_net_fn_shims. Called from runtime.c
 * _hexa_init_fn_shims. After the next hexa_cc bootstrap cycle teaches
 * the transpiler direct-lowering, these globals become harmless. */
HexaVal os_sig_install;
HexaVal os_sig_uninstall;
HexaVal os_sig_current;
HexaVal os_sig_raise;
HexaVal os_sig_kill;
HexaVal os_sig_pipe_fd;
HexaVal os_sig_drain;
HexaVal os_sig_block;
HexaVal os_sig_unblock;
HexaVal os_flock_open;
HexaVal os_flock_close;
HexaVal os_getpid;

static void _hexa_init_os_fn_shims(void) {
    os_sig_install   = hexa_fn_new((void*)hexa_os_sig_install,   2);
    os_sig_uninstall = hexa_fn_new((void*)hexa_os_sig_uninstall, 1);
    os_sig_current   = hexa_fn_new((void*)hexa_os_sig_current,   1);
    os_sig_raise     = hexa_fn_new((void*)hexa_os_sig_raise,     1);
    os_sig_kill      = hexa_fn_new((void*)hexa_os_sig_kill,      2);
    os_sig_pipe_fd   = hexa_fn_new((void*)hexa_os_sig_pipe_fd,   0);
    os_sig_drain     = hexa_fn_new((void*)hexa_os_sig_drain,     0);
    os_sig_block     = hexa_fn_new((void*)hexa_os_sig_block,     1);
    os_sig_unblock   = hexa_fn_new((void*)hexa_os_sig_unblock,   1);
    os_flock_open    = hexa_fn_new((void*)hexa_os_flock_open,    2);
    os_flock_close   = hexa_fn_new((void*)hexa_os_flock_close,   1);
    os_getpid        = hexa_fn_new((void*)hexa_os_getpid,        0);
}
