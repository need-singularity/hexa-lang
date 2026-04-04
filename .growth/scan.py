#!/usr/bin/env python3
"""HEXA-LANG Growth Scanner — keyword/operator/parser/codegen/REPL completeness."""

import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC = REPO_ROOT / "src"
EXAMPLES = REPO_ROOT / "examples"
GROWTH_DIR = REPO_ROOT / ".growth"
STATE_FILE = GROWTH_DIR / "growth_state.json"
GROWTH_BUS = Path.home() / "Dev" / "nexus6" / "shared" / "growth_bus.jsonl"

# ── n=6 constants ──
EXPECTED_KEYWORDS = 53   # sigma*tau + sopfr = 48 + 5
EXPECTED_OPERATORS = 24  # J2 = 24
EXPECTED_PRIMITIVES = 8  # sigma - tau = 8

# ── 53 keywords (from token.rs) ──
ALL_KEYWORDS = [
    "if", "else", "match", "for", "while", "loop",
    "type", "struct", "enum", "trait", "impl", "dyn",
    "fn", "return", "yield", "async", "await",
    "let", "mut", "const", "static",
    "mod", "use", "pub", "crate",
    "own", "borrow", "move", "drop",
    "spawn", "channel", "select", "atomic", "scope",
    "effect", "handle", "resume", "pure",
    "proof", "assert", "invariant", "theorem",
    "macro", "derive", "where", "comptime",
    "try", "catch", "throw", "panic", "recover",
    "intent", "generate", "verify", "optimize",
]

# ── 24 operators ──
ALL_OPERATORS = [
    "Plus", "Minus", "Star", "Slash", "Percent", "Power",
    "EqEq", "NotEq", "Lt", "Gt", "LtEq", "GtEq",
    "And", "Or", "Not", "Xor",
    "BitAnd", "BitOr", "BitXor", "BitNot",
    "Eq", "ColonEq",
    "DotDot", "DotDotEq",
]

# ── Core source modules ──
CORE_MODULES = [
    "lexer", "parser", "ast", "type_checker", "types", "interpreter",
    "compiler", "vm", "codegen_wgsl", "codegen_esp32", "codegen_verilog",
    "jit", "memory", "ownership", "async_runtime", "proof_engine",
    "lsp", "debugger", "formatter", "linter", "macro_expand",
    "comptime", "wasm", "pgo", "simd_hint", "inline_cache",
    "escape_analysis", "dce", "loop_unroll", "work_stealing",
    "nanbox", "package", "dream", "llm", "anima_bridge",
]

REPL_FEATURES = [
    "tab_completion", "history", "multiline", "syntax_highlight",
    "type_info", "auto_import", "help_command", "benchmark_mode",
]


def read_file(path):
    """Read file contents, return empty string on failure."""
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return ""


def scan_keyword_implementation():
    """Check which keywords have actual implementation beyond tokenization."""
    results = {"implemented": [], "token_only": [], "missing": []}
    parser_src = read_file(SRC / "parser.rs")
    interp_src = read_file(SRC / "interpreter.rs")
    compiler_src = read_file(SRC / "compiler.rs")
    combined = parser_src + interp_src + compiler_src

    for kw in ALL_KEYWORDS:
        token_name = kw.capitalize()
        # Check if keyword is parsed beyond just tokenization
        if re.search(rf"Token::{token_name}\b", combined):
            results["implemented"].append(kw)
        else:
            # Check token.rs at least
            token_src = read_file(SRC / "token.rs")
            if re.search(rf'"{kw}"', token_src):
                results["token_only"].append(kw)
            else:
                results["missing"].append(kw)
    return results


def scan_operator_tests():
    """Check which operators have test coverage."""
    results = {"tested": [], "untested": []}
    # Collect all test files
    test_content = ""
    for f in EXAMPLES.glob("test_*.hexa"):
        test_content += read_file(f)
    for f in SRC.iterdir():
        if f.suffix == ".rs":
            src = read_file(f)
            if "#[test]" in src or "#[cfg(test)]" in src:
                test_content += src

    op_symbols = {
        "Plus": "+", "Minus": "-", "Star": "*", "Slash": "/",
        "Percent": "%", "Power": "**", "EqEq": "==", "NotEq": "!=",
        "Lt": "<", "Gt": ">", "LtEq": "<=", "GtEq": ">=",
        "And": "&&", "Or": "||", "Not": "!", "Xor": "^^",
        "BitAnd": "&", "BitOr": "|", "BitXor": "^", "BitNot": "~",
        "Eq": "=", "ColonEq": ":=", "DotDot": "..", "DotDotEq": "..=",
    }
    for op_name, sym in op_symbols.items():
        if sym in test_content:
            results["tested"].append(op_name)
        else:
            results["untested"].append(op_name)
    return results


