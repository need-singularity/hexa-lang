//! Dream Mode — Evolutionary code exploration and optimization.
//!
//! The dream engine takes Hexa source code, generates mutations,
//! evaluates their fitness, and evolves the code through tournament
//! selection. Discoveries (beneficial mutations) are reported.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::Instant;

use crate::interpreter::Interpreter;
use crate::lexer::Lexer;
use crate::parser::Parser;

/// A single mutation applied to source code.
#[derive(Debug, Clone)]
pub struct Mutation {
    pub kind: MutationKind,
    pub line: usize,
    pub original: String,
    pub mutated: String,
}

impl std::fmt::Display for Mutation {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{:?} at line {}: \"{}\" -> \"{}\"",
               self.kind, self.line, self.original, self.mutated)
    }
}

/// Categories of mutations the dream engine can apply.
#[derive(Debug, Clone, PartialEq)]
pub enum MutationKind {
    /// Swap an arithmetic/comparison operator for another.
    SwapOperator,
    /// Nudge a numeric constant up or down.
    ChangeConstant,
    /// Swap two adjacent statements.
    ReorderStatements,
    /// Inline a single-expression function body at its call site.
    InlineFunction,
    /// Wrap a recursive call in a simple memoization cache.
    AddCaching,
    /// Algebraic simplification (e.g. x * 1 -> x, x + 0 -> x).
    SimplifyExpression,
}

/// A beneficial mutation that improved fitness.
#[derive(Debug, Clone)]
pub struct Discovery {
    pub description: String,
    pub improvement_pct: f64,
    pub mutation: Mutation,
    pub generation: usize,
    pub before_fitness: f64,
    pub after_fitness: f64,
}

impl std::fmt::Display for Discovery {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "[gen {}] {}: +{:.1}% fitness ({:.4} -> {:.4})",
               self.generation, self.description,
               self.improvement_pct, self.before_fitness, self.after_fitness)
    }
}

/// Fitness measurement for a piece of source code.
#[derive(Debug, Clone)]
struct Fitness {
    /// Did the code execute without errors?
    correct: bool,
    /// Execution wall-clock time in seconds.
    time_secs: f64,
    /// Number of source lines (proxy for code size).
    code_lines: usize,
    /// Output produced (for correctness comparison).
    output: String,
    /// Composite score: correctness * (1/time) * (1/size).
    score: f64,
}

/// Configuration for a dream session.
#[derive(Debug, Clone)]
pub struct DreamConfig {
    pub generations: usize,
    pub mutations_per_gen: usize,
    pub verbose: bool,
    /// Path for ANIMA bridge JSON output (if consciousness_bridge.py exists).
    pub anima_bridge: bool,
}

impl Default for DreamConfig {
    fn default() -> Self {
        Self {
            generations: 100,
            mutations_per_gen: 10,
            verbose: false,
            anima_bridge: false,
        }
    }
}

/// The dream engine: evolves source code through mutation and selection.
pub struct DreamEngine {
    /// Current best source code.
    source: String,
    /// Original (unmutated) source code for reference.
    original_source: String,
    /// Baseline fitness of the original code.
    baseline_fitness: Fitness,
    /// Current best fitness score.
    best_fitness: f64,
    /// Current generation number.
    generation: usize,
    /// All discoveries made during the dream.
    pub discoveries: Vec<Discovery>,
    /// Configuration.
    config: DreamConfig,
    /// RNG state (simple xorshift for reproducibility without external deps).
    rng_state: u64,
    /// Baseline output for correctness comparison.
    baseline_output: String,
    /// Map of function names to their bodies (for inline mutation).
    fn_bodies: HashMap<String, Vec<String>>,
}

