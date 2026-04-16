#!/usr/bin/env bash
# rebuild_stage0.sh — safe wrapper around build_stage0.sh with layered cache
#
# WHY: 이번 세션(2026-04-15)에서 stage0 재빌드가 5~10초마다 반복되어
#   flatten_imports → hexa_v2 → clang → cc1 → linker 6-8 child × 누적
#   → macOS xpcproxy 폭주 → system load 24→112. 근본 원인:
#   (1) 동시 claude 세션 다중 재빌드
#   (2) 입력 변경 없이도 매번 풀 파이프라인 실행
#   (3) 재빌드마다 smoke test 또 스폰
#
# 이 wrapper 가 해결:
#   - mkdir lock (atomic, macOS-safe) → 동시 재빌드 차단
#   - LAYERED CONTENT CACHE (2026-04-16) — 각 파이프라인 단계 독립 fingerprint:
#       * flatten/<key>.hexa   key = sha256(flatten_imports + hexa_full + 모든 `use` 의존 .hexa)
#       * transpile/<key>.c    key = sha256(flatten_key + hexa_v2 바이너리)
#       * link/<key>.bin       key = sha256(transpile_key + runtime.c + CFLAGS + LDFLAGS + uname)
#     → touch 없으면 cache hit 복사 (clang/hexa_v2 모두 skip), 단일 stage 변경 시 해당 stage 부터 재실행.
#   - NO_SMOKE 기본 on → smoke 스폰 제거 (호출측이 필요하면 본인 테스트 1회)
#   - 실패 시 lock 자동 해제
#
# 사용:
#   bash scripts/rebuild_stage0.sh               # layered cache + per-file incremental
#   FORCE=1 bash scripts/rebuild_stage0.sh       # 캐시 무시 강제 rebuild
#   WITH_SMOKE=1 bash scripts/rebuild_stage0.sh  # smoke test 포함
#   NO_CACHE=1 bash scripts/rebuild_stage0.sh    # 캐시 read/write 전체 비활성 (legacy 경로)
#
# 캐시 레이아웃 (HEXA_CACHE_DIR, 기본값 /tmp/hexa-cache):
#   flatten/
#     <hash>.hexa            flatten_imports 산출물
#     <hash>.key             (디버그용) 키 구성 요소 메타
#   transpile/
#     <hash>.c               hexa_v2 트랜스파일 산출물
#     <hash>.key
#   link/
#     <hash>.bin             clang 링크 결과 (최종 stage0)
#     <hash>.key
#   meta/
#     last_build.json        마지막 빌드의 키 체인 + 통계 (hit/miss)
#   file_hashes.manifest     per-file sha256 for incremental diff
#
# 캐시 key 전략:
#   flatten_key   = sha256( flatten_imports.hexa ‖ hexa_full.hexa ‖
#                           use-transitive .hexa (sorted) )
#   transpile_key = sha256( flatten_key ‖ hexa_v2_binary ‖ hexa_v2_dedup_binary? )
#   link_key      = sha256( transpile_key ‖ runtime.c ‖ CFLAGS ‖ LDFLAGS ‖ uname )
#
# Per-file incremental (2026-04-16):
#   Before: any file change → full-chain invalidation (flatten+transpile+link)
#   After:  per-file sha256 manifest tracks which files changed:
#     - 0 files changed   → layered cache hit as before (<0.5s)
#     - runtime.c only    → skip flatten+transpile, reuse .c, only re-link (~2s)
#     - any .hexa changed → re-flatten + re-transpile + re-link (full ~5-10s)
#   Diagnostic output shows exactly which file(s) triggered the rebuild.
#
# 예상 속도향상: touch 없으면 ~30s → <0.5s (link cache hit만 복사)
#               runtime.c only → ~2s (clang link only, skip flatten+hexa_v2)
#
# 반환 코드:
#   0 — rebuilt or cache hit
#   1 — build failed
#   2 — another session holds the lock (caller should wait or abort)

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HEXA_DIR/build/hexa_stage0"
LOCK_DIR="/tmp/hexa_rebuild_stage0.lock"
LOCK_TTL_SEC=300   # 5min: stale 판단 임계치
CACHE_DIR="${HEXA_CACHE_DIR:-/tmp/hexa-cache}"
CACHE_FLATTEN_DIR="$CACHE_DIR/flatten"
CACHE_TRANSPILE_DIR="$CACHE_DIR/transpile"
CACHE_LINK_DIR="$CACHE_DIR/link"
CACHE_META_DIR="$CACHE_DIR/meta"

