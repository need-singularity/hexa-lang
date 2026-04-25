/* self/native/exec_argv_sha256.c
 *
 * hxa-20260424-005 items #1 + #7 (2026-04-24).
 *
 * Two pure-libc/Zig-stdlib-equivalent builtins:
 *
 *   hexa_exec_argv(argv_arr)             : HexaVal array<str> → HexaVal str
 *   hexa_exec_argv_with_status(argv_arr) : HexaVal array<str> → HexaVal [str,int]
 *       Direct fork/execvp without `/bin/sh -c`. No shell metachar expansion.
 *       argv_arr[0] is the program (searched via PATH if not absolute).
 *       argv_arr[1..] are arguments passed verbatim to the child.
 *
 *   hexa_sha256(s)                       : HexaVal str → HexaVal str (lowercase hex)
 *   hexa_sha256_file(path)               : HexaVal str → HexaVal str (lowercase hex of file bytes)
 *       FIPS 180-4 reference implementation. Returns 64-char lowercase hex.
 *       sha256_file returns "" on I/O error.
 *
 * Included from self/runtime.c via #include "native/exec_argv_sha256.c"
 * analogous to net.c / signal_flock.c. Pure libc; portable macOS/Linux.
 *
 * Rationale: exec(cmd)/exec_with_status(cmd) both route through
 * `/bin/sh -c`. Any command containing shell metacharacters (quotes,
 * `$`, backticks, heredocs) must be manually escaped. Three nexus tools
 * (tool/proposal_archive.hexa, drill_cycle_trigger.hexa,
 * dormancy_wake_tick.hexa) work around this by writing temporary script
 * files. exec_argv bypasses the shell entirely, removing the escape
 * burden on the call site.
 *
 * sha256 previously required shelling out to `shasum -a 256` or
 * `openssl dgst -sha256`, doubling the latency of anything that hashes
 * (inventory entries, manifests, cross-repo floor checks).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ══════════════════════════════════════════════════════════════════
 * exec_argv — direct fork/execvp, no shell
 * ══════════════════════════════════════════════════════════════════ */

/* Internal: materialize argv_val into a heap-allocated NULL-terminated
 * char** suitable for execvp. Returns NULL on allocation failure or if
 * the array is empty. Caller must free both the array and its cells. */
static char** _hxa_materialize_argv(HexaVal argv_val, int* out_argc) {
    *out_argc = 0;
    if (!HX_IS_ARRAY(argv_val)) return NULL;
    int64_t n = (int64_t)HX_ARR_LEN(argv_val);
    if (n <= 0) return NULL;
    char** argv = (char**)calloc((size_t)n + 1, sizeof(char*));
    if (!argv) return NULL;
    for (int64_t i = 0; i < n; i++) {
        HexaVal el = hexa_array_get(argv_val, i);
        const char* s = HX_IS_STR(el) ? HX_STR(el) : "";
        if (!s) s = "";
        argv[i] = strdup(s);
        if (!argv[i]) {
            for (int64_t j = 0; j < i; j++) free(argv[j]);
            free(argv);
            return NULL;
        }
    }
    argv[n] = NULL;
    *out_argc = (int)n;
    return argv;
}

static void _hxa_free_argv(char** argv, int argc) {
    if (!argv) return;
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
}

