/* ═══════════════════════════════════════════════════════════════════
 * self/native/term_ffi.c — TUI-PRIM L1 FFI shim (POSIX termios + ioctl + signal)
 *
 * KICK VERDICT
 *   2026-04-27_hexa-native-tui-implementation-plan_KICK_omega_cycle.json
 *   T1.A 5-layer rollout — Layer 1 (this file).
 *   Forward-spec deliverable: architecture-only contract, no hexa builtin
 *   registration yet (5-touch surgery + self-host rebuild deferred per
 *   kick obstruction O1).
 *
 * SURFACE (12-symbol C ABI)
 *   Raw mode lifecycle:
 *     int  term_raw_enter(void)            — save current + cfmakeraw + tcsetattr; rc=0 ok
 *     int  term_raw_restore(void)           — tcsetattr restore saved; rc=0 ok
 *   Window size:
 *     int  term_get_winsize(int *rows, int *cols)  — ioctl TIOCGWINSZ; rc=0 ok
 *   I/O (non-blocking via poll):
 *     int  term_poll_stdin(int timeout_ms)  — 1=ready, 0=timeout, -1=error, -2=hangup/eof
 *     int  term_read_byte(void)             — read 1 byte; -1 on EOF/error
 *     int  term_read_bytes(unsigned char *buf, int max_n) — short read; bytes-read or -1
 *     int  term_write(const char *buf, int n) — write n bytes to stdout; rc=bytes or -1
 *   Signals:
 *     int  term_install_sigwinch(void)      — install SA_RESTART handler that
 *                                             flips a volatile flag; rc=0 ok
 *     int  term_sigwinch_pending(void)      — returns flag; resets to 0 on read
 *     int  term_install_sigint(void)        — SIGINT/SIGTERM → flag for graceful exit
 *     int  term_sigint_pending(void)        — flag; resets to 0 on read
 *   Introspection:
 *     int  term_isatty_stdin(void)          — isatty(0)
 *     int  term_isatty_stdout(void)         — isatty(1)
 *     int  term_getppid(void)               — getppid() for orphan-suicide
 *
 * INVARIANTS
 *   - Single-process (no thread safety; matches hexa runtime's single-threaded
 *     surface). Saved termios kept in module-static storage.
 *   - term_raw_restore is idempotent (safe to call without prior _enter).
 *   - Signal handlers are async-signal-safe: only sig_atomic_t flag writes.
 *   - All errors set errno per POSIX; callers can read it via standard means
 *     (a future term_strerror builtin can wrap strerror_r if needed).
 *
 * BUILD
 *   gcc -O2 -Wall -Wextra -c term_ffi.c -o term_ffi.o
 *   (No external deps beyond libc + standard POSIX headers.)
 *   Smoke-test main embedded under #ifdef TERM_FFI_SMOKE — compile with
 *   `gcc -DTERM_FFI_SMOKE -O2 term_ffi.c -o /tmp/term_ffi_smoke`.
 *
 * NEXT
 *   - Layer 2 (input decoder, self/tui/input.hexa) consumes term_read_byte
 *     stream + decodes utf8/csi/sgr/paste/focus.
 *   - Hexa builtin registration: codegen_c2.hexa:3292 + :3736 + :5519 +
 *     env.hexa:178 + hexa_full.hexa:11821 + build_c.hexa:1784 — 5-touch
 *     surgery + self-host rebuild (defer to next session).
 * ═══════════════════════════════════════════════════════════════════ */

/* Feature-gate ordering: Darwin/BSD first (cfmakeraw, SIGWINCH need
 * _DARWIN_C_SOURCE on macOS); _DEFAULT_SOURCE re-exposes BSDism on glibc;
 * _POSIX_C_SOURCE pins the baseline portable surface. Order matters —
 * defining _POSIX_C_SOURCE alone hides BSDisms on Mac. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* ─── module state ─── */

static struct termios _term_saved;
static int _term_saved_valid = 0;

static volatile sig_atomic_t _sigwinch_flag = 0;
static volatile sig_atomic_t _sigint_flag = 0;

static void _on_sigwinch(int sig) {
    (void)sig;
    _sigwinch_flag = 1;
}

static void _on_sigint(int sig) {
    (void)sig;
    _sigint_flag = 1;
}

/* ─── raw mode lifecycle ─── */

int term_raw_enter(void) {
    if (_term_saved_valid) {
        return 0;
    }
    if (tcgetattr(STDIN_FILENO, &_term_saved) != 0) {
        return -1;
    }
    struct termios raw = _term_saved;
    cfmakeraw(&raw);
    /* Per kick spec: deliver byte-at-a-time without echo, but keep
     * SIGINT/SIGQUIT/SIGTSTP active so Ctrl-C reaches our handler. */
    raw.c_lflag |= ISIG;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return -1;
    }
    _term_saved_valid = 1;
    return 0;
}

int term_raw_restore(void) {
    if (!_term_saved_valid) {
        return 0;  /* idempotent: nothing to restore */
    }
    int rc = tcsetattr(STDIN_FILENO, TCSANOW, &_term_saved);
    _term_saved_valid = 0;
    return (rc == 0) ? 0 : -1;
}