# ── 1. Lock 획득 (atomic mkdir) ─────────────────────────────
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    HOLDER_PID=""
    HOLDER_TIME=""
    if [ -f "$LOCK_DIR/pid" ]; then
        HOLDER_PID=$(cat "$LOCK_DIR/pid" 2>/dev/null || echo "?")
    fi
    if [ -d "$LOCK_DIR" ]; then
        # macOS stat vs GNU stat 호환
        HOLDER_TIME=$(stat -f %m "$LOCK_DIR" 2>/dev/null || stat -c %Y "$LOCK_DIR" 2>/dev/null || echo 0)
    fi
    NOW=$(date +%s)
    AGE=$(( NOW - ${HOLDER_TIME:-$NOW} ))

    if [ -n "$HOLDER_PID" ] && kill -0 "$HOLDER_PID" 2>/dev/null && [ "$AGE" -lt "$LOCK_TTL_SEC" ]; then
        echo "[rebuild_stage0] another session is building (pid=$HOLDER_PID, age=${AGE}s) — abort" >&2
        exit 2
    fi

    # stale: holder 죽음 OR TTL 초과 → 회수
    echo "[rebuild_stage0] stale lock (pid=$HOLDER_PID, age=${AGE}s) — reclaiming" >&2
    rm -rf "$LOCK_DIR"
    mkdir "$LOCK_DIR"
fi
echo $$ > "$LOCK_DIR/pid"
cleanup() { rm -rf "$LOCK_DIR"; rm -rf /tmp/hexa_flatten.lock 2>/dev/null || :; [ -n "${BACKUP:-}" ] && rm -f "$BACKUP" || :; }
trap cleanup EXIT INT TERM

# ── 2. Helper: SHA-256 (macOS shasum -a 256 / Linux sha256sum 공통 fallback) ──
sha256_cmd() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256
    else
        sha256sum
    fi
}
hash_of_files() {
    # Concat files in argv order, pipe to sha256, strip filename
    cat "$@" 2>/dev/null | sha256_cmd | cut -d' ' -f1
}
hash_of_file() {
    # Hash a single file, return just the hash
    sha256_cmd < "$1" 2>/dev/null | cut -d' ' -f1
}
hash_of_strings() {
    # Hash a sequence of strings joined by \n
    printf '%s\n' "$@" | sha256_cmd | cut -d' ' -f1
}

# ── 2b. Per-file manifest helpers ──────────────────────────────────────
#    file_hashes.manifest format: "sha256  filepath" per line (sorted by path).
#    Enables per-file change detection: only the files whose hash differs
#    from the manifest are considered "dirty". This avoids full-chain
#    invalidation when only runtime.c (or a single .hexa) changes.
MANIFEST_FILE="$CACHE_DIR/file_hashes.manifest"

# Write a manifest from an array of file paths.
# Outputs sorted "hash  path" lines to $MANIFEST_FILE.
write_manifest() {
    local -a files=("$@")
    mkdir -p "$CACHE_DIR"
    local tmp_manifest
    tmp_manifest="$(mktemp "${CACHE_DIR}/manifest.tmp.XXXXXX")"
    local f
    for f in "${files[@]}"; do
        [ -f "$f" ] || continue
        local h
        h=$(hash_of_file "$f")
        echo "$h  $f"
    done | sort -k2 > "$tmp_manifest"
    mv -f "$tmp_manifest" "$MANIFEST_FILE"
}

