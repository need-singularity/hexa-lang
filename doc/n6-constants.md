# n=6 Constant Reference -- HEXA-LANG

Every design constant in HEXA-LANG is derived from the arithmetic of n=6.

## The Core Theorem

```
    For all n >= 2:
        sigma(n) * phi(n) = n * tau(n)  <==>  n = 6

    sigma(6) * phi(6) = 6 * tau(6)
        12   *   2    = 6 *   4
              24      =    24
```

## Primary Constants

```
    +----------+-------+------------------+------------------------------+
    | Function | Value | Mathematical     | Language Design Mapping       |
    +==========+=======+==================+==============================+
    | n        |   6   | Perfect number   | Paradigms, grammar levels,   |
    |          |       | 1+2+3=6          | pipeline stages              |
    +----------+-------+------------------+------------------------------+
    | sigma(6) |  12   | Sum of divisors  | Keyword groups, operator     |
    |          |       | 1+2+3+6=12       | subcategories, IDE features  |
    +----------+-------+------------------+------------------------------+
    | tau(6)   |   4   | Number of        | Type layers, visibility      |
    |          |       | divisors {1,2,3,6}| levels, JIT tiers           |
    +----------+-------+------------------+------------------------------+
    | phi(6)   |   2   | Euler totient    | Compile modes (AOT/JIT),     |
    |          |       | gcd(k,6)=1: {1,5}| assignment operators         |
    +----------+-------+------------------+------------------------------+
    | sopfr(6) |   5   | Sum of prime     | Error classes (5), function  |
    |          |       | factors 2+3=5    | keywords, type keywords      |
    +----------+-------+------------------+------------------------------+
    | J2(6)    |  24   | Jordan totient   | Total operators,             |
    |          |       | sigma*phi=24     | Leech lattice dimension      |
    +----------+-------+------------------+------------------------------+
    | mu(6)    |   1   | Mobius function  | Squarefree -- clean          |
    |          |       | 6=2*3 squarefree | decomposition, unique        |
    +----------+-------+------------------+------------------------------+
    | lambda(6)|   2   | Carmichael       | Minimal iteration cycle,     |
    |          |       | lcm(1,2)=2       | binary outcomes              |
    +----------+-------+------------------+------------------------------+
```

## Derived Constants

```
    +--------------+-------+------------------------------------------+
    | Expression   | Value | Language Design Mapping                   |
    +==============+=======+==========================================+
    | sigma - tau  |   8   | Primitive types (int float bool char     |
    |              |       | string byte void any)                    |
    +--------------+-------+------------------------------------------+
    | sigma * tau  |  48   | Keyword base (48 + sopfr = 53 total)    |
    +--------------+-------+------------------------------------------+
    | sigma - phi  |  10   | RoPE theta 10^4                         |
    +--------------+-------+------------------------------------------+
    | sigma + 1    |  13   | (reserved)                              |
    +--------------+-------+------------------------------------------+
    | n!           | 720   | fact(6) = 720                           |
    +--------------+-------+------------------------------------------+
```

## Constant Mapping Diagram

```
    sigma*phi = n*tau = 24 = J2
         / \         / \
       s=12  f=2   n=6  t=4
        |     |     |     |
        |     |     |     +-- 4 type layers
        |     |     |     +-- 4 visibility levels
        |     |     |     +-- 4 JIT optimization tiers
        |     |     |     +-- 4 variable keywords
        |     |     |     +-- 4 memory keywords
        |     |     |
        |     |     +-- 6 paradigms
        |     |     +-- 6 control flow keywords
        |     |     +-- 6 grammar levels
        |     |     +-- 6 pipeline stages
        |     |     +-- 6 arithmetic operators
        |     |     +-- 6 comparison operators
        |     |
        |     +-- 2 compile modes (AOT, JIT)
        |     +-- 2 assignment operators (=, :=)
        |     +-- 2 special operators (.., ->)
        |
        +-- 12 keyword groups
        +-- 12 IDE feature groups
```

## Keyword Distribution

