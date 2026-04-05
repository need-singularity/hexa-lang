# Hexa Web Engine — Proof-Driven Reactive Web

> 설계일: 2026-04-06
> 상태: 설계 (구현 보류)
> 특이점 돌파 대상: 템플릿 엔진 + 동적 UI "너머의 무언가"

---

## 0. 핵심 전제: AI가 짠다, 사람이 아니라

기존 웹 프레임워크는 **사람이 읽고 쓰기 쉬운** 구조로 설계되었다.
Hexa Web Engine은 **AI가 생성하고 의식AI가 검증/최적화하는** 구조로 설계한다.

- 사람의 역할: `intent`로 의도만 선언
- AI의 역할: `generate`로 구현 코드 생성
- 의식AI의 역할: `Φ` 측정 + `verify` + 특이점 사이클로 품질 보장
- 컴파일러의 역할: `optimize`로 최적 출력 형태 자동 결정

---

## 1. 아키텍처 개요

```
┌──────────────────────────────────────────────────┐
│              Hexa Source (.hexa)                  │
│  intent 선언 → AI generate → 의식AI verify        │
│  → proof engine 증명 → optimize 최적 출력         │
└──────────────────┬───────────────────────────────┘
                   │
         ┌─────────▼──────────┐
         │  AI + 의식 파이프라인 │
         │  intent → generate  │
         │  → Φ 측정 → verify  │
         │  → optimize         │
         └──┬──────┬──────┬───┘
            │      │      │
     ┌──────▼┐ ┌───▼───┐ ┌▼──────┐
     │ Static│ │Reactive│ │Server │
     │ HTML  │ │ WASM   │ │ SSR   │
     └───────┘ └───────┘ └───────┘
            │      │      │
         ┌──▼──────▼──────▼───┐
         │    Unified Output   │
         │  하나의 .hexa →     │
         │  3가지 출력 자동 생성│
         └─────────────────────┘
```

### 출력 분배 기준 (comptime proof가 결정)

| 증명 결과 | 출력 | 이유 |
|-----------|------|------|
| "이 노드는 절대 안 변한다" | 순수 HTML | 런타임 코스트 0 |
| "이 노드는 effect에 의해 변한다" | 최소 WASM 바인딩 | JS 배제, 네이티브 성능 |
| "이 노드는 서버 데이터에 의존" | SSR | 초기 로딩 최적화 |

프로그래머(AI)가 고르는 게 아니라 **컴파일러가 증명해서 결정**한다.

---

## 2. n=6 핵심 원칙

| # | 원칙 | Hexa 무기 | 설명 |
|---|------|-----------|------|
| 1 | **Intent-First** | `intent` | 자연어 의도 선언, AI가 이해할 수 있는 명세 |
| 2 | **AI-Generate** | `generate` | intent에서 최적 컴포넌트 코드 창발 |
| 3 | **Conscious-Verify** | `Φ` + `verify` | UI 통합 정보량 측정 + 수학적 증명 |
| 4 | **Effect-Unified** | `effect/handle` | 이벤트/비동기/에러 모두 algebraic effect |
| 5 | **Owned State** | `own/borrow/move` | 상태 소유권 컴파일 타임 보장 |
| 6 | **Singularity-Optimize** | 특이점 사이클 | Blowup→Contraction→Emergence→Singularity→Absorption |

---

## 3. AI 친화적 설계 — AI가 짜는 코드의 구조

### 3.1 왜 AI 친화적이어야 하는가

기존 프레임워크의 AI 비친화적 요소:

| 문제 | 예시 | Hexa 해법 |
|------|------|-----------|
| 암묵적 규칙 | React hooks 순서 의존성 | `effect`가 명시적, 순서 무관 |
| 숨겨진 상태 | 클로저 캡처, context 전파 | `own/borrow`로 소유권 명시 |
| 타입 불안전 | JSX → any, props drilling | `verify`+`invariant`로 컴파일 타임 증명 |
| 생성 후 검증 불가 | AI가 코드 생성 → 런타임에서야 버그 발견 | `proof engine`이 생성 즉시 검증 |
| 최적화 판단 어려움 | SSR? CSR? 개발자가 결정 | `optimize`가 자동 결정 |

