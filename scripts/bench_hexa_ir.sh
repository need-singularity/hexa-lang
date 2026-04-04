#!/usr/bin/env bash
# bench_hexa_ir.sh — HEXA-IR performance benchmark harness
# Runs bench_loop across all available backends + C/Rust ceilings
# Appends results to docs/hexa-ir-benchmark.md
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCH_HEXA="$PROJECT_DIR/examples/bench_loop.hexa"
BENCH_C="$PROJECT_DIR/examples/bench_loop_native.c"
BENCH_DOC="$PROJECT_DIR/docs/hexa-ir-benchmark.md"
TMP_DIR="$PROJECT_DIR/target/bench_tmp"
EXPECTED="677900000"

mkdir -p "$TMP_DIR"

echo "==================================================="
echo "  HEXA-IR Benchmark Harness"
echo "  $(date '+%Y-%m-%d %H:%M:%S')"
echo "==================================================="
echo ""

# --- Step 1: Build release ---
echo "[1/6] Building release..."
cd "$PROJECT_DIR"
if cargo build --release 2>&1; then
    cp target/release/hexa . 2>/dev/null || true
    echo "  -> Build OK"
else
    echo "  -> Build FAILED (continuing with existing binary)"
fi
echo ""

# --- Helper: time a command, return seconds + correctness ---
# Writes: <seconds> to stdout, PASS/MISMATCH/ERROR to $1_status_file
run_bench() {
    local status_file="$1"
    shift
    local start end elapsed output
    start=$(python3 -c 'import time; print(time.time())')
    output=$("$@" 2>&1) || true
    end=$(python3 -c 'import time; print(time.time())')
    elapsed=$(python3 -c "print(f'{$end - $start:.3f}')")
    echo "$elapsed"
    local first_line
    first_line=$(echo "$output" | head -1 | tr -d '[:space:]')
    if [ "$first_line" = "$EXPECTED" ]; then
        echo "PASS" > "$status_file"
    else
        echo "MISMATCH(got:$first_line)" > "$status_file"
    fi
}

# Result storage (simple variables, bash 3 compatible)
t_interp="N/A"; s_interp="SKIP"
t_cranelift="N/A"; s_cranelift="SKIP"
t_hexair="N/A"; s_hexair="SKIP"
t_c="N/A"; s_c="SKIP"
t_rust="N/A"; s_rust="SKIP"

# --- Step 2: Interpreter ---
echo "[2/6] Interpreter..."
if [ -x "$PROJECT_DIR/hexa" ]; then
    t_interp=$(run_bench "$TMP_DIR/interp_status" "$PROJECT_DIR/hexa" "$BENCH_HEXA")
    s_interp=$(cat "$TMP_DIR/interp_status")
    echo "  -> ${t_interp}s (${s_interp})"
else
    echo "  -> SKIPPED (no hexa binary)"
fi
echo ""

# --- Step 3: Cranelift JIT ---
echo "[3/6] Cranelift JIT (--native)..."
if [ -x "$PROJECT_DIR/hexa" ]; then
    # Test if --native flag is accepted
    if "$PROJECT_DIR/hexa" --native "$BENCH_HEXA" >/dev/null 2>&1; then
        t_cranelift=$(run_bench "$TMP_DIR/crank_status" "$PROJECT_DIR/hexa" "--native" "$BENCH_HEXA")
        s_cranelift=$(cat "$TMP_DIR/crank_status")
        echo "  -> ${t_cranelift}s (${s_cranelift})"
    else
        echo "  -> SKIPPED (--native not available or failed)"
    fi
else
    echo "  -> SKIPPED (no hexa binary)"
fi
echo ""

# --- Step 4: HEXA-IR ---
echo "[4/6] HEXA-IR (--hexa-ir)..."
if [ -x "$PROJECT_DIR/hexa" ]; then
    if "$PROJECT_DIR/hexa" --hexa-ir "$BENCH_HEXA" >/dev/null 2>&1; then
        t_hexair=$(run_bench "$TMP_DIR/ir_status" "$PROJECT_DIR/hexa" "--hexa-ir" "$BENCH_HEXA")
        s_hexair=$(cat "$TMP_DIR/ir_status")
        echo "  -> ${t_hexair}s (${s_hexair})"
    else
        echo "  -> SKIPPED (--hexa-ir not available or failed)"
    fi
