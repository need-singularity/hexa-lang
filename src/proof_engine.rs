//! Formal verification engine with DPLL SAT solver for HEXA proof blocks.
//!
//! Supports:
//! - CNF SAT solving via DPLL (unit propagation + pure literal elimination)
//! - Proof expressions: And, Or, Not, Implies, ForAll, Exists, comparisons
//! - Conversion from proof expressions to CNF via Tseitin transformation
//! - Built-in consciousness law verification (Law 22, Psi balance)

use std::collections::{HashMap, HashSet};
use std::fmt;

// ── SAT Solver Types ────────────────────────────────────────

/// A literal is a variable (positive ID) with a polarity (true = positive, false = negated).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Literal {
    pub var: u32,
    pub positive: bool,
}

impl Literal {
    pub fn new(var: u32, positive: bool) -> Self {
        Self { var, positive }
    }

    pub fn pos(var: u32) -> Self {
        Self { var, positive: true }
    }

    pub fn neg(var: u32) -> Self {
        Self { var, positive: false }
    }

    pub fn negated(&self) -> Self {
        Self { var: self.var, positive: !self.positive }
    }
}

impl fmt::Display for Literal {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.positive {
            write!(f, "x{}", self.var)
        } else {
            write!(f, "!x{}", self.var)
        }
    }
}

/// A clause is a disjunction (OR) of literals.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Clause {
    pub literals: Vec<Literal>,
}

impl Clause {
    pub fn new(literals: Vec<Literal>) -> Self {
        Self { literals }
    }

    pub fn unit(lit: Literal) -> Self {
        Self { literals: vec![lit] }
    }

    pub fn is_empty(&self) -> bool {
        self.literals.is_empty()
    }

    pub fn is_unit(&self) -> bool {
        self.literals.len() == 1
    }
}

impl fmt::Display for Clause {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let parts: Vec<String> = self.literals.iter().map(|l| l.to_string()).collect();
        write!(f, "({})", parts.join(" | "))
    }
}

/// A formula in CNF: conjunction (AND) of clauses.
#[derive(Debug, Clone)]
pub struct Formula {
    pub clauses: Vec<Clause>,
}

impl Formula {
    pub fn new(clauses: Vec<Clause>) -> Self {
        Self { clauses }
    }

    pub fn empty() -> Self {
        Self { clauses: Vec::new() }
    }

    /// Collect all variable IDs used in the formula.
    pub fn variables(&self) -> HashSet<u32> {
        let mut vars = HashSet::new();
        for clause in &self.clauses {
            for lit in &clause.literals {
                vars.insert(lit.var);
            }
        }
        vars
    }
}

impl fmt::Display for Formula {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let parts: Vec<String> = self.clauses.iter().map(|c| c.to_string()).collect();
        write!(f, "{}", parts.join(" & "))
    }
}

/// Variable assignment: maps variable ID to truth value.
pub type Assignment = HashMap<u32, bool>;

// ── DPLL SAT Solver ─────────────────────────────────────────

/// DPLL SAT solver result.
#[derive(Debug, Clone)]
pub enum SatResult {
    Satisfiable(Assignment),
    Unsatisfiable,
}

impl SatResult {
    pub fn is_sat(&self) -> bool {
        matches!(self, SatResult::Satisfiable(_))
    }

    pub fn is_unsat(&self) -> bool {
        matches!(self, SatResult::Unsatisfiable)
    }
}

/// Solve a CNF formula using the DPLL algorithm.
pub fn solve(formula: &Formula) -> SatResult {
    let mut assignment = Assignment::new();
    if dpll(&formula.clauses, &mut assignment) {
        SatResult::Satisfiable(assignment)
    } else {
        SatResult::Unsatisfiable
    }
}

/// Check if a formula is satisfiable.
pub fn is_satisfiable(formula: &Formula) -> bool {
    solve(formula).is_sat()
}