# Compare current file hashes against the stored manifest.
# Sets CHANGED_FILES (array) and CHANGED_CATEGORIES (string: "hexa", "runtime", "tool", or combos).
# Returns 0 if any file changed, 1 if all match (nothing changed).
diff_manifest() {
    local -a files=("$@")
    CHANGED_FILES=()
    CHANGED_CATEGORIES=""
    local any_changed=1  # 1 = no change (shell convention)

    if [ ! -f "$MANIFEST_FILE" ]; then
        # No manifest = treat everything as changed
        CHANGED_FILES=("${files[@]}")
        CHANGED_CATEGORIES="hexa,runtime,tool"
        return 0
    fi

    local f
    for f in "${files[@]}"; do
        [ -f "$f" ] || continue
        local cur_hash
        cur_hash=$(hash_of_file "$f")
        # Look up stored hash for this file
        local stored_hash=""
        stored_hash=$(awk -v path="$f" '$2 == path {print $1; exit}' "$MANIFEST_FILE" 2>/dev/null || echo "")
        if [ "$cur_hash" != "$stored_hash" ]; then
            CHANGED_FILES+=("$f")
            any_changed=0
            # Categorize the change
            case "$f" in
                */runtime.c)           CHANGED_CATEGORIES="${CHANGED_CATEGORIES:+$CHANGED_CATEGORIES,}runtime" ;;
                */flatten_imports.hexa) CHANGED_CATEGORIES="${CHANGED_CATEGORIES:+$CHANGED_CATEGORIES,}tool" ;;
                *.hexa)                CHANGED_CATEGORIES="${CHANGED_CATEGORIES:+$CHANGED_CATEGORIES,}hexa" ;;
                *)                     CHANGED_CATEGORIES="${CHANGED_CATEGORIES:+$CHANGED_CATEGORIES,}other" ;;
            esac
        fi
    done
    return $any_changed
}

# ── 3. Discover transitive `use` imports from hexa_full.hexa ────────────
#    runtime_core / codegen_c2 / bc_emitter / bc_vm 등 전부 포함 (재귀)
#    BFS on `use "name"` → self/<name>.hexa; 중복 제거 + sort (결정론)
discover_use_imports() {
    local root="$1"
    local -a queue=("$root")
    local -a seen=("$root")
    local i=0
    while [ "$i" -lt "${#queue[@]}" ]; do
        local cur="${queue[$i]}"
        i=$((i + 1))
        [ -f "$cur" ] || continue
        local dir
        dir="$(cd "$(dirname "$cur")" && pwd)"
        # Extract `use "name"` (exclude comment lines defensively: only match from BOL)
        local deps
        deps=$(grep -E '^use "' "$cur" 2>/dev/null | sed -E 's/^use "([^"]+)".*/\1/' || true)
        local dep
        for dep in $deps; do
            local cand="$dir/$dep.hexa"
            [ -f "$cand" ] || continue
            # seen 검사
            local already=0
            local s
            for s in "${seen[@]}"; do
                if [ "$s" = "$cand" ]; then already=1; break; fi
            done
            if [ "$already" = "0" ]; then
                seen+=("$cand")
                queue+=("$cand")
            fi
        done
    done
    # deterministic: sort
    printf '%s\n' "${seen[@]}" | sort -u
}

# ── 4. Bail-out: NO_CACHE mode skips the layered cache entirely ────────
if [ -n "$NO_CACHE" ]; then
    echo "[rebuild_stage0] NO_CACHE=1 → legacy single-hash path"
    HASH_FILE="$HEXA_DIR/build/.rebuild_hash"
    compute_legacy_hash() {
        cat "$HEXA_DIR/self/hexa_full.hexa" "$HEXA_DIR/self/runtime.c" 2>/dev/null \
            | sha256_cmd | cut -d' ' -f1
    }
    if [ -z "$FORCE" ] && [ -x "$OUT" ] && [ -f "$HASH_FILE" ]; then
        CURRENT_HASH=$(compute_legacy_hash)
        STORED_HASH=$(cat "$HASH_FILE" 2>/dev/null || echo "")
        if [ "$CURRENT_HASH" = "$STORED_HASH" ]; then
            echo "[rebuild_stage0] content-hash unchanged ($CURRENT_HASH), skip"
            exit 0
        fi
    fi
    [ -z "$WITH_SMOKE" ] && export NO_SMOKE=1
    bash "$HEXA_DIR/scripts/build_stage0.sh" || exit 1
    mkdir -p "$(dirname "$HASH_FILE")"
    compute_legacy_hash > "$HASH_FILE"
    echo "[rebuild_stage0] OK (legacy)"
    exit 0
fi

# ── 5. Compute FLATTEN key ──────────────────────────────────────────────
#    입력: flatten_imports.hexa + hexa_full.hexa + 재귀 use 의존 .hexa
FLATTEN_SCRIPT="$HEXA_DIR/scripts/flatten_imports.hexa"
ROOT_HEXA="$HEXA_DIR/self/hexa_full.hexa"
if [ ! -f "$ROOT_HEXA" ] || [ ! -f "$FLATTEN_SCRIPT" ]; then
    echo "[rebuild_stage0] missing root inputs — falling back to legacy" >&2
    exec bash "$HEXA_DIR/scripts/build_stage0.sh"
