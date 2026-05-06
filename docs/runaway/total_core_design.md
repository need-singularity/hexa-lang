# Total Functional Core + Process Layer 분리 설계

**status:** draft / RFC
**date:** 2026-05-06
**authors:** runaway-prevention working group
**scope:** hexa-lang language semantics, sema layer, codegen
**sibling docs:** `docs/runaway/effect_system_*.md` (concurrent worker), `docs/phase_delta_language_v2_spec.ai.md` (purity attribute precedent)

---

## 0. Executive summary

폭주(runaway: stack overflow, infinite loop, divergent compile-time eval, exec 폭발)는 hexa-lang 의 self-host 컴파일러와 stdlib 양쪽에서 반복적으로 재발해 왔다. 본 문서는 이를 *deep* 단계에서 차단하는 두 축을 제안한다.

1. **Total functional core (TFC):** 모든 hexa 함수는 기본값으로 *total* — 즉 모든 입력에 대해 유한 시간 안에 값을 산출함이 검증되어야 한다. 검증되지 않으면 컴파일러가 reject 하거나 `partial` annotation 을 강제한다 (Coq `Function`/Agda termination checker/Idris `total` 키워드와 동치 모델).
2. **Process layer 격리:** 실패·발산·OS effect 가 발생할 수 있는 모든 계산은 `Process` (또는 `IO`, `Eff`) monad 안으로 격리된다. Pure layer 는 *parametric polymorphism + algebraic data types + structurally-recursive function* 만 허용한다. 폭주 가능 코드는 type signature 에 명시되며, runtime supervisor 가 timeout/quota 를 강제한다.
3. **Productive corecursion:** 무한 stream/codata 는 *guardedness* 검증 (Coq `CoFixpoint`, Agda `coinductive`) 으로 productive 함을 정적 보장 — `take n` 을 호출하면 유한 시간 안에 prefix 를 반환한다.

이 두 축이 결합되면 폭주는 (a) Process monad 안의 사용자-가시 effect 로 압축되고, (b) Pure layer 에서는 정의 시점에 거부된다. 결과: 컴파일러 self-host 의 무한 루프, stdlib 의 exec 폭발, AOT cache invalidation 의 fixpoint 발산이 *type-level* 에서 차단된다.

---

## 1. 배경 — 왜 지금이 적기인가

### 1.1 hexa-lang 의 현재 폭주 표면

self-host 컴파일러 (`self/parser.hexa`, `self/sema.hexa`, `self/lsp.hexa`, `self/bootstrap.hexa`) 와 stdlib (`stdlib/portable_fs.hexa`, `stdlib/proc.hexa`) 는 다음 폭주 패턴을 반복했다:

| 폭주 종류 | 사례 | 현재 방어 |
|---|---|---|
| infinite loop in parser | `parser.hexa` 의 attr 흡수 루프가 token 을 소비하지 않을 때 (`@pure` 흡수 버그) | ad-hoc `if pos == prev { break }` |
| divergent constant-fold | `@pure` fn 안의 fixpoint eval | 미구현 — runaway risk |
| exec 폭주 | `portable_fs.hexa` 가 매 호출마다 `exec("stat ...")` 6번 발사 | resolver bypass marker (recent commit `bafedbcd`) |
| AOT cache fixpoint | resolver routing → cache invalidate → re-resolve → infinite | resolver_routing_fix (commit `acf4e5e3`) |
| LSP feedback loop | `self/lsp.hexa` 의 incremental analysis fixpoint | 현재 worktree 에 unstaged change |

각각이 ad-hoc fix 로 처리되고 있다. *근본* 원인은 type system 이 termination 을 강제하지 않기 때문이다.

### 1.2 prior art

- **Coq:** `Function ... {measure ...}` 또는 `Program Fixpoint` — well-founded recursion 강제. `CoFixpoint` 는 guardedness 강제.
- **Agda:** termination checker (default on); `{-# TERMINATING #-}` pragma 만 escape hatch. Sized types (`Size`) 로 corecursion 관리.
- **Idris 2:** `total` 키워드 — `total fn x = ...` 가 termination 을 보장. `partial` 은 명시 escape. `covering` 은 patterns 만 total.
- **Dhall:** *non-Turing-complete* 언어의 극단 사례 — 모든 표현식이 normalization 종료. hexa 는 이 정도 극단까진 가지 않으나 pure layer 에서는 유사한 정신.
- **Haskell `Data.Functor.Identity` / `IO` 분리:** 실용적 모범 — pure 와 IO 를 monad 로 분리하지만 termination 은 enforce 하지 않음 (hexa 는 한 단계 더 강하게 enforce).
- **Koka / Eff / Frank:** algebraic effect rows 로 effect 격리. hexa 의 (concurrent worker 가 작업 중인) effect system 과 자연스럽게 결합.

