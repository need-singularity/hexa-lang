/* hexa_daemon_serve — Option A daemon listener (A1 + A2 phases).
 *
 * Plan: docs/hexa_daemon_option_a_plan_2026_05_06.ai.md
 * Predecessor MVP: scripts/hexa_daemon.sh + scripts/hexa_daemon_handler.sh
 *                  (commit b5560a74 — bash + socat).
 *
 * What this ships
 * ───────────────
 * A1. Native AF_UNIX listener that hosts the same on-the-wire protocol the
 *     bash MVP exposed:
 *         request : <cwd>\x1f<arg1>\x1f<arg2>\x1f...\n
 *         response: <hexa.real stdout/stderr interleaved>
 *                   __HEXA_DAEMON_RC__=<exit_code>\n
 *     Replaces `socat UNIX-LISTEN ... fork EXEC:hexa-daemon-handler` with a
 *     single in-process loop. Per-request: fork() + execv(hexa.real). The
 *     bash shim + the socat fork-exec layer are gone — that's the visible
 *     wall-time delta vs the MVP.
 *
 * A2. Module mtime / size LRU cache (this binary side). Each request that
 *     looks like `["run", file, …]` triggers a stat() + cache update. The
 *     cache itself does not yet skip parse — that requires hexa.real-side
 *     plumbing, deferred. What it does today:
 *       - tracks (canonical_path → mtime + size + hits/misses)
 *       - evicts LRU above HEXA_DAEMON_CACHE_ENTRIES (default 256)
 *       - feeds the __STATS__ endpoint (A4 hook, exposed early)
 *     This is the scaffolding the parse-skip lands on next cycle.
 *
 * A4 (early). __STATS__ request returns a single JSON line and closes.
 *
 * A5. SIGTERM / SIGINT handler stops accept and shuts the listener.
 *
 * NOT SHIPPED (deferred to follow-up cycles)
 * ──────────────────────────────────────────
 * A3 — worker pool. Today the loop is one-request-at-a-time. The cache is
 *      single-threaded. A3 lands when in-process parse-skip lands; until
 *      then concurrent forks would not be a net win.
 *
 * Build
 * ─────
 *   tool/build_hexa_daemon_serve.sh  (clang wrapper)
 *   Outputs:                          bin/hexa-daemon-serve
 *
 * Runtime
 * ───────
 *   bin/hexa-daemon-serve --sock /tmp/hexa-daemon-A.sock
 *
 * Env vars
 * ────────
 *   HEXA_REAL_BIN              path to hexa.real (default: $HOME/.hx/packages/hexa/hexa.real)
 *   HEXA_DAEMON_CACHE_ENTRIES  LRU size cap (default 256)
 *   HEXA_DAEMON_LOG            log path (default: stderr)
 */

#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef HEXA_DAEMON_DEFAULT_CACHE_ENTRIES
#define HEXA_DAEMON_DEFAULT_CACHE_ENTRIES 256
#endif

#ifndef HEXA_DAEMON_MAX_REQ_BYTES
#define HEXA_DAEMON_MAX_REQ_BYTES (256 * 1024)
#endif

#ifndef HEXA_DAEMON_MAX_ARGS
#define HEXA_DAEMON_MAX_ARGS 256
#endif

/* ── globals ───────────────────────────────────────────────────────────── */

static volatile sig_atomic_t g_shutdown = 0;
static int   g_listen_fd = -1;
static char  g_sock_path[1024] = {0};
static char  g_hexa_real[1024] = {0};
static char  g_log_path[1024]  = {0};
static FILE* g_log = NULL;

/* ── A2 cache ──────────────────────────────────────────────────────────── */

typedef struct cache_entry {
    char*    path;          /* canonical absolute path (heap) */
    time_t   mtime_sec;
    long     mtime_nsec;
    off_t    size;
    uint64_t hits;
    uint64_t misses;
    struct cache_entry* prev;   /* LRU doubly-linked */
    struct cache_entry* next;
} cache_entry;