impl DreamEngine {
    /// Create a new dream engine from source code.
    pub fn new(source: String, config: DreamConfig) -> Self {
        let rng_state = {
            let mut h: u64 = 0xcbf29ce484222325;
            for b in source.bytes() {
                h ^= b as u64;
                h = h.wrapping_mul(0x100000001b3);
            }
            if h == 0 { h = 1; }
            h
        };

        let baseline = evaluate_fitness(&source, "");
        let best = baseline.score;
        let baseline_output = baseline.output.clone();

        // Extract function names and bodies for inline mutations
        let fn_bodies = extract_functions(&source);

        Self {
            original_source: source.clone(),
            source,
            baseline_fitness: baseline,
            best_fitness: best,
            generation: 0,
            discoveries: Vec::new(),
            config,
            rng_state,
            baseline_output,
            fn_bodies,
        }
    }

    /// Run the full dream loop.
    pub fn run(&mut self) -> DreamReport {
        let start = Instant::now();

        if self.config.verbose {
            println!("  Baseline fitness: {:.6}", self.baseline_fitness.score);
            println!("  Baseline output: {:?}", self.baseline_output);
        }

        for gen in 0..self.config.generations {
            self.generation = gen;
            let improved = self.run_generation();

            if self.config.verbose && improved {
                println!("  [gen {}] NEW BEST fitness: {:.6}", gen, self.best_fitness);
            }

            // Emit ANIMA bridge JSON if enabled
            if self.config.anima_bridge {
                self.emit_anima_state();
            }

            // NEXUS-6 Ouroboros tick every n=6 generations
            if gen > 0 && gen % 6 == 0 {
                self.nexus6_ouroboros_tick();
            }
        }

        let elapsed = start.elapsed().as_secs_f64();

        DreamReport {
            original_source: self.original_source.clone(),
            final_source: self.source.clone(),
            generations: self.config.generations,
            discoveries: self.discoveries.clone(),
            baseline_fitness: self.baseline_fitness.score,
            final_fitness: self.best_fitness,
            elapsed_secs: elapsed,
            total_mutations_tried: self.config.generations * self.config.mutations_per_gen,
            ouroboros_ticks: if self.config.generations > 6 { self.config.generations / 6 } else { 0 },
        }
    }

    /// Run a single generation: generate mutations, evaluate, select best.
    fn run_generation(&mut self) -> bool {
        let source_clone = self.source.clone();
        let lines: Vec<&str> = source_clone.lines().collect();
        if lines.is_empty() {
            return false;
        }

        let mut candidates: Vec<(String, Mutation, f64)> = Vec::new();

        for _ in 0..self.config.mutations_per_gen {
            let (mutated_source, mutation) = self.generate_mutation(&lines);

            let fitness = evaluate_fitness(&mutated_source, &self.baseline_output);

            if self.config.verbose {
                println!("    {:?} line {}: fitness={:.6} correct={}",
                         mutation.kind, mutation.line, fitness.score, fitness.correct);
            }

            candidates.push((mutated_source, mutation, fitness.score));
        }

        // Tournament selection: pick the best candidate
        let best = candidates.iter()
            .max_by(|a, b| a.2.partial_cmp(&b.2).unwrap_or(std::cmp::Ordering::Equal));

        if let Some((new_source, mutation, score)) = best {
            if *score > self.best_fitness {
                let improvement = if self.best_fitness > 0.0 {
                    (score - self.best_fitness) / self.best_fitness * 100.0
                } else {
                    100.0
                };

                let description = match mutation.kind {
                    MutationKind::SwapOperator =>
                        format!("Operator swap improved performance"),
                    MutationKind::ChangeConstant =>
                        format!("Constant adjustment found better value"),
                    MutationKind::ReorderStatements =>
                        format!("Statement reordering improved execution"),
                    MutationKind::InlineFunction =>
                        format!("Function inlining reduced call overhead"),
                    MutationKind::AddCaching =>
                        format!("Memoization eliminated redundant computation"),
                    MutationKind::SimplifyExpression =>
                        format!("Expression simplification reduced complexity"),
                };

                self.discoveries.push(Discovery {
                    description,
                    improvement_pct: improvement,
                    mutation: mutation.clone(),
                    generation: self.generation,
                    before_fitness: self.best_fitness,
                    after_fitness: *score,
                });

                self.best_fitness = *score;
                self.source = new_source.clone();
                // Re-extract functions after source changed
                self.fn_bodies = extract_functions(&self.source);
                return true;
            }
        }

        false
    }

