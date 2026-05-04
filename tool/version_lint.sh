#!/usr/bin/env bash
# tool/version_lint.sh — F-VERSION-1 enforcer for stdlib module governance.
#
# Spec ref: docs/hexa_lang_module_versioning_spec_2026_05_03.md (proc.hexa cite).
# Enforces:
#   (a) every stdlib/*.hexa (non-test) declares `@version <semver>` + `@capabilities [...]`.
#   (b) if `@version` is unchanged since the last git tag (or HEAD~1 fallback)
#       AND the public surface (`pub fn` / `fn` / `struct` / `let ` top-level)
#       changed, error — caller must bump major.
#
# Heuristic detail (b): we extract the module's "public surface signature
# set" — sorted unique exported declaration names — at HEAD vs base commit.
# If the set differs (any add/rename/remove) and `@version` is byte-identical
# in both revisions, lint fails. Type/arity changes inside a kept name are
# NOT detected — caller responsibility (per proc.hexa governance comment).
#
# Usage:
#   tool/version_lint.sh              # check all stdlib (HEAD vs last tag)
#   tool/version_lint.sh --base REV   # diff against an explicit git rev
#   tool/version_lint.sh --selftest   # internal self-check
#   tool/version_lint.sh PATH...      # check a specific file list
#
# Exit codes:
#   0 = pass     1 = lint violation     2 = bad usage / internal error
#
# raw#9 hexa-only spirit: the linter itself is shell because it must run in
# pre-commit / CI before the hexa toolchain is necessarily built (bootstrap
# concern). Body of work is `git`/`grep`/`awk` — no python/jq.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

MODE="check"
BASE_REV=""
EXPLICIT_FILES=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --selftest) MODE="selftest"; shift;;
        --base) BASE_REV="$2"; shift 2;;
        --base=*) BASE_REV="${1#--base=}"; shift;;
        -h|--help)
            sed -n '2,28p' "$0"; exit 0;;
        *) EXPLICIT_FILES+=("$1"); shift;;
    esac
done

# Determine base revision for diff. Last annotated tag, then any tag, then HEAD~1.
if [[ -z "$BASE_REV" ]]; then
    BASE_REV="$(git describe --tags --abbrev=0 2>/dev/null || true)"
    if [[ -z "$BASE_REV" ]]; then
        BASE_REV="$(git rev-parse --verify --quiet HEAD~1 || true)"
    fi
fi