static cache_entry* g_lru_head = NULL;
static cache_entry* g_lru_tail = NULL;
static size_t       g_cache_count = 0;
static size_t       g_cache_cap   = HEXA_DAEMON_DEFAULT_CACHE_ENTRIES;
static uint64_t     g_total_hits   = 0;
static uint64_t     g_total_misses = 0;
static uint64_t     g_total_reqs   = 0;

/* ── log ───────────────────────────────────────────────────────────────── */

static void dlog(const char* fmt, ...) {
    if (!g_log) return;
    char ts[32];
    time_t now = time(NULL);
    struct tm tm_buf;
    if (localtime_r(&now, &tm_buf)) {
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    } else {
        snprintf(ts, sizeof(ts), "%lld", (long long)now);
    }
    fprintf(g_log, "[%s] ", ts);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* ── cache impl ────────────────────────────────────────────────────────── */

static cache_entry* cache_find(const char* path) {
    /* simple linear scan; LRU sizes are small (default 256). hash table
     * is straightforward to add later if profiling shows it matters. */
    for (cache_entry* e = g_lru_head; e; e = e->next) {
        if (strcmp(e->path, path) == 0) return e;
    }
    return NULL;
}

static void lru_unlink(cache_entry* e) {
    if (e->prev) e->prev->next = e->next; else g_lru_head = e->next;
    if (e->next) e->next->prev = e->prev; else g_lru_tail = e->prev;
    e->prev = e->next = NULL;
}

static void lru_push_front(cache_entry* e) {
    e->prev = NULL;
    e->next = g_lru_head;
    if (g_lru_head) g_lru_head->prev = e;
    g_lru_head = e;
    if (!g_lru_tail) g_lru_tail = e;
}

static void lru_evict_one(void) {
    if (!g_lru_tail) return;
    cache_entry* e = g_lru_tail;
    lru_unlink(e);
    free(e->path);
    free(e);
    g_cache_count--;
}

static void cache_touch(const char* path, const struct stat* st) {
    if (!path || !*path) return;
    cache_entry* e = cache_find(path);
    if (e) {
        if (e->mtime_sec == st->st_mtime && e->size == st->st_size) {
            e->hits++;
            g_total_hits++;
        } else {
            e->mtime_sec = st->st_mtime;
            e->size = st->st_size;
            e->misses++;
            g_total_misses++;
        }
        lru_unlink(e);
        lru_push_front(e);
        return;
    }
    /* insert new */
    while (g_cache_count >= g_cache_cap) lru_evict_one();
    e = (cache_entry*)calloc(1, sizeof(*e));
    if (!e) return;
    e->path = strdup(path);
    if (!e->path) { free(e); return; }
    e->mtime_sec = st->st_mtime;
    e->size = st->st_size;
    e->misses = 1;
    g_total_misses++;
    lru_push_front(e);
    g_cache_count++;
}

/* ── request parsing ───────────────────────────────────────────────────── */

/* Read one request line up to '\n' or HEXA_DAEMON_MAX_REQ_BYTES.
 * Returns malloc'd null-terminated string (without the trailing '\n'),
 * or NULL on EOF/error. */
static char* read_request(int fd) {
    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;
    while (len < HEXA_DAEMON_MAX_REQ_BYTES) {
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            if (ncap > HEXA_DAEMON_MAX_REQ_BYTES + 1) ncap = HEXA_DAEMON_MAX_REQ_BYTES + 1;
            char* nb = (char*)realloc(buf, ncap);
            if (!nb) { free(buf); return NULL; }
            buf = nb; cap = ncap;
        }
        ssize_t n = read(fd, buf + len, 1);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return NULL;
        }
        if (buf[len] == '\n') { buf[len] = '\0'; return buf; }
        len++;
    }
    if (len == 0) { free(buf); return NULL; }
    buf[len] = '\0';
    return buf;
}

