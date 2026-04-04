#!/usr/bin/env bash
set -euo pipefail

GROWTH_NAME="hexa-lang"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOMAIN="programming_language"
MAX_CYCLES=${MAX_CYCLES:-${1:-999}}
INTERVAL=${INTERVAL:-${2:-1800}}
DRY_RUN="${3:-false}"

COMMON="$HOME/Dev/nexus6/scripts/lib/growth_common.sh"
source "$COMMON"

domain_phases() {
    local cycle="$1" load="$2"
    local cargo="${HOME}/.cargo/bin/cargo"

    # Phase 1: 키워드 구현 상태
    log_info "Phase 1: Keyword implementation"
    local token_file="$PROJECT_ROOT/src/token.rs"
    if [ -f "$token_file" ]; then
        local kw_count
        kw_count=$(grep -c '"[a-z]*" => Some(Token::' "$token_file" 2>/dev/null || echo 0)
        log_info "  Keywords in token.rs: $kw_count"
        if [ -f "$PROJECT_ROOT/src/parser.rs" ]; then
            local parser_refs
            parser_refs=$(grep -c 'Token::' "$PROJECT_ROOT/src/parser.rs" 2>/dev/null || echo 0)
            log_info "  Parser token refs: $parser_refs"
        fi
        write_growth_bus "keywords" "ok" "count=$kw_count"
    else
        log_info "  token.rs not found"
        write_growth_bus "keywords" "skip" "no_token_rs"
    fi

    # Phase 2: 연산자 커버리지
    log_info "Phase 2: Operator coverage"
    local test_dir="$PROJECT_ROOT/examples"
    if [ -d "$test_dir" ]; then
        local test_count
        test_count=$(ls "$test_dir"/test_*.hexa 2>/dev/null | wc -l | tr -d ' ')
        log_info "  Test files: $test_count"
        local op_hits=0
        for sym in "+" "-" "*" "/" "%" "==" "!=" "<=" ">=" "&&" "||"; do
            if grep -q "$(printf '%s' "$sym" | sed 's/[.*+?^${}()|[\]\\]/\\&/g')" "$test_dir"/*.hexa 2>/dev/null; then
                op_hits=$((op_hits + 1))
            fi
        done
        log_info "  Operators tested: $op_hits/11"
        write_growth_bus "operators" "ok" "tests=$test_count,ops=$op_hits"
    else
        log_info "  examples/ not found"
        write_growth_bus "operators" "skip" "no_examples"
    fi

    [ "$load" = "LIGHT" ] && return 0

    # Phase 3: 파서 상태 (cargo test)
    log_info "Phase 3: Parser health"
    if [ -f "$PROJECT_ROOT/Cargo.toml" ] && [ -x "$cargo" ]; then
        local test_output
        test_output=$("$cargo" test --manifest-path "$PROJECT_ROOT/Cargo.toml" -- --quiet 2>&1 || true)
        local passed failed
        passed=$(echo "$test_output" | grep -o '[0-9]* passed' | head -1 | grep -o '[0-9]*' || echo 0)
        failed=$(echo "$test_output" | grep -o '[0-9]* failed' | head -1 | grep -o '[0-9]*' || echo 0)
        log_info "  Tests: ${passed} passed, ${failed} failed"
        write_growth_bus "parser_health" "ok" "passed=$passed,failed=$failed"
    else
        log_info "  No Cargo.toml or cargo"
        write_growth_bus "parser_health" "skip" "no_cargo"
    fi

    # Phase 4: 코드젠 완성도
    log_info "Phase 4: Codegen completeness"
    local backends="wasm codegen_wgsl codegen_esp32 codegen_verilog jit vm"
    local found_backends=0 total_backends=0
    for backend in $backends; do
        total_backends=$((total_backends + 1))
        local f="$PROJECT_ROOT/src/${backend}.rs"
        if [ -f "$f" ]; then
            found_backends=$((found_backends + 1))
            local lines todos
            lines=$(wc -l < "$f" | tr -d ' ')
            todos=$(grep -ci 'todo\|unimplemented' "$f" 2>/dev/null || echo 0)
            log_info "  $backend: ${lines} lines, ${todos} TODOs"
        else
            log_info "  $backend: MISSING"
        fi
    done
    write_growth_bus "codegen" "ok" "found=$found_backends/$total_backends"

    # Phase 5: REPL 기능 체크
    log_info "Phase 5: REPL features"
    local main_file="$PROJECT_ROOT/src/main.rs"
    if [ -f "$main_file" ]; then
        local features="completion history multiline highlight type_info help benchmark"
        local feat_found=0 feat_total=0
        for feat in $features; do
            feat_total=$((feat_total + 1))
            if grep -qi "$feat" "$main_file" "$PROJECT_ROOT/src/interpreter.rs" "$PROJECT_ROOT/src/lsp.rs" 2>/dev/null; then
                feat_found=$((feat_found + 1))
            fi
        done
        log_info "  REPL features: $feat_found/$feat_total"
        write_growth_bus "repl" "ok" "features=$feat_found/$feat_total"
    else
        log_info "  main.rs not found"
        write_growth_bus "repl" "skip" "no_main"
    fi

    # Phase 6: 성장 스캔
    log_info "Phase 6: Growth scan"
    if [ -f "$PROJECT_ROOT/.growth/scan.py" ]; then
        python3 "$PROJECT_ROOT/.growth/scan.py" 2>&1 | tail -10 | while IFS= read -r line; do
            log_info "  $line"
        done
        write_growth_bus "growth_scan" "ok" "scanned"
    else
        log_info "  scan.py not found"
        write_growth_bus "growth_scan" "skip" "no_scanner"
    fi
}

run_growth_loop "domain_phases"
