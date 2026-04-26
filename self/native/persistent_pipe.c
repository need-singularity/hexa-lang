/* self/native/persistent_pipe.c
 *
 * ω-runtime-1 Phase A — persistent-pipe primitive (2026-04-26).
 *
 * Five new builtins (handle-based bidirectional child-process API):
 *
 *   hexa_pipe_spawn(cmd)               : str        → int   handle id (≥0) or -1
 *   hexa_pipe_send_line(h, payload)    : int, str   → bool  true on full write
 *   hexa_pipe_recv_line(h, timeout_ms) : int, int   → str   line (\n trimmed) or ""
 *   hexa_pipe_close(h)                 : int        → bool  true on clean close
 *   hexa_pipe_alive(h)                 : int        → bool  child still running
 *
 * Maps onto the existing hexa_exec_capture (pipe+fork+execvp+dup2)
 * pattern from runtime.c:6010 — about 60% reusable. Difference: keeps
 * pipes open after fork() instead of draining-then-closing them, and
 * exposes the parent ends through a static handle table.
 *
 * Design doc: /Users/ghost/core/anima/state/omega_runtime_1_persistent_pipe_design.json
 *
 * Lifecycle:
 *   - Soft cap: min(RLIMIT_NOFILE/4, 64) concurrent live handles.
 *     Each handle eats 2 fds (stdin pipe write-end, stdout pipe read-end).
 *   - close(h): SIGTERM → 100ms grace → SIGKILL → waitpid → free slot.
 *   - atexit handler: SIGTERM all live, sleep 50ms, SIGKILL, waitpid(WNOHANG).
 *
 * v1 limitations (per design):
 *   - line-protocol-only on recv: response >PIPE_BUF (4KB darwin) risks deadlock
 *     for the worker if its newline-flush is delayed; the audio-worker NDJSON
 *     responses are <256B so this is safe for the immediate consumer.
 *   - stderr inherited (not separately captured); matches design.
 *   - synchronous per handle (one in-flight req); concurrency across handles is
 *     the user's problem.
 *
 * Included from self/runtime.c via #include "native/persistent_pipe.c"
 * analogous to net.c / signal_flock.c / exec_argv_sha256.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

/* ── handle table ───────────────────────────────────────────── */

typedef struct {
    pid_t  pid;          /* 0 = free slot */
    int    stdin_fd;     /* parent write-end → child stdin */
    int    stdout_fd;    /* parent read-end  ← child stdout */
    int    state;        /* 0=free, 1=live, 2=dead-pending-close */
    int    last_exit;    /* captured exit code (waitpid) */
} HexaChild;

#define HXP_MAX_HANDLES 64

static HexaChild g_pipe_table[HXP_MAX_HANDLES];
static int       g_pipe_table_inited = 0;
static int       g_pipe_atexit_installed = 0;

static long _hxp_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

static void _hxp_sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void _hxp_atexit_cleanup(void) {
    for (int i = 0; i < HXP_MAX_HANDLES; i++) {
        HexaChild* c = &g_pipe_table[i];
        if (c->state == 1 && c->pid > 0) {
            if (c->stdin_fd  >= 0) { close(c->stdin_fd);  c->stdin_fd  = -1; }
            kill(c->pid, SIGTERM);
        }
    }
    /* short grace period for SIGTERM */
    _hxp_sleep_ms(50);
    for (int i = 0; i < HXP_MAX_HANDLES; i++) {
        HexaChild* c = &g_pipe_table[i];
        if (c->state == 1 && c->pid > 0) {
            kill(c->pid, SIGKILL);
            waitpid(c->pid, NULL, WNOHANG);
            if (c->stdout_fd >= 0) { close(c->stdout_fd); c->stdout_fd = -1; }
            c->state = 0;
        }
    }
}

static void _hxp_init_table(void) {
    if (g_pipe_table_inited) return;
    for (int i = 0; i < HXP_MAX_HANDLES; i++) {
        g_pipe_table[i].pid       = 0;
        g_pipe_table[i].stdin_fd  = -1;
        g_pipe_table[i].stdout_fd = -1;
        g_pipe_table[i].state     = 0;
        g_pipe_table[i].last_exit = 0;
    }
    g_pipe_table_inited = 1;
    if (!g_pipe_atexit_installed) {
        atexit(_hxp_atexit_cleanup);
        g_pipe_atexit_installed = 1;
    }
}

static int _hxp_max_handles(void) {
    /* soft cap = min(RLIMIT_NOFILE/4, HXP_MAX_HANDLES). */
    struct rlimit rl;
    int cap = HXP_MAX_HANDLES;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur > 0) {
        int soft = (int)(rl.rlim_cur / 4);
        if (soft > 0 && soft < cap) cap = soft;
    }
    if (cap < 1) cap = 1;
    return cap;
}