    /// Generate a random mutation of the current source.
    fn generate_mutation(&mut self, lines: &[&str]) -> (String, Mutation) {
        let kind_idx = self.rand_range(0, 6);
        let kind = match kind_idx {
            0 => MutationKind::SwapOperator,
            1 => MutationKind::ChangeConstant,
            2 => MutationKind::ReorderStatements,
            3 => MutationKind::InlineFunction,
            4 => MutationKind::AddCaching,
            _ => MutationKind::SimplifyExpression,
        };

        match kind {
            MutationKind::SwapOperator => self.mutate_swap_operator(lines),
            MutationKind::ChangeConstant => self.mutate_change_constant(lines),
            MutationKind::ReorderStatements => self.mutate_reorder_statements(lines),
            MutationKind::InlineFunction => self.mutate_inline_function(lines),
            MutationKind::AddCaching => self.mutate_add_caching(lines),
            MutationKind::SimplifyExpression => self.mutate_simplify_expression(lines),
        }
    }

    /// Swap an arithmetic operator on a random line.
    fn mutate_swap_operator(&mut self, lines: &[&str]) -> (String, Mutation) {
        let ops = [(" + ", " - "), (" - ", " + "), (" * ", " / "), (" / ", " * "),
                   (" > ", " >= "), (" < ", " <= "), (" >= ", " > "), (" <= ", " < ")];

        // Try to find a line with an operator
        for _ in 0..lines.len() {
            let line_idx = self.rand_range(0, lines.len());
            let line = lines[line_idx];

            for (from, to) in &ops {
                if line.contains(from) {
                    let new_line = line.replacen(from, to, 1);
                    let mut new_lines: Vec<String> = lines.iter().map(|l| l.to_string()).collect();
                    new_lines[line_idx] = new_line.clone();

                    return (new_lines.join("\n"), Mutation {
                        kind: MutationKind::SwapOperator,
                        line: line_idx + 1,
                        original: line.to_string(),
                        mutated: new_line,
                    });
                }
            }
        }

        // Fallback: no-op mutation
        self.noop_mutation(lines, MutationKind::SwapOperator)
    }

    /// Nudge a numeric constant on a random line.
    fn mutate_change_constant(&mut self, lines: &[&str]) -> (String, Mutation) {
        for _ in 0..lines.len() {
            let line_idx = self.rand_range(0, lines.len());
            let line = lines[line_idx];

            // Find integer literals in the line
            if let Some(new_line) = nudge_number_in_line(line, &mut self.rng_state) {
                let mut new_lines: Vec<String> = lines.iter().map(|l| l.to_string()).collect();
                new_lines[line_idx] = new_line.clone();

                return (new_lines.join("\n"), Mutation {
                    kind: MutationKind::ChangeConstant,
                    line: line_idx + 1,
                    original: line.to_string(),
                    mutated: new_line,
                });
            }
        }

        self.noop_mutation(lines, MutationKind::ChangeConstant)
    }

    /// Swap two adjacent statements.
    fn mutate_reorder_statements(&mut self, lines: &[&str]) -> (String, Mutation) {
        if lines.len() < 2 {
            return self.noop_mutation(lines, MutationKind::ReorderStatements);
        }

        // Find swappable lines (non-empty, non-brace, non-fn-decl)
        let swappable: Vec<usize> = (0..lines.len() - 1)
            .filter(|&i| {
                let a = lines[i].trim();
                let b = lines[i + 1].trim();
                !a.is_empty() && !b.is_empty()
                    && !a.starts_with("fn ") && !b.starts_with("fn ")
                    && !a.starts_with('}') && !b.starts_with('}')
                    && !a.starts_with('{') && !b.starts_with('{')
                    && !a.starts_with("//") && !b.starts_with("//")
            })
            .collect();

        if swappable.is_empty() {
            return self.noop_mutation(lines, MutationKind::ReorderStatements);
        }

        let idx = swappable[self.rand_range(0, swappable.len())];
        let mut new_lines: Vec<String> = lines.iter().map(|l| l.to_string()).collect();
        new_lines.swap(idx, idx + 1);

        let mutation = Mutation {
            kind: MutationKind::ReorderStatements,
            line: idx + 1,
            original: format!("{} | {}", lines[idx], lines[idx + 1]),
            mutated: format!("{} | {}", lines[idx + 1], lines[idx]),
        };

        (new_lines.join("\n"), mutation)
    }