---

## 2. 두 layer 의 boundary

### 2.1 Pure core (TFC)

다음만 허용:

- algebraic data types (sum + product), 단 *strictly positive* recursion 만.
- pattern matching with exhaustiveness check.
- *structurally* 감소하는 recursion (= primitive recursion + 그 자명한 확장). measure-based recursion 은 explicit `decreases` clause 필요.
- record / tuple / immutable map / immutable list.
- `let`, `match`, lambda, application.
- 다른 pure fn 호출.

다음 금지:

- 가변 reference, mutable state.
- `exec`, file I/O, network, time, RNG.
- unbounded `while`, unbounded `loop` — `for x in finite_collection` 만 허용.
- `Process<a>`, `Future<a>`, `IO<a>` 타입을 unwrap 하는 어떤 op (예: `await`, `unsafePerformIO`).
- Type-level recursion 으로 비약하는 `@meta`/`@compile_time_eval` (별도 fuel-budget 으로 처리).

### 2.2 Process layer

Pure core 외 모든 것. 형식적으로:

```
Process<E, A>   // effect row E, return type A
  ::= return : A -> Process<{}, A>
    | bind   : Process<E1, A> -> (A -> Process<E2, B>) -> Process<E1 ∪ E2, B>
    | perform: op : Op<E, A> -> Process<{op}, A>
```

E 는 effect row (concurrent worker 의 effect system 에서 import). 폭주 가능 op (`exec`, `unbounded_loop`, `partial_pattern_match`, `external_callback`) 는 모두 effect 로 표시된다.

**경계 규칙 (boundary law):**
- Pure → Process: `return : a -> Process<{}, a>` 자유.
- Process → Pure: 불가능. monad 를 unwrap 하려면 top-level (main) 에서 *runner* 가 필요. 즉 Pure 안에서는 Process 를 *기술* 만 가능하고 *실행* 불가.
- 컴파일러는 두 layer 가 type 으로 분리됨을 강제. annotation `@pure` (이미 parser.hexa 에 존재) 는 곧 "이 함수의 본문은 TFC subset 안에 있다" 의 alias 가 된다.

### 2.3 마이그레이션 — annotation gradient

기존 hexa 코드는 갑자기 total 화할 수 없다. 다음 4-step gradient:

1. **Phase α (현재):** `@pure` 는 의미 약함 — sema 가 idempotency hint 정도로 사용.
2. **Phase β:** `@pure` 가 *syntactic* total check 를 trigger. structural recursion + no-IO 만 통과. 위반은 warning.
3. **Phase γ:** `@pure` 미명시 fn 도 default-pure 를 시도하되, IO/recursion 위반 시 자동으로 `Process<E, A>` 로 inferred. annotation `@partial` 또는 `@process` 로 explicit escape.
4. **Phase δ:** Pure default. `Process` 는 explicit 이 강제됨. legacy code 는 `@partial` 자동 부착 migration script.

---

## 3. Total fn 검증 알고리즘

3-tier escalation. 각 tier 에서 통과하면 다음 tier 로 가지 않는다.

### Tier 1 — Syntactic structural recursion (fast, ~all real code)

검사: 함수 `f(x1, ..., xn)` 의 모든 재귀 호출 `f(y1, ..., yn)` 에 대해, 어떤 인자 `xi` 가 ADT decomposition (pattern match) 의 *strict subterm* 으로 `yi` 위치에 들어가야 한다.

- 통과: total 확정, 코드 생성 정상.
- 실패: Tier 2 escalate.

구현 비용: 낮음 (sema pass 에서 AST 1회 traverse). Coq 의 `Fixpoint` default check 와 동치.