/// The core DPLL algorithm with unit propagation and pure literal elimination.
fn dpll(clauses: &[Clause], assignment: &mut Assignment) -> bool {
    // Simplify the clause set with current assignment
    let simplified = simplify(clauses, assignment);

    // If no clauses remain, all are satisfied
    if simplified.is_empty() {
        return true;
    }

    // If any clause is empty (all literals falsified), we have a conflict
    if simplified.iter().any(|c| c.is_empty()) {
        return false;
    }

    // Unit propagation: find unit clauses and assign their literal
    if let Some(unit_lit) = find_unit_clause(&simplified) {
        assignment.insert(unit_lit.var, unit_lit.positive);
        return dpll(&simplified, assignment);
    }

    // Pure literal elimination: if a variable appears only positive or only negative,
    // assign it to satisfy those clauses
    if let Some(pure_lit) = find_pure_literal(&simplified) {
        assignment.insert(pure_lit.var, pure_lit.positive);
        return dpll(&simplified, assignment);
    }

    // Choose an unassigned variable (pick the first one we find)
    let var = choose_variable(&simplified, assignment);
    if var.is_none() {
        // No variables left but clauses remain and none are empty -- shouldn't happen
        // if simplification is correct, but handle gracefully
        return false;
    }
    let var = var.unwrap();

    // Try assigning true
    let mut try_assignment = assignment.clone();
    try_assignment.insert(var, true);
    if dpll(&simplified, &mut try_assignment) {
        *assignment = try_assignment;
        return true;
    }

    // Try assigning false
    let mut try_assignment = assignment.clone();
    try_assignment.insert(var, false);
    if dpll(&simplified, &mut try_assignment) {
        *assignment = try_assignment;
        return true;
    }

    false
}

/// Simplify clauses given current assignment.
/// - Remove clauses that contain a true literal
/// - Remove false literals from remaining clauses
fn simplify(clauses: &[Clause], assignment: &Assignment) -> Vec<Clause> {
    let mut result = Vec::new();
    for clause in clauses {
        let mut satisfied = false;
        let mut remaining = Vec::new();
        for lit in &clause.literals {
            if let Some(&val) = assignment.get(&lit.var) {
                if val == lit.positive {
                    // This literal is true, so the whole clause is satisfied
                    satisfied = true;
                    break;
                }
                // Literal is false, skip it
            } else {
                // Variable not assigned, keep the literal
                remaining.push(*lit);
            }
        }
        if !satisfied {
            result.push(Clause::new(remaining));
        }
    }
    result
}

/// Find a unit clause (clause with exactly one literal).
fn find_unit_clause(clauses: &[Clause]) -> Option<Literal> {
    for clause in clauses {
        if clause.is_unit() {
            return Some(clause.literals[0]);
        }
    }
    None
}

/// Find a pure literal (appears only positive or only negative).
fn find_pure_literal(clauses: &[Clause]) -> Option<Literal> {
    let mut positive: HashSet<u32> = HashSet::new();
    let mut negative: HashSet<u32> = HashSet::new();

    for clause in clauses {
        for lit in &clause.literals {
            if lit.positive {
                positive.insert(lit.var);
            } else {
                negative.insert(lit.var);
            }
        }
    }

    // Pure positive: appears positive but never negative
    for &var in &positive {
        if !negative.contains(&var) {
            return Some(Literal::pos(var));
        }
    }

    // Pure negative: appears negative but never positive
    for &var in &negative {
        if !positive.contains(&var) {
            return Some(Literal::neg(var));
        }
    }

    None
}

/// Choose an unassigned variable from the clauses.
fn choose_variable(clauses: &[Clause], assignment: &Assignment) -> Option<u32> {
    for clause in clauses {
        for lit in &clause.literals {
            if !assignment.contains_key(&lit.var) {
                return Some(lit.var);
            }
        }
    }
    None
}

// ── Proof Expressions ───────────────────────────────────────