# Module list: explicit args, or every non-test stdlib/*.hexa.
list_modules() {
    if [[ ${#EXPLICIT_FILES[@]} -gt 0 ]]; then
        printf '%s\n' "${EXPLICIT_FILES[@]}"
    else
        find stdlib -type f -name '*.hexa' \
            ! -path '*/test/*' \
            ! -name '*_test.hexa' \
            ! -name 'test_*.hexa' \
            | sort
    fi
}

# Extract `@version X.Y.Z` value (first occurrence). Empty if absent.
extract_version() {
    local content="$1"
    printf '%s\n' "$content" | grep -E '^[[:space:]]*//[[:space:]]*@version[[:space:]]' \
        | head -n1 \
        | sed -E 's|^[[:space:]]*//[[:space:]]*@version[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*|\1|'
}

# Has `@capabilities [ ... ]`?
has_capabilities() {
    local content="$1"
    printf '%s\n' "$content" \
        | grep -qE '^[[:space:]]*//[[:space:]]*@capabilities[[:space:]]*\['
}

# Public surface signature: sorted unique top-level declaration names.
# Captures: `pub fn NAME`, `fn NAME` (top-level), `struct NAME`, `let NAME`.
# Underscore-prefixed names are private convention -> excluded.
public_surface() {
    local content="$1"
    printf '%s\n' "$content" | awk '
        /^pub[[:space:]]+fn[[:space:]]+/ {
            sub(/^pub[[:space:]]+fn[[:space:]]+/, "");
            n = $0; sub(/[(<[:space:]].*/, "", n);
            if (n !~ /^_/ && n != "") print "fn:" n;
        }
        /^fn[[:space:]]+/ {
            sub(/^fn[[:space:]]+/, "");
            n = $0; sub(/[(<[:space:]].*/, "", n);
            if (n !~ /^_/ && n != "") print "fn:" n;
        }
        /^struct[[:space:]]+/ {
            sub(/^struct[[:space:]]+/, "");
            n = $0; sub(/[[:space:]{].*/, "", n);
            if (n !~ /^_/ && n != "") print "struct:" n;
        }
        /^let[[:space:]]+/ {
            sub(/^let[[:space:]]+/, "");
            sub(/^mut[[:space:]]+/, "");
            n = $0; sub(/[[:space:]=:].*/, "", n);
            if (n !~ /^_/ && n != "") print "let:" n;
        }
    ' | sort -u
}

# Read base-rev content (empty string if file absent at base).
read_base_content() {
    local path="$1"
    if [[ -z "$BASE_REV" ]]; then
        printf ''
        return
    fi
    git show "$BASE_REV:$path" 2>/dev/null || printf ''
}

violations=0
checked=0

check_file() {
    local f="$1"
    checked=$((checked + 1))
    local head_content
    head_content="$(cat "$f")"

    # (a) presence checks
    local v
    v="$(extract_version "$head_content" || true)"
    if [[ -z "$v" ]]; then
        printf 'VERSION_LINT FAIL %s missing @version header\n' "$f" >&2
        violations=$((violations + 1))
        return
    fi
    if ! has_capabilities "$head_content"; then
        printf 'VERSION_LINT FAIL %s missing @capabilities header\n' "$f" >&2
        violations=$((violations + 1))
        return
    fi

    # (b) drift check vs base rev
    if [[ -n "$BASE_REV" ]]; then
        local base_content
        base_content="$(read_base_content "$f")"
        if [[ -n "$base_content" ]]; then
            local v_base
            v_base="$(extract_version "$base_content" || true)"
            if [[ -n "$v_base" && "$v" == "$v_base" ]]; then
                local s_head s_base
                s_head="$(public_surface "$head_content")"
                s_base="$(public_surface "$base_content")"
                if [[ "$s_head" != "$s_base" ]]; then
                    printf 'VERSION_LINT FAIL %s public surface changed since %s but @version still %s — bump major.\n' \
                        "$f" "$BASE_REV" "$v" >&2
                    diff <(printf '%s' "$s_base") <(printf '%s' "$s_head") | sed 's/^/    /' >&2 || true
                    violations=$((violations + 1))
                fi
            fi
        fi
    fi
}

selftest() {
    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' EXIT
    # missing version → fail expected
    cat > "$tmpdir/no_version.hexa" <<EOF
// stdlib/no_version.hexa — no version header
pub fn foo() {}
EOF
    BASE_REV="" check_file "$tmpdir/no_version.hexa" 2>/dev/null || true
    if [[ "$violations" -lt 1 ]]; then
        echo "[version_lint selftest] FAIL: missing-version case did not error" >&2
        return 1
    fi
    violations=0
    # has version + capabilities → pass
    cat > "$tmpdir/ok.hexa" <<EOF
// stdlib/ok.hexa
// @version 0.1.0
// @capabilities [pure]
pub fn foo() {}
EOF
    BASE_REV="" check_file "$tmpdir/ok.hexa"
    if [[ "$violations" -ne 0 ]]; then
        echo "[version_lint selftest] FAIL: ok-case errored unexpectedly ($violations)" >&2
        return 1
    fi
    # public surface extraction
    local got
    got="$(public_surface "$(cat <<EOF
pub fn alpha(x) {}
fn _private(x) {}
fn beta(y) {}
struct Probe { a: int }
let TOP = 42
EOF
)")"
    local want
    want="$(printf 'fn:alpha\nfn:beta\nlet:TOP\nstruct:Probe')"
    if [[ "$got" != "$want" ]]; then
        echo "[version_lint selftest] FAIL: public_surface mismatch" >&2
        echo "got:" >&2; printf '%s\n' "$got" >&2
        echo "want:" >&2; printf '%s\n' "$want" >&2
        return 1
    fi
    echo "[version_lint selftest] OK"
    return 0
}

if [[ "$MODE" == "selftest" ]]; then
    selftest
    exit $?
fi

while IFS= read -r f; do
    [[ -z "$f" ]] && continue
    check_file "$f"
done < <(list_modules)

if [[ "$violations" -gt 0 ]]; then
    printf 'VERSION_LINT: %d violation(s) across %d module(s) (base=%s)\n' \
        "$violations" "$checked" "${BASE_REV:-<none>}" >&2
    exit 1
fi

printf 'VERSION_LINT: OK — %d module(s) checked (base=%s)\n' \
    "$checked" "${BASE_REV:-<none>}"
exit 0