/* ─── window size ─── */

int term_get_winsize(int *rows, int *cols) {
    if (rows == NULL || cols == NULL) {
        errno = EINVAL;
        return -1;
    }
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
        return -1;
    }
    *rows = (int)ws.ws_row;
    *cols = (int)ws.ws_col;
    return 0;
}

/* ─── I/O (non-blocking) ─── */

int term_poll_stdin(int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0) return -1;
    if (rc == 0) return 0;
    if (pfd.revents & POLLIN) return 1;
    /* No POLLIN but POLLHUP/POLLERR/POLLNVAL — stdin permanently dead
     * (parent/terminal hangup, orphan case). Distinct from -1 (poll
     * syscall error) and 0 (timeout) so callers can route to clean EOF.
     * Root-cause for the 2026-05-05 hive-hexa-bin runaway (PPID=1 orphan
     * tight-looped poll() → hexa_array_push allocations for 5h, RSS 36GB):
     * the prior `(POLLIN ? 1 : 0)` collapse made callers treat hangup as
     * timeout and re-poll forever, never reaching an exit branch. */
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) return -2;
    return 0;
}

/* getppid() — exposed to hexa for orphan-suicide defense-in-depth.
 * If parent dies, kernel reparents to PID 1 (init/launchd). hexa app
 * loops can poll this every N iterations and exit when parent==1, in
 * case POLLHUP isn't delivered (e.g. stdin is /dev/null or a still-live
 * pipe whose other end leaked to a different process). */
int term_getppid(void) {
    return (int)getppid();
}

int term_read_byte(void) {
    unsigned char b;
    ssize_t n;
    do {
        n = read(STDIN_FILENO, &b, 1);
    } while (n < 0 && errno == EINTR);
    if (n != 1) return -1;
    return (int)b;
}

int term_read_bytes(unsigned char *buf, int max_n) {
    if (buf == NULL || max_n <= 0) {
        errno = EINVAL;
        return -1;
    }
    ssize_t n;
    do {
        n = read(STDIN_FILENO, buf, (size_t)max_n);
    } while (n < 0 && errno == EINTR);
    if (n < 0) return -1;
    return (int)n;
}

int term_write(const char *buf, int n) {
    if (buf == NULL || n < 0) {
        errno = EINVAL;
        return -1;
    }
    int total = 0;
    while (total < n) {
        ssize_t w = write(STDOUT_FILENO, buf + total, (size_t)(n - total));
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (int)w;
    }
    return total;
}

/* ─── signals ─── */

int term_install_sigwinch(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = _on_sigwinch;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    return (sigaction(SIGWINCH, &sa, NULL) == 0) ? 0 : -1;
}

int term_sigwinch_pending(void) {
    int v = _sigwinch_flag;
    _sigwinch_flag = 0;
    return v;
}

int term_install_sigint(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = _on_sigint;
    sa.sa_flags = 0;  /* no SA_RESTART — Ctrl-C interrupts blocking reads */
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0) return -1;
    if (sigaction(SIGTERM, &sa, NULL) != 0) return -1;
    return 0;
}

int term_sigint_pending(void) {
    int v = _sigint_flag;
    _sigint_flag = 0;
    return v;
}

/* ─── introspection ─── */

int term_isatty_stdin(void) {
    return isatty(STDIN_FILENO) ? 1 : 0;
}

int term_isatty_stdout(void) {
    return isatty(STDOUT_FILENO) ? 1 : 0;
}

/* ─── PTY harness primitives (forward-only "real PTY harness" closure) ─── */

#if defined(__APPLE__) || defined(__linux__)
#  if defined(__APPLE__)
#    include <util.h>          /* forkpty */
#  else
#    include <pty.h>           /* forkpty (libutil) on Linux */
#  endif
#  include <sys/wait.h>
#endif

/* Spawn a command in a pseudo-terminal slave. Returns master fd; child
 * runs argv via execvp. On error returns -1. The pid is stored in
 * *out_pid. The slave terminal has the requested winsize.
 *
 * Caller flow:
 *   int pid = 0;
 *   int mfd = term_pty_spawn(["/bin/sh", "-c", "echo hi", NULL], 24, 80, &pid);
 *   // write to mfd to drive child stdin; read mfd to consume stdout/err
 *   waitpid(pid, &status, 0);
 *   close(mfd);
 *
 * Caller is responsible for closing mfd + reaping the child.
 */
int term_pty_spawn(const char *const argv[], int rows, int cols, int *out_pid) {
#if defined(__APPLE__) || defined(__linux__)
    if (out_pid) *out_pid = -1;
    if (!argv || !argv[0]) return -1;
    struct winsize ws = {0};
    ws.ws_row = (unsigned short)(rows > 0 ? rows : 24);
    ws.ws_col = (unsigned short)(cols > 0 ? cols : 80);
    int mfd = -1;
    // 2026-05-06 — POSIX fork buffer flush (forkpty = fork + open pty;
    // parent stdio is inherited by child until execvp swaps it out)
    fflush(NULL);
    pid_t pid = forkpty(&mfd, NULL, NULL, &ws);
    if (pid < 0) return -1;
    if (pid == 0) {
        /* child: execvp into argv. cast to char *const * for execvp signature. */
        execvp(argv[0], (char *const *)argv);
        /* if exec fails, exit immediately */
        _exit(127);
    }
    if (out_pid) *out_pid = (int)pid;
    return mfd;
#else
    (void)argv; (void)rows; (void)cols; (void)out_pid;
    return -1;
#endif
}