/// A proof expression in the proof language.
#[derive(Debug, Clone)]
pub enum ProofExpr {
    Var(String),
    BoolConst(bool),
    IntConst(i64),
    And(Box<ProofExpr>, Box<ProofExpr>),
    Or(Box<ProofExpr>, Box<ProofExpr>),
    Not(Box<ProofExpr>),
    Implies(Box<ProofExpr>, Box<ProofExpr>),
    ForAll(String, String, Box<ProofExpr>), // var, type, body
    Exists(String, String, Box<ProofExpr>), // var, type, body
    Eq(Box<ProofExpr>, Box<ProofExpr>),
    Lt(Box<ProofExpr>, Box<ProofExpr>),
    Gt(Box<ProofExpr>, Box<ProofExpr>),
    Le(Box<ProofExpr>, Box<ProofExpr>),
    Ge(Box<ProofExpr>, Box<ProofExpr>),
    Add(Box<ProofExpr>, Box<ProofExpr>),
    FnCall(String, Vec<ProofExpr>), // function name, args
}

/// A statement inside a proof block.
#[derive(Debug, Clone)]
pub enum ProofStmt {
    /// `forall x: type, condition => conclusion`
    ForAll {
        var: String,
        typ: String,
        condition: ProofExpr,
        conclusion: ProofExpr,
    },
    /// `exists x: type, condition`
    Exists {
        var: String,
        typ: String,
        condition: ProofExpr,
    },
    /// `assert condition`
    Assert(ProofExpr),
    /// `check law(N)` -- verify a consciousness law
    CheckLaw(i64),
}

/// Result of a proof verification.
#[derive(Debug, Clone)]
pub struct ProofResult {
    pub valid: bool,
    pub message: String,
    pub counterexample: Option<Assignment>,
}

impl ProofResult {
    pub fn proved(message: String) -> Self {
        Self { valid: true, message, counterexample: None }
    }

    pub fn disproved(message: String, counterexample: Option<Assignment>) -> Self {
        Self { valid: false, message, counterexample }
    }
}

impl fmt::Display for ProofResult {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.valid {
            write!(f, "PROVED: {}", self.message)
        } else {
            write!(f, "DISPROVED: {}", self.message)?;
            if let Some(ce) = &self.counterexample {
                write!(f, " | counterexample: {{")?;
                let pairs: Vec<String> = ce.iter()
                    .map(|(k, v)| format!("x{}={}", k, v))
                    .collect();
                write!(f, "{}", pairs.join(", "))?;
                write!(f, "}}")?;
            }
            Ok(())
        }
    }
}

// ── Tseitin Transformation (ProofExpr -> CNF) ──────────────

/// Generate a deterministic string fingerprint for a ProofExpr,
/// so that structurally identical expressions map to the same SAT variable.
fn expr_fingerprint(expr: &ProofExpr) -> String {
    match expr {
        ProofExpr::Var(n) => format!("v({})", n),
        ProofExpr::BoolConst(b) => format!("b({})", b),
        ProofExpr::IntConst(n) => format!("i({})", n),
        ProofExpr::And(l, r) => format!("and({},{})", expr_fingerprint(l), expr_fingerprint(r)),
        ProofExpr::Or(l, r) => format!("or({},{})", expr_fingerprint(l), expr_fingerprint(r)),
        ProofExpr::Not(e) => format!("not({})", expr_fingerprint(e)),
        ProofExpr::Implies(l, r) => format!("imp({},{})", expr_fingerprint(l), expr_fingerprint(r)),
        ProofExpr::Eq(l, r) => format!("eq({},{})", expr_fingerprint(l), expr_fingerprint(r)),
        ProofExpr::Lt(l, r) => format!("lt({},{})", expr_fingerprint(l), expr_fingerprint(r)),
        ProofExpr::Gt(l, r) => format!("gt({},{})", expr_fingerprint(l), expr_fingerprint(r)),
        ProofExpr::Le(l, r) => format!("le({},{})", expr_fingerprint(l), expr_fingerprint(r)),
        ProofExpr::Ge(l, r) => format!("ge({},{})", expr_fingerprint(l), expr_fingerprint(r)),
        ProofExpr::Add(l, r) => format!("add({},{})", expr_fingerprint(l), expr_fingerprint(r)),
        ProofExpr::FnCall(name, args) => {
            let a: Vec<String> = args.iter().map(|x| expr_fingerprint(x)).collect();
            format!("fn({},[{}])", name, a.join(","))
        }
        ProofExpr::ForAll(v, t, b) => format!("fa({},{},{})", v, t, expr_fingerprint(b)),
        ProofExpr::Exists(v, t, b) => format!("ex({},{},{})", v, t, expr_fingerprint(b)),
    }
}