/* Split request on US (\x1f). First field = cwd, rest = argv passed to
 * hexa.real. Returns argc (cwd is OUT, argv array is OUT, ownership is
 * shared with `req` buffer — do not free argv elements separately). */
static int parse_request(char* req, char** out_cwd, char** out_argv, int max_argv) {
    int argc = 0;
    char* p = req;
    *out_cwd = p;
    while (*p && *p != '\x1f') p++;
    if (*p == '\x1f') { *p = '\0'; p++; }
    while (*p && argc < max_argv) {
        out_argv[argc++] = p;
        while (*p && *p != '\x1f') p++;
        if (*p == '\x1f') { *p = '\0'; p++; }
    }
    return argc;
}

/* ── stats endpoint (A4) ───────────────────────────────────────────────── */

static void emit_stats(int fd) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "{\"schema\":\"hexa-lang/daemon_stats/1\","
        "\"cached\":%zu,\"cap\":%zu,\"hits\":%llu,\"misses\":%llu,\"requests\":%llu}\n"
        "__HEXA_DAEMON_RC__=0\n",
        g_cache_count, g_cache_cap,
        (unsigned long long)g_total_hits,
        (unsigned long long)g_total_misses,
        (unsigned long long)g_total_reqs);
    if (n > 0) {
        ssize_t w = write(fd, buf, (size_t)n);
        (void)w;
    }
}

/* ── exec hexa.real, streaming output back through the client fd ───────── */

static int run_hexa_real(const char* cwd, char** child_argv, int child_argc, int client_fd) {
    /* Build argv vector: argv[0]=hexa.real path, argv[1..]=child_argv, then NULL. */
    char* argv[HEXA_DAEMON_MAX_ARGS + 2];
    int ai = 0;
    argv[ai++] = g_hexa_real;
    for (int i = 0; i < child_argc && ai < HEXA_DAEMON_MAX_ARGS + 1; i++) {
        argv[ai++] = child_argv[i];
    }
    argv[ai] = NULL;

    /* Pipe to capture child stdout+stderr together. */
    int pipefd[2];
    if (pipe(pipefd) != 0) return -errno;

    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close(pipefd[0]); close(pipefd[1]);
        return -e;
    }
    if (pid == 0) {
        /* child */
        close(pipefd[0]);
        if (cwd && *cwd) {
            if (chdir(cwd) != 0) { /* ignore — propagate via stderr */ }
        }
        /* dup pipe write-end to stdout AND stderr (interleaved is the same
         * ordering the bash MVP gave callers). */
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        /* mirror legacy shim env from scripts/hexa_daemon_handler.sh */
        setenv("HEXA_SHIM_NO_DARWIN_LANDING", "1", 1);
        execv(g_hexa_real, argv);
        /* execv failed */
        const char* msg = "hexa_daemon_serve: execv failed: ";
        write(STDERR_FILENO, msg, strlen(msg));
        const char* err = strerror(errno);
        write(STDERR_FILENO, err, strlen(err));
        write(STDERR_FILENO, "\n", 1);
        _exit(127);
    }

    close(pipefd[1]);
    /* Forward child output back to the client. */
    char buf[8192];
    for (;;) {
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(client_fd, buf + off, (size_t)(n - off));
                if (w <= 0) {
                    if (w < 0 && errno == EINTR) continue;
                    /* client gone — drain into /dev/null by continuing read */
                    off = n;
                    break;
                }
                off += w;
            }
        } else if (n == 0) {
            break;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return -errno;
    }
    int rc = WIFEXITED(status) ? WEXITSTATUS(status)
           : WIFSIGNALED(status) ? (128 + WTERMSIG(status))
           : 1;
    return rc;
}

/* ── per-connection handler ────────────────────────────────────────────── */

