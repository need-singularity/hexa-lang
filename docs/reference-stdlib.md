# HEXA Standard Library Reference

## Modules (13)

| Module | File | LOC | Functions |
|--------|------|-----|-----------|
| **std::collections** | self/std_collections.hexa | 242 | btree_new/set/get/remove/keys/len, pq_new/push/pop/len, deque_new/push_front/push_back/pop_front/pop_back/len |
| **std::consciousness** | self/std_consciousness.hexa | 312 | psi_alpha, phi_compute, law_count |
| **std::crypto** | self/std_crypto.hexa | 237 | sha256, xor_cipher, hash_djb2, random_bytes, hmac_sha256 |
| **std::encoding** | self/std_encoding.hexa | 279 | csv_parse, csv_format, url_encode, url_decode, base64_encode, base64_decode, hex_encode, hex_decode |
| **std::fs** | self/std_fs.hexa | 188 | fs_read, fs_write, fs_append, fs_exists, fs_remove, fs_mkdir, fs_list, fs_copy, fs_move, fs_metadata, fs_glob, fs_watch |
| **std::io** | self/std_io.hexa | 91 | io_stdin, io_read_lines, io_write_bytes, io_pipe, io_tempfile, io_buffered_reader, io_reader_next |
| **std::log** | self/std_log.hexa | 87 | log_debug, log_info, log_warn, log_error, log_set_level, log_get_entries |
| **std::math** | self/std_math.hexa | 268 | math_pi/e/phi/abs/sqrt/pow/log/sin/cos/floor/ceil/round/min/max/gcd, matrix_new/set/get/mul/det |
| **std::net** | self/std_net.hexa | 153 | net_listen, net_accept, net_connect, net_read, net_write, net_close, http_serve |
| **std::nexus** | self/std_nexus.hexa | 296 | nexus_scan, nexus_verify, nexus_omega, nexus_lenses, nexus_consensus, nexus_n6_check |
| **std::testing** | self/std_testing.hexa | 283 | assert_eq, assert_ne, assert_true, assert_false, test_run, test_suite, bench_fn |
| **std::time** | self/std_time.hexa | 208 | time_now, time_now_ms, time_sleep, time_format, time_parse, time_elapsed |
| **std::web_template** | self/std_web_template.hexa | 268 | Template rendering engine |

## Usage

```hexa
// Direct builtin call
let data = fs_read("file.txt")

// Method-style (for strings, arrays)
let parts = "a,b,c".split(",")
let sorted = [3,1,2].sort()
```

## Built-in Functions (non-std)

See `docs/history/2026-04-06-language-survey.md` for the complete builtin list (150+).