fi

# IFS 안전하게 임포트 목록을 배열로
IMPORTS_SORTED=()
while IFS= read -r line; do
    [ -n "$line" ] && IMPORTS_SORTED+=("$line")
done < <(discover_use_imports "$ROOT_HEXA")

# ── 5b. Per-file incremental change detection ─────────────────────────
#    Compare each input file's sha256 against the stored manifest.
#    This enables:
#      (a) Diagnostic output showing exactly which file(s) changed
#      (b) runtime.c-only changes skip flatten+transpile (only re-link)
#      (c) Unchanged runs get a fast-path exit before even computing layer keys
RUNTIME_C="$HEXA_DIR/self/runtime.c"
ALL_BUILD_INPUTS=("$FLATTEN_SCRIPT" "${IMPORTS_SORTED[@]}" "$RUNTIME_C")
CHANGED_FILES=()
CHANGED_CATEGORIES=""
HEXA_ONLY_CHANGED=0
RUNTIME_ONLY_CHANGED=0
TOOL_CHANGED=0

if [ -z "$FORCE" ] && [ -x "$OUT" ]; then
    if diff_manifest "${ALL_BUILD_INPUTS[@]}"; then
        # Something changed — report which files
        echo "[rebuild_stage0] per-file diff: ${#CHANGED_FILES[@]} file(s) changed:"
        for cf in "${CHANGED_FILES[@]}"; do
            echo "  * $(basename "$cf")"
        done
        # Determine change category for partial-skip optimization
        case "$CHANGED_CATEGORIES" in
            runtime)
                RUNTIME_ONLY_CHANGED=1
                echo "[rebuild_stage0] OPTIMIZATION: only runtime.c changed → skip flatten+transpile"
                ;;
            tool|tool,*)
                TOOL_CHANGED=1
                ;;
            hexa)
                HEXA_ONLY_CHANGED=1
                ;;
        esac
    else
        # Nothing changed at all — but the layered cache may have been evicted.
        # Fall through to the layered cache check (section 8) which handles this.
        echo "[rebuild_stage0] per-file manifest: 0 files changed"
    fi
fi

# FLATTEN key 는 flatten_imports 스크립트 자체 + 전체 .hexa 의존 + flatten 지시자
FLATTEN_INPUTS=("$FLATTEN_SCRIPT" "${IMPORTS_SORTED[@]}")
FLATTEN_KEY=$(hash_of_files "${FLATTEN_INPUTS[@]}")
echo "[rebuild_stage0] flatten inputs: ${#IMPORTS_SORTED[@]} .hexa files"
echo "[rebuild_stage0] flatten_key=$FLATTEN_KEY"

# ── 6. Compute TRANSPILE key ────────────────────────────────────────────
#    입력: flatten_key + hexa_v2 바이너리 (+ dedup4 가 있으면 사용)
HEXA_V2_MAIN="$HEXA_DIR/self/native/hexa_v2"
HEXA_V2_DEDUP="$HEXA_DIR/build/hexa_v2_dedup4"
HEXA_V2_EFFECTIVE="$HEXA_V2_MAIN"
# build_stage0.sh 는 dedup4 binary 가 빌드 가능하면 우선 사용한다. 같은 선호도 재현:
if [ -f "$HEXA_DIR/self/native/hexa_cc_dedup4.c" ]; then
    # dedup4 가 이번 런에 rebuild 될 여지가 있으므로 "예상" 경로를 key 에 반영한다
    # (binary 가 없으면 key 엔 자리만 차지 — 다음 세션에 dedup4 생성되면 자동 invalidate)
    if [ -x "$HEXA_V2_DEDUP" ]; then
        HEXA_V2_EFFECTIVE="$HEXA_V2_DEDUP"
    fi
fi
TRANSPILE_KEY_INPUTS=("$FLATTEN_KEY")
if [ -x "$HEXA_V2_EFFECTIVE" ]; then
    HEXA_V2_HASH=$(hash_of_files "$HEXA_V2_EFFECTIVE")
    TRANSPILE_KEY_INPUTS+=("$HEXA_V2_HASH")