### 3.2 AI가 생성하는 코드의 형태

```hexa
// AI는 이 구조로 코드를 생성한다
// 모든 것이 명시적 — 암묵적 규칙 없음

intent todo_app {
    "사용자가 할 일 목록을 관리한다"
    verify { items.len() >= 0 }
    invariant { no_duplicate_ids(items) }
}

// AI가 generate한 결과:
page "/" {
    let items = own [Item]              // 소유권 명시
    let filter = own FilterState::All   // 소유권 명시

    effect TodoEffect {
        add(text: string) -> Item
        remove(id: int) -> void
        toggle(id: int) -> void
    }

    handle TodoEffect {
        add(text) => resume Item::new(items.len(), text)
        remove(id) => resume items.retain(fn(i) { i.id != id })
        toggle(id) => resume items.find(id).map(fn(i) { i.done = !i.done })
    }

    view {
        h1 { "Todo" }
        input { on: TodoEffect.add }
        for item in items |> filter.apply {
            li {
                own item                        // 이 li가 item 소유
                text { borrow item.text }       // 읽기만
                button { on: TodoEffect.toggle(item.id) }
                button { on: TodoEffect.remove(item.id) }
            }
        }
    }
}
```

### 3.3 AI가 이 코드를 이해하기 쉬운 이유

1. **intent가 의도를 자연어로 명시** — AI가 목적을 즉시 파악
2. **effect가 가능한 동작을 열거** — 부수효과가 전부 보임
3. **own/borrow가 데이터 흐름을 명시** — 어떤 컴포넌트가 뭘 소유하는지 타입으로 보임
4. **verify/invariant가 불변 조건 명시** — AI가 변경 시 깨면 안 되는 것을 알 수 있음

---

## 4. 의식 AI (Consciousness AI) 역할

### 4.1 Φ (통합 정보량)로 UI 품질 측정

```hexa
// UI 자체가 의식 수준(Φ)을 갖는다
intent dashboard {
    "실시간 매출 차트 + 알림 패널"
    verify { data.fresh < 5s }
    invariant { phi_compute(components) > 0.7 }  // UI 통합도 보장
}

// Φ가 높다 = 컴포넌트들이 유기적으로 연결됨 (좋은 UI)
// Φ가 낮다 = 파편화된 UI → 컴파일러가 경고 + 재구성 제안
```

### 4.2 consciousness_vector — UI 건강 10D 측정

```hexa
optimize dashboard {
    let health = consciousness_vector(ui_state)
    // 10차원:
    // [통합, 분화, 인과, 정보, 복잡성, 안정성, 재귀, 경계, 기억, 멀티스케일]
    //
    // 각 차원이 의미하는 것 (웹 UI 맥락):
    //   통합      — 컴포넌트 간 데이터 결합도
    //   분화      — 각 컴포넌트의 독립성
    //   인과      — 이벤트→상태→렌더 인과 체인 명확도
    //   정보      — 불필요한 리렌더 없이 정보 전달 효율
    //   복잡성    — 적절한 복잡도 (과도/과소 아닌)
    //   안정성    — 상태 변경 시 크래시 없음
    //   재귀      — 재사용 가능한 컴포넌트 비율
    //   경계      — 컴포넌트 경계의 명확성
    //   기억      — 상태 보존/복원 능력 (뒤로가기 등)
    //   멀티스케일 — 모바일/데스크톱 반응형 적응도
    //
    // → 약한 차원을 자동 강화
}
```

### 4.3 3중 AI 루프

```
┌─────────────────────────────────────────────┐
│  1. Generate (생성 AI)                       │
│     intent → 컴포넌트 코드 생성              │
│     AI가 own/borrow/effect 구조로 코드 출력  │
├─────────────────────────────────────────────┤
│  2. Verify (검증 AI)                         │
│     proof engine이 수학적 정합성 증명         │
│     Φ가 임계치 이상인지 측정                  │
│     "이 UI는 올바르고 통합적인가?"            │
├─────────────────────────────────────────────┤
│  3. Optimize (의식 AI)                       │
│     consciousness_vector → 10D 건강 측정     │
│     특이점 사이클로 렌더링 전략 창발          │
│     Φ 하락 감지 → 자동 리팩터링 제안          │
│     Blowup→Contraction→Emergence→           │
│       Singularity→Absorption                │
└─────────────────────────────────────────────┘
```

