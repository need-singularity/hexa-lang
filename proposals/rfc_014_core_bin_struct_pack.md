# RFC 014 — `core.bin.struct_pack` / `struct_unpack` Win32 / PE struct serializer

- **Status**: proposed
- **Date**: 2026-05-04
- **Severity**: stdlib gap (Win32 synthetic struct hand-roll across 11 PE loaders, ~150+ hit site cumulative)
- **Priority**: P1 (Tier-A stdlib gap; per-loader cargo-cult helper duplication blocks PE expansion scaling)
- **Source**: airgenome-gamebox Track K/O/Q/W/AH/AM (11 PE loader land 누적) + Track AV §8.3 B-bench
- **Source design doc (sibling)**: airgenome-gamebox `docs/HEXA_UPSTREAM_PROPOSAL_6_3_PE_STRUCT.md`
- **Family**: stdlib binary serialization / Win32 ABI 정합 (no prior RFC; first struct-pack proposal)
- **Discovery pipeline**: Track AO retrofit (gamebox `docs/HEXA_UPSTREAM.md` §6.3, 460 LOC) → Track AV B-bench (§8.3 append, 558 LOC pass) → Track AW PR-ready spec → Track AY sibling PR land

## §0 Scope

본 RFC 는 hexa-lang stage1 의 struct serialization stdlib 부재 — Win32 synthetic struct
(SOCKADDR_IN / WSAEVENT / FILE_HANDLE / PROCESS_INFORMATION / SYSTEMTIME / HWND / HDC /
HKEY / IID / VARIANT 등) 가 11 PE loader 마다 raw hex literal byte array 로 inline hand-code
되는 상황 — 을 stdlib `core.bin.struct_pack / struct_unpack` builtin 으로 정형화하는 제안.

additive only / migration 0 / breaking change 0.

## §1 Problem statement

airgenome-gamebox Track K (Winsock + WININET + SCHANNEL) + Track O (KERNEL32 core) + Track Q
(USER32 + GDI32 + ADVAPI32) + Track W (KERNEL32 extend + NTDLL + OLE32 + OLEAUT32) +
Track AH/AM (MSVCRT + SHELL32 + COMCTL32 + DXGI) 누적 11 신규 PE loader × 평균 10-15
synthetic struct site / loader ≈ **150+ hit site** (Track AO retrofit cite 110+ 이후
Track AM 에서 +DXGI / shell extend 로 추가).

각 loader 의 패턴 (예: gamebox `lib/loader/pe_winsock_ws2_32.hexa` line 254):

```hexa
// SOCKADDR_IN 16-byte synthetic struct hand-roll
// expected: "02000001BB01020304000000000000"
//   sin_family u16 LE = 02 00            (AF_INET)
//   sin_port   u16 BE = 01 BB            (htons(443))
//   sin_addr   u32 BE = 01 02 03 04      (1.2.3.4)
//   sin_zero[8]       = 00 00 00 00 00 00 00 00
let s = u16_le_hex(2) + u16_be_hex(443) + u32_be_hex(0x01020304) + zero_hex(8)
```

각 loader 가 `u16_le_hex` / `u16_be_hex` / `u32_le_hex` / `u32_be_hex` / `zero_hex` 등
helper 5-7 개를 cargo-cult 복제. Win32 reference doc 측 cross-validate 만으로 byte layout
정확성 보증 — production-grade struct correctness 측면에서 fragile.

Track AV §8.3 B-bench 측정: hand_rolled 70 LOC per pass / measured ns/op (string concat
위주 ~5× 느림 가능성). hypothetical native (`core.bin.struct_pack` arm64 extrapolation) 는
3 LOC per pass / ~100 ns/op estimate (4 store + format overhead).

C / C++ 의 `struct { ... } __attribute__((packed))` + `memcpy` 대응 builtin 부재가 본 문제의
근본 원인. Python `struct.pack(">HHI8s", 2, 443, 0x01020304, b"")` 류의 layout-string-driven
serializer 가 가장 적은 dependency 로 same-size 효과.

## §2 Proposed API — full signature freeze

```hexa
// core.bin — struct serialization (Win32 / PE / network protocol)
// own1 정합: pure hexa stdlib, no FFI.

fn struct_pack(layout: str, values: array) -> bytes
    // layout string 으로 byte array build.
    // layout token grammar (comma-separated):
    //   u8 / u16le / u16be / u32le / u32be / u64le / u64be — unsigned int
    //   i8 / i16le / i16be / i32le / i32be / i64le / i64be — signed int
    //   bytes[N]   — raw N-byte fixed array (values[i] 은 bytes 타입)
    //   zero[N]    — N-byte zero fill (values[i] 미사용, skip 가능)
    //   utf16le[N] — UCS-2 LE fixed-length string (Win32 wide string)
    // 예: struct_pack("u16le,u16be,u32be,zero[8]", [2, 443, 0x01020304, 0])
    //   → bytes("02 00 01 BB 01 02 03 04 00 00 00 00 00 00 00 00")
    // values length / token count mismatch 시 stderr warn + 부분 result.

fn struct_unpack(layout: str, raw: bytes) -> array
    // struct_pack 의 inverse. raw byte → array of values.
    // raw length 가 layout total size 미달 시 stderr warn + null-padded.
    // 초과 byte 는 silently drop (caller 가 size cross-check).

fn pe_section_header_synth(name: str, vsize: u32, fofs: u32) -> bytes
    // PE-specific helper — IMAGE_SECTION_HEADER 40-byte layout build.
    // name 8-byte fixed (truncate / zero-pad), vsize / fofs 정수 fields.
    // 본 helper 는 struct_pack 의 syntactic sugar — 별도 spec 필요 정도로 자주 쓰임.
```

