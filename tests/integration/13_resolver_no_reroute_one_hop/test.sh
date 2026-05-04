#!/usr/bin/env bash
# 13_resolver_no_reroute_one_hop — FIX-5 regression test.
#
# Verifies the FIX-5 (2026-05-04) NO_REROUTE foot-gun fix in ~/.hx/bin/hexa:
#
#   A. Resolver emits a one-line stderr note when honoring NO_REROUTE
#      (`# resolver: NO_REROUTE honored, executing $REAL_HEXA directly`).
#   B. The bypass is scoped to one hop — `HEXA_RESOLVER_NO_REROUTE` is
#      `unset` in the env before `exec`'ing $REAL_HEXA, so any nested
#      `hexa` invocation re-enters the resolver instead of inheriting the
#      bypass silently.
#
# Without the fix, a script invoked as
#   `HEXA_RESOLVER_NO_REROUTE=1 hexa parent_script.hexa`
# would propagate NO_REROUTE=1 through every descendant `hexa` call,
# silently disabling routing for arbitrary depth (gotcha for any pattern
# where a hexa script shells out to itself or the test infra — e.g.
# tool/spec_runner.hexa::run_one).
#
# Test method: black-box the resolver by invoking it directly as a shell
# script with a dummy $REAL_HEXA that just dumps its env. We verify:
#   T1  one-shot bypass logs NO_REROUTE-honored on stderr (Fix A)
#   T2  bypass var is unset in REAL_HEXA's env (Fix B — one-hop)
#   T3  STICKY=1 opt-out preserves the var (legacy semantics)
#   T4  no NO_REROUTE set → no honored-log (default path uncontaminated)
#
# Skip rules:
#   - rc=77 if ~/.hx/bin/hexa not present (resolver not installed)

set -u

RESOLVER="${HEXA_RESOLVER_BIN:-$HOME/.hx/bin/hexa}"
if [ ! -x "$RESOLVER" ]; then
    echo "SKIP: resolver not present at $RESOLVER"
    exit 77
fi

WORK="$(mktemp -d -t hexa_fix5.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

# Dummy REAL_HEXA — dump argv + relevant env vars and exit 0.
# Stamps a sentinel line `__REAL_HEXA__` so the test can confirm the
# resolver actually exec'd this binary (vs taking some other code path).
cat > "$WORK/fake_real" <<'EOF'
#!/usr/bin/env bash
echo "__REAL_HEXA__"
echo "ARGV: $*"
echo "NO_REROUTE_AT_REAL=${HEXA_RESOLVER_NO_REROUTE:-<unset>}"
echo "STICKY_AT_REAL=${HEXA_RESOLVER_NO_REROUTE_STICKY:-<unset>}"
EOF
chmod +x "$WORK/fake_real"

run_resolver() {
    # Strip env to keep test deterministic. Inherit only PATH/HOME plus
    # the bypass vars we explicitly want to set per-case.
    env -i \
        PATH="$PATH" HOME="$HOME" \
        HEXA_RESOLVER_REAL_BIN="$WORK/fake_real" \
        "$@" \
        "$RESOLVER" --version 2>&1
}

PASS=0
FAIL=0
results=""

# ── T1 — Fix A: NO_REROUTE=1 logs honored note on stderr ───────────
out_t1="$(run_resolver HEXA_RESOLVER_NO_REROUTE=1 || true)"
if echo "$out_t1" | grep -q "resolver: NO_REROUTE honored"; then
    if echo "$out_t1" | grep -q "__REAL_HEXA__"; then
        echo "  PASS T1_log_emitted_when_honored"
        PASS=$((PASS+1)); results="$results,T1=PASS"
    else
        echo "  FAIL T1 — log present but REAL_HEXA never exec'd"
        echo "  --- output ---"; echo "$out_t1" | sed 's/^/    /'
        FAIL=$((FAIL+1)); results="$results,T1=FAIL"
    fi