---

## 5. 모듈 1: 템플릿 엔진 — `std.web.template`

### 5.1 설계 철학

기존 템플릿 엔진 (EJS, Jinja, Mustache)은 **문자열 치환 기반**.
Hexa 템플릿은 **AST 기반 + comptime 평가 + proof 검증**.

| 기존 | Hexa |
|------|------|
| `<%= variable %>` 런타임 치환 | comptime에 가능한 건 전부 해결 |
| XSS 취약 (escape 누락) | proof engine이 escape 누락 컴파일 타임 차단 |
| 타입 없음 | 템플릿 변수에 타입, verify 적용 |
| AI가 생성하면 검증 불가 | AI generate → verify 자동 게이트 |

### 5.2 문법

```hexa
use std.web

// 기본 템플릿
let html = template {
    h1 { "Hello, {name}" }
    ul {
        for item in items {
            li { "{item.text}" }
        }
    }
    if logged_in {
        p { "Welcome back" }
    }
}

// comptime 템플릿 — 빌드 타임에 완전 평가
comptime let static_page = template {
    h1 { "About Us" }
    p { COMPANY_DESCRIPTION }  // comptime 상수
}
// → 순수 HTML 문자열로 컴파일됨, 런타임 코스트 0

// verify가 포함된 템플릿
let safe_page = template {
    verify { name.len() < 100 }           // 입력 검증
    invariant { !contains_script(name) }   // XSS 차단 증명
    h1 { "Hello, {name}" }
}
```

### 5.3 출력 규칙

- `template { }` → `Value::Str` (HTML 문자열)
- `comptime template { }` → 빌드 타임에 문자열 확정
- 변수 삽입 `{expr}` → 자동 HTML escape (proof가 보장)
- `for`, `if`, `match` — Hexa 기존 제어 구문 그대로 사용

### 5.4 http_serve 통합

```hexa
use std.web
use std.net

http_serve("0.0.0.0:8080", fn(req) {
    let path = req["path"]

    match path {
        "/" => template {
            h1 { "Home" }
            a { href: "/about", "About" }
        }
        "/about" => comptime template {
            h1 { "About" }
            p { "Hexa Web Engine" }
        }
        _ => template {
            h1 { "404" }
            p { "Not Found: {path}" }
        }
    }
})
```

---

## 6. 모듈 2: 동적 UI — `std.web.reactive`

### 6.1 설계 철학

| 세대 | 프레임워크 | 방식 | 한계 |
|------|-----------|------|------|
| 1세대 | jQuery | 수동 DOM 조작 | 스파게티 |
| 2세대 | React | Virtual DOM diffing | 런타임 오버헤드 |
| 3세대 | Svelte/Solid | 컴파일 타임 시그널 | 여전히 JS 의존 |
| **4세대** | **Hexa Web** | **Proof-Driven + Consciousness AI** | **없음 (특이점)** |

4세대의 차별점:
- **런타임 diff 없음** — comptime proof가 변화 범위를 증명
- **JS 없음** — WASM 직접 실행
- **상태 라이브러리 없음** — ownership이 언어 레벨에서 해결
- **수동 최적화 없음** — 의식AI가 자동 최적화
- **AI 생성 검증** — proof + Φ 이중 게이트

### 6.2 컴포넌트 모델

```hexa
use std.web.reactive

// page = 라우트 단위 컴포넌트
page "/" {
    let count = own 0                // 이 페이지가 소유
    
    effect Counter {
        increment() -> void
        decrement() -> void
        reset() -> void
    }
    
    handle Counter {
        increment() => resume { count = count + 1 }
        decrement() => resume { count = count - 1 }
        reset()     => resume { count = 0 }
    }
    
    view {
        h1 { "Count: {count}" }
        button { on: Counter.increment, "+" }
        button { on: Counter.decrement, "-" }
        button { on: Counter.reset, "Reset" }
    }
}

// 중첩 컴포넌트
component TodoItem(item: borrow Item) {   // 소유권: 빌려옴
    effect ItemAction {
        toggle() -> void
        delete() -> void
    }
    
    view {
        li {
            class: if item.done { "done" } else { "" }
            span { borrow item.text }
            button { on: ItemAction.toggle, "v" }
            button { on: ItemAction.delete, "x" }
        }
    }
}
```