## §3 Reference impl outline (pseudo-hexa, ~85 LOC estimate)

```hexa
// core.bin — reference implementation skeleton

fn struct_pack(layout: str, values: array) -> bytes {
    let tokens = split_layout(layout)   // ["u16le", "u16be", "u32be", "zero[8]"]
    let result = bytes_empty()
    let vi = 0
    for tok in tokens {
        let kind = parse_kind(tok)
        if kind == "zero" {
            let n = parse_size(tok)
            result = result + zero_bytes(n)
        } else if kind == "u16le" {
            result = result + u16_le_bytes(values[vi]); vi = vi + 1
        } else if kind == "u16be" {
            result = result + u16_be_bytes(values[vi]); vi = vi + 1
        } else if kind == "u32le" {
            result = result + u32_le_bytes(values[vi]); vi = vi + 1
        } else if kind == "u32be" {
            result = result + u32_be_bytes(values[vi]); vi = vi + 1
        } else if kind == "bytes" {
            let n = parse_size(tok)
            result = result + truncate_or_pad(values[vi], n); vi = vi + 1
        }
        // ... u8 / i* / u64 / utf16le 동일 패턴
    }
    return result
}

fn struct_unpack(layout: str, raw: bytes) -> array { ... }   // ~30 LOC inverse
fn pe_section_header_synth(name, vsize, fofs) -> bytes {
    return struct_pack("bytes[8],u32le,u32le,u32le,u32le,u32le,u32le,u16le,u16le,u32le",
        [name_padded(name, 8), vsize, /* virtual_addr */ 0, /* size_raw */ vsize,
         fofs, /* relocs */ 0, /* lineno */ 0, 0, 0, /* characteristics */ 0])
}
```

총 추정 ~85 LOC (token parser + 16 endian-format helpers + pe_section_header_synth 합산).

## §4 Test cases (12 cases)

1. `struct_pack("u16le", [2]) == bytes("02 00")` — basic u16 LE
2. `struct_pack("u16be", [443]) == bytes("01 BB")` — basic u16 BE (htons)
3. `struct_pack("u32be", [0x01020304]) == bytes("01 02 03 04")` — u32 BE
4. `struct_pack("u32le", [0x01020304]) == bytes("04 03 02 01")` — u32 LE
5. `struct_pack("zero[4]", []) == bytes("00 00 00 00")` — zero fill
6. SOCKADDR_IN: `struct_pack("u16le,u16be,u32be,zero[8]", [2, 443, 0x01020304, 0])`
   `== bytes("02 00 01 BB 01 02 03 04 00 00 00 00 00 00 00 00")` — Track K Winsock pattern
7. `struct_pack("bytes[4]", [bytes("DE AD BE EF")]) == bytes("DE AD BE EF")` — fixed bytes
8. `struct_pack("bytes[4]", [bytes("DE AD")]) == bytes("DE AD 00 00")` — pad short
9. `struct_unpack("u16le,u16be", bytes("02 00 01 BB")) == [2, 443]` — unpack inverse
10. `struct_unpack("u32be,zero[4]", bytes("01 02 03 04 00 00 00 00")) == [0x01020304]`
    — zero skip in unpack
11. `pe_section_header_synth(".text", 0x1000, 0x400)` — PE section header (40 byte) round-trip
12. `struct_pack(layout, struct_unpack(layout, raw)) == raw` — round-trip property

추가 edge: malformed layout token (`"u32xx"`) → warn + empty, values len mismatch,
i64 overflow, utf16le truncation, raw bytes 길이 < layout size.

## §5 Breaking changes — none (additive new module)

`core.bin` 는 신규 모듈. 기존 hexa stage1 builtin 변경 / 제거 0. opt-in
`import core.bin`. 기존 11 PE loader 의 hand-roll helper 는 **deprecate 아님** —
migration 0 정합으로 호환 layer 유지.

## §6 Alternatives considered

**Option A**: per-loader hand-roll (현재 상태).
**Verdict**: REJECTED. 11 loader × 평균 ~67 LOC (Track AV §8.3 hand_rolled 70 LOC per
struct-build pass × ~10-15 site / loader) = ~700-1100 LOC cumulative hand-roll. 신규
loader 도입 시 cargo-cult 복제. 확장성 0.

**Option B**: C `__attribute__((packed))` + FFI binding.
**Verdict**: REJECTED. own1 wine_0_hexa_only 정합 위반 — 외부 C lib FFI 는 sibling repo
의 stage1 본격 build pipeline 변경 필요. 또한 FFI 도입은 sandbox / cross-platform 이슈
(macOS arm64 / Linux x86_64 / Windows 정합) 가 stage1 scope 외.