else
    echo "  -> SKIPPED (no hexa binary)"
fi
echo ""

# --- Step 5: C ceiling ---
echo "[5/6] C ceiling (-O2)..."
if [ -f "$BENCH_C" ]; then
    if cc -O2 -o "$TMP_DIR/bench_c" "$BENCH_C" 2>/dev/null; then
        t_c=$(run_bench "$TMP_DIR/c_status" "$TMP_DIR/bench_c")
        s_c=$(cat "$TMP_DIR/c_status")
        echo "  -> ${t_c}s (${s_c})"
    else
        echo "  -> SKIPPED (compilation failed)"
    fi
else
    echo "  -> SKIPPED (no bench_loop_native.c)"
fi
echo ""

# --- Step 6: Rust ceiling ---
echo "[6/6] Rust ceiling (-O)..."
BENCH_RS="$TMP_DIR/bench_loop.rs"
cat > "$BENCH_RS" << 'RUSTEOF'
fn sigma(n: i64) -> i64 {
    let mut sum = 0i64;
    for i in 1..=n { if n % i == 0 { sum += i; } }
    sum
}
fn phi(n: i64) -> i64 {
    let mut count = 0i64;
    for i in 1..n {
        let (mut a, mut b) = (i, n);
        while b != 0 { let t = b; b = a % b; a = t; }
        if a == 1 { count += 1; }
    }
    count
}
fn fib(n: i64) -> i64 {
    let (mut a, mut b) = (0i64, 1i64);
    for _ in 0..n { let t = a + b; a = b; b = t; }
    a
}
fn main() {
    let iterations = 100_000i64;
    let mut total = 0i64;
    for _ in 0..iterations {
        total += sigma(6);
        total += phi(6);
        total += fib(20);
    }
    println!("{}", total);
}
RUSTEOF
if rustc -O -o "$TMP_DIR/bench_rs" "$BENCH_RS" 2>/dev/null; then
    t_rust=$(run_bench "$TMP_DIR/rs_status" "$TMP_DIR/bench_rs")
    s_rust=$(cat "$TMP_DIR/rs_status")
    echo "  -> ${t_rust}s (${s_rust})"
else
    echo "  -> SKIPPED (rustc not available)"
fi
echo ""

# =============================================
# ESO (Emergent Self-Optimization) Benchmark
# =============================================
# Runs --eso-tune for each policy (fixed, adaptive, hybrid)
# Captures: wall time, converge round, final ED

ESO_ROUNDS=10
eso_fixed_time="N/A"; eso_fixed_conv="N/A"; eso_fixed_ed="N/A"
eso_adaptive_time="N/A"; eso_adaptive_conv="N/A"; eso_adaptive_ed="N/A"
eso_hybrid_time="N/A"; eso_hybrid_conv="N/A"; eso_hybrid_ed="N/A"

echo "==================================================="
echo "  ESO Benchmark (${ESO_ROUNDS}-round tune per policy)"
echo "==================================================="
echo ""

run_eso_bench() {
    local policy="$1"
    local out_file="$TMP_DIR/eso_${policy}.txt"

    if [ ! -x "$PROJECT_DIR/hexa" ]; then
        echo "N/A N/A N/A"
        return
    fi

    local start end elapsed
    start=$(python3 -c 'import time; print(time.time())')
    "$PROJECT_DIR/hexa" --eso-tune "$BENCH_HEXA" --rounds "$ESO_ROUNDS" --policy "$policy" \
        2>"$out_file" >/dev/null || true
    end=$(python3 -c 'import time; print(time.time())')
    elapsed=$(python3 -c "print(f'{$end - $start:.3f}')")

    # Parse converge round: first round where changes==0
    local conv_round="$ESO_ROUNDS"
    local found_zero=false
    while IFS= read -r line; do
        if echo "$line" | grep -q '\[eso-tune\] round'; then
            local rnum changes
            rnum=$(echo "$line" | sed 's/.*round \([0-9]*\)\/.*/\1/')
            changes=$(echo "$line" | sed 's/.*: \([0-9]*\) changes.*/\1/')
            if [ "$changes" = "0" ] && [ "$found_zero" = "false" ]; then
                conv_round="$rnum"
                found_zero=true
            fi
        fi
    done < "$out_file"

    # Parse final ED from last round line
    local final_ed="0.0000"
    final_ed=$(grep '\[eso-tune\] round' "$out_file" | tail -1 | sed 's/.*ED=\([0-9.]*\).*/\1/')

    echo "$elapsed $conv_round $final_ed"
}