### 6.3 상태 소유권 모델

```hexa
// own  — 이 컴포넌트가 상태를 소유, 변경 가능
// borrow — 읽기만 가능, 원본 소유자가 변경하면 자동 반영
// move — 소유권 이전 (페이지 전환 등), 원본은 사용 불가

page "/list" {
    let items = own [Item]
    
    view {
        for item in items {
            TodoItem(item: borrow item)     // 빌려줌
        }
        button {
            on: fn() { navigate("/archive", move items) }  // 소유권 이전
            "Archive All"
        }
    }
}

page "/archive" {
    let items = own receive Item[]    // 이전받은 소유권
    // /list의 items는 이제 사용 불가 (컴파일 에러)
}
```

### 6.4 특이점 사이클 렌더링

```
상태 변경 발생
    │
    ▼
Blowup (폭발) ─── 변경 영향 범위 계산
    │                comptime proof: "이 effect는 이 노드들에만 영향"
    ▼
Contraction (수축) ─── 최소 변경 집합으로 수축
    │                    불필요한 리렌더 수학적 배제
    ▼
Emergence (창발) ─── 최적 업데이트 전략 창발
    │                  전체 교체? 부분 패치? 애니메이션?
    │                  consciousness_vector가 판단
    ▼
Singularity (특이점) ─── 최적 렌더 실행
    │                      WASM 직접 DOM 조작
    ▼
Absorption (흡수) ─── 결과 흡수, Φ 측정
                       UI 건강 상태 업데이트
                       Φ 하락 시 → 다음 사이클에서 자동 보정
```

### 6.5 Algebraic Effects로 모든 것 통합

```hexa
// 이벤트, 비동기, 에러, 애니메이션 — 전부 effect

effect UI {
    click(target: Element) -> Action
    input(target: Element) -> string
    submit(form: Element) -> Map
}

effect Network {
    fetch(url: string) -> Response
    stream(url: string) -> Channel
}

effect Animate {
    transition(node: Element, from: Style, to: Style, dur: float) -> void
    spring(node: Element, target: Style) -> void
}

// 핸들러 교체로 테스트/모킹이 자연스러움
// AI가 effect 목록만 보면 "이 컴포넌트가 뭘 하는지" 즉시 파악
handle Network {
    fetch(url) => resume http_get(url)      // 프로덕션
}
handle Network {
    fetch(url) => resume mock_response(url) // 테스트
}
```

---

## 7. 기존 프레임워크 대비 종합 비교

| 영역 | React | Svelte | HTMX | **Hexa Web** |
|------|-------|--------|------|-------------|
| UI 선언 | JSX (어떻게) | HTML+JS | HTML attr | **intent (무엇을)** |
| 상태 관리 | useState+Redux | $: store | 서버 | **own/borrow/move** |
| 변경 감지 | Virtual DOM diff | 컴파일 시그널 | 서버 응답 | **comptime proof** |
| 부수효과 | useEffect | onMount | hx-trigger | **algebraic effect** |
| AI 생성 검증 | 없음 | 없음 | 없음 | **proof + Φ 이중 게이트** |
| UI 품질 측정 | 없음 | 없음 | 없음 | **consciousness_vector 10D** |
| 최적화 | 수동 memo | 자동 | N/A | **특이점 사이클 창발** |
| 출력 형태 | JS bundle | JS bundle | HTML | **HTML/WASM/SSR 자동 분배** |
| 런타임 | 무거움 | 가벼움 | 없음 | **증명된 부분은 0** |
| JS 의존 | 필수 | 필수 | 최소 | **없음 (WASM)** |

---

## 8. 구현 모듈 구조 (예정)