fi
# dedup4 .c 소스도 key 에 포함 — .c 변경 시 bin rebuild 트리거됨
if [ -f "$HEXA_DIR/self/native/hexa_cc_dedup4.c" ]; then
    TRANSPILE_KEY_INPUTS+=("$(hash_of_files "$HEXA_DIR/self/native/hexa_cc_dedup4.c")")
fi
TRANSPILE_KEY=$(hash_of_strings "${TRANSPILE_KEY_INPUTS[@]}")
echo "[rebuild_stage0] transpile_key=$TRANSPILE_KEY"

# ── 7. Compute LINK key ────────────────────────────────────────────────
#    입력: transpile_key + runtime.c + CFLAGS/LDFLAGS + uname + clang version
UNAME="$(uname 2>/dev/null | tr -d ' \n\t')"
CLANG_BIN="${CLANG:-clang}"
CLANG_VERSION="$("$CLANG_BIN" --version 2>/dev/null | head -1 || echo unknown)"
case "$UNAME" in
    Linux)
        LINK_CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -Wno-trigraphs"
        LINK_LDFLAGS="-lm -ldl"
        ;;
    Darwin|*)
        LINK_CFLAGS="-O2 -std=c11 -Wno-trigraphs"
        LINK_LDFLAGS="-Wl,-stack_size,0x4000000"
        ;;
esac
# RUNTIME_C already defined in section 5b
LINK_KEY_PARTS=("$TRANSPILE_KEY" \
                "$(hash_of_files "$RUNTIME_C")" \
                "$LINK_CFLAGS" "$LINK_LDFLAGS" "$UNAME" "$CLANG_VERSION")
LINK_KEY=$(hash_of_strings "${LINK_KEY_PARTS[@]}")
echo "[rebuild_stage0] link_key=$LINK_KEY"

# ── 8. Fast path: LINK cache hit → cp and done ──────────────────────────
mkdir -p "$CACHE_FLATTEN_DIR" "$CACHE_TRANSPILE_DIR" "$CACHE_LINK_DIR" "$CACHE_META_DIR"
LINK_CACHE_BIN="$CACHE_LINK_DIR/$LINK_KEY.bin"

HIT_FLATTEN=0
HIT_TRANSPILE=0
HIT_LINK=0

