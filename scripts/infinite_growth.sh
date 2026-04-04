#!/bin/bash
# HEXA-LANG Infinite Growth Daemon — 10-phase loop
# bash 3.2 compatible, no bashisms
# Usage: bash scripts/infinite_growth.sh [--max-cycles N] [--interval SECS]

set -e
source "$(cd "$(dirname "$0")" && pwd)/lib/growth_common.sh"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GROWTH_DIR="${REPO_ROOT}/.growth"
STATE_FILE="${GROWTH_DIR}/growth_state.json"
LOG_FILE="${GROWTH_DIR}/growth.log"
GROWTH_BUS="${HOME}/Dev/nexus6/shared/growth_bus.jsonl"
CARGO="${HOME}/.cargo/bin/cargo"

MAX_CYCLES=999
INTERVAL=60
CYCLE=0

# Parse args (bash 3.2 safe)
while [ $# -gt 0 ]; do
    case "$1" in
        --max-cycles) MAX_CYCLES="$2"; shift 2 ;;
        --interval)   INTERVAL="$2"; shift 2 ;;
        --once)       MAX_CYCLES=1; shift ;;
        *)            shift ;;
    esac
done

mkdir -p "${GROWTH_DIR}"
mkdir -p "$(dirname "${GROWTH_BUS}")" 2>/dev/null || true

log() {
    local msg="[$(date -u '+%Y-%m-%d %H:%M:%S')] $1"
    echo "$msg"
    echo "$msg" >> "${LOG_FILE}"
}