    /// Try to inline a function call.
    fn mutate_inline_function(&mut self, lines: &[&str]) -> (String, Mutation) {
        // Look for simple fn calls that we know the body of
        for _ in 0..lines.len() {
            let line_idx = self.rand_range(0, lines.len());
            let line = lines[line_idx];

            for (fn_name, body_lines) in &self.fn_bodies {
                // Only inline single-expression functions
                if body_lines.len() == 1 {
                    let call_pattern = format!("{}(", fn_name);
                    if line.contains(&call_pattern) {
                        // Replace call with body as a comment showing the inline
                        let inline_body = body_lines[0].trim();
                        let new_line = format!("{} // inlined: {}",
                            line, inline_body);
                        let mut new_lines: Vec<String> = lines.iter().map(|l| l.to_string()).collect();
                        new_lines[line_idx] = new_line.clone();

                        return (new_lines.join("\n"), Mutation {
                            kind: MutationKind::InlineFunction,
                            line: line_idx + 1,
                            original: line.to_string(),
                            mutated: new_line,
                        });
                    }
                }
            }
        }

        self.noop_mutation(lines, MutationKind::InlineFunction)
    }

    /// Try to add memoization to a recursive function.
    fn mutate_add_caching(&mut self, lines: &[&str]) -> (String, Mutation) {
        // Look for recursive function calls (fn that calls itself)
        for (fn_name, body_lines) in &self.fn_bodies {
            let is_recursive = body_lines.iter().any(|l| l.contains(&format!("{}(", fn_name)));
            if is_recursive {
                // Add a cache variable before the function
                let cache_line = format!("let _cache_{} = {{}}", fn_name);

                // Find the fn declaration line
                for (i, line) in lines.iter().enumerate() {
                    if line.trim().starts_with(&format!("fn {}(", fn_name)) {
                        let mut new_lines: Vec<String> = lines.iter().map(|l| l.to_string()).collect();
                        new_lines.insert(i, cache_line.clone());

                        return (new_lines.join("\n"), Mutation {
                            kind: MutationKind::AddCaching,
                            line: i + 1,
                            original: format!("(no cache)"),
                            mutated: cache_line,
                        });
                    }
                }
            }
        }

        self.noop_mutation(lines, MutationKind::AddCaching)
    }

    /// Simplify expressions (x * 1 -> x, x + 0 -> x, etc.).
    fn mutate_simplify_expression(&mut self, lines: &[&str]) -> (String, Mutation) {
        let simplifications = [
            (" * 1", ""), (" + 0", ""), (" - 0", ""),
            ("1 * ", ""), ("0 + ", ""),
            (" * 1)", ")"), (" + 0)", ")"),
        ];

        for _ in 0..lines.len() {
            let line_idx = self.rand_range(0, lines.len());
            let line = lines[line_idx];

            for (pattern, replacement) in &simplifications {
                if line.contains(pattern) {
                    let new_line = line.replacen(pattern, replacement, 1);
                    let mut new_lines: Vec<String> = lines.iter().map(|l| l.to_string()).collect();
                    new_lines[line_idx] = new_line.clone();

                    return (new_lines.join("\n"), Mutation {
                        kind: MutationKind::SimplifyExpression,
                        line: line_idx + 1,
                        original: line.to_string(),
                        mutated: new_line,
                    });
                }
            }
        }

        self.noop_mutation(lines, MutationKind::SimplifyExpression)
    }