/* Shared core: fork, execvp, capture stdout, optionally return exit code. */
static HexaVal _hxa_exec_argv_core(HexaVal argv_val, int want_status) {
    int argc = 0;
    char** argv = _hxa_materialize_argv(argv_val, &argc);
    if (!argv || argc == 0) {
        if (want_status) {
            HexaVal arr = hexa_array_new();
            arr = hexa_array_push(arr, hexa_str(""));
            arr = hexa_array_push(arr, hexa_int(127));
            return arr;
        }
        return hexa_str("");
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        _hxa_free_argv(argv, argc);
        if (want_status) {
            HexaVal arr = hexa_array_new();
            arr = hexa_array_push(arr, hexa_str(""));
            arr = hexa_array_push(arr, hexa_int(127));
            return arr;
        }
        return hexa_str("");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        _hxa_free_argv(argv, argc);
        if (want_status) {
            HexaVal arr = hexa_array_new();
            arr = hexa_array_push(arr, hexa_str(""));
            arr = hexa_array_push(arr, hexa_int(127));
            return arr;
        }
        return hexa_str("");
    }
    if (pid == 0) {
        /* child: stdout+stderr → pipe write end. Matches the
         * `exec(cmd)` behavior (popen merges stderr by default when
         * the caller appends `2>&1`; we default-merge here so the
         * migration from exec→exec_argv is stdout-compatible). */
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        /* execvp failed */
        fprintf(stderr, "exec_argv: execvp %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    /* parent */
    close(pipefd[1]);
    _hxa_free_argv(argv, argc);

    size_t cap = 4096, total = 0;
    char* buf = (char*)malloc(cap);
    if (buf) buf[0] = '\0';
    char tmp[4096];
    for (;;) {
        ssize_t n = read(pipefd[0], tmp, sizeof(tmp));
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        if (buf) {
            while (total + (size_t)n + 1 > cap) {
                cap *= 2;
                char* nb = (char*)realloc(buf, cap);
                if (!nb) { free(buf); buf = NULL; break; }
                buf = nb;
            }
            if (buf) { memcpy(buf + total, tmp, (size_t)n); total += (size_t)n; buf[total] = '\0'; }
        }
    }
    close(pipefd[0]);

    int status = 0;
    (void)waitpid(pid, &status, 0);
    int exit_code;
    if (WIFEXITED(status))        exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);
    else                          exit_code = -1;

    if (want_status) {
        HexaVal arr = hexa_array_new();
        arr = hexa_array_push(arr, buf ? hexa_str_own(buf) : hexa_str(""));
        arr = hexa_array_push(arr, hexa_int(exit_code));
        return arr;
    }
    return buf ? hexa_str_own(buf) : hexa_str("");
}

HexaVal hexa_exec_argv(HexaVal argv_val) {
    return _hxa_exec_argv_core(argv_val, 0);
}

HexaVal hexa_exec_argv_with_status(HexaVal argv_val) {
    return _hxa_exec_argv_core(argv_val, 1);
}

/* ══════════════════════════════════════════════════════════════════
 * sha256 — FIPS 180-4 reference implementation
 *
 * Pure-C; independent of OpenSSL / CommonCrypto / libsodium so the
 * hexa_interp binary stays dependency-free across macOS/Linux.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t h[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    uint32_t buflen;
} _hxa_sha256_ctx;

static const uint32_t _hxa_sha256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define _HXA_ROTR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void _hxa_sha256_init(_hxa_sha256_ctx* c) {
    c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372; c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f; c->h[5]=0x9b05688c; c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
    c->bitlen = 0;
    c->buflen = 0;
}

static void _hxa_sha256_transform(_hxa_sha256_ctx* c, const uint8_t* blk) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)blk[i*4] << 24) | ((uint32_t)blk[i*4+1] << 16)
             | ((uint32_t)blk[i*4+2] << 8) | (uint32_t)blk[i*4+3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = _HXA_ROTR(w[i-15],7) ^ _HXA_ROTR(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = _HXA_ROTR(w[i-2],17) ^ _HXA_ROTR(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c->h[0], b=c->h[1], d=c->h[2], e=c->h[3];
    uint32_t f=c->h[4], g=c->h[5], hh=c->h[6], ii=c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = _HXA_ROTR(f,6) ^ _HXA_ROTR(f,11) ^ _HXA_ROTR(f,25);
        uint32_t ch = (f & g) ^ (~f & hh);
        uint32_t t1 = ii + S1 + ch + _hxa_sha256_K[i] + w[i];
        uint32_t S0 = _HXA_ROTR(a,2) ^ _HXA_ROTR(a,13) ^ _HXA_ROTR(a,22);
        uint32_t mj = (a & b) ^ (a & d) ^ (b & d);
        uint32_t t2 = S0 + mj;
        ii = hh; hh = g; g = f; f = e + t1;
        e = d; d = b; b = a; a = t1 + t2;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=d; c->h[3]+=e;
    c->h[4]+=f; c->h[5]+=g; c->h[6]+=hh; c->h[7]+=ii;
}

static void _hxa_sha256_update(_hxa_sha256_ctx* c, const uint8_t* data, size_t len) {
    c->bitlen += (uint64_t)len * 8;
    while (len > 0) {
        size_t want = 64 - c->buflen;
        size_t take = len < want ? len : want;
        memcpy(c->buf + c->buflen, data, take);
        c->buflen += (uint32_t)take;
        data += take;
        len  -= take;
        if (c->buflen == 64) {
            _hxa_sha256_transform(c, c->buf);
            c->buflen = 0;
        }
    }
}

static void _hxa_sha256_final(_hxa_sha256_ctx* c, uint8_t out[32]) {
    uint64_t bitlen = c->bitlen;
    c->buf[c->buflen++] = 0x80;
    if (c->buflen > 56) {
        while (c->buflen < 64) c->buf[c->buflen++] = 0;
        _hxa_sha256_transform(c, c->buf);
        c->buflen = 0;
    }
    while (c->buflen < 56) c->buf[c->buflen++] = 0;
    for (int i = 7; i >= 0; i--) c->buf[c->buflen++] = (uint8_t)((bitlen >> (i*8)) & 0xff);
    _hxa_sha256_transform(c, c->buf);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)((c->h[i] >> 24) & 0xff);
        out[i*4+1] = (uint8_t)((c->h[i] >> 16) & 0xff);
        out[i*4+2] = (uint8_t)((c->h[i] >>  8) & 0xff);
        out[i*4+3] = (uint8_t)( c->h[i]        & 0xff);
    }
}

static char* _hxa_hex_encode(const uint8_t* bytes, size_t n) {
    static const char* hex = "0123456789abcdef";
    char* out = (char*)malloc(n * 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        out[i*2]   = hex[(bytes[i] >> 4) & 0xf];
        out[i*2+1] = hex[ bytes[i]       & 0xf];
    }
    out[n*2] = '\0';
    return out;
}

HexaVal hexa_sha256(HexaVal s_val) {
    if (!HX_IS_STR(s_val)) return hexa_str("");
    const char* s = HX_STR(s_val) ? HX_STR(s_val) : "";
    _hxa_sha256_ctx ctx;
    _hxa_sha256_init(&ctx);
    _hxa_sha256_update(&ctx, (const uint8_t*)s, strlen(s));
    uint8_t digest[32];
    _hxa_sha256_final(&ctx, digest);
    char* hex = _hxa_hex_encode(digest, 32);
    if (!hex) return hexa_str("");
    return hexa_str_own(hex);
}

HexaVal hexa_sha256_file(HexaVal path_val) {
    if (!HX_IS_STR(path_val)) return hexa_str("");
    const char* path = HX_STR(path_val);
    if (!path) return hexa_str("");
    int fd = open(path, O_RDONLY);
    if (fd < 0) return hexa_str("");
    _hxa_sha256_ctx ctx;
    _hxa_sha256_init(&ctx);
    uint8_t buf[8192];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) { if (errno == EINTR) continue; close(fd); return hexa_str(""); }
        if (n == 0) break;
        _hxa_sha256_update(&ctx, buf, (size_t)n);
    }
    close(fd);
    uint8_t digest[32];
    _hxa_sha256_final(&ctx, digest);
    char* hex = _hxa_hex_encode(digest, 32);
    if (!hex) return hexa_str("");
    return hexa_str_own(hex);
}

/* ── TAG_FN shim globals (bootstrap bridge) ──────────────────────
 * Same pattern as net.c / signal_flock.c. Interpreter path resolves
 * `exec_argv(...)` / `sha256(...)` through these shims until the
 * transpiler learns direct-lowering. After direct-lowering the
 * globals simply remain unused. */
HexaVal hx_exec_argv;
HexaVal hx_exec_argv_with_status;
HexaVal hx_sha256;
HexaVal hx_sha256_file;

static void _hexa_init_exec_sha_fn_shims(void) {
    hx_exec_argv             = hexa_fn_new((void*)hexa_exec_argv,             1);
    hx_exec_argv_with_status = hexa_fn_new((void*)hexa_exec_argv_with_status, 1);
    hx_sha256                = hexa_fn_new((void*)hexa_sha256,                1);
    hx_sha256_file           = hexa_fn_new((void*)hexa_sha256_file,           1);
}