for pol in fixed adaptive hybrid; do
    echo "  [$pol] running ${ESO_ROUNDS} rounds..."
    result=$(run_eso_bench "$pol")
    t=$(echo "$result" | awk '{print $1}')
    c=$(echo "$result" | awk '{print $2}')
    e=$(echo "$result" | awk '{print $3}')
    eval "eso_${pol}_time=$t"
    eval "eso_${pol}_conv=$c"
    eval "eso_${pol}_ed=$e"
    echo "  -> ${t}s, converge@round ${c}, ED=${e}"
done
echo ""

# --- Helper: calculate ratio vs C ceiling ---
calc_ratio() {
    local t="$1"
    local c="$2"
    if [ "$t" = "N/A" ] || [ "$c" = "N/A" ]; then
        echo "N/A"
    else
        python3 -c "print(f'{float(\"$t\") / float(\"$c\"):.1f}x')" 2>/dev/null || echo "N/A"
    fi
}

# --- Format results table ---
echo "==================================================="
echo "  Results"
echo "==================================================="
echo ""
printf "| %-16s | %-10s | %-10s | %-8s |\n" "Engine" "Time (s)" "vs C ceil" "Status"
printf "|%-18s|%-12s|%-12s|%-10s|\n" "------------------" "------------" "------------" "----------"

for entry in "Rust -O:$t_rust:$s_rust" "C -O2:$t_c:$s_c" "Cranelift JIT:$t_cranelift:$s_cranelift" "HEXA-IR:$t_hexair:$s_hexair" "Interpreter:$t_interp:$s_interp"; do
    label=$(echo "$entry" | cut -d: -f1)
    t=$(echo "$entry" | cut -d: -f2)
    s=$(echo "$entry" | cut -d: -f3-)
    ratio=$(calc_ratio "$t" "$t_c")
    printf "| %-16s | %-10s | %-10s | %-8s |\n" "$label" "$t" "$ratio" "$s"
done
echo ""

# --- ESO results table ---
echo "==================================================="
echo "  ESO Results (${ESO_ROUNDS}-round tune)"
echo "==================================================="
echo ""
printf "| %-12s | %-10s | %-14s | %-10s |\n" "Policy" "Time (s)" "Converge@Rnd" "Final ED"
printf "|%-14s|%-12s|%-16s|%-12s|\n" "--------------" "------------" "----------------" "------------"
for pol in fixed adaptive hybrid; do
    t_var="eso_${pol}_time"; c_var="eso_${pol}_conv"; e_var="eso_${pol}_ed"
    printf "| %-12s | %-10s | %-14s | %-10s |\n" "$pol" "${!t_var}" "${!c_var}" "${!e_var}"
done
echo ""

# --- Append to benchmark doc ---
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
{
    echo ""
    echo "### Benchmark Run: $TIMESTAMP"
    echo ""
    echo "| Engine | Time (s) | vs C ceil | Status |"
    echo "|--------|----------|-----------|--------|"
    for entry in "Rust -O:$t_rust:$s_rust" "C -O2:$t_c:$s_c" "Cranelift JIT:$t_cranelift:$s_cranelift" "HEXA-IR:$t_hexair:$s_hexair" "Interpreter:$t_interp:$s_interp"; do
        label=$(echo "$entry" | cut -d: -f1)
        t=$(echo "$entry" | cut -d: -f2)
        s=$(echo "$entry" | cut -d: -f3-)
        ratio=$(calc_ratio "$t" "$t_c")
        echo "| $label | $t | $ratio | $s |"
    done
    echo ""
    echo "#### ESO Benchmark (${ESO_ROUNDS}-round tune)"
    echo ""
    echo "| Policy | Time (s) | Converge@Rnd | Final ED |"
    echo "|--------|----------|--------------|----------|"
    for pol in fixed adaptive hybrid; do
        t_var="eso_${pol}_time"; c_var="eso_${pol}_conv"; e_var="eso_${pol}_ed"
        echo "| $pol | ${!t_var} | ${!c_var} | ${!e_var} |"
    done
    echo ""
} >> "$BENCH_DOC"

echo "Results appended to $BENCH_DOC"
echo "Done."