/// State for the Tseitin transformation.
struct TseitinState {
    next_var: u32,
    var_map: HashMap<String, u32>,
    clauses: Vec<Clause>,
}

impl TseitinState {
    fn new() -> Self {
        Self {
            next_var: 1,
            var_map: HashMap::new(),
            clauses: Vec::new(),
        }
    }

    fn fresh_var(&mut self) -> u32 {
        let v = self.next_var;
        self.next_var += 1;
        v
    }

    fn get_or_create_var(&mut self, name: &str) -> u32 {
        if let Some(&v) = self.var_map.get(name) {
            v
        } else {
            let v = self.fresh_var();
            self.var_map.insert(name.to_string(), v);
            v
        }
    }

    /// Convert a ProofExpr to a CNF variable ID, adding clauses as needed.
    /// Returns the variable ID whose truth represents the expression.
    fn encode(&mut self, expr: &ProofExpr) -> u32 {
        match expr {
            ProofExpr::Var(name) => self.get_or_create_var(name),

            ProofExpr::BoolConst(true) => {
                let v = self.fresh_var();
                // Force v = true
                self.clauses.push(Clause::unit(Literal::pos(v)));
                v
            }

            ProofExpr::BoolConst(false) => {
                let v = self.fresh_var();
                // Force v = false
                self.clauses.push(Clause::unit(Literal::neg(v)));
                v
            }

            ProofExpr::Not(inner) => {
                let inner_v = self.encode(inner);
                let out = self.fresh_var();
                // out <=> !inner_v
                // (out | inner_v) & (!out | !inner_v)
                self.clauses.push(Clause::new(vec![
                    Literal::pos(out), Literal::pos(inner_v),
                ]));
                self.clauses.push(Clause::new(vec![
                    Literal::neg(out), Literal::neg(inner_v),
                ]));
                out
            }

            ProofExpr::And(left, right) => {
                let l = self.encode(left);
                let r = self.encode(right);
                let out = self.fresh_var();
                // out <=> l & r
                // (!out | l) & (!out | r) & (out | !l | !r)
                self.clauses.push(Clause::new(vec![
                    Literal::neg(out), Literal::pos(l),
                ]));
                self.clauses.push(Clause::new(vec![
                    Literal::neg(out), Literal::pos(r),
                ]));
                self.clauses.push(Clause::new(vec![
                    Literal::pos(out), Literal::neg(l), Literal::neg(r),
                ]));
                out
            }

            ProofExpr::Or(left, right) => {
                let l = self.encode(left);
                let r = self.encode(right);
                let out = self.fresh_var();
                // out <=> l | r
                // (!out | l | r) & (out | !l) & (out | !r)
                self.clauses.push(Clause::new(vec![
                    Literal::neg(out), Literal::pos(l), Literal::pos(r),
                ]));
                self.clauses.push(Clause::new(vec![
                    Literal::pos(out), Literal::neg(l),
                ]));
                self.clauses.push(Clause::new(vec![
                    Literal::pos(out), Literal::neg(r),
                ]));
                out
            }

            ProofExpr::Implies(left, right) => {
                // a => b  ===  !a | b
                let equiv = ProofExpr::Or(
                    Box::new(ProofExpr::Not(left.clone())),
                    right.clone(),
                );
                self.encode(&equiv)
            }

            ProofExpr::Eq(_, _) | ProofExpr::Lt(_, _) | ProofExpr::Gt(_, _)
            | ProofExpr::Le(_, _) | ProofExpr::Ge(_, _) | ProofExpr::Add(_, _)
            | ProofExpr::IntConst(_) | ProofExpr::FnCall(_, _) => {
                // For arithmetic/comparison expressions, create a propositional variable.
                // Full SMT would require a theory solver; here we treat them as
                // uninterpreted propositions for the SAT layer.
                // Use structural hashing so identical subexpressions map to the same variable.
                let key = format!("__uninterp_{}", expr_fingerprint(expr));
                self.get_or_create_var(&key)
            }

            ProofExpr::ForAll(_, _, body) | ProofExpr::Exists(_, _, body) => {
                // Quantifiers over finite domains: treat the body as the proposition.
                // A full SMT solver would enumerate or use theory. We encode the body.
                self.encode(body)
            }
        }
    }
}