def scan_parser_coverage():
    """Check parser completeness — which AST node types are handled."""
    results = {"covered": [], "gaps": []}
    parser_src = read_file(SRC / "parser.rs")
    ast_src = read_file(SRC / "ast.rs")

    # Extract AST enum variants
    ast_variants = re.findall(r"(\w+)\s*[\({]", ast_src)
    ast_variants = list(set(ast_variants))

    for variant in sorted(ast_variants):
        if len(variant) < 3:
            continue
        if variant in parser_src:
            results["covered"].append(variant)
        else:
            results["gaps"].append(variant)
    return results


def scan_codegen_completeness():
    """Check codegen backends for completeness."""
    backends = {
        "wasm": SRC / "wasm.rs",
        "wgsl": SRC / "codegen_wgsl.rs",
        "esp32": SRC / "codegen_esp32.rs",
        "verilog": SRC / "codegen_verilog.rs",
        "jit": SRC / "jit.rs",
        "vm": SRC / "vm.rs",
    }
    results = {}
    for name, path in backends.items():
        src = read_file(path)
        lines = len(src.splitlines()) if src else 0
        fn_count = len(re.findall(r"\bfn\s+\w+", src))
        todo_count = len(re.findall(r"(?i)todo|unimplemented|todo!", src))
        results[name] = {
            "lines": lines,
            "functions": fn_count,
            "todos": todo_count,
            "exists": path.exists(),
        }
    return results


def scan_repl_features():
    """Check REPL / interactive features."""
    results = {"present": [], "missing": []}
    main_src = read_file(SRC / "main.rs")
    interp_src = read_file(SRC / "interpreter.rs")
    lsp_src = read_file(SRC / "lsp.rs")
    combined = main_src + interp_src + lsp_src

    feature_patterns = {
        "tab_completion": r"complet|tab_complet|autocomplete",
        "history": r"history|readline|rustyline",
        "multiline": r"multiline|multi_line|continuation",
        "syntax_highlight": r"highlight|color|ansi",
        "type_info": r"type_info|hover|type_at",
        "auto_import": r"auto_import|suggest_import",
        "help_command": r"help|:help|--help",
        "benchmark_mode": r"benchmark|bench_mode|:bench",
    }
    for feat, pattern in feature_patterns.items():
        if re.search(pattern, combined, re.IGNORECASE):
            results["present"].append(feat)
        else:
            results["missing"].append(feat)
    return results


def run_cargo_test():
    """Run cargo test and return pass/fail counts."""
    try:
        result = subprocess.run(
            [os.path.expanduser("~/.cargo/bin/cargo"), "test", "--", "--quiet"],
            cwd=str(REPO_ROOT),
            capture_output=True, text=True, timeout=120,
        )
        output = result.stdout + result.stderr
        m = re.search(r"(\d+) passed.*?(\d+) failed", output)
        if m:
            return {"passed": int(m.group(1)), "failed": int(m.group(2)), "raw": output[-500:]}
        m2 = re.search(r"test result: ok\. (\d+) passed", output)
        if m2:
            return {"passed": int(m2.group(1)), "failed": 0, "raw": output[-500:]}
        return {"passed": 0, "failed": 0, "raw": output[-500:]}
    except Exception as e:
        return {"passed": 0, "failed": 0, "error": str(e)}


def count_examples():
    """Count .hexa example files."""
    if not EXAMPLES.exists():
        return {"total": 0, "test_files": 0, "demo_files": 0}
    all_hexa = list(EXAMPLES.glob("*.hexa"))
    tests = [f for f in all_hexa if f.name.startswith("test_")]
    demos = [f for f in all_hexa if not f.name.startswith("test_")]
    return {"total": len(all_hexa), "test_files": len(tests), "demo_files": len(demos)}


def compute_health_score(kw, ops, parser, codegen, repl):
    """Compute overall health score 0-100."""
    kw_score = len(kw["implemented"]) / max(EXPECTED_KEYWORDS, 1) * 100
    op_score = len(ops["tested"]) / max(EXPECTED_OPERATORS, 1) * 100
    parser_total = len(parser["covered"]) + len(parser["gaps"])
    parser_score = len(parser["covered"]) / max(parser_total, 1) * 100
    repl_score = len(repl["present"]) / max(len(REPL_FEATURES), 1) * 100

    codegen_total = sum(1 for v in codegen.values() if v["exists"])
    codegen_score = codegen_total / max(len(codegen), 1) * 100

    # Weighted average
    score = (kw_score * 0.3 + op_score * 0.15 + parser_score * 0.25 +
             codegen_score * 0.15 + repl_score * 0.15)
    return round(score, 1)