static void handle_connection(int client_fd) {
    char* req = read_request(client_fd);
    if (!req) { close(client_fd); return; }
    g_total_reqs++;

    /* Stats endpoint short-circuit. */
    if (strcmp(req, "__STATS__") == 0) {
        emit_stats(client_fd);
        free(req);
        close(client_fd);
        return;
    }

    char* cwd = NULL;
    char* child_argv[HEXA_DAEMON_MAX_ARGS];
    int child_argc = parse_request(req, &cwd, child_argv, HEXA_DAEMON_MAX_ARGS);

    /* A2: opportunistic cache update for `run <file>` shape.
     * Resolve relative paths against the request cwd (the daemon's own
     * cwd is irrelevant — each request carries its own cwd in field 0). */
    if (child_argc >= 2 && strcmp(child_argv[0], "run") == 0) {
        const char* file = child_argv[1];
        /* skip leading `--` flags such as `--no-sentinel`, `--vm`. */
        for (int fi = 1; fi < child_argc && file && file[0] == '-' && file[1] == '-'; fi++) {
            if (fi + 1 < child_argc) file = child_argv[fi + 1];
            else file = NULL;
        }
        if (file && *file) {
            char abs_path[4096];
            if (file[0] == '/') {
                snprintf(abs_path, sizeof(abs_path), "%s", file);
            } else if (cwd && *cwd) {
                snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd, file);
            } else {
                snprintf(abs_path, sizeof(abs_path), "%s", file);
            }
            struct stat st;
            if (stat(abs_path, &st) == 0) {
                cache_touch(abs_path, &st);
            }
        }
    }

    int rc = run_hexa_real(cwd, child_argv, child_argc, client_fd);
    char rcline[64];
    int n;
    if (rc < 0) {
        /* internal error — surface via sentinel as 127. */
        dlog("run_hexa_real internal error: %d (%s)", -rc, strerror(-rc));
        n = snprintf(rcline, sizeof(rcline), "__HEXA_DAEMON_RC__=127\n");
    } else {
        n = snprintf(rcline, sizeof(rcline), "__HEXA_DAEMON_RC__=%d\n", rc);
    }
    if (n > 0) {
        ssize_t w = write(client_fd, rcline, (size_t)n);
        (void)w;
    }

    free(req);
    close(client_fd);
}

/* ── signal + listen ───────────────────────────────────────────────────── */

static void on_term(int sig) {
    (void)sig;
    g_shutdown = 1;
    if (g_listen_fd >= 0) {
        /* shutdown(2) wakes accept(2) on most kernels; close as backstop. */
        shutdown(g_listen_fd, SHUT_RDWR);
    }
}

/* No SIGCHLD auto-reaper: handle_connection waitpid()s explicitly and the
 * loop is single-threaded, so a default-disposition SIGCHLD avoids racing
 * the explicit wait (would otherwise return ECHILD). */

static int listen_on(const char* path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -errno;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(sa.sun_path)) { close(fd); return -ENAMETOOLONG; }
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
    unlink(path); /* clean stale socket */
    if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) != 0) {
        int e = errno; close(fd); return -e;
    }
    if (listen(fd, 64) != 0) {
        int e = errno; close(fd); unlink(path); return -e;
    }
    /* tighten perms — daemon socket is per-user only. */
    chmod(path, 0600);
    return fd;
}

static void usage(FILE* f) {
    fprintf(f,
        "hexa-daemon-serve — Option A native daemon listener\n\n"
        "Usage: hexa-daemon-serve --sock <path> [--hexa-real <path>] [--log <path>]\n"
        "                         [--cache-entries N]\n\n"
        "Env:\n"
        "  HEXA_REAL_BIN              hexa.real path (default ~/.hx/packages/hexa/hexa.real)\n"
        "  HEXA_DAEMON_CACHE_ENTRIES  LRU size (default %d)\n"
        "  HEXA_DAEMON_LOG            log path (default stderr)\n",
        HEXA_DAEMON_DEFAULT_CACHE_ENTRIES);
}

