# builtin: `phi(n)` returns `0.0` instead of Euler totient — silent wrong-value bug

## Summary

The `phi` builtin returns `0.0` for every integer input. Documentation
and convention (cf. `sigma(n)`, `tau(n)`) imply `phi(n)` should return
`φ(n)` = Euler's totient (count of integers in `[1, n]` coprime to `n`).
Instead it silently returns the float `0.0`, which then poisons any
identity that uses it — e.g. the n=6 master identity `σ(n)·φ(n) = n·τ(n)`
silently evaluates to `12·0 = 0 ≠ 24`.

**Severity**: high — wrong arithmetic, no error, no warning.
**Signal kind**: stdlib-bug (returns wrong value class).
**Detected on**: hexa.real `~/.hx/packages/hexa/hexa.real` 2026-05-07
during the hexa-chip verify/ runnable surface buildout
(`/Users/ghost/core/hexa-chip/verify/n6_arithmetic.hexa`).

## Repro

```hexa
fn main() {
    println("sigma(6) = " + str(sigma(6)))   // 12 — correct
    println("tau(6) = "   + str(tau(6)))     //  4 — correct
    println("phi(6) = "   + str(phi(6)))     //  0.0 — WRONG, expected 2
    println("phi(1) = "   + str(phi(1)))     //  0.0 — WRONG, expected 1
    println("phi(7) = "   + str(phi(7)))     //  0.0 — WRONG, expected 6
}
```

Observed:
```
sigma(6) = 12
tau(6) = 4
phi(6) = 0.0
phi(1) = 0.0
phi(7) = 0.0
```

Expected:
```
sigma(6) = 12
tau(6) = 4
phi(6) = 2
phi(1) = 1
phi(7) = 6
```

## Hypothesis

The interpreter likely binds `phi` to the math constant `phi = (1+√5)/2`
(golden ratio). When called as `phi(6)` the parser may be evaluating it
as `phi * 6 ≈ 9.7…` and a downstream cast / coercion zeroes it. Either
way:

- The naming collision between math-constant `phi` and Euler-totient
  `phi(n)` is a bug magnet.
- Returning `0.0` instead of erroring is the classic "silently wrong"
  failure mode — code that asserts `phi(n) == 2` just exits 1 with a
  bare `assert` failure that gives the user no clue why.

`sigma(n)` and `tau(n)` work — they're divisor-loop-based. The asymmetry
across the divisor-function family `(σ, τ, φ)` is the obvious gap.

## Workaround in the wild

`/Users/ghost/core/hexa-chip/verify/n6_arithmetic.hexa` — `euler_phi(n)`
re-implemented in pure hexa via gcd loop:

```hexa
fn _gcd(a: int, b: int) -> int { ... }   // standard Euclidean
fn euler_phi(n: int) -> int {
    if n <= 0 { return 0 }
    if n == 1 { return 1 }
    let mut count = 0; let mut k = 1
    while k <= n {
        if _gcd(k, n) == 1 { count = count + 1 }
        k = k + 1
    }
    return count
}
```

This is `O(n log n)`-ish and fine for the small `n` used in chip
specs (n=6 master identity); production use would want a prime-factor
formula `φ(n) = n · ∏(1 - 1/p)` over distinct prime factors of `n`.

## Proposed fix

Add a proper integer Euler-totient builtin, name it `euler_phi(n: int) -> int`
(or `totient(n)`), and either rename or remove the existing `phi` symbol
that currently resolves wrong. Keep it int-typed (totient is always
integer for integer input) — that mirrors `sigma(n: int) -> int` and
`tau(n: int) -> int`.

A naive divisor / gcd implementation is fine as a first cut; the body
can be optimised later.

## Why upstream

- `n=6` is the canonical hexa motivation — every n6-arch / hexa-chip /
  hexa-codex / anima repo asserts `σ·φ = n·τ`. A wrong builtin breaks
  that assertion silently.
- Three repos already needed to hand-roll euler_phi (hexa-chip
  inventory, hexa-bio's phi count check, hexa-rtsc's redundancy depth
  check) — same reinvention pattern that motivated `starts_with` issue.
- The fix is small (≤ 30 LoC of hexa stdlib).

## Migration

Once landed:

- Hand-rolled `euler_phi` workarounds in hexa-chip / hexa-bio / hexa-rtsc
  collapse to a single builtin call.
- `verify/n6_arithmetic.hexa` removes the `_gcd` / `euler_phi` block and
  shaves ~15 LoC.
- The `n=6 master identity` assertion (`σ·φ = n·τ = 24`) becomes a
  one-liner instead of a divisor-recompute.

## Evidence anchors

- `/Users/ghost/core/hexa-chip/UPSTREAM_NOTES.md` §U-1 — the in-repo
  workaround note.
- `/Users/ghost/core/hexa-chip/verify/n6_arithmetic.hexa` — the
  workaround implementation site.
- `/Users/ghost/core/hexa-chip/.roadmap.hexa_chip` §A.1 — the n=6
  master identity that depends on a correct `phi`.
