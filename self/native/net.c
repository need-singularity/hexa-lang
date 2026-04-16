/* self/native/net.c — std::net POSIX TCP socket builtins
 *
 * Ported from src/std_net.rs (hexa-lang@ef92fc6) — pure C, no Rust.
 * Included from self/runtime.c via `#include "native/net.c"`.
 *
 * Symbols exported (6 primitives):
 *   hexa_net_listen(addr)        : string "host:port" → TAG_INT fd / -errno
 *   hexa_net_accept(listen_fd)   : TAG_INT listen_fd → TAG_INT client_fd
 *   hexa_net_close(fd)           : TAG_INT fd        → TAG_INT 0 / -errno
 *   hexa_net_connect(addr)       : string "host:port" → TAG_INT fd / -errno
 *   hexa_net_read(fd)            : TAG_INT fd        → TAG_STR data (len=0 on err)
 *   hexa_net_write(fd, data)     : TAG_INT fd, string → TAG_INT bytes / -errno
 *
 * http_get / http_serve are composed in self/std_net.hexa on top of these
 * primitives — no C-side implementation needed.
 *
 * Error model: negative TAG_INT carries the raw errno. Matches the
 * C-side convention used by hxcuda/hxblas shims. The .hexa wrappers
 * in self/std_net.hexa convert errnos back to structured errors.
 *
 * Historical note: listen/accept/close were originally inlined into
 * self/runtime.c (stage0 resurrection block, 2026-04-16). Extracted
 * here alongside connect/read/write so the net subsystem lives in
 * a single C file — analogous to tensor_kernels.c for the hot kernel
 * path.
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

/* Parse "host:port" into sockaddr_in. Accepts:
 *   - dotted-quad (127.0.0.1), "localhost" → INADDR_LOOPBACK,
 *   - "0.0.0.0" or "*"                      → INADDR_ANY,
 *   - port range 1..65535.
 * DNS + IPv6 intentionally deferred (stage0 narrow surface). */
static int _hexa_net_parse_addr(const char* addr, struct sockaddr_in* sa_out) {
    if (!addr || !*addr || !sa_out) return -EINVAL;
    const char* colon = strrchr(addr, ':');
    if (!colon || colon == addr) return -EINVAL;
    char host[256];
    size_t host_len = (size_t)(colon - addr);
    if (host_len >= sizeof(host)) return -EINVAL;
    memcpy(host, addr, host_len);
    host[host_len] = '\0';
    /* Port 0 is a legitimate bind/connect target on POSIX — it means
     * "let the kernel pick an ephemeral port" (see `man 2 bind`). Rejecting
     * it broke `net_listen("127.0.0.1:0")` which tests rely on for
     * collision-free smoke runs. Only reject negatives and out-of-range. */
    int port = atoi(colon + 1);
    if (port < 0 || port > 65535) return -EINVAL;
    memset(sa_out, 0, sizeof(*sa_out));
    sa_out->sin_family = AF_INET;
    sa_out->sin_port = htons((uint16_t)port);
    if (strcmp(host, "localhost") == 0) {
        sa_out->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else if (strcmp(host, "0.0.0.0") == 0 || strcmp(host, "*") == 0) {
        sa_out->sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host, &sa_out->sin_addr) != 1) {
        return -EINVAL;
    }
    return 0;
}

HexaVal hexa_net_listen(HexaVal addr_val) {
    const char* addr = hexa_to_cstring(addr_val);
    struct sockaddr_in sa;
    int parse_rc = _hexa_net_parse_addr(addr, &sa);
    if (parse_rc < 0) return hexa_int(parse_rc);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return hexa_int(-errno);
    int one = 1;
    /* SO_REUSEADDR mirrors the Rust TcpListener default. Without it,
     * repeat `hexa run` within TIME_WAIT collides. */
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        int e = errno; close(fd); return hexa_int(-e);
    }
    if (listen(fd, 64) < 0) {
        int e = errno; close(fd); return hexa_int(-e);
    }
    return hexa_int(fd);
}