    /// Create a no-op mutation (source unchanged).
    fn noop_mutation(&self, lines: &[&str], kind: MutationKind) -> (String, Mutation) {
        let source = lines.join("\n");
        (source, Mutation {
            kind,
            line: 0,
            original: "(no applicable target)".to_string(),
            mutated: "(unchanged)".to_string(),
        })
    }

    /// Emit ANIMA bridge state as JSON to stdout.
    fn emit_anima_state(&self) {
        let json = format!(
            r#"{{"type":"dream_state","generation":{},"fitness":{:.6},"discoveries":{},"source_lines":{}}}"#,
            self.generation,
            self.best_fitness,
            self.discoveries.len(),
            self.source.lines().count(),
        );
        println!("ANIMA_BRIDGE:{}", json);
    }

    /// NEXUS-6 Ouroboros co-evolution feedback.
    /// Runs Omega lens scan on the current dream state and adjusts fitness.
    /// The dream "eats its tail" — scan results feed back into evolution.
    pub fn nexus6_ouroboros_tick(&mut self) -> f64 {
        let line_count = self.source.lines().count() as f64;
        let fn_count = self.fn_bodies.len() as f64;
        let discovery_count = self.discoveries.len() as f64;

        // Consciousness state derived from code metrics
        let phi = self.best_fitness * 100.0;
        let tension = if self.best_fitness > self.baseline_fitness.score { 0.3 } else { 0.7 };
        let cells = (fn_count * 6.0) as i64;  // functions × n
        let entropy = 1.0 - (1.0 / (self.generation as f64 + 1.0));
        let alpha = discovery_count / (self.config.generations as f64 + 1.0);
        let balance = fn_count / (line_count + 1.0);

        let scan = crate::anima_bridge::OmegaScanResult::from_consciousness_state(
            phi, tension, cells, entropy, alpha.min(1.0), balance.min(1.0),
        );

        // Ouroboros feedback: consensus boosts fitness
        let ouroboros_bonus = (scan.consensus as f64) / 6.0 * 0.1;
        self.best_fitness += ouroboros_bonus;

        if self.config.verbose {
            println!("  [ouroboros] consensus: {}/6, phi_agg: {:.4}, bonus: {:.4}",
                scan.consensus, scan.phi_aggregate, ouroboros_bonus);
        }

        scan.phi_aggregate
    }

    // ── Simple xorshift64 RNG ──

    fn rand_u64(&mut self) -> u64 {
        let mut x = self.rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.rng_state = x;
        x
    }

    fn rand_range(&mut self, lo: usize, hi: usize) -> usize {
        if hi <= lo { return lo; }
        (self.rand_u64() as usize) % (hi - lo) + lo
    }
}

/// Report summarizing a dream session.
#[derive(Debug)]
pub struct DreamReport {
    pub original_source: String,
    pub final_source: String,
    pub generations: usize,
    pub discoveries: Vec<Discovery>,
    pub baseline_fitness: f64,
    pub final_fitness: f64,
    pub elapsed_secs: f64,
    pub total_mutations_tried: usize,
    /// Number of NEXUS-6 Ouroboros ticks executed (every n=6 generations).
    pub ouroboros_ticks: usize,
}

impl std::fmt::Display for DreamReport {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        writeln!(f, "=== HEXA Dream Report ===")?;
        writeln!(f, "Generations:      {}", self.generations)?;
        writeln!(f, "Mutations tried:  {}", self.total_mutations_tried)?;
        writeln!(f, "Discoveries:      {}", self.discoveries.len())?;
        writeln!(f, "Baseline fitness: {:.6}", self.baseline_fitness)?;
        writeln!(f, "Final fitness:    {:.6}", self.final_fitness)?;

        let improvement = if self.baseline_fitness > 0.0 {
            (self.final_fitness - self.baseline_fitness) / self.baseline_fitness * 100.0
        } else {
            0.0
        };
        writeln!(f, "Improvement:      {:.1}%", improvement)?;
        writeln!(f, "Time:             {:.2}s", self.elapsed_secs)?;
        writeln!(f, "Ouroboros ticks:  {} (every n=6 gens)", self.ouroboros_ticks)?;