if [ -z "$FORCE" ] && [ -f "$LINK_CACHE_BIN" ]; then
    # 산출물 손상 여부 체크: 최소 크기
    MIN_SIZE="${HEXA_STAGE0_MIN_SIZE:-1000000}"
    CSIZE=$(wc -c < "$LINK_CACHE_BIN" | tr -d ' ')
    if [ "$CSIZE" -ge "$MIN_SIZE" ]; then
        mkdir -p "$HEXA_DIR/build"
        cp -f "$LINK_CACHE_BIN" "$OUT"
        # Darwin: codesign ad-hoc (copy 해도 signature 유지되지만 안전망)
        if [ "$UNAME" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
            codesign --force --sign - "$OUT" 2>/dev/null || true
        fi
        # Legacy hash file 도 갱신 (다른 consumer 호환)
        cat "$ROOT_HEXA" "$RUNTIME_C" 2>/dev/null | sha256_cmd | cut -d' ' -f1 \
            > "$HEXA_DIR/build/.rebuild_hash" || :
        HIT_FLATTEN=1; HIT_TRANSPILE=1; HIT_LINK=1
        # Update per-file manifest on cache hit too (keeps manifest in sync)
        write_manifest "${ALL_BUILD_INPUTS[@]}"
        cat > "$CACHE_META_DIR/last_build.json" <<EOF
{"flatten_key":"$FLATTEN_KEY","transpile_key":"$TRANSPILE_KEY","link_key":"$LINK_KEY","hit":{"flatten":1,"transpile":1,"link":1},"uname":"$UNAME","incremental":{"runtime_only":0,"changed_files":"","changed_count":0}}
EOF
        echo "[rebuild_stage0] CACHE HIT (all 3 layers) → cp $LINK_CACHE_BIN → $OUT ($CSIZE bytes)"
        echo "[rebuild_stage0] OK (cache)"
        exit 0
    else
        echo "[rebuild_stage0] link cache entry too small ($CSIZE < $MIN_SIZE) — ignore and rebuild" >&2
    fi
fi

# ── 9. Mid path: 부분 hit — flatten/transpile 단계별 재사용 ────────────
#    이 경로는 stage 단위로 재실행한 뒤 build_stage0.sh 의 SKIP_TRANSPILE 경로를 활용한다.
#
#    Per-file incremental optimization (2026-04-16):
#      When RUNTIME_ONLY_CHANGED=1, the .hexa sources haven't changed, so
#      flatten and transpile outputs are identical to the last build. We
#      look up the PREVIOUS transpile cache entry (keyed by the unchanged
#      flatten_key) and reuse it, skipping both flatten_imports and hexa_v2.
#      Only clang re-links with the new runtime.c.

FLAT_OUT="${HEXA_STAGE0_FLAT:-/tmp/hexa_full_flat.hexa}"
FLAT_CACHE="$CACHE_FLATTEN_DIR/$FLATTEN_KEY.hexa"
SRC_C="${HEXA_STAGE0_C:-/tmp/hexa_full_regen.c}"
TRANSPILE_CACHE="$CACHE_TRANSPILE_DIR/$TRANSPILE_KEY.c"

# 9-pre. Runtime-only fast path: reuse previous flatten+transpile artifacts.
#        The flatten_key and transpile_key are still based on the .hexa content
#        which hasn't changed. But the transpile_key also incorporates the
#        hexa_v2 binary hash, which might differ. We look up by flatten_key
#        in the previous build metadata to find the old transpile cache.
if [ "$RUNTIME_ONLY_CHANGED" = "1" ] && [ -z "$FORCE" ]; then
    # Try to find the previous transpile .c from the last build metadata
    PREV_TRANSPILE_KEY=""
    if [ -f "$CACHE_META_DIR/last_build.json" ]; then
        # Extract transpile_key from the JSON (simple sed, no jq dependency)
        PREV_TRANSPILE_KEY=$(sed -n 's/.*"transpile_key":"\([^"]*\)".*/\1/p' "$CACHE_META_DIR/last_build.json" 2>/dev/null || echo "")
    fi
    # Also try current transpile_key (most likely case: hexa_v2 binary unchanged)
    REUSE_TRANSPILE_C=""
    if [ -n "$PREV_TRANSPILE_KEY" ] && [ -f "$CACHE_TRANSPILE_DIR/$PREV_TRANSPILE_KEY.c" ]; then
        REUSE_TRANSPILE_C="$CACHE_TRANSPILE_DIR/$PREV_TRANSPILE_KEY.c"
    elif [ -f "$TRANSPILE_CACHE" ]; then
        REUSE_TRANSPILE_C="$TRANSPILE_CACHE"
    fi
    if [ -n "$REUSE_TRANSPILE_C" ]; then
        echo "[rebuild_stage0] INCREMENTAL: runtime.c-only change → reusing transpile from $REUSE_TRANSPILE_C"
        cp -f "$REUSE_TRANSPILE_C" "$SRC_C"
        HIT_FLATTEN=1
        HIT_TRANSPILE=1
        # Ensure runtime.c sibling is fresh (this is the file that changed)
        SRC_C_DIR="$(cd "$(dirname "$SRC_C")" && pwd)"
        if [ "$SRC_C_DIR" != "$HEXA_DIR/self" ]; then
            cp -f "$RUNTIME_C" "$SRC_C_DIR/runtime.c"
        fi
    fi
fi

# 9a. FLATTEN — cache or re-run (skipped if runtime-only path already set HIT_FLATTEN)
if [ "$HIT_FLATTEN" = "0" ]; then
    if [ -z "$FORCE" ] && [ -f "$FLAT_CACHE" ]; then
        cp -f "$FLAT_CACHE" "$FLAT_OUT"
        HIT_FLATTEN=1
        echo "[rebuild_stage0] CACHE HIT flatten → $FLAT_CACHE"
    else
        # flatten_imports 는 build_stage0.sh 안에서 실행되기 때문에 여기서 미리 돌려서 캐시에 저장.
        STAGE0_PREV="$HEXA_DIR/build/hexa_stage0"
        FLATTEN_RUNNER=""
        if [ -x "$STAGE0_PREV" ]; then FLATTEN_RUNNER="$STAGE0_PREV"
        elif [ -x "$HEXA_DIR/hexa" ]; then FLATTEN_RUNNER="$HEXA_DIR/hexa"; fi
        if [ -z "$FLATTEN_RUNNER" ]; then
            echo "[rebuild_stage0] no flatten runner (build/hexa_stage0 or ./hexa) — falling back" >&2
            # build_stage0.sh 가 자체 flatten 을 수행하므로 진행
        else
            # ── Flatten lock (atomic mkdir) — prevents concurrent flatten_imports ──
            FLATTEN_LOCK="/tmp/hexa_flatten.lock"
            FLATTEN_LOCK_TTL=120
            FLATTEN_GOT_LOCK=0
            if mkdir "$FLATTEN_LOCK" 2>/dev/null; then
                echo $$ > "$FLATTEN_LOCK/pid"
                FLATTEN_GOT_LOCK=1
            else
                FL_PID=$(cat "$FLATTEN_LOCK/pid" 2>/dev/null || echo "")
                FL_TIME=$(stat -f %m "$FLATTEN_LOCK" 2>/dev/null || stat -c %Y "$FLATTEN_LOCK" 2>/dev/null || echo 0)
                FL_AGE=$(( $(date +%s) - ${FL_TIME:-$(date +%s)} ))
                if [ -n "$FL_PID" ] && kill -0 "$FL_PID" 2>/dev/null && [ "$FL_AGE" -lt "$FLATTEN_LOCK_TTL" ]; then
                    echo "[rebuild_stage0] flatten: waiting for concurrent flatten (pid=$FL_PID)" >&2
                    FL_WAIT=0
                    while [ -d "$FLATTEN_LOCK" ] && [ "$FL_WAIT" -lt 60 ]; do
                        sleep 1; FL_WAIT=$((FL_WAIT + 1))
                    done
                    if [ -d "$FLATTEN_LOCK" ]; then
                        echo "[rebuild_stage0] flatten: lock wait timeout — reclaiming" >&2
                        rm -rf "$FLATTEN_LOCK"
                        mkdir "$FLATTEN_LOCK" && echo $$ > "$FLATTEN_LOCK/pid" && FLATTEN_GOT_LOCK=1
                    fi
                else
                    rm -rf "$FLATTEN_LOCK"
                    mkdir "$FLATTEN_LOCK" && echo $$ > "$FLATTEN_LOCK/pid" && FLATTEN_GOT_LOCK=1
                fi
            fi
            if [ "$FLATTEN_GOT_LOCK" = "1" ]; then
                echo "[rebuild_stage0] MISS flatten — running flatten_imports"
                HEXA_VAL_ARENA=0 "$FLATTEN_RUNNER" "$FLATTEN_SCRIPT" "$ROOT_HEXA" "$FLAT_OUT" \
                    || { rm -rf "$FLATTEN_LOCK"; false; }
                rm -rf "$FLATTEN_LOCK"
                if [ -f "$FLAT_OUT" ]; then
                    cp -f "$FLAT_OUT" "$FLAT_CACHE"
                fi
            else
                echo "[rebuild_stage0] flatten: reusing output from concurrent run" >&2
            fi
        fi
    fi
fi

# 9b. TRANSPILE — cache or re-run via hexa_v2 (skipped if runtime-only path already set HIT_TRANSPILE)
if [ "$HIT_TRANSPILE" = "0" ]; then
    if [ -z "$FORCE" ] && [ -f "$TRANSPILE_CACHE" ]; then
        cp -f "$TRANSPILE_CACHE" "$SRC_C"
        HIT_TRANSPILE=1
        echo "[rebuild_stage0] CACHE HIT transpile → $TRANSPILE_CACHE"
        # runtime.c sibling 복사 (build_stage0.sh 가 해주지만, SKIP_TRANSPILE 경로에서도 필요)
        SRC_C_DIR="$(cd "$(dirname "$SRC_C")" && pwd)"
        if [ "$SRC_C_DIR" != "$HEXA_DIR/self" ]; then
            cp -f "$RUNTIME_C" "$SRC_C_DIR/runtime.c"
        fi
    fi
fi

# ── 10. 빌드 위임 (smoke 기본 off) ──────────────────────────
[ -z "$WITH_SMOKE" ] && export NO_SMOKE=1

# SKIP_TRANSPILE 을 활용해서 hexa_v2 재실행 회피
if [ "$HIT_TRANSPILE" = "1" ] && [ -f "$SRC_C" ]; then
    export SKIP_TRANSPILE=1
fi

# pre-build backup
BACKUP=""
MIN_SIZE="${HEXA_STAGE0_MIN_SIZE:-1000000}"
if [ -x "$OUT" ]; then
    OLD_SIZE=$(wc -c < "$OUT" | tr -d ' ')
    if [ "$OLD_SIZE" -ge "$MIN_SIZE" ]; then
        BACKUP="/tmp/hexa_stage0_backup_$$"
        cp -f "$OUT" "$BACKUP"
        echo "[rebuild_stage0] backup: $BACKUP (${OLD_SIZE} bytes)"
    fi
fi

echo "[rebuild_stage0] invoking build_stage0.sh (pid=$$, flatten=$HIT_FLATTEN transpile=$HIT_TRANSPILE link=0)"
if ! bash "$HEXA_DIR/scripts/build_stage0.sh"; then
    echo "[rebuild_stage0] build FAILED" >&2
    if [ -n "$BACKUP" ] && [ -f "$BACKUP" ]; then
        cp -f "$BACKUP" "$OUT"
        echo "[rebuild_stage0] rolled back to backup" >&2
    fi
    exit 1
fi
unset SKIP_TRANSPILE

# size guard
if [ -x "$OUT" ]; then
    NEW_SIZE=$(wc -c < "$OUT" | tr -d ' ')
    if [ "$NEW_SIZE" -lt "$MIN_SIZE" ]; then
        echo "[rebuild_stage0] ERROR: output too small (${NEW_SIZE} < ${MIN_SIZE} bytes) — broken transpile?" >&2
        if [ -n "$BACKUP" ] && [ -f "$BACKUP" ]; then
            cp -f "$BACKUP" "$OUT"
            if [ "$UNAME" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
                codesign --force --sign - "$OUT" 2>/dev/null || true
            fi
            echo "[rebuild_stage0] rolled back to backup ($(wc -c < "$OUT" | tr -d ' ') bytes)" >&2
        fi
        rm -f "$BACKUP"
        exit 1
    fi
fi
rm -f "$BACKUP"

# ── 11. 캐시 저장 — 빌드 성공 후에만 ───────────────────────
#    다음 세션에 같은 key 면 cp 만으로 복원.
if [ -f "$SRC_C" ] && [ ! -f "$TRANSPILE_CACHE" ]; then
    cp -f "$SRC_C" "$TRANSPILE_CACHE" 2>/dev/null || :
fi
if [ -f "$FLAT_OUT" ] && [ ! -f "$FLAT_CACHE" ]; then
    cp -f "$FLAT_OUT" "$FLAT_CACHE" 2>/dev/null || :
fi
if [ -x "$OUT" ]; then
    cp -f "$OUT" "$LINK_CACHE_BIN" 2>/dev/null || :
fi

# ── 11b. Per-file manifest update ─────────────────────────────────────
#    Store per-file sha256 hashes for the next run's incremental diff.
#    This enables the per-file change detection in section 5b.
write_manifest "${ALL_BUILD_INPUTS[@]}"
echo "[rebuild_stage0] manifest updated: ${#ALL_BUILD_INPUTS[@]} files → $MANIFEST_FILE"

# meta / legacy hash file 갱신
CHANGED_LIST=""
if [ "${#CHANGED_FILES[@]}" -gt 0 ]; then
    CHANGED_LIST=$(printf '%s' "${CHANGED_FILES[@]##*/}" | tr ' ' ',')
fi
cat > "$CACHE_META_DIR/last_build.json" <<EOF
{"flatten_key":"$FLATTEN_KEY","transpile_key":"$TRANSPILE_KEY","link_key":"$LINK_KEY","hit":{"flatten":$HIT_FLATTEN,"transpile":$HIT_TRANSPILE,"link":$HIT_LINK},"uname":"$UNAME","incremental":{"runtime_only":$RUNTIME_ONLY_CHANGED,"changed_files":"$CHANGED_LIST","changed_count":${#CHANGED_FILES[@]}}}
EOF
mkdir -p "$HEXA_DIR/build"
cat "$ROOT_HEXA" "$RUNTIME_C" 2>/dev/null | sha256_cmd | cut -d' ' -f1 \
    > "$HEXA_DIR/build/.rebuild_hash" || :

echo "[rebuild_stage0] cache updated → flatten=$FLATTEN_KEY transpile=$TRANSPILE_KEY link=$LINK_KEY"
echo "[rebuild_stage0] OK"