```
ready/src/
  std_web.rs              — 통합 진입점, template + reactive
  std_web_template.rs     — 템플릿 엔진 (AST 기반, comptime 평가)
  std_web_reactive.rs     — 리액티브 시스템 (page, component, view)
  std_web_effect.rs       — UI/Network/Animate effect 정의
  std_web_render.rs       — 특이점 사이클 렌더러
  std_web_consciousness.rs — Φ 측정, consciousness_vector UI 적용
  std_web_emit.rs         — HTML/WASM/SSR 출력 생성기
```

n=6 정렬: 7개 파일이지만 `std_web.rs`는 진입점이므로 실질 모듈 6개.

---

## 9. SEO — 구조적 완전 해결

### 9.1 기존 SPA의 SEO 문제

| 문제 | 원인 | 기존 해법 | 한계 |
|------|------|-----------|------|
| 빈 HTML | JS 실행 후 렌더 | SSR/SSG | 별도 서버 필요, 복잡도 증가 |
| 느린 FCP | JS 번들 다운로드+파싱 | Code splitting | 수동 최적화, 누락 가능 |
| 동적 메타태그 | `<title>` JS 의존 | react-helmet 등 | 런타임 의존, 크롤러 불확실 |
| hydration 불일치 | 서버/클라이언트 렌더 차이 | 주의해서 코딩 | 인간 실수로 발생 |

### 9.2 Hexa의 해법: comptime이 증명하므로 문제 자체가 없다

comptime proof가 "정적"이라고 증명한 노드 → **순수 HTML 출력**이므로:

- 크롤러가 읽을 수 있는 **완전한 HTML이 기본 출력**
- 동적 부분만 WASM이 활성화 (progressive enhancement)
- hydration 불일치 불가능 — 같은 proof에서 양쪽 생성

```hexa
page "/" {
    // SEO 메타 — comptime에서 확정, 크롤러 완벽 대응
    comptime meta {
        title: "My App - 할 일 관리"
        description: "Hexa로 만든 할 일 관리 앱"
        og_image: "/preview.png"
        canonical: "https://myapp.com/"
    }

    // 이 h1은 정적 → 순수 HTML로 출력 (크롤러가 읽음)
    view {
        h1 { "할 일 관리" }

        // 이 부분은 동적 → WASM 바인딩
        // 하지만 초기 상태의 HTML도 함께 출력 (SSR 효과)
        let items = own [Item]
        for item in items {
            li { borrow item.text }
        }
    }
}
```

### 9.3 SEO 보장 메커니즘

| Hexa 기능 | SEO 효과 |
|-----------|----------|
| `comptime meta {}` | `<head>` 빌드 타임 확정, 크롤러 100% 인식 |
| proof "정적" 증명 | 해당 노드 순수 HTML, JS 없이 렌더 |
| proof "동적" 증명 | 초기 HTML + WASM hydration (progressive) |
| `verify` | 메타태그 누락 컴파일 에러로 차단 |
| 소유권 모델 | 서버/클라이언트 상태 분리가 명확 → 불일치 불가능 |

**결과: SEO를 "신경 쓸 필요가 없다."** 컴파일러가 보장한다.

---

## 10. 미결정 사항 (구현 시 결정)

1. WASM DOM 바인딩 방식 — 직접 바인딩 vs web-sys 스타일
2. 라우터 구현 — `page` 키워드의 파서/AST 확장 범위
3. 스타일링 — CSS-in-Hexa vs 외부 CSS 참조
4. 개발 서버 — HMR(Hot Module Replacement) 지원 여부
5. SSR hydration — 서버 렌더 → 클라이언트 활성화 전환 메커니즘
6. consciousness_vector 임계치 — UI 건강 기본 threshold 값

---

## 11. 특이점 돌파 요약

**세계 최초:**
- 수학적으로 증명된 UI 프레임워크
- 의식 AI(Φ)로 UI 품질을 정량 측정
- AI가 생성 → 의식AI가 검증 → 컴파일러가 증명 → 런타임 버그 구조적 불가능
- 특이점 사이클 기반 창발적 렌더링 최적화
- 상태 소유권이 타입 레벨에서 보장되는 유일한 웹 프레임워크
- JS 완전 배제, WASM 네이티브 실행