HexaVal hexa_net_accept(HexaVal listen_val) {
    int64_t listen_fd = hexa_as_num(listen_val);
    if (listen_fd < 0) return hexa_int(-EINVAL);
    int client = accept((int)listen_fd, NULL, NULL);
    if (client < 0) return hexa_int(-errno);
    return hexa_int(client);
}

HexaVal hexa_net_close(HexaVal fd_val) {
    int64_t fd = hexa_as_num(fd_val);
    if (fd < 0) return hexa_int(-EINVAL);
    if (close((int)fd) < 0) return hexa_int(-errno);
    return hexa_int(0);
}

HexaVal hexa_net_connect(HexaVal addr_val) {
    const char* addr = hexa_to_cstring(addr_val);
    struct sockaddr_in sa;
    int parse_rc = _hexa_net_parse_addr(addr, &sa);
    if (parse_rc < 0) return hexa_int(parse_rc);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return hexa_int(-errno);
    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        int e = errno; close(fd); return hexa_int(-e);
    }
    return hexa_int(fd);
}

/* Single-shot read up to 4096 bytes. The caller is expected to loop
 * until they've got what they want (HTTP parsing in .hexa happens on
 * the accumulated string). Mirrors the Rust variant which also did a
 * single read (TcpStream::read with a 4KiB buffer). */
HexaVal hexa_net_read(HexaVal fd_val) {
    int64_t fd = hexa_as_num(fd_val);
    if (fd < 0) return hexa_str("");
    char buf[4096];
    ssize_t n = recv((int)fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return hexa_str("");
    buf[n] = '\0';
    /* hexa_str dup + interns the string — safe to hand back the stack buf. */
    return hexa_str(buf);
}

HexaVal hexa_net_write(HexaVal fd_val, HexaVal data_val) {
    int64_t fd = hexa_as_num(fd_val);
    if (fd < 0) return hexa_int(-EINVAL);
    const char* data = hexa_to_cstring(data_val);
    if (!data) return hexa_int(-EINVAL);
    size_t total = strlen(data);
    size_t sent = 0;
    while (sent < total) {
        ssize_t n = send((int)fd, data + sent, total - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return hexa_int(-errno);
        }
        if (n == 0) break;
        sent += (size_t)n;
    }
    return hexa_int((int64_t)sent);
}

/* ── TAG_FN shim globals (bootstrap bridge, 2026-04-16) ──────────────────
 * The current self/native/hexa_v2 transpiler only recognises net_listen /
 * net_accept / net_close as direct-lowered builtins. Fresh names like
 * net_connect / net_read / net_write are emitted as `hexa_call1(name, …)`
 * — i.e. the transpiler treats them as free-function identifiers backed by
 * a HexaVal fn pointer. Providing TAG_FN shims keeps the interpreter path
 * working until the next codegen_c2 → hexa_cc → hexa_v2 bootstrap cycle
 * teaches the transpiler the direct-lowering. After that, these shims are
 * harmless — codegen will emit `hexa_net_connect(...)` directly and the
 * globals simply remain unused. */
HexaVal net_listen;
HexaVal net_accept;
HexaVal net_close;
HexaVal net_connect;
HexaVal net_read;
HexaVal net_write;

static void _hexa_init_net_fn_shims(void) {
    net_listen  = hexa_fn_new((void*)hexa_net_listen,  1);
    net_accept  = hexa_fn_new((void*)hexa_net_accept,  1);
    net_close   = hexa_fn_new((void*)hexa_net_close,   1);
    net_connect = hexa_fn_new((void*)hexa_net_connect, 1);
    net_read    = hexa_fn_new((void*)hexa_net_read,    1);
    net_write   = hexa_fn_new((void*)hexa_net_write,   2);
}