static int _hxp_alloc_slot(void) {
    int cap = _hxp_max_handles();
    for (int i = 0; i < cap; i++) {
        if (g_pipe_table[i].state == 0 && g_pipe_table[i].pid == 0) return i;
    }
    return -1;
}

static HexaChild* _hxp_slot(int h) {
    if (h < 0 || h >= HXP_MAX_HANDLES) return NULL;
    return &g_pipe_table[h];
}

static HexaChild* _hxp_live(int h) {
    HexaChild* c = _hxp_slot(h);
    if (!c) return NULL;
    if (c->state != 1) return NULL;
    return c;
}

/* ── spawn ──────────────────────────────────────────────────── */

HexaVal hexa_pipe_spawn(HexaVal cmd_val) {
    _hxp_init_table();
    if (!HX_IS_STR(cmd_val) || !HX_STR(cmd_val)) return hexa_int(-1);
    const char* cmd = HX_STR(cmd_val);

    int slot = _hxp_alloc_slot();
    if (slot < 0) return hexa_int(-1);

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0) return hexa_int(-1);
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        return hexa_int(-1);
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return hexa_int(-1);
    }
    if (pid == 0) {
        /* child */
        dup2(in_pipe[0],  0);
        dup2(out_pipe[1], 1);
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        _exit(127);
    }

    /* parent */
    close(in_pipe[0]);   /* parent doesn't read from stdin pipe */
    close(out_pipe[1]);  /* parent doesn't write to stdout pipe */

    /* Probe for early-exec failure: poll stdout pipe for EOF / waitpid
     * the child up to ~60ms total. If child died with non-zero exit
     * (typically 127 from /bin/sh: command not found), surface as -1.
     * This gives spawn() of a nonexistent command a clean failure path
     * (matches design "spawn returns -1 on error"). */
    int status = 0;
    pid_t r = 0;
    for (int probe = 0; probe < 6; probe++) {
        _hxp_sleep_ms(10);
        r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
    }
    if (r == pid) {
        /* child already exited — likely exec failure */
        int ec = WIFEXITED(status) ? WEXITSTATUS(status)
              : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        if (ec != 0) {
            /* non-zero exit during spawn-probe window → treat as failure.
             * Most commonly /bin/sh -c 'nonexistent_cmd' exits 127. */
            close(in_pipe[1]);
            close(out_pipe[0]);
            return hexa_int(-1);
        }
        /* child exited cleanly — keep the slot so caller can recv any
         * already-buffered output; record exit code. */
        HexaChild* c = &g_pipe_table[slot];
        c->pid = pid;
        c->stdin_fd = in_pipe[1];
        c->stdout_fd = out_pipe[0];
        c->state = 2; /* dead-pending-close */
        c->last_exit = ec;
        return hexa_int(slot);
    }

    HexaChild* c = &g_pipe_table[slot];
    c->pid = pid;
    c->stdin_fd = in_pipe[1];
    c->stdout_fd = out_pipe[0];
    c->state = 1;
    c->last_exit = 0;
    return hexa_int(slot);
}

/* ── send_line ──────────────────────────────────────────────── */

HexaVal hexa_pipe_send_line(HexaVal h_val, HexaVal payload_val) {
    _hxp_init_table();
    if (!HX_IS_INT(h_val)) return hexa_int(0);
    int h = (int)HX_INT(h_val);
    HexaChild* c = _hxp_live(h);
    if (!c) return hexa_int(0);
    if (c->stdin_fd < 0) return hexa_int(0);
    const char* p = HX_IS_STR(payload_val) && HX_STR(payload_val) ? HX_STR(payload_val) : "";
    size_t n = strlen(p);
    /* write payload then trailing '\n'. Tolerate short writes via loop. */
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(c->stdin_fd, p + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            c->state = 2;
            return hexa_int(0);
        }
        if (w == 0) { c->state = 2; return hexa_int(0); }
        off += (size_t)w;
    }
    const char nl = '\n';
    while (1) {
        ssize_t w = write(c->stdin_fd, &nl, 1);
        if (w == 1) break;
        if (w < 0) {
            if (errno == EINTR) continue;
            c->state = 2;
            return hexa_int(0);
        }
    }
    return hexa_int(1);
}

/* ── recv_line ──────────────────────────────────────────────── */

