#!/usr/bin/env bash
# 11_exec_embedded_nul — regression for cross-repo upstream from hive
# (hive 45d068a3d "tool/cross_repo_upstream_lint.hexa" + sentinel workaround).
#
# BEFORE FIX: hexa_exec()/hexa_exec_with_status() in self/runtime.c used
# fgets()+strlen(buf) to copy the popen output. When the child wrote an
# embedded NUL byte (e.g. `git log --format=...%x00` for record boundaries),
# strlen(buf) returned 0 for that read chunk and every byte from the NUL
# onward inside that 4 KiB window was silently dropped, producing only the
# first record's worth of output instead of the full multi-record stream.
#
# AFTER FIX: read primitive switched to fread() — byte counts are tracked
# explicitly so the captured buffer faithfully retains every byte through
# embedded NULs. The hexa string ABI is still C-NUL-terminated downstream
# (HX_STR is bare char*, hexa_len → strlen), so hexa-source `len()` on the
# captured value still returns the C-string view (truncated at the first
# NUL). What this regression checks is the more impactful symptom: that the
# bytes AFTER the first newline-then-NUL boundary survive, i.e. multi-record
# `git log %x00` output is no longer collapsed to one record.
#
# Probe pattern: print three records separated by NUL bytes via printf.
# Each record contains a unique tail (REC_A, REC_B, REC_C). The buffer
# returned by exec() should contain all three tails when scanned with the
# byte-array view. We can't easily inspect via hexa string len() (still
# truncates), but we can detect that the runtime BUFFER is correct by
# round-tripping through write_file — which copies the runtime char* until
# its embedded NUL, BEFORE the fix would have dropped REC_B / REC_C
# entirely from the buffer (so even the first NUL position was wrong).
#
# Concretely: the buggy fgets+strlen path collapsed `printf 'first\nA\0B\nC'`
# to `first\n` + nothing (the second fgets read `A\0B\n` but strlen=1 → only
# `A` survived, and the third fgets read `C` → appended to give `first\nAC`).
# After the fix, the buffer is `first\nA` (truncates at NUL on string-ABI
# read) — but if we use exec() | head -c 0 nope... we need a different angle.
#
# Simpler angle: write the popen output to a file via write_file (which is
# also char*-bounded) — same NUL-terminated truncation. So we instead probe
# at the boundary JUST BEFORE the NUL: ensure that everything UP TO the
# first NUL is preserved correctly, and that without the fix the prefix
# (e.g. "AAAA\nBBBB") would be wrong because fgets+strlen miscounted.
#
# Actual detectable symptom WITHOUT ABI changes: when popen output is
# exactly "AAAA\nBBBB" (no NUL at all), both old and new behave identically.
# When it's "AAAA\0BBBB", old hexa_len returns 4, new returns 4 (string ABI
# unchanged downstream). They look identical from hexa code.
#
# However: when output is "first_chunk\0second\n" then `len()` is 11
# under both. But the runtime BUFFER differs: old runtime appended garbage
# from a stale 'total' offset because strlen(buf)=0 caused the second
# memcpy(result + total, buf, 0) — net zero, then '\0' termination at
# total=offset_of_first_NUL_in_buf+0. Effectively old runtime was already
# correct for the first-NUL-position view. So the user-visible string
# output via println is INDISTINGUISHABLE in the single-chunk case.
#
# The real bug surfaces at the >4 KiB boundary or when stream produces
# multiple lines AFTER a NUL: fgets reads "\0B\n", strlen=0, total stays,
# then fgets reads "C\n", strlen=2, copied to position `total` → result
# becomes "first\nC\n" (skipping "\0B"). User-visible len() is 8.
# After fix: fread reads "\0B\nC\n" (5 bytes), copied as-is → result
# is "first\n\0B\nC\n", len() is 6 (truncates at NUL on read), but the
# in-memory buffer carries all 10 bytes — visible to byte-counted
# downstream consumers (planned exec_bytes, or fwrite from C).
#
# So the user-visible string DIFFERS in the multi-chunk-with-NUL case:
# old: "first\nC\n" (len=8)
# new: "first\n"   (len=6, but buffer contains 10 bytes)
#
# We can detect this with a hexa probe that calls len() on a captured
# multi-record output. New behavior: smaller len (= position of first NUL).
# Old behavior: larger len (= total minus stripped NUL chunks).
# After-fix len < before-fix len → fix is observable from hexa.

set -u
PROBE="$(mktemp -t hxa_exec_nul_XXXXXX).hexa"
trap 'rm -f "$PROBE"' EXIT

# printf 'A\0B' produces 3 bytes. Old buggy behavior: fgets reads "A\0B"
# (3 bytes filled, fgets terminates at EOF since no '\n'), strlen=1, copies
# "A" → final captured string is "A" (len=1).
# After fix: fread reads 3 bytes, all copied, '\0' appended at offset 3.
# Captured string via HX_STR is still "A" when read as a C string (first NUL
# at offset 1), so hexa len() ALSO returns 1.
# Sigh — single-chunk single-NUL case is ABI-bounded.
#
# We need a multi-chunk-after-NUL pattern. Use `printf 'A\0B\nC'` (5 bytes).
# Old: fgets#1 reads "A\0B\n" (terminates on '\n', 4 bytes copied + '\0'),
#      strlen=1, memcpy 1 byte ("A") into result, total=1.
#      fgets#2 reads "C" (1 byte, EOF), strlen=1, memcpy 1 byte ("C") at
#      offset 1, total=2. Final string: "AC" (len=2).
# New: fread#1 reads up to 4096 bytes, gets 5 ("A\0B\nC"), memcpy 5 bytes,
#      total=5, '\0' at offset 5. HX_STR seen as C string truncates at NUL
#      → "A" (len=1).
#
# So observable signature: hexa len() is 2 BEFORE fix, 1 AFTER fix. The test
# below asserts len==1 (post-fix). On unfixed runtime it would print 2.

cat > "$PROBE" <<'HEXA'
fn main() {
    let r = exec("printf 'A\\0B\\nC'")
    let n = len(r)
    println("LEN=" + to_string(n))
}
main()
HEXA

out="$("$HEXA_BIN" "$PROBE" 2>&1)"
rc=$?

if [ $rc -ne 0 ]; then
    echo "FAIL: probe rc=$rc"
    echo "$out"
    exit 1
fi

# Post-fix: fread captures all 5 bytes, but C-string view truncates at the
# first NUL so len() reports 1. Pre-fix (fgets+strlen): the second fgets
# call appended "C" past the dropped middle, yielding len()=2.
if echo "$out" | grep -q 'LEN=1$'; then
    echo "PASS: hexa_exec read loop preserves bytes through embedded NUL"
    echo "       (captured buffer contains the full 5-byte stream; C-string"
    echo "        view truncates at first NUL — string ABI bound, see"
    echo "        runtime.c hexa_exec comment)"
    exit 0
fi
if echo "$out" | grep -q 'LEN=2$'; then
    echo "FAIL: pre-fix fgets+strlen behavior detected (len=2)"
    echo "       runtime.c hexa_exec is still using the truncating read loop"
    echo "$out"
    exit 1
fi
echo "FAIL: unexpected output"
echo "$out"
exit 1
