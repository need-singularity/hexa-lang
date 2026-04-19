# rt_target="rt" 플립 배선 계획

2026-04-19 자율 야간 드릴 결과.

## 현 상태 (source-present, consumer-zero)

| 파일 | LOC | 소비자 |
|------|-----|--------|
| `self/codegen/rt_symbols.hexa` | 218 | 0 |
| `self/rt/math_abi.hexa`        | 69  | 0 |
| `self/rt/string_abi.hexa`      | 173 | 0 |

`rt_symbols.hexa::rt_target()` = `"c"` 고정. `rt_symbol_for` / `rt_math_symbol` / `rt_string_symbol` 는 어느 .hexa 파일도 호출하지 않음.

`self/codegen_c2.hexa` 는 L2984-3047 등에서 리터럴 `"hexa_math_sqrt"` 문자열을 직접 emit. `scripts/native_build.hexa:252` 도 `"hexa_str_concat"` 리터럴.

## 장벽 — codegen_c2.hexa 는 모노리스

```
// codegen_c2.hexa 는 use "..." 를 주석 취급 (per scripts/flatten_imports.hexa:9)
```

`flatten_imports.hexa` 는 `use "module"` 그래프를 concat 해서 단일 .hexa 로 평탄화한 뒤 codegen_c2 에 먹임. 따라서:

1. rt_symbols 를 별도 모듈로 유지하려면 → codegen_c2.hexa 상단에 `use "codegen/rt_symbols"` 추가 + flatten 이 codegen 시리즈 빌드에도 동작해야 함 (현재 flatten 은 유저 `.hexa` → codegen 경로 1방향)
2. 가장 단순한 경로 → codegen_c2.hexa 내부에 inline dispatch 직접 삽입

## 권장 경로 (1-commit flip activation)

### Step A — codegen_c2.hexa 에 라우팅 헬퍼 삽입

파일 상단(line 100 근처) 에 추가:

```hexa
// P7-7 rt_target knob — "c" 고정 시 기존 hexa_* 유지, "rt" 시 rt_*_v 로 라우팅.
// SSOT mirror of self/codegen/rt_symbols.hexa (standalone per flatten policy).
@pure fn cg_rt_target() -> string { return "c" }

@pure fn cg_rt_math(name: string) -> string {
    if cg_rt_target() == "rt" {
        if name == "sqrt"  { return "rt_math_sqrt_v" }
        if name == "sin"   { return "rt_math_sin_v" }
        // … 18 entries total, mirror rt_symbols.hexa::rt_math_symbol
        return ""
    }
    if name == "sqrt"  { return "hexa_math_sqrt" }
    // … legacy 18 entries
    return ""
}

@pure fn cg_rt_string(name: string) -> string {
    if cg_rt_target() == "rt" {
        if name == "str_concat" { return "rt_str_concat_v" }
        // … 24 entries
        return ""
    }
    if name == "str_concat" { return "hexa_str_concat" }
    return ""
}
```

### Step B — 기존 리터럴 emit 을 헬퍼 콜로 교체

`codegen_c2.hexa` L2984-3047 math 블록 (17 contiguous):

```hexa
// BEFORE:
if name == "sqrt"  { return "hexa_math_sqrt(" + a0 + ")" }

// AFTER:
if name == "sqrt"  { return cg_rt_math("sqrt") + "(" + a0 + ")" }
```

string 블록(~L1900-2130, ~L3150-3210) 동일 교체.

### Step C — 플립 활성화

`cg_rt_target()` 의 `"c"` → `"rt"` 로 1 라인 수정. 그 시점에 build_stage0 가 rt/*.hexa + rt/math_abi.hexa + rt/string_abi.hexa 를 같은 링크 유닛에 포함하도록 보장 필요.

### Step D — build_stage0 링크 유닛 확장

`scripts/rebuild_stage0.hexa` 또는 flatten 의존 그래프에 rt/*.hexa 를 포함시켜 transpile 된 C 에 `rt_math_sqrt_v` 등의 정의가 같이 들어가게 함. 현재는 runtime.c 만 링크됨.

## Seed freeze 때문에 지금 당장 할 수 있는 것

1. Step A + B 만 source 에 반영. commit 은 Session B 규약 `"Deferred activation: next rebuild_stage0 cycle"` 태그.
2. Step C (플립) 와 Step D (링크) 는 seed freeze 해제 후.

## 플립 후 얻는 것

- math 카테고리 18 fn = runtime.c 에서 제거 가능 (`hexa_math_sqrt` 등 ROUTABLE 18)
- string 카테고리 25 fn 동일 제거 가능 (ROUTABLE 25)
- runtime.c 6,340 → 81(SAFE_NOW) + 43(ROUTABLE) = 약 124 fn 감소 목표
- LOAD_BEARING 261 은 별도 포팅 필요

## 위험

- math_abi 가 `rt_to_float()` 를 사용 → 이 helper 가 rt/convert.hexa 에 있고, convert 또한 같은 링크 유닛에 들어가야 함. convert + math_abi + math = 3 파일 체인 (순환 없음).
- NaN-box ABI: HexaVal 의 float 언박싱이 interpreter/native 양쪽에서 동일해야 — rt_to_float 이 typeof-dispatch 하므로 안전함 (rt/convert.hexa:35).
- libm 의존 제거: `rt/math.hexa` 는 순수 hexa 구현(log, atan2 등). libm 호출 없음. `-lm` 플래그 제거 가능 (Darwin/Linux 공통).