예: `len(xs : List<a>) = match xs { Nil -> 0, Cons(h, t) -> 1 + len(t) }` — `t` 가 `xs` 의 strict subterm. 통과.

### Tier 2 — Lexicographic / measure with SMT (medium, ~5–10% 코드)

`@decreases <expr>` annotation 또는 자동 추론된 measure (e.g. tuple of args) 가 모든 재귀 호출에서 strictly decreasing 임을 SMT 로 증명.

- SMT backend: 기존 `attr_format`/AOT 와 같은 Z3 또는 cvc5 binding 재사용.
- 통과 시 termination certificate 를 build cache 에 저장 (재컴파일 시 skip).
- 실패: Tier 3 escalate or `@partial` 강요.

예:
```
@decreases (m + n)
fn ackermann_like(m: Nat, n: Nat) -> Nat = ...
```

### Tier 3 — Coinductive guardedness (productive corecursion)

`@codata` 또는 `Stream<a>` 반환 fn 에 적용. 검사: 재귀 호출이 반드시 *constructor* 아래에 nested 되어야 한다 (guarded).

```
codata Stream<a> = Cons(head: a, tail: () -> Stream<a>)

@productive
fn nats_from(n: Int) -> Stream<Int> =
    Cons(n, () => nats_from(n + 1))   // recursive call under Cons constructor — guarded ✓
```

guardedness 위반 예:
```
fn bad() -> Stream<Int> = bad()       // no constructor wraps recursion — REJECTED
```

검사 구현: AST 위에서 "재귀 호출까지 거치는 path 에 constructor 가 최소 1개" 를 확인하는 traversal. Coq `CoFixpoint` 의 syntactic guardedness 와 동치.

`Stream.take n s` 는 `Pure` layer 로 *내려올 수 있다* — 왜냐하면 productive stream 으로부터 prefix 추출은 `n` 에 대한 structural recursion 이라 total. 이것이 corecursion 이 폭주 없이 무한 데이터를 다루는 핵심.

### Tier 4 — Escape: `@partial`

위 3개를 통과 못 하면 함수는 `partial` 로 표시된다. 그 본문은 자동으로 `Process<{partial}, A>` 타입으로 lifted, caller 도 같은 effect 를 inherit.

---

## 4. Process monad 와 effect system 통합

(concurrent worker 의 effect system 결과와 합치는 지점 — 본 문서는 protocol 만 명시.)

### 4.1 Effect row 정의

```
type Effect = exec | fs_read | fs_write | net | time | rng | partial | spawn | mut
type Process<E: Set<Effect>, A> = ...
```

### 4.2 통합 지점

| Effect-system 산출물 | 본 설계 사용처 |
|---|---|
| `effect Op { ... }` declaration | Process layer 의 op 정의 |
| handler / runner | Process → top-level main 에서 effect 처리 |
| effect row inference | Tier 3 → 4 escalation 시 자동 effect annotation |
| effect polymorphism (`forall E. ...`) | pure higher-order fn (e.g. `map`) 이 callback 의 effect 를 transparent 하게 propagate |

특히 `map : (a -> b) -> List<a> -> List<b>` 는 pure (callback 도 pure 일 때). Effect-poly 버전 `mapM : (a -> Process<E, b>) -> List<a> -> Process<E, List<b>>` 도 자동 생성. 두 변형이 source 에서 같은 이름이지만 row variable 로 분리.

### 4.3 boundary 강제

sema 단계에서 다음 invariant:

```
Γ ⊢ e : A      A 가 non-Process     ⇒   e 안에 effect op 호출 없음
Γ ⊢ e : Process<E, A>               ⇒   E 에 e 의 모든 op 가 포함됨
```

위반 시 컴파일 에러. error message 는 "you called `exec` inside a function declared `@pure`. Either remove the call or change return type to `Process<{exec}, _>`." 처럼 actionable.

---

## 5. Stream / codata 도입

### 5.1 syntax 제안

```
codata Stream<a> = Cons(a, () -> Stream<a>)
codata Conats   = Zero | Succ(() -> Conats)
codata Tree<a>  = Node(a, () -> List<Tree<a>>)   // finitely-branching infinite tree
```

`() -> X` 는 thunk 표기 — strict 언어인 hexa 에서 lazy productive 보장.

### 5.2 productive 검증

§3 Tier 3 와 동일. 추가로:

- mutual corecursion: 호출 cycle 안에 적어도 한 constructor.
- nested pattern: `match s { Cons(h, t) -> Cons(f(h), map_s(g, t())) }` 처럼 `t()` 를 pattern 안에서 force 후 재귀 — guarded 인정.

### 5.3 stdlib API

```
@productive fn iterate(f: a -> a, x: a) -> Stream<a>
@productive fn nats() -> Stream<Int>
@total      fn take(n: Int, s: Stream<a>) -> List<a>     // n 에 structural recursion
@total      fn head(s: Stream<a>) -> a
@total      fn tail(s: Stream<a>) -> Stream<a>
@productive fn map_s(f: a -> b, s: Stream<a>) -> Stream<b>
@productive fn filter_s(p: a -> Bool, s: Stream<a>) -> Stream<a>
            // ⚠ 주의: filter_s 는 productive 하지 않을 수 있음 (모든 원소가 p 거부)
            // → 실제로는 Process<{partial}, Stream<b>> 또는 size-bounded variant 제공
```

### 5.4 corecursion vs runtime cost

guarded corecursion 은 *thunk* 를 사용하므로 strict-by-default 인 hexa 는 thunk 를 명시 (`() -> ...`). AOT 는 thunk 를 closure → on-demand call 로 컴파일. 메모리: lazy list 의 spine 만 alive.

---

## 6. 예시 코드

### 6.1 `Stream.take` — total

```hexa
@total
fn take(n: Int, s: Stream<a>) -> List<a> =
    if n <= 0 {
        Nil
    } else {
        match s {
            Cons(h, t) -> Cons(h, take(n - 1, t()))
        }
    }
```

검증: `n` 이 매 재귀 호출에서 strictly decreasing (Tier 1 syntactic 통과 — `n - 1`). 모든 input 에 대해 |n| 단계 후 종료.

### 6.2 `fix` — total 변형 vs partial 변형

**total 변형 (well-founded):**
```hexa
@decreases (measure_of x)
@total
fn fix_wf<a, b>(f: (a -> b) -> a -> b, x: a) -> b =
    f((y) => fix_wf(f, y), x)
    // ⚠ 위 정의는 그대로는 Tier 1 통과 못함.
    // 실용 hexa 에서는 fix 를 primitive 로 두거나
    // measure 가 명시된 specialized variant 를 제공.
```

**partial 변형 (Process):**
```hexa
@partial
fn fix<a, b>(f: (a -> b) -> a -> b, x: a) -> Process<{partial}, b> =
    return (f((y) => fix(f, y).run_partial(), x))
    // 사용자는 explicit 하게 Process effect 를 받아야 함
```

### 6.3 `fold` — total 변형

```hexa
@total
fn foldl<a, b>(f: (b, a) -> b, init: b, xs: List<a>) -> b =
    match xs {
        Nil         -> init
        Cons(h, t)  -> foldl(f, f(init, h), t)
    }
```

`xs` 가 strict subterm (`t`) 로 감소. Tier 1 통과. `f` 가 pure 면 전체 pure, `f : (b, a) -> Process<E, b>` 면 effect-poly 버전 (`foldlM`) 이 inference 로 갈라짐.

### 6.4 `fold` — partial 변형 (lazy fold of stream)

```hexa
@partial
fn foldl_stream<a, b>(f: (b, a) -> b, init: b, s: Stream<a>) -> Process<{partial}, b> =
    // 무한 stream 위 fold 는 종료 보장 없음 → Process<{partial}>
    match s {
        Cons(h, t) -> foldl_stream(f, f(init, h), t())
    }
```

### 6.5 hexa-self 의 parser attr 흡수 (현실 사례)

현재:
```hexa
// self/parser.hexa:713 부근, attr loop
while peek() == AT { /* 흡수 */ }
```

Tier 1 검사 시: `pos` 가 매 iteration 마다 strict 증가함을 SMT 로 증명해야 통과. 못 하면 `@partial` 강요 → 호출 chain 이 모두 `Process<{partial}>` 로 lifted. 이것이 현재 worktree 의 `self/lsp.hexa` 미스테리 hang 을 root cause level 에서 차단했을 시나리오.

---

## 7. 마이그레이션 — 기존 hexa 코드 분류