def emit_to_bus(event_type, data):
    """Write event to shared growth bus."""
    try:
        GROWTH_BUS.parent.mkdir(parents=True, exist_ok=True)
        entry = {
            "ts": datetime.now(timezone.utc).isoformat(),
            "repo": "hexa-lang",
            "event": event_type,
            "data": data,
        }
        with open(GROWTH_BUS, "a") as f:
            f.write(json.dumps(entry, ensure_ascii=False) + "\n")
    except Exception:
        pass


def main():
    verbose = "--verbose" in sys.argv or "-v" in sys.argv
    run_tests = "--test" in sys.argv

    print("=== HEXA-LANG Growth Scanner ===")
    print(f"Repo: {REPO_ROOT}")
    print(f"Time: {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')}")
    print()

    # 1. Keywords
    kw = scan_keyword_implementation()
    print(f"[Keywords]  {len(kw['implemented'])}/{EXPECTED_KEYWORDS} implemented, "
          f"{len(kw['token_only'])} token-only, {len(kw['missing'])} missing")
    if verbose and kw["token_only"]:
        print(f"  Token-only: {', '.join(kw['token_only'][:10])}")
    if verbose and kw["missing"]:
        print(f"  Missing:    {', '.join(kw['missing'][:10])}")

    # 2. Operators
    ops = scan_operator_tests()
    print(f"[Operators] {len(ops['tested'])}/{EXPECTED_OPERATORS} tested")
    if verbose and ops["untested"]:
        print(f"  Untested:   {', '.join(ops['untested'][:10])}")

    # 3. Parser
    parser = scan_parser_coverage()
    total_ast = len(parser["covered"]) + len(parser["gaps"])
    print(f"[Parser]    {len(parser['covered'])}/{total_ast} AST nodes covered")
    if verbose and parser["gaps"]:
        print(f"  Gaps:       {', '.join(parser['gaps'][:10])}")

    # 4. Codegen
    codegen = scan_codegen_completeness()
    print("[Codegen]")
    for name, info in codegen.items():
        status = "OK" if info["exists"] else "MISSING"
        todo_str = f" ({info['todos']} TODOs)" if info["todos"] > 0 else ""
        print(f"  {name:10s}: {info['lines']:5d} lines, {info['functions']:3d} fns [{status}]{todo_str}")

    # 5. REPL
    repl = scan_repl_features()
    print(f"[REPL]      {len(repl['present'])}/{len(REPL_FEATURES)} features")
    if verbose and repl["missing"]:
        print(f"  Missing:    {', '.join(repl['missing'][:10])}")

    # 6. Examples
    examples = count_examples()
    print(f"[Examples]  {examples['total']} total ({examples['test_files']} tests, "
          f"{examples['demo_files']} demos)")

    # 7. Cargo test (optional)
    test_results = None
    if run_tests:
        print("\n[Tests] Running cargo test...")
        test_results = run_cargo_test()
        print(f"  Passed: {test_results['passed']}, Failed: {test_results['failed']}")

    # Health score
    health = compute_health_score(kw, ops, parser, codegen, repl)
    print(f"\n{'='*40}")
    print(f"Health Score: {health}/100")
    print(f"{'='*40}")

    # Build result
    result = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "health_score": health,
        "keywords": {
            "implemented": len(kw["implemented"]),
            "token_only": len(kw["token_only"]),
            "missing": len(kw["missing"]),
            "total": EXPECTED_KEYWORDS,
            "detail": kw,
        },
        "operators": {
            "tested": len(ops["tested"]),
            "untested": len(ops["untested"]),
            "total": EXPECTED_OPERATORS,
            "detail": ops,
        },
        "parser": {
            "covered": len(parser["covered"]),
            "gaps": len(parser["gaps"]),
            "detail": parser,
        },
        "codegen": codegen,
        "repl": {
            "present": len(repl["present"]),
            "missing": len(repl["missing"]),
            "detail": repl,
        },
        "examples": examples,
        "tests": test_results,
    }

    # Save to state
    state = {}
    if STATE_FILE.exists():
        try:
            state = json.loads(STATE_FILE.read_text())
        except Exception:
            state = {}
    state["last_scan"] = result
    state["scan_count"] = state.get("scan_count", 0) + 1
    GROWTH_DIR.mkdir(parents=True, exist_ok=True)
    STATE_FILE.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n")

    # Emit to bus
    emit_to_bus("scan", {
        "health": health,
        "keywords": f"{len(kw['implemented'])}/{EXPECTED_KEYWORDS}",
        "operators": f"{len(ops['tested'])}/{EXPECTED_OPERATORS}",
        "parser_coverage": f"{len(parser['covered'])}/{total_ast}",
    })

    return 0 if health >= 50 else 1


if __name__ == "__main__":
    sys.exit(main())