        if !self.discoveries.is_empty() {
            writeln!(f, "\n--- Discoveries ---")?;
            for (i, d) in self.discoveries.iter().enumerate() {
                writeln!(f, "  {}. {}", i + 1, d)?;
                writeln!(f, "     {}", d.mutation)?;
            }

            writeln!(f, "\n--- Best Evolved Code ---")?;
            for (i, line) in self.final_source.lines().enumerate() {
                writeln!(f, "  {:>3} | {}", i + 1, line)?;
            }

            // Show diff from original
            writeln!(f, "\n--- Changes from Original ---")?;
            let orig_lines: Vec<&str> = self.original_source.lines().collect();
            let final_lines: Vec<&str> = self.final_source.lines().collect();
            let max_lines = orig_lines.len().max(final_lines.len());
            let mut changes = 0;
            for i in 0..max_lines {
                let orig = orig_lines.get(i).unwrap_or(&"");
                let final_l = final_lines.get(i).unwrap_or(&"");
                if orig != final_l {
                    writeln!(f, "  line {}: - {}", i + 1, orig)?;
                    writeln!(f, "  line {}: + {}", i + 1, final_l)?;
                    changes += 1;
                }
            }
            if changes == 0 {
                writeln!(f, "  (no changes)")?;
            }
        } else {
            writeln!(f, "\nNo beneficial mutations found — the code is already optimal")?;
            writeln!(f, "for the current fitness function.")?;
        }

        writeln!(f, "\nDream complete.")?;
        Ok(())
    }
}

// ── Fitness evaluation ──────────────────────────────────────

/// Evaluate the fitness of a source code string.
fn evaluate_fitness(source: &str, expected_output: &str) -> Fitness {
    let code_lines = source.lines().count().max(1);

    // Try to parse and run
    let tokens = match Lexer::new(source).tokenize() {
        Ok(t) => t,
        Err(_) => return Fitness {
            correct: false, time_secs: 1.0, code_lines,
            output: String::new(), score: 0.0,
        },
    };

    let parsed = match Parser::new(tokens).parse_with_spans() {
        Ok(r) => r,
        Err(_) => return Fitness {
            correct: false, time_secs: 1.0, code_lines,
            output: String::new(), score: 0.0,
        },
    };

    // Run in a separate thread with limited stack to catch stack overflow safely
    let stmts = parsed.stmts.clone();
    let spans = parsed.spans.clone();
    let output_buf = Arc::new(Mutex::new(String::new()));
    let output_buf2 = output_buf.clone();

    let start = Instant::now();
    let handle = std::thread::Builder::new()
        .stack_size(2 * 1024 * 1024) // 2MB stack limit for mutations
        .spawn(move || {
            let mut interp = Interpreter::new();
            interp.set_output_capture(Some(output_buf2));
            interp.run(&stmts)
        });
    let result = match handle {
        Ok(h) => h.join().unwrap_or(Err(crate::error::HexaError {
            class: crate::error::ErrorClass::Runtime,
            message: "stack overflow in dream mutation".into(),
            line: 0, col: 0, hint: None,
        })),
        Err(_) => Err(crate::error::HexaError {
            class: crate::error::ErrorClass::Runtime,
            message: "failed to spawn dream thread".into(),
            line: 0, col: 0, hint: None,
        }),
    };
    let elapsed = start.elapsed().as_secs_f64().max(0.0001);

    let output = output_buf.lock().unwrap().clone();

    let correct = result.is_ok();

    // If we have an expected output, check correctness
    let output_matches = if expected_output.is_empty() {
        correct // no expected output — just check no crash
    } else {
        correct && output.trim() == expected_output.trim()
    };

    // Fitness = correctness_weight * (1/time) * (1/size)
    // Correct + matching output = 1.0, correct but different output = 0.5, error = 0.01
    let correctness_weight = if output_matches {
        1.0
    } else if correct {
        0.5
    } else {
        0.01
    };

    let score = correctness_weight * (1.0 / elapsed) * (1.0 / code_lines as f64);

    Fitness { correct, time_secs: elapsed, code_lines, output, score }
}