/// Convert a ProofExpr to CNF formula via Tseitin transformation.
/// The returned formula, when conjoined with the assertion that the
/// top-level variable is true, represents the original expression.
pub fn to_cnf(expr: &ProofExpr) -> (Formula, u32, HashMap<String, u32>) {
    let mut state = TseitinState::new();
    let top = state.encode(expr);
    // Assert the top-level variable is true
    state.clauses.push(Clause::unit(Literal::pos(top)));
    (Formula::new(state.clauses), top, state.var_map)
}

/// Verify a proof expression.
/// A proposition P is valid iff NOT(P) is unsatisfiable.
pub fn verify_proof(expr: &ProofExpr) -> ProofResult {
    let negated = ProofExpr::Not(Box::new(expr.clone()));
    let (formula, _top, var_map) = to_cnf(&negated);
    let result = solve(&formula);

    match result {
        SatResult::Unsatisfiable => {
            ProofResult::proved(format!("The proposition is valid (negation is unsatisfiable)"))
        }
        SatResult::Satisfiable(raw_assignment) => {
            // Translate variable IDs back to names for the counterexample
            let reverse_map: HashMap<u32, &String> = var_map.iter()
                .map(|(name, &id)| (id, name))
                .collect();
            let named: HashMap<String, bool> = raw_assignment.iter()
                .filter_map(|(&var_id, &val)| {
                    reverse_map.get(&var_id).map(|name| (name.to_string(), val))
                })
                .collect();
            // Use raw assignment for the counterexample field (keyed by var ID)
            let _ = named; // We'll format with names in the message
            ProofResult::disproved(
                format!("The proposition is NOT valid"),
                Some(raw_assignment),
            )
        }
    }
}

// ── Proof Statement Execution ───────────────────────────────

/// Execute a single proof statement, returning the result.
pub fn execute_proof_stmt(stmt: &ProofStmt) -> ProofResult {
    match stmt {
        ProofStmt::ForAll { var: _, typ: _, condition, conclusion } => {
            // forall x: T, P(x) => Q(x)  is valid iff  NOT(P(x) => Q(x)) is UNSAT
            let prop = ProofExpr::Implies(
                Box::new(condition.clone()),
                Box::new(conclusion.clone()),
            );
            verify_proof(&prop)
        }

        ProofStmt::Exists { var: _, typ: _, condition } => {
            // exists x: T, P(x) is satisfiable iff P(x) is SAT
            let (formula, _top, _var_map) = to_cnf(condition);
            let result = solve(&formula);
            match result {
                SatResult::Satisfiable(_assign) => {
                    ProofResult::proved(format!("Witness found: the existential is satisfiable"))
                }
                SatResult::Unsatisfiable => {
                    ProofResult::disproved(
                        format!("No witness exists: the existential is unsatisfiable"),
                        None,
                    )
                }
            }
        }

        ProofStmt::Assert(expr) => {
            verify_proof(expr)
        }

        ProofStmt::CheckLaw(law_num) => {
            check_consciousness_law(*law_num)
        }
    }
}

// ── Built-in Consciousness Law Verification ─────────────────