int main(int argc, char** argv) {
    /* ── arg parse ── */
    const char* arg_sock = NULL;
    const char* arg_real = NULL;
    const char* arg_log  = NULL;
    long arg_cache = -1;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "--sock") == 0 && i + 1 < argc) { arg_sock = argv[++i]; }
        else if (strcmp(a, "--hexa-real") == 0 && i + 1 < argc) { arg_real = argv[++i]; }
        else if (strcmp(a, "--log") == 0 && i + 1 < argc) { arg_log = argv[++i]; }
        else if (strcmp(a, "--cache-entries") == 0 && i + 1 < argc) { arg_cache = atol(argv[++i]); }
        else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { usage(stdout); return 0; }
        else { usage(stderr); return 2; }
    }
    if (!arg_sock) { usage(stderr); return 2; }

    /* ── env defaults ── */
    if (!arg_real) arg_real = getenv("HEXA_REAL_BIN");
    if (!arg_real || !*arg_real) {
        const char* home = getenv("HOME");
        if (home) {
            snprintf(g_hexa_real, sizeof(g_hexa_real),
                     "%s/.hx/packages/hexa/hexa.real", home);
        } else {
            snprintf(g_hexa_real, sizeof(g_hexa_real),
                     "/usr/local/lib/hexa/hexa.real");
        }
    } else {
        strncpy(g_hexa_real, arg_real, sizeof(g_hexa_real) - 1);
    }
    if (access(g_hexa_real, X_OK) != 0) {
        fprintf(stderr, "hexa-daemon-serve: hexa.real not executable: %s\n", g_hexa_real);
        return 2;
    }

    if (!arg_log) arg_log = getenv("HEXA_DAEMON_LOG");
    if (arg_log && *arg_log) {
        strncpy(g_log_path, arg_log, sizeof(g_log_path) - 1);
        g_log = fopen(g_log_path, "a");
        if (!g_log) g_log = stderr;
    } else {
        g_log = stderr;
    }

    if (arg_cache < 0) {
        const char* env_c = getenv("HEXA_DAEMON_CACHE_ENTRIES");
        if (env_c && *env_c) arg_cache = atol(env_c);
    }
    if (arg_cache > 0) g_cache_cap = (size_t)arg_cache;

    strncpy(g_sock_path, arg_sock, sizeof(g_sock_path) - 1);

    /* ── signals ── */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    /* SIGPIPE is silently ignored — client may close mid-write. */
    signal(SIGPIPE, SIG_IGN);

    /* ── listen ── */
    int rc = listen_on(g_sock_path);
    if (rc < 0) {
        fprintf(stderr, "hexa-daemon-serve: listen on %s failed: %s\n",
                g_sock_path, strerror(-rc));
        return 1;
    }
    g_listen_fd = rc;

    dlog("hexa-daemon-serve up: sock=%s hexa_real=%s cache_cap=%zu",
         g_sock_path, g_hexa_real, g_cache_cap);

    /* ── accept loop ── */
    while (!g_shutdown) {
        int cfd = accept(g_listen_fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (g_shutdown) break;
            dlog("accept failed: %s", strerror(errno));
            continue;
        }
        handle_connection(cfd);
    }

    /* ── shutdown ── */
    dlog("hexa-daemon-serve shutdown: requests=%llu hits=%llu misses=%llu cached=%zu",
         (unsigned long long)g_total_reqs,
         (unsigned long long)g_total_hits,
         (unsigned long long)g_total_misses,
         g_cache_count);
    close(g_listen_fd);
    unlink(g_sock_path);

    /* free cache (cosmetic — process is exiting) */
    while (g_lru_head) {
        cache_entry* e = g_lru_head;
        g_lru_head = e->next;
        free(e->path);
        free(e);
    }

    if (g_log && g_log != stderr) fclose(g_log);
    return 0;
}