else
    echo "  FAIL T1 — expected 'resolver: NO_REROUTE honored' on stderr"
    echo "  --- output ---"; echo "$out_t1" | sed 's/^/    /'
    FAIL=$((FAIL+1)); results="$results,T1=FAIL"
fi

# ── T2 — Fix B: NO_REROUTE is UNSET in $REAL_HEXA's env (one-hop) ──
# This is the "real" foot-gun fix. Caller sets the var; resolver must
# strip it before exec so a nested `hexa` invocation from inside the
# bypassed binary re-enters the resolver normally.
out_t2="$(run_resolver HEXA_RESOLVER_NO_REROUTE=1 || true)"
no_reroute_at_real="$(echo "$out_t2" | sed -n 's/^NO_REROUTE_AT_REAL=//p')"
if [ "$no_reroute_at_real" = "<unset>" ]; then
    echo "  PASS T2_one_hop_scope (NO_REROUTE unset before exec REAL)"
    PASS=$((PASS+1)); results="$results,T2=PASS"
else
    echo "  FAIL T2 — expected NO_REROUTE_AT_REAL=<unset>, got '$no_reroute_at_real'"
    echo "  --- output ---"; echo "$out_t2" | sed 's/^/    /'
    FAIL=$((FAIL+1)); results="$results,T2=FAIL"
fi

# ── T3 — STICKY=1 opt-out preserves legacy inherit-forever semantics ──
out_t3="$(run_resolver HEXA_RESOLVER_NO_REROUTE=1 HEXA_RESOLVER_NO_REROUTE_STICKY=1 || true)"
no_reroute_at_real="$(echo "$out_t3" | sed -n 's/^NO_REROUTE_AT_REAL=//p')"
sticky_at_real="$(echo "$out_t3" | sed -n 's/^STICKY_AT_REAL=//p')"
if [ "$no_reroute_at_real" = "1" ] && [ "$sticky_at_real" = "1" ]; then
    echo "  PASS T3_sticky_opt_out (NO_REROUTE preserved when STICKY=1)"
    PASS=$((PASS+1)); results="$results,T3=PASS"
else
    echo "  FAIL T3 — expected NO_REROUTE_AT_REAL=1 + STICKY_AT_REAL=1, got '$no_reroute_at_real' / '$sticky_at_real'"
    echo "  --- output ---"; echo "$out_t3" | sed 's/^/    /'
    FAIL=$((FAIL+1)); results="$results,T3=FAIL"
fi

# ── T4 — no NO_REROUTE set → no honored-log emitted ───────────────
# Sanity: confirm the log only fires on the bypass path. Use FORCE_DOWN
# + a non-existent docker bin to keep the resolver from actually
# trying to exec docker; we only need stderr-grep up to the moment the
# resolver decides routing.
out_t4="$(env -i PATH="$PATH" HOME="$HOME" \
    HEXA_RESOLVER_REAL_BIN="$WORK/fake_real" \
    HEXA_RESOLVER_FORCE_DOWN=1 \
    HEXA_DOCKER_BIN=/nonexistent/docker \
    "$RESOLVER" --version 2>&1 || true)"
if echo "$out_t4" | grep -q "NO_REROUTE honored"; then
    echo "  FAIL T4 — log fired on default path (false positive)"
    echo "  --- output ---"; echo "$out_t4" | sed 's/^/    /'
    FAIL=$((FAIL+1)); results="$results,T4=FAIL"
else
    echo "  PASS T4_log_not_fired_on_default_path"
    PASS=$((PASS+1)); results="$results,T4=PASS"
fi

echo "[13_resolver_no_reroute_one_hop] results: PASS=$PASS FAIL=$FAIL ($results)"
if [ $FAIL -ne 0 ]; then
    exit 1
fi
echo "PASS: 4/4 FIX-5 NO_REROUTE foot-gun fix verified"
exit 0