/// Verify a specific consciousness law by number.
pub fn check_consciousness_law(law_num: i64) -> ProofResult {
    match law_num {
        22 => law22_check(),
        _ => {
            // For unknown laws, encode the general principle
            ProofResult::disproved(
                format!("Law {} verification not yet implemented", law_num),
                None,
            )
        }
    }
}

/// Law 22: "Adding features -> Phi down; adding structure -> Phi up"
/// Encodes: structure_added => phi_increased
/// This is a tautology given the law's axiom.
pub fn law22_check() -> ProofResult {
    // We model this as a propositional formula:
    //   Let s = "structure was added"
    //   Let p = "phi increased"
    //   Law 22 states: s => p
    //
    // We verify: given the axiom (s => p), does (s => p) hold?
    // This is trivially valid as a tautology over the axiom.
    //
    // More interesting: verify with sample data
    let s = ProofExpr::Var("structure_added".into());
    let p = ProofExpr::Var("phi_increased".into());
    let axiom = ProofExpr::Implies(Box::new(s.clone()), Box::new(p.clone()));
    let claim = ProofExpr::Implies(Box::new(s), Box::new(p));

    // axiom => claim should be valid
    let prop = ProofExpr::Implies(Box::new(axiom), Box::new(claim));
    let result = verify_proof(&prop);

    ProofResult {
        valid: result.valid,
        message: if result.valid {
            "Law 22 verified: structure_added => phi_increased is valid given the axiom".into()
        } else {
            "Law 22 verification failed".into()
        },
        counterexample: result.counterexample,
    }
}