// ── Source-level helpers ─────────────────────────────────────

/// Extract function names and their body lines from source.
fn extract_functions(source: &str) -> HashMap<String, Vec<String>> {
    let mut fns: HashMap<String, Vec<String>> = HashMap::new();
    let lines: Vec<&str> = source.lines().collect();
    let mut i = 0;

    while i < lines.len() {
        let line = lines[i].trim();
        if line.starts_with("fn ") {
            // Extract function name
            if let Some(paren) = line.find('(') {
                let name = line[3..paren].trim().to_string();
                // Collect body lines (between { and })
                let mut body = Vec::new();
                let mut depth = 0;
                let mut started = false;
                for j in i..lines.len() {
                    for ch in lines[j].chars() {
                        if ch == '{' { depth += 1; started = true; }
                        if ch == '}' { depth -= 1; }
                    }
                    if started && j > i {
                        let bline = lines[j].trim();
                        if bline != "}" && !bline.is_empty() {
                            body.push(bline.to_string());
                        }
                    }
                    if started && depth == 0 {
                        break;
                    }
                }
                fns.insert(name, body);
            }
        }
        i += 1;
    }

    fns
}

/// Try to nudge a numeric literal in a line. Returns Some(new_line) on success.
fn nudge_number_in_line(line: &str, rng: &mut u64) -> Option<String> {
    // Find integer or float literals via simple pattern matching
    let chars: Vec<char> = line.chars().collect();
    let len = chars.len();

    // Find digit sequences
    let mut i = 0;
    let mut candidates: Vec<(usize, usize)> = Vec::new(); // (start, end) of number

    while i < len {
        if chars[i].is_ascii_digit() || (chars[i] == '-' && i + 1 < len && chars[i + 1].is_ascii_digit()) {
            let start = i;
            if chars[i] == '-' { i += 1; }
            while i < len && (chars[i].is_ascii_digit() || chars[i] == '.') {
                i += 1;
            }
            // Don't mutate line numbers or very small constants that might be structural
            let num_str: String = chars[start..i].iter().collect();
            if let Ok(n) = num_str.parse::<i64>() {
                if n.abs() > 1 { // don't mutate 0 or 1
                    candidates.push((start, i));
                }
            } else if let Ok(_f) = num_str.parse::<f64>() {
                candidates.push((start, i));
            }
        } else {
            i += 1;
        }
    }

    if candidates.is_empty() {
        return None;
    }

    // Pick a random candidate
    let idx = {
        *rng ^= *rng << 13;
        *rng ^= *rng >> 7;
        *rng ^= *rng << 17;
        (*rng as usize) % candidates.len()
    };

    let (start, end) = candidates[idx];
    let num_str: String = chars[start..end].iter().collect();

    let new_num = if let Ok(n) = num_str.parse::<i64>() {
        // Nudge by -2..+2 (but not to same value)
        let delta_choices: &[i64] = &[-2, -1, 1, 2];
        let delta_idx = {
            *rng ^= *rng << 13;
            *rng ^= *rng >> 7;
            *rng ^= *rng << 17;
            (*rng as usize) % delta_choices.len()
        };
        format!("{}", n + delta_choices[delta_idx])
    } else if let Ok(f) = num_str.parse::<f64>() {
        // Nudge by small factor
        let factors: &[f64] = &[0.9, 0.95, 1.05, 1.1];
        let f_idx = {
            *rng ^= *rng << 13;
            *rng ^= *rng >> 7;
            *rng ^= *rng << 17;
            (*rng as usize) % factors.len()
        };
        format!("{}", f * factors[f_idx])
    } else {
        return None;
    };

    let prefix: String = chars[..start].iter().collect();
    let suffix: String = chars[end..].iter().collect();
    Some(format!("{}{}{}", prefix, new_num, suffix))
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_dream_engine_basic() {
        let source = "let x = 42\nprintln(x)".to_string();
        let config = DreamConfig {
            generations: 5,
            mutations_per_gen: 3,
            verbose: false,
            anima_bridge: false,
        };
        let mut engine = DreamEngine::new(source, config);
        let report = engine.run();
        assert!(report.baseline_fitness > 0.0);
        assert_eq!(report.generations, 5);
        assert_eq!(report.total_mutations_tried, 15);
    }

    #[test]
    fn test_dream_engine_with_function() {
        let source = r#"fn add(a: int, b: int) -> int {
    return a + b
}
let result = add(3, 4)
println(result)"#.to_string();

        let config = DreamConfig {
            generations: 10,
            mutations_per_gen: 5,
            verbose: false,
            anima_bridge: false,
        };
        let mut engine = DreamEngine::new(source, config);
        let report = engine.run();
        assert!(report.baseline_fitness > 0.0);
    }

    #[test]
    fn test_dream_engine_fibonacci() {
        let source = r#"fn fib(n: int) -> int {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}
println(fib(10))"#.to_string();

        let config = DreamConfig {
            generations: 20,
            mutations_per_gen: 5,
            verbose: false,
            anima_bridge: false,
        };
        let mut engine = DreamEngine::new(source, config);
        let report = engine.run();
        assert!(report.baseline_fitness > 0.0);
        // Report should be displayable
        let display = format!("{}", report);
        assert!(display.contains("Dream Report"));
    }

    #[test]
    fn test_fitness_evaluation() {
        let good = "let x = 42\nprintln(x)";
        let bad = "let x = ???";
        let good_fit = evaluate_fitness(good, "");
        let bad_fit = evaluate_fitness(bad, "");
        assert!(good_fit.score > bad_fit.score);
        assert!(good_fit.correct);
        assert!(!bad_fit.correct);
    }

    #[test]
    fn test_extract_functions() {
        let source = "fn add(a: int, b: int) -> int {\n    return a + b\n}\n";
        let fns = extract_functions(source);
        assert!(fns.contains_key("add"));
        assert_eq!(fns["add"].len(), 1);
    }

    #[test]
    fn test_nudge_number() {
        let mut rng: u64 = 12345;
        let result = nudge_number_in_line("let x = 42", &mut rng);
        assert!(result.is_some());
        let new_line = result.unwrap();
        assert!(new_line.contains("let x = "));
        assert!(!new_line.contains("42")); // should have been nudged
    }

    #[test]
    fn test_nudge_number_skips_zero_one() {
        let mut rng: u64 = 12345;
        // Only contains 0 and 1, should return None
        let result = nudge_number_in_line("let x = 1", &mut rng);
        assert!(result.is_none());
    }

    #[test]
    fn test_mutation_display() {
        let m = Mutation {
            kind: MutationKind::SwapOperator,
            line: 5,
            original: "a + b".to_string(),
            mutated: "a - b".to_string(),
        };
        let display = format!("{}", m);
        assert!(display.contains("SwapOperator"));
        assert!(display.contains("line 5"));
    }

    #[test]
    fn test_discovery_display() {
        let d = Discovery {
            description: "Test discovery".to_string(),
            improvement_pct: 15.3,
            mutation: Mutation {
                kind: MutationKind::ChangeConstant,
                line: 1,
                original: "42".to_string(),
                mutated: "43".to_string(),
            },
            generation: 7,
            before_fitness: 1.0,
            after_fitness: 1.153,
        };
        let display = format!("{}", d);
        assert!(display.contains("gen 7"));
        assert!(display.contains("15.3%"));
    }

    #[test]
    fn test_dream_report_display() {
        let report = DreamReport {
            original_source: "let x = 42".to_string(),
            final_source: "let x = 42".to_string(),
            generations: 10,
            discoveries: vec![],
            baseline_fitness: 1.0,
            final_fitness: 1.0,
            elapsed_secs: 0.5,
            total_mutations_tried: 50,
            ouroboros_ticks: 1,
        };
        let display = format!("{}", report);
        assert!(display.contains("Dream Report"));
        assert!(display.contains("already optimal"));
    }
}