emit_bus() {
    local event="$1"
    local data="$2"
    local ts
    ts="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "{\"ts\":\"${ts}\",\"repo\":\"hexa-lang\",\"event\":\"${event}\",\"data\":${data}}" >> "${GROWTH_BUS}" 2>/dev/null || true
}

update_state_cycle() {
    # Update cycle count in state file using python3
    python3 -c "
import json, sys
from datetime import datetime, timezone
f = '${STATE_FILE}'
try:
    with open(f) as fh: s = json.load(fh)
except: s = {}
s['cycle_count'] = ${CYCLE}
s['last_cycle'] = datetime.now(timezone.utc).isoformat()
h = s.get('health_history', [])
if s.get('last_scan') and s['last_scan'].get('health_score') is not None:
    score = s['last_scan']['health_score']
    h.append({'cycle': ${CYCLE}, 'score': score, 'ts': datetime.now(timezone.utc).isoformat()})
    if len(h) > 100: h = h[-100:]
    s['health_history'] = h
with open(f, 'w') as fh: json.dump(s, fh, indent=2, ensure_ascii=False); fh.write('\n')
" 2>/dev/null || true
}

# ══════════════════════════════════════════════════
#  Phase functions
# ══════════════════════════════════════════════════

phase_1_keywords() {
    log "Phase 1: Keyword implementation status"
    local token_file="${REPO_ROOT}/src/token.rs"
    if [ ! -f "${token_file}" ]; then
        log "  WARN: token.rs not found"
        return
    fi
    local kw_count
    kw_count=$(grep -c '"[a-z]*" => Some(Token::' "${token_file}" 2>/dev/null || echo 0)
    log "  Keywords in token.rs: ${kw_count}/53"

    # Check parser usage
    local parser_file="${REPO_ROOT}/src/parser.rs"
    if [ -f "${parser_file}" ]; then
        local parser_refs
        parser_refs=$(grep -c 'Token::' "${parser_file}" 2>/dev/null || echo 0)
        log "  Parser token refs: ${parser_refs}"
    fi
}

phase_2_operators() {
    log "Phase 2: Operator coverage"
    local test_dir="${REPO_ROOT}/examples"
    if [ ! -d "${test_dir}" ]; then
        log "  WARN: examples/ not found"
        return
    fi
    local test_count
    test_count=$(ls "${test_dir}"/test_*.hexa 2>/dev/null | wc -l | tr -d ' ')
    log "  Test files: ${test_count}"

    # Check operator symbols in test files
    local op_hits=0
    for sym in "+" "-" "*" "/" "%" "==" "!=" "<=" ">=" "&&" "||"; do
        if grep -q "$(printf '%s' "${sym}" | sed 's/[.*+?^${}()|[\]\\]/\\&/g')" "${test_dir}"/*.hexa 2>/dev/null; then
            op_hits=$((op_hits + 1))
        fi
    done
    log "  Operators tested (basic): ${op_hits}/11"
}

phase_3_parser_health() {
    log "Phase 3: Parser health (cargo test)"
    if [ -f "${REPO_ROOT}/Cargo.toml" ] && [ -x "${CARGO}" ]; then
        local test_output
        test_output=$("${CARGO}" test --manifest-path "${REPO_ROOT}/Cargo.toml" -- --quiet 2>&1 || true)
        local passed
        passed=$(echo "${test_output}" | grep -o '[0-9]* passed' | head -1 | grep -o '[0-9]*' || echo 0)
        local failed
        failed=$(echo "${test_output}" | grep -o '[0-9]* failed' | head -1 | grep -o '[0-9]*' || echo 0)
        log "  Tests: ${passed} passed, ${failed} failed"
        emit_bus "test" "{\"passed\":${passed:-0},\"failed\":${failed:-0}}"
    else
        log "  SKIP: No Cargo.toml or cargo not found"
    fi
}

phase_4_nexus6_scan() {
    log "Phase 4: NEXUS-6 scan"
    local n6_script="${HOME}/Dev/nexus6/scripts/n6.py"
    if [ -f "${n6_script}" ]; then
        python3 "${n6_script}" scan --repo "${REPO_ROOT}" 2>/dev/null | tail -5 | while IFS= read -r line; do
            log "  ${line}"
        done
    else
        # Fallback: scan src/ for n=6 patterns
        local n6_hits=0
        if [ -d "${REPO_ROOT}/src" ]; then
            n6_hits=$(grep -rl 'n.*=.*6\|sigma\|phi(6)\|tau(6)\|J2\|egyptian\|perfect.*number' "${REPO_ROOT}/src/" 2>/dev/null | wc -l | tr -d ' ')
        fi
        log "  n=6 pattern files: ${n6_hits}"
    fi
}

phase_5_codegen() {
    log "Phase 5: Codegen completeness"
    local backends="wasm codegen_wgsl codegen_esp32 codegen_verilog jit vm"
    for backend in ${backends}; do
        local f="${REPO_ROOT}/src/${backend}.rs"
        if [ -f "${f}" ]; then
            local lines
            lines=$(wc -l < "${f}" | tr -d ' ')
            local todos
            todos=$(grep -ci 'todo\|unimplemented' "${f}" 2>/dev/null || echo 0)
            log "  ${backend}: ${lines} lines, ${todos} TODOs"
        else
            log "  ${backend}: MISSING"
        fi
    done
}

phase_6_growth_scan() {
    log "Phase 6: Growth scan (Python)"
    if [ -f "${GROWTH_DIR}/scan.py" ]; then
        python3 "${GROWTH_DIR}/scan.py" 2>&1 | tail -10 | while IFS= read -r line; do
            log "  ${line}"
        done
    else
        log "  WARN: scan.py not found"
    fi
}

phase_7_repl_features() {
    log "Phase 7: REPL feature check"
    local main_file="${REPO_ROOT}/src/main.rs"
    if [ ! -f "${main_file}" ]; then
        log "  WARN: main.rs not found"
        return
    fi
    local features="completion history multiline highlight type_info help benchmark"
    local found=0
    local total=0
    for feat in ${features}; do
        total=$((total + 1))
        if grep -qi "${feat}" "${main_file}" "${REPO_ROOT}/src/interpreter.rs" "${REPO_ROOT}/src/lsp.rs" 2>/dev/null; then
            found=$((found + 1))
        fi
    done
    log "  REPL features: ${found}/${total}"
}

phase_8_nexus6_sync() {
    log "Phase 8: NEXUS-6 sync"
    local sync_script="${HOME}/Dev/nexus6/sync/sync-all.sh"
    if [ -f "${sync_script}" ]; then
        bash "${sync_script}" 2>/dev/null | tail -3 | while IFS= read -r line; do
            log "  ${line}"
        done
    else
        log "  SKIP: nexus6 sync not available"
    fi
}

phase_9_growth_tick() {
    log "Phase 9: Growth tick"
    update_state_cycle
    local health="?"
    if [ -f "${STATE_FILE}" ]; then
        health=$(python3 -c "
import json
try:
    with open('${STATE_FILE}') as f: s = json.load(f)
    ls = s.get('last_scan')
    if ls and ls.get('health_score') is not None:
        print(ls['health_score'])
    else:
        print('?')
except: print('?')
" 2>/dev/null || echo "?")
    fi
    log "  Cycle ${CYCLE} complete | Health: ${health}"
    emit_bus "growth_tick" "{\"cycle\":${CYCLE},\"health\":\"${health}\"}"
}

phase_10_auto_commit() {
    log "Phase 10: Auto-commit check"
    cd "${REPO_ROOT}"
    local changed
    changed=$(git diff --stat HEAD 2>/dev/null | wc -l | tr -d ' ')
    if [ "${changed}" -gt 0 ]; then
        # Only auto-commit growth state files
        if git diff --name-only HEAD 2>/dev/null | grep -q '.growth/'; then
            git add .growth/ 2>/dev/null || true
            git commit -m "growth(hexa-lang): cycle ${CYCLE} auto-scan" 2>/dev/null || true
            git push origin main 2>/dev/null || true
            log "  Committed growth state (cycle ${CYCLE})"
        else
            log "  Changes detected but not in .growth/ — skipping auto-commit"
        fi
    else
        log "  No changes to commit"
    fi
}

# ══════════════════════════════════════════════════
#  Main loop
# ══════════════════════════════════════════════════

log "========================================="
log "HEXA-LANG Infinite Growth Daemon"
log "Max cycles: ${MAX_CYCLES}, Interval: ${INTERVAL}s"
log "========================================="

emit_bus "daemon_start" "{\"max_cycles\":${MAX_CYCLES},\"interval\":${INTERVAL}}"

while [ "${CYCLE}" -lt "${MAX_CYCLES}" ]; do
    CYCLE=$((CYCLE + 1))
    log ""
    log "═══ Cycle ${CYCLE}/${MAX_CYCLES} ═══"

    phase_1_keywords
    phase_2_operators
    phase_3_parser_health
    phase_4_nexus6_scan
    phase_5_codegen
    phase_6_growth_scan
    phase_7_repl_features
    phase_8_nexus6_sync
    phase_9_growth_tick
    phase_10_auto_commit

    # ── Common Phases (paper loop, doc update, domain explore, emergence, bus sync) ──
    run_common_phases "hexa-lang" $cycle

    log "═══ Cycle ${CYCLE} done ═══"

    if [ "${CYCLE}" -lt "${MAX_CYCLES}" ]; then
        log "Sleeping ${INTERVAL}s..."
        sleep "${INTERVAL}"
    fi
done

log "========================================="
log "Growth daemon complete: ${CYCLE} cycles"
log "========================================="
emit_bus "daemon_stop" "{\"total_cycles\":${CYCLE}}"