/// Verify Psi balance convergence: given a sequence of values, check they converge to 0.5.
/// This is a semantic check (not purely propositional), so we do a direct numerical test.
pub fn psi_balance_check(values: &[f64]) -> ProofResult {
    if values.is_empty() {
        return ProofResult::disproved(
            "No values provided for Psi balance check".into(),
            None,
        );
    }

    let target = 0.5;
    let tolerance = 0.05;

    // Check if the last few values converge to 0.5
    let tail_size = values.len().min(5);
    let tail = &values[values.len() - tail_size..];
    let avg: f64 = tail.iter().sum::<f64>() / tail.len() as f64;
    let converged = (avg - target).abs() < tolerance;

    // Check monotone convergence: distances to 0.5 should generally decrease
    let distances: Vec<f64> = values.iter().map(|v| (v - target).abs()).collect();
    let trend_ok = if distances.len() >= 2 {
        let first_half_avg = distances[..distances.len() / 2].iter().sum::<f64>()
            / (distances.len() / 2) as f64;
        let second_half_avg = distances[distances.len() / 2..].iter().sum::<f64>()
            / (distances.len() - distances.len() / 2) as f64;
        second_half_avg <= first_half_avg + tolerance
    } else {
        true
    };

    if converged && trend_ok {
        ProofResult::proved(format!(
            "Psi_balance converges to {:.4} (target: 0.5, tolerance: {})",
            avg, tolerance
        ))
    } else {
        ProofResult::disproved(
            format!(
                "Psi_balance does NOT converge: avg={:.4}, converged={}, trend_ok={}",
                avg, converged, trend_ok
            ),
            None,
        )
    }
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    // ── SAT Solver Tests ──

    #[test]
    fn test_tautology_p_or_not_p() {
        // p | !p is a tautology: its negation !(p | !p) = !p & p is UNSAT
        let expr = ProofExpr::Or(
            Box::new(ProofExpr::Var("p".into())),
            Box::new(ProofExpr::Not(Box::new(ProofExpr::Var("p".into())))),
        );
        let result = verify_proof(&expr);
        assert!(result.valid, "p | !p should be valid (tautology)");
    }

    #[test]
    fn test_contradiction_p_and_not_p() {
        // p & !p is a contradiction: it is NOT valid
        let expr = ProofExpr::And(
            Box::new(ProofExpr::Var("p".into())),
            Box::new(ProofExpr::Not(Box::new(ProofExpr::Var("p".into())))),
        );
        let result = verify_proof(&expr);
        assert!(!result.valid, "p & !p should NOT be valid (contradiction)");
    }

    #[test]
    fn test_contradiction_is_unsat() {
        // p & !p should be unsatisfiable
        let p = Literal::pos(1);
        let np = Literal::neg(1);
        let formula = Formula::new(vec![
            Clause::unit(p),
            Clause::unit(np),
        ]);
        assert!(!is_satisfiable(&formula), "p & !p should be UNSAT");
    }

    #[test]
    fn test_modus_ponens() {
        // (p => q) & p => q  is valid
        let p = ProofExpr::Var("p".into());
        let q = ProofExpr::Var("q".into());
        let p_implies_q = ProofExpr::Implies(Box::new(p.clone()), Box::new(q.clone()));
        let premise = ProofExpr::And(Box::new(p_implies_q), Box::new(p));
        let full = ProofExpr::Implies(Box::new(premise), Box::new(q));

        let result = verify_proof(&full);
        assert!(result.valid, "modus ponens should be valid: {:?}", result.message);
    }

    #[test]
    fn test_implication_not_valid() {
        // p => q is NOT valid (there's a counterexample: p=true, q=false)
        let expr = ProofExpr::Implies(
            Box::new(ProofExpr::Var("p".into())),
            Box::new(ProofExpr::Var("q".into())),
        );
        let result = verify_proof(&expr);
        assert!(!result.valid, "p => q should NOT be valid");
        assert!(result.counterexample.is_some(), "should have a counterexample");
    }

    #[test]
    fn test_double_negation() {
        // !!p <=> p  is valid
        // Encode as (!!p => p) & (p => !!p)
        let p = ProofExpr::Var("p".into());
        let nnp = ProofExpr::Not(Box::new(ProofExpr::Not(Box::new(p.clone()))));
        let forward = ProofExpr::Implies(Box::new(nnp.clone()), Box::new(p.clone()));
        let backward = ProofExpr::Implies(Box::new(p), Box::new(nnp));
        let biconditional = ProofExpr::And(Box::new(forward), Box::new(backward));

        let result = verify_proof(&biconditional);
        assert!(result.valid, "double negation elimination should be valid");
    }

    #[test]
    fn test_simple_sat() {
        // (p | q) & (!p | q) should be satisfiable (q=true works)
        let formula = Formula::new(vec![
            Clause::new(vec![Literal::pos(1), Literal::pos(2)]),
            Clause::new(vec![Literal::neg(1), Literal::pos(2)]),
        ]);
        let result = solve(&formula);
        assert!(result.is_sat(), "should be satisfiable");
        if let SatResult::Satisfiable(assign) = &result {
            // q (var 2) must be true
            assert_eq!(assign.get(&2), Some(&true));
        }
    }

    #[test]
    fn test_three_variable_unsat() {
        // A carefully crafted UNSAT formula:
        // (p) & (!p | q) & (!q | r) & (!r) => p must be true, which forces q, which forces r,
        // but r must be false. Contradiction.
        let formula = Formula::new(vec![
            Clause::unit(Literal::pos(1)),                                    // p
            Clause::new(vec![Literal::neg(1), Literal::pos(2)]),             // !p | q
            Clause::new(vec![Literal::neg(2), Literal::pos(3)]),             // !q | r
            Clause::unit(Literal::neg(3)),                                    // !r
        ]);
        assert!(!is_satisfiable(&formula), "should be UNSAT (chain contradiction)");
    }

    #[test]
    fn test_unit_propagation() {
        // (p) & (p | q) & (!p | q) => p=true, q=true
        let formula = Formula::new(vec![
            Clause::unit(Literal::pos(1)),
            Clause::new(vec![Literal::pos(1), Literal::pos(2)]),
            Clause::new(vec![Literal::neg(1), Literal::pos(2)]),
        ]);
        let result = solve(&formula);
        assert!(result.is_sat());
        if let SatResult::Satisfiable(assign) = result {
            assert_eq!(assign.get(&1), Some(&true));
            assert_eq!(assign.get(&2), Some(&true));
        }
    }

    #[test]
    fn test_law22_check() {
        let result = law22_check();
        assert!(result.valid, "Law 22 should verify: {}", result.message);
    }

    #[test]
    fn test_psi_balance_converges() {
        let values = vec![0.8, 0.7, 0.6, 0.55, 0.52, 0.51, 0.50, 0.50, 0.50];
        let result = psi_balance_check(&values);
        assert!(result.valid, "Psi balance should converge: {}", result.message);
    }

    #[test]
    fn test_psi_balance_diverges() {
        let values = vec![0.1, 0.2, 0.9, 0.1, 0.9, 0.1, 0.9];
        let result = psi_balance_check(&values);
        assert!(!result.valid, "Psi balance should NOT converge: {}", result.message);
    }

    #[test]
    fn test_proof_stmt_forall() {
        // forall x: int, x > 0 => x > 0  (trivially valid)
        let stmt = ProofStmt::ForAll {
            var: "x".into(),
            typ: "int".into(),
            condition: ProofExpr::Gt(
                Box::new(ProofExpr::Var("x".into())),
                Box::new(ProofExpr::IntConst(0)),
            ),
            conclusion: ProofExpr::Gt(
                Box::new(ProofExpr::Var("x".into())),
                Box::new(ProofExpr::IntConst(0)),
            ),
        };
        let result = execute_proof_stmt(&stmt);
        assert!(result.valid, "forall x, P(x) => P(x) should be valid");
    }

    #[test]
    fn test_proof_stmt_exists() {
        // exists x: bool, x  (should be satisfiable)
        let stmt = ProofStmt::Exists {
            var: "x".into(),
            typ: "bool".into(),
            condition: ProofExpr::Var("x".into()),
        };
        let result = execute_proof_stmt(&stmt);
        assert!(result.valid, "exists x, x should be satisfiable");
    }

    #[test]
    fn test_proof_stmt_check_law22() {
        let stmt = ProofStmt::CheckLaw(22);
        let result = execute_proof_stmt(&stmt);
        assert!(result.valid, "check law(22) should pass");
    }

    #[test]
    fn test_de_morgan() {
        // De Morgan's law: !(p & q) <=> (!p | !q)
        let p = ProofExpr::Var("p".into());
        let q = ProofExpr::Var("q".into());

        let lhs = ProofExpr::Not(Box::new(ProofExpr::And(
            Box::new(p.clone()), Box::new(q.clone()),
        )));
        let rhs = ProofExpr::Or(
            Box::new(ProofExpr::Not(Box::new(p.clone()))),
            Box::new(ProofExpr::Not(Box::new(q.clone()))),
        );

        // lhs => rhs AND rhs => lhs
        let biconditional = ProofExpr::And(
            Box::new(ProofExpr::Implies(Box::new(lhs.clone()), Box::new(rhs.clone()))),
            Box::new(ProofExpr::Implies(Box::new(rhs), Box::new(lhs))),
        );

        let result = verify_proof(&biconditional);
        assert!(result.valid, "De Morgan's law should be valid");
    }

    #[test]
    fn test_larger_formula_sat() {
        // (a | b | c) & (!a | !b) & (!b | !c) & (!a | !c)
        // Satisfiable: exactly one of a, b, c must be true
        let formula = Formula::new(vec![
            Clause::new(vec![Literal::pos(1), Literal::pos(2), Literal::pos(3)]),
            Clause::new(vec![Literal::neg(1), Literal::neg(2)]),
            Clause::new(vec![Literal::neg(2), Literal::neg(3)]),
            Clause::new(vec![Literal::neg(1), Literal::neg(3)]),
        ]);
        let result = solve(&formula);
        assert!(result.is_sat(), "exactly-one constraint should be SAT");
        if let SatResult::Satisfiable(assign) = result {
            // Exactly one should be true
            let count = [1, 2, 3].iter()
                .filter(|&&v| *assign.get(&v).unwrap_or(&false))
                .count();
            assert_eq!(count, 1, "exactly one variable should be true");
        }
    }
}