/* Read up to max_bytes from an fd. Returns count read, 0 on EOF, -1
 * on error. Non-blocking when fd is set O_NONBLOCK. */
int term_fd_read(int fd, unsigned char *buf, int max_bytes) {
    if (fd < 0 || !buf || max_bytes <= 0) return -1;
    ssize_t n = read(fd, buf, (size_t)max_bytes);
    if (n < 0) return -1;
    return (int)n;
}

/* Write n bytes to fd. Returns count written, -1 on error. */
int term_fd_write(int fd, const unsigned char *buf, int n) {
    if (fd < 0 || !buf || n < 0) return -1;
    ssize_t w = write(fd, buf, (size_t)n);
    if (w < 0) return -1;
    return (int)w;
}

int term_fd_close(int fd) {
    return close(fd);
}

/* Non-blocking poll on fd — returns 1 if readable, 0 on timeout, -1 on error. */
int term_fd_poll(int fd, int timeout_ms) {
    if (fd < 0) return -1;
    struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0) return -1;
    if (rc == 0) return 0;
    if (pfd.revents & (POLLIN | POLLHUP)) return 1;
    return 0;
}

/* Reap a child non-blockingly. Returns:
 *   1  if child exited (status stored in *out_status)
 *   0  if still running
 *  -1  on error (pid invalid, etc.)
 */
int term_pty_reap(int pid, int *out_status) {
#if defined(__APPLE__) || defined(__linux__)
    int st = 0;
    pid_t r = waitpid((pid_t)pid, &st, WNOHANG);
    if (r < 0) return -1;
    if (r == 0) return 0;
    if (out_status) *out_status = st;
    return 1;
#else
    (void)pid; (void)out_status;
    return -1;
#endif
}

/* Convenience wrapper: spawn `/bin/sh -c <cmd>` in pty. Used by the
 * hexa builtin layer where passing argv arrays through the HexaVal
 * boundary is awkward — sh -c gives shell glob/quote handling for free. */
int term_pty_spawn_sh(const char *cmd, int rows, int cols, int *out_pid) {
#if defined(__APPLE__) || defined(__linux__)
    if (!cmd) return -1;
    const char *argv[] = {"/bin/sh", "-c", cmd, NULL};
    return term_pty_spawn(argv, rows, cols, out_pid);
#else
    (void)cmd; (void)rows; (void)cols; (void)out_pid;
    return -1;
#endif
}

/* ═══════════════════════════════════════════════════════════════════
 * SMOKE TEST — compile with -DTERM_FFI_SMOKE
 *
 *   gcc -DTERM_FFI_SMOKE -O2 -Wall term_ffi.c -o /tmp/term_ffi_smoke
 *   /tmp/term_ffi_smoke           # in a real TTY: prints winsize, reads 1 byte
 *   echo x | /tmp/term_ffi_smoke  # in pipe: prints winsize fail, isatty=0
 * ═══════════════════════════════════════════════════════════════════ */

#ifdef TERM_FFI_SMOKE
int main(void) {
    int is_tty_in = term_isatty_stdin();
    int is_tty_out = term_isatty_stdout();
    printf("isatty(stdin)=%d isatty(stdout)=%d\n", is_tty_in, is_tty_out);

    int rows = 0, cols = 0;
    if (term_get_winsize(&rows, &cols) == 0) {
        printf("winsize rows=%d cols=%d\n", rows, cols);
    } else {
        printf("winsize: error errno=%d (%s)\n", errno, strerror(errno));
    }

    if (term_install_sigwinch() == 0) {
        printf("SIGWINCH handler installed\n");
    }

    if (!is_tty_in) {
        printf("not a TTY — skipping raw-mode read test\n");
        return 0;
    }

    if (term_raw_enter() != 0) {
        printf("raw_enter failed errno=%d (%s)\n", errno, strerror(errno));
        return 1;
    }

    const char *prompt = "press a key (q to quit): ";
    term_write(prompt, (int)strlen(prompt));

    int ready = term_poll_stdin(5000);
    if (ready <= 0) {
        const char *msg = "\r\n[timeout/error]\r\n";
        term_write(msg, (int)strlen(msg));
        term_raw_restore();
        return 0;
    }

    int b = term_read_byte();
    char line[64];
    int ln = snprintf(line, sizeof(line), "\r\nread byte=0x%02x ('%c')\r\n",
                      b, (b >= 0x20 && b < 0x7f) ? b : '?');
    term_write(line, ln);

    term_raw_restore();
    return 0;
}
#endif /* TERM_FFI_SMOKE */