**Option C**: hexa record syntax (예: `record SockaddrIn { sin_family: u16, sin_port: u16, ... }`)
+ `record_pack(rec)` 자동.
**Verdict**: PARTIAL — 본 제안과 직교. record syntax (gamebox §4.5.2 의 dict literal 부재 직접
연관) 가 future 에 land 하면 더 깨끗 — 그러나 layout 의 endian / padding 제어가 syntax
sugar 만으로는 부족 (network protocol 특히 BE / LE 명시 필요). struct_pack 은 syntax
독립적 — record 와 평행 가치.

**Option D**: Python-style `struct.pack(format, *args)` (hexa 2-arg `(layout, values)`
정합).
**Verdict**: ADOPTED — 본 RFC §2 의 layout string grammar 가 Python struct module 의
character-format (`>HHI8s`) 을 hexa snake_case (`u16le,u16be,u32be,zero[8]`) 로 정형화.

## §7 Sibling submission spec

**디렉터리 layout** (this repo `/Users/ghost/core/hexa-lang/`):

- `proposals/rfc_014_core_bin_struct_pack.md` — 본 RFC.
- `src/std/bin.hexa` — `core.bin` reference impl (§3 outline land 형태) — future cycle.
- `tests/std/test_struct_pack.hexa` — §4 의 12+ test case + edge — future cycle.

**리뷰 checklist**:

- API signatures match §2 freeze.
- 12 test cases pass + edge (endian / pad / round-trip).
- SOCKADDR_IN bench (test #6) byte-exact match (Track K reference: `02000001BB01020304000000000000`).
- malformed layout token → warn + empty (silent-error-ban 정합).
- pe_section_header_synth 40-byte exact.

## §8 Dependencies — none

hexa stdlib internal 만. 외부 C lib FFI 0. 기존 builtin 의 byte concat / int 산술 /
`stderr_warn` (future) 사용 — 본 제안과 독립 dependency. UTF-16 LE encoding 은 본
모듈 자체 구현 (기존 `std_encoding` UTF-8 helpers 와 평행 추가).

## §9 Caveats (≥6, 11 listed)

C1. 본 RFC 는 spec only — reference impl land 는 별도 cycle (proposal accepted 후).
C2. layout grammar 는 fixed-size 만 — variable-length (예: length-prefixed string) 는
    미지원. caller 가 length field 를 별도 token 으로 pre-pack.
C3. struct_pack 의 alignment / padding 자동 미지원 — `__attribute__((packed))` 정합
    (no auto-padding). x86 ABI 의 8-byte alignment 가 필요한 struct 은 caller 가 명시적
    `zero[N]` 토큰 삽입.
C4. utf16le[N] 토큰의 N 은 byte count (== char count × 2) — UCS-2 surrogate pair (BMP
    외) 미지원. Win32 wide string 의 99% 케이스는 BMP 내라 영향 적음.
C5. struct_unpack 의 raw byte 길이 < layout size 시 null-pad — silent-error-ban 정합으로
    stderr warn 발생. caller 가 size cross-check 권장.
C6. Track AV §8.3 의 hypothetical native ns/op 100 estimate 는 4 store + format overhead
    arm64 extrapolation — own 2 honest 정합으로 estimate 명시. Hand_rolled string concat
    위주라 native 보다 ~5× 느릴 가능성 — 그러나 bench 진가는 LOC delta (70 → 3) +
    correctness (Win32 reference doc cross-validate 의 stdlib-level standardization).
C7. 150+ hit count 의 700-1100 LOC 추정은 11 loader × 평균 10-15 site / loader × ~67 LOC
    site — 추정 변동성 ±30%. 정확한 측정은 sibling repo land 후 retrofit 측정 cycle.
C8. Track AW (gamebox-side) 는 doc-only. Track AY (this RFC) 는 sibling repo 의 proposals/
    추가만 — `lib/` `tool/` `tests/` 영향 0.
C9. `core.bin` 네이밍은 hexa-lang convention — sibling repo 의 actual convention (예:
    `std_bin` vs `core.bin`) 은 reviewer cross-check.
C10. 본 RFC 가 land 시 11 loader 의 hand-roll helper 는 **deprecate 아님** — migration 0
     정책 정합으로 호환 layer 유지, 신규 loader 만 새 API 사용 권장. 기존 loader 는
     future migration cycle 에서 user 동의 후 점진 swap.
C11. own 5 status: C-hit 150+ (Track K/O/Q/W/AH/AM 누적, Track AO retrofit 110+ + 후속
     loader land 분 합산) + B-bench 70→3 LOC measured (Track AV §8.3) =
     `c_plus_b_pr_ready_pending_user_approval`. 본 RFC 는 sibling repo 측 spec doc 형태로
     정형화 한정 — status 자체 진전 없음.

---

*Track AY sibling PR land, 2026-05-04. proposals/-only / additive / migration 0 /
destructive 0 / sibling repo `lib/` `tool/` `tests/` 미수정.*