`self/`, `stdlib/`, `lib/` 표본 분류 (수기 추정, large-grain):

| 카테고리 | 비율 | 예시 | 결과 |
|---|---|---|---|
| 명백한 pure (수치 연산, AST manipulator inner fn) | ~35% | `parser.hexa` 의 `@pure` 마킹 fn 들, `nn.hexa` forward, `math.hexa` | TFC core 직진입 |
| structural recursion 인데 annotation 없음 | ~25% | `foldl`, `map`, list/tree traversal 다수 | Tier 1 통과 → auto-pure |
| measure recursion (parser, sema 의 일부) | ~10% | parser attr loop, sema fixpoint | Tier 2 SMT 또는 명시 measure |
| IO + 계산 혼재 (compiler driver, build_c) | ~15% | `build_c.hexa`, `bootstrap.hexa` | Process 로 lifted, sub-pure 추출 권장 |
| 순수 IO/exec (stdlib/portable_fs, stdlib/proc) | ~10% | `portable_fs.hexa`, `proc.hexa` | Process layer 만 정의, pure 변형 없음 |
| 진짜 partial (사용자 입력 의존, network) | ~5% | `http.hexa`, `python_ffi.hexa` | `@partial` 명시 |

총 partial 비율 추정: **15–25%** — 위 표의 hybrid + IO + partial 합계의 lower~upper. (자세한 risk: §9.)

---

## 8. 기존 effect 작업 (concurrent worker) 와의 통합 지점

다른 worker 가 effect system 을 작업 중. 본 설계가 의존하는 인터페이스:

1. **effect declaration syntax:** `effect Exec { exec(cmd: String) -> String }` 같은 형식. 본 문서는 declaration syntax 를 *입력* 으로 받는다.
2. **effect row inference 알고리즘:** `Process<E, A>` 의 E 를 자동으로 합집합으로 전파. 본 문서의 Tier 3→4 escalation 은 inference 결과를 사용.
3. **handler / runner:** `with handler { ... } run e` 가 main 에서 effect 를 ground 한다. Pure layer 는 그 안쪽에 존재.
4. **공통 attribute:** `@pure` (본 문서 owner), `@effect <Row>` (effect worker owner). 두 attr 의 sema integration 은 하나의 unified attribute table 에서 (현재 `self/parser.hexa` 의 attr collection 위치). 충돌 방지 위해 attr namespace 분할: `@purity.*` vs `@effect.*` 권장.

merge 순서:
1. 본 문서 Tier 1 (syntactic structural) — effect system 없이도 가능 → 먼저 land.
2. Effect system declaration + row inference land.
3. Tier 3/4 (productive + Process lifting) — effect system 의 row 합집합 op 의존 → 그 다음.
4. Tier 2 SMT — 독립적, 언제든.

---

## 9. 위험 평가

### 9.1 false-negative reject 위험 (HIGH)

Tier 1 의 syntactic check 는 보수적. 실제로 total 인 코드를 reject 할 수 있다. 예: mutual recursion, accumulator-style recursion, divide-and-conquer (`mergesort` 의 left half / right half 가 syntactic subterm 이 아님 — list slice 가 새 list 를 생성).

대응: Tier 2 SMT escalation 을 *aggressive* 하게 적용하되, SMT solver call 비용으로 컴파일 시간 회귀 발생. 측정 지표: self-host build time delta < 30% 가 acceptance bar.

### 9.2 partial 전염 (MEDIUM)

`Process<{partial}>` 가 한번 쓰이면 호출 chain 전체가 Process 로 lifted. 95% 의 코드가 IO 와 한두 단계 거리에 있는 hexa stdlib 에서, 이는 거의 모든 fn 이 결국 Process 가 되는 것을 의미. *형식적* total 보장은 가지지만 *practical* 가치가 떨어진다.

대응: explicit boundary 권장 — IO 에 닿는 함수는 명시적으로 main / driver layer 로 모으고 inner pure fn 을 추출. 이는 Haskell 의 "imperative shell, functional core" 패턴.

### 9.3 코드 리팩토링 비용 추정

self-host 컴파일러 ~25k LOC, stdlib ~15k LOC. §7 표 기준 partial 분류 비율:

