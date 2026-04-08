# HEXA Standard Library Reference

## Modules (13)

| Module | File | LOC | Functions |
|--------|------|-----|-----------|
| **std::collections** | src/std_collections.rs | 346 | btree_new/set/get/remove/keys/len, pq_new/push/pop/len, deque_new/push_front/push_back/pop_front/pop_back/len |
| **std::consciousness** | src/std_consciousness.rs | 227 | psi_alpha, phi_compute, law_count |
| **std::crypto** | src/std_crypto.rs | 317 | sha256, xor_cipher, hash_djb2, random_bytes, hmac_sha256 |
| **std::encoding** | src/std_encoding.rs | 356 | csv_parse, csv_format, url_encode, url_decode, base64_encode, base64_decode, hex_encode, hex_decode |
| **std::fs** | src/std_fs.rs | 560 | fs_read, fs_write, fs_append, fs_exists, fs_remove, fs_mkdir, fs_list, fs_copy, fs_move, fs_metadata, fs_glob, fs_watch |
| **std::io** | src/std_io.rs | 342 | io_stdin, io_read_lines, io_write_bytes, io_pipe, io_tempfile, io_buffered_reader, io_reader_next |
| **std::log** | src/std_log.rs | 190 | log_debug, log_info, log_warn, log_error, log_set_level, log_get_entries |
| **std::math** | src/std_math.rs | 417 | math_pi/e/phi/abs/sqrt/pow/log/sin/cos/floor/ceil/round/min/max/gcd, matrix_new/set/get/mul/det |
| **std::net** | src/std_net.rs | 536 | net_listen, net_accept, net_connect, net_read, net_write, net_close, http_serve |
| **std::nexus** | src/std_nexus.rs | 174 | nexus_scan, nexus_verify, nexus_omega, nexus_lenses, nexus_consensus, nexus_n6_check |
| **std::testing** | src/std_testing.rs | 297 | assert_eq, assert_ne, assert_true, assert_false, test_run, test_suite, bench_fn |
| **std::time** | src/std_time.rs | 329 | time_now, time_now_ms, time_sleep, time_format, time_parse, time_elapsed |
| **std::web_template** | src/std_web_template.rs | 275 | Template rendering engine |

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