HexaVal hexa_pipe_recv_line(HexaVal h_val, HexaVal timeout_val) {
    _hxp_init_table();
    if (!HX_IS_INT(h_val)) return hexa_str("");
    int h = (int)HX_INT(h_val);
    HexaChild* c = _hxp_slot(h);
    if (!c) return hexa_str("");
    if (c->state == 0) return hexa_str("");
    if (c->stdout_fd < 0) return hexa_str("");

    long timeout_ms = HX_IS_INT(timeout_val) ? (long)HX_INT(timeout_val) : 5000;
    if (timeout_ms < 0) timeout_ms = 0;

    long t0 = _hxp_now_ms();
    size_t cap = 256, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return hexa_str("");

    while (1) {
        long elapsed = _hxp_now_ms() - t0;
        long remain = timeout_ms - elapsed;
        if (remain < 0) remain = 0;

        struct pollfd pfd;
        pfd.fd = c->stdout_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, (int)remain);
        if (pr == 0) {
            /* timeout — return whatever we accumulated (may be partial) */
            if (len == 0) { free(buf); return hexa_str(""); }
            buf[len] = '\0';
            return hexa_str_own(buf);
        }
        if (pr < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return hexa_str("");
        }
        /* data ready (or EOF on hangup) — read 1 byte to avoid over-read past '\n' */
        char ch;
        ssize_t rd = read(c->stdout_fd, &ch, 1);
        if (rd == 0) {
            /* EOF — child closed stdout, likely exited */
            int status = 0;
            pid_t wr = waitpid(c->pid, &status, WNOHANG);
            if (wr == c->pid) {
                c->last_exit = WIFEXITED(status) ? WEXITSTATUS(status)
                              : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
                c->state = 2;
            }
            if (len == 0) { free(buf); return hexa_str(""); }
            buf[len] = '\0';
            return hexa_str_own(buf);
        }
        if (rd < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return hexa_str("");
        }
        if (ch == '\n') {
            buf[len] = '\0';
            return hexa_str_own(buf);
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); return hexa_str(""); }
            buf = nb;
        }
        buf[len++] = ch;
    }
}

/* ── close ──────────────────────────────────────────────────── */

HexaVal hexa_pipe_close(HexaVal h_val) {
    _hxp_init_table();
    if (!HX_IS_INT(h_val)) return hexa_int(0);
    int h = (int)HX_INT(h_val);
    HexaChild* c = _hxp_slot(h);
    if (!c) return hexa_int(0);
    if (c->state == 0) return hexa_int(0);

    if (c->stdin_fd  >= 0) { close(c->stdin_fd);  c->stdin_fd  = -1; }
    if (c->state == 1 && c->pid > 0) {
        kill(c->pid, SIGTERM);
        int reaped = 0;
        for (int i = 0; i < 10; i++) {
            int status = 0;
            pid_t r = waitpid(c->pid, &status, WNOHANG);
            if (r == c->pid) {
                c->last_exit = WIFEXITED(status) ? WEXITSTATUS(status)
                              : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
                reaped = 1;
                break;
            }
            _hxp_sleep_ms(10);
        }
        if (!reaped) {
            kill(c->pid, SIGKILL);
            int status = 0;
            waitpid(c->pid, &status, 0);
            c->last_exit = WIFEXITED(status) ? WEXITSTATUS(status)
                          : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        }
    } else if (c->state == 2 && c->pid > 0) {
        /* child already dead, just reap if we haven't yet */
        int status = 0;
        waitpid(c->pid, &status, WNOHANG);
    }
    if (c->stdout_fd >= 0) { close(c->stdout_fd); c->stdout_fd = -1; }
    c->state = 0;
    c->pid = 0;
    return hexa_int(1);
}

/* ── alive ──────────────────────────────────────────────────── */

HexaVal hexa_pipe_alive(HexaVal h_val) {
    _hxp_init_table();
    if (!HX_IS_INT(h_val)) return hexa_int(0);
    int h = (int)HX_INT(h_val);
    HexaChild* c = _hxp_slot(h);
    if (!c) return hexa_int(0);
    if (c->state != 1) return hexa_int(0);
    if (c->pid <= 0) return hexa_int(0);
    /* Reap if child has exited (non-blocking). */
    int status = 0;
    pid_t r = waitpid(c->pid, &status, WNOHANG);
    if (r == c->pid) {
        c->last_exit = WIFEXITED(status) ? WEXITSTATUS(status)
                      : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        c->state = 2;
        return hexa_int(0);
    }
    /* Probe with kill(pid, 0) — distinguishes alive vs unreachable. */
    if (kill(c->pid, 0) != 0) {
        c->state = 2;
        return hexa_int(0);
    }
    return hexa_int(1);
}

/* ── TAG_FN shim globals ────────────────────────────────────── */

HexaVal hx_pipe_spawn;
HexaVal hx_pipe_send_line;
HexaVal hx_pipe_recv_line;
HexaVal hx_pipe_close;
HexaVal hx_pipe_alive;

static void _hexa_init_persistent_pipe_fn_shims(void) {
    hx_pipe_spawn     = hexa_fn_new((void*)hexa_pipe_spawn,     1);
    hx_pipe_send_line = hexa_fn_new((void*)hexa_pipe_send_line, 2);
    hx_pipe_recv_line = hexa_fn_new((void*)hexa_pipe_recv_line, 2);
    hx_pipe_close     = hexa_fn_new((void*)hexa_pipe_close,     1);
    hx_pipe_alive     = hexa_fn_new((void*)hexa_pipe_alive,     1);
}