- **하한 (15%):** ~6k LOC 가 직접 손이 가야 함 — `@partial` annotation 추가, 시그니처 변경.
- **상한 (25%):** ~10k LOC. 추가로 caller 가 effect 를 받아야 하므로 *전파* refactor cost 가 ×2~3.

총 1–3 person-month 작업으로 추정 (현재 hexa-lang RFC 평균 PR 크기 비교). 위험: 컴파일러 self-host 가 도중에 깨지면 rollback 비용 큼 → feature flag (`--enable-totality`) 로 점진 전환 필수.

### 9.4 corecursion productivity 의 subtle false-positive (LOW)

Coq/Agda 의 guardedness checker 는 알려진 oversight 사례가 있다 (subject reduction violation 사례). hexa 가 동일 trap 에 빠질 수 있다.

대응: Sized types (Agda 식) 도입은 phase 2. phase 1 은 pure syntactic guardedness — 알려진 unsoundness window 는 받아들이고 escape hatch 로 `@trust_me_productive` 제공 (warning).

### 9.5 생태계 학습 곡선 (MEDIUM)

hexa 의 현재 사용자는 대부분 dynamically-typed 배경. total/partial 구분, monad 표기, codata 는 학습 부담. 마이그레이션을 강제하면 사용자 이탈 위험.

대응: Phase α–δ gradient (§2.3) — 4단계로 걸쳐 도입, 각 단계 6주 minimum dwell. 중간에 metric (사용자 build 실패율) 모니터링.

---

## 10. open questions

1. compile-time eval (`@meta`) 은 본 문서 scope 에서 제외했다. 그러나 meta 자체가 폭주 가능 (template expansion fixpoint). 별도 fuel-budget 설계 필요.
2. `Process<{spawn}>` 의 child process 는 hexa-process 인가 OS-process 인가? `stdlib/proc.hexa` 와 통합 필요.
3. Cohabitation: Tier 4 의 `@partial` lifted Process 와 사용자가 직접 적은 `Process<{ourown}, A>` 는 같은 type 인가 alias 인가? 현재 design 은 동일 type — sema 가 unified row 로 처리.
4. `Future<a>` (async) 는 `Process<{async}, a>` 의 alias 로 정의 가능. 그러나 productive 보장? — async fn 도 guardedness 필요할지 별도 RFC.

---

## 11. 결론

Pure / Process 분리 + 3-tier total check + productive corecursion 은 hexa-lang 의 폭주 표면을 type system level 에서 압축한다. ad-hoc fix 의 종착지가 아닌 *이론적 천장* 을 제공한다는 점이 본 설계의 핵심 가치다.

implementation 은 effect-system worker 산출물에 의존하므로 두 worker 간 syntax/sema integration 회의가 land 직전에 필수다.

---

## Appendix A — 참고문헌

- Coq Reference Manual — `Function`, `Program Fixpoint`, `CoFixpoint`. https://coq.inria.fr/doc/
- Agda Wiki — Termination Checking, Sized Types. https://agda.readthedocs.io/
- Brady, E. *Idris 2: Quantitative Type Theory in Practice.* ECOOP 2021. (`total` keyword)
- Bauer, A., Pretnar, M. *Programming with algebraic effects and handlers.* JLAMP 2015. (Eff)
- Leijen, D. *Koka: Programming with Row-polymorphic Effect Types.* MSFP 2014.
- Lindley, S., McBride, C., McLaughlin, C. *Do be do be do.* POPL 2017. (Frank)
- Coquand, T. *Infinite objects in type theory.* TYPES 1993. (guardedness)
- Abel, A. *Type-based termination.* PhD thesis, LMU München, 2006. (sized types)
- Wadler, P. *The essence of functional programming.* POPL 1992. (monads)

## Appendix B — 본 문서가 영향 주는 hexa 파일 (impl phase 시)

- `self/parser.hexa` — `@pure` / `@partial` / `@productive` / `@codata` / `@decreases` attr 흡수.
- `self/sema.hexa` — Tier 1/3 검사기, Process type inference.
- `self/aot/` — Process monad 의 codegen lowering, thunk closure 생성.
- `stdlib/` — 모든 IO 함수 시그니처 review, Process 로 wrap.
- `proposals/rfc_*.md` — 본 문서를 RFC 화 (rfc_016 또는 next 번호).