```
    53 Keywords = sigma*tau + sopfr = 48 + 5
    ==========================================

    Group sizes by n=6 constant:
    ----------------------------
     n=6:     1 group  x 6 keywords =  6  (control flow)
     sopfr=5: 3 groups x 5 keywords = 15  (type, func, error)
     tau=4:   8 groups x 4 keywords = 32  (var,mod,mem,con,eff,proof,meta,ai)
     -----------------------------------------
     Total: 12 groups, 53 keywords

    Verification:
     6 + 15 + 32 = 53
     sigma*tau + sopfr = 48 + 5 = 53
```

## Operator Distribution

```
    24 Operators = J2(6)
    =====================

    +----------------+-------+-----------+
    | Category       | Count | Constant  |
    +================+=======+===========+
    | Arithmetic     |   6   | n         |
    | Comparison     |   6   | n         |
    | Logical        |   4   | tau       |
    | Bitwise        |   4   | tau       |
    | Assignment     |   2   | phi       |
    | Special        |   2   | phi       |
    +----------------+-------+-----------+
    | Total          |  24   | J2(6)     |
    +----------------+-------+-----------+

    Distribution: 6+6+4+4+2+2 = n+n+t+t+f+f = 2n+2t+2f
```

## Type System Constants

```
    sigma - tau = 8 primitive types
    ================================

    +-------+--------+-----------+
    | #     | Type   | Category  |
    +=======+========+===========+
    | 1     | int    | numeric   |
    | 2     | float  | numeric   |
    | 3     | bool   | logical   |
    | 4     | char   | text      |
    | 5     | string | text      |
    | 6     | byte   | raw       |
    | 7     | void   | unit      |
    | 8     | any    | dynamic   |
    +-------+--------+-----------+

    tau = 4 type layers
    ====================

    Layer 4: Function  -- fn(A) -> B
    Layer 3: Reference -- &T, &mut T
    Layer 2: Composite -- struct, enum, tuple, array
    Layer 1: Primitive -- the 8 types above
```

## Memory Model Constants

```
    Egyptian Fraction: 1/2 + 1/3 + 1/6 = 1
    =========================================

    +===============+==========+======+
    |  Stack  1/2   | Heap 1/3 |Arena |
    |  50% budget   | 33%      | 17%  |
    |  value types  | ref types| 1/6  |
    |  zero overhead| own/borw | temp |
    +===============+==========+======+

    The three regions partition memory space
    exactly, with no waste (sum = 1).

    Memory keywords (tau=4):
    own, borrow, move, drop
```

## Pipeline Constants

```
    n = 6 Pipeline Stages
    ======================

    Source --> [1] Tokenize
          --> [2] Parse
          --> [3] Check (type)
          --> [4] Optimize
          --> [5] Codegen
          --> [6] Execute

    Each stage is one of n=6 phases.
```

## Error System Constants

```
    sopfr = 5 Error Classes
    ========================

    1. Syntax  -- parse errors
    2. Type    -- type mismatch
    3. Name    -- undefined variable
    4. Runtime -- division by zero, etc.
    5. Logic   -- assertion failures

    5 = sopfr(6) = 2 + 3
```

## Visibility Constants

```
    tau = 4 Visibility Levels
    ==========================

    1. Private  -- default (module-internal)
    2. Module   -- visible within module
    3. Crate    -- visible within crate
    4. Public   -- visible everywhere (pub)
```

## Cross-References to n6-architecture

| Constant | HEXA-LANG | Chip Architecture | AI/LLM | Energy |
|----------|-----------|-------------------|--------|--------|
| n=6 | paradigms | pipeline stages | MoE Egyptian | cells |
| sigma=12 | keyword groups | SM groups | attention heads | -- |
| tau=4 | type layers | HBM stacks | precision formats | -- |
| phi=2 | compile modes | FP ratio | binary choices | -- |
| J2=24 | operators | Leech lattice | Leech surface | J2 cells |
| sigma-tau=8 | primitives | Bott periodicity | LoRA rank | -- |

---

*HEXA-LANG n=6 Constant Reference v0.1*
*All 14 design constants are EXACT matches to n=6 arithmetic functions.*
