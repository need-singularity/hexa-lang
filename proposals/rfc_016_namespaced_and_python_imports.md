# RFC-016 — Namespaced & Python-style `import`

- **상태**: **Implemented (P1+P2+P3+P4)** — 2026-05-07 land
- **작성일**: 2026-05-07
- **선행 RFC**: RFC-008 (`project_python()` subprocess), `stdlib/python_ffi.hexa` (embedded CPython)
- **참조 프로젝트**: `~/core/hexa-bio` · `~/core/anima` · `~/core/hexa-codex`
- **영향 영역**: `self/module_loader.hexa` (전 단계 구현 — interp 가 매 import-using 파일에 대해 source-source preprocess 로 invoke), `stdlib/python_ffi.hexa` (예약어 충돌 fix)
- **검증**: `bench/import_alias_e2e.hexa` (P1) · `bench/import_searchpath_e2e.hexa` (P2) · `bench/import_from_e2e.hexa` (P3) · `bench/import_py_e2e.hexa` (P4) — 4 파일 모두 PASS · `bench/import_chain.hexa` 회귀 0

---

## 1. 동기 (Why)

현 hexa-lang의 `import`는 **파일 단위 인라인 전처리**만 한다.

```hexa
import "iit_ei.hexa"            // ✅ 파일 평탄화 — module_loader.hexa 가 leaves-first 로 합침
```

이 방식의 한계 (Python·Rust·TS 비교):

| 누락된 기능 | Python 비유 | 현재 hexa | 결과 |
|---|---|---|---|
| 네임스페이스 | `import math; math.sqrt(x)` | 모든 심볼이 평탄화 후 전역 | 충돌 위험 — `min`/`max`/`abs` 같은 흔한 이름이 stdlib 여러 파일에 중복 |
| 선택적 import | `from math import sqrt, pi` | 불가 | 1개 함수 쓰려고 파일 전체 인라인 |
| 별칭 | `import numpy as np` | 불가 | 가독성/충돌 회피 수단 없음 |
| 심볼릭 경로 | `import math` (따옴표 X) | 불가 | 항상 상대경로 문자열 — 위치 변경 시 grep으로 찾아 고침 |
| 파이썬 직접 호출 | `import torch` | `py_call("torch", "fn", "json_arg")` 수동 래핑 | 호출부가 장황·타입 정보 손실 |

같은 `~/core/` 코드베이스의 다른 프로젝트들이 이 갭을 우회하고 있다:

### 1.1 `hexa-bio/_python_bridge/module/` — 파이썬 부산물 모듈 28종

```
quantum_h_molecule.py        weave_pi_p2_verifier_v2.py
quantum_vqe_general.py       weave_senolytic_construct_candidate.py
quantum_pauli_expectation.py nanobot_actuation_simulation.py
ligand_smiles_to_h.py        nanobot_pancov_decorated_skeleton_candidate.py
... (총 28개)
```

각 파일은 **순수 Python 모듈** 이고, hexa 측은 `py_call(module, fn, json_arg)` 으로 부른다.
- 호출부 예: `py_call("quantum_h_molecule", "build_hamiltonian", "{\"name\":\"h2\",\"r_angstrom\":0.74}")`
- 문제: 매번 JSON 인코딩/디코딩, 컴파일 시점 시그니처 검증 없음, `import math` 같은 첫 클래스 표현 불가.

### 1.2 `anima/` — staging 디렉토리에 `_python_bridge` 복제

`anima/state/qmirror_phase1_staging_2026_05_03/_python_bridge` — hexa-bio 와 동일한 패턴이 다시 등장. 즉 **여러 프로젝트에 같은 우회 패턴이 복제**되고 있다.

### 1.3 `hexa-codex/` — `install.hexa`, `cli/hexa-codex.hexa` 만 존재

내부 모듈 분할이 거의 없다. 이유 추정: `import` 가 고통스러우니 작은 도구는 **단일 파일**로 유지하는 경향.

### 1.4 `hexa-lang/stdlib/anima_audio_worker.hexa`

> `import sys`, `import os`, `import json`, `from scipy.signal import lfilter, ...`

`.hexa` 파일이지만 사실은 **Python 본문을 문자열 heredoc으로 들고 있다**. 즉 hexa 안에서 Python 을 호출할 정형 문법이 없어, 워커를 별도 .hexa 안에 보관·문자열화·subprocess 실행하는 임시 패턴이 굳어졌다.

### 결론
- 같은 문제(파이썬 호출 + 네임스페이스)의 우회를 **3개 이상의 자매 레포가 각자 재발명** 하고 있다.
- hexa-lang 자체에서 1급 문법을 제공하면 이 우회들이 모두 단순화된다.

---

## 2. 목표 (What)

| 단계 | 도입 문법 | 산출물 |
|---|---|---|
| **P1** | `import "stdlib/math.hexa" as math` → `math.abs(x)` | 네임스페이스 해소 — 다른 변경 없이 충돌 제거 |
| **P2** | `import stdlib/math as math` (따옴표·확장자 생략) → `HEXA_PATH` 검색 | 위치 이동에 강건, 표기 단축 |
| **P3** | `from stdlib/math import abs, max` (P2 표면과 동일) | 선택적 import — 인라인 비용 절감 |
| **P4 — 최종 단계** | `import py math as math` → `math.sqrt(2.0)` 가 `py_call` 자동 래핑 | Python 1급 통합 |

각 단계는 **이전 단계 위에 누적**된다. 한 단계만 따로 채택해도 동작해야 함.

### 2.1 표면(syntax) 통일 원칙 — 따옴표·확장자 생략

**P2 부터 모든 단계에서 `""` 와 `.hexa` 는 선택 사항**으로 통일한다. 즉 같은 import가 다음 네 형태 모두로 표기 가능 (의미는 동일):

```hexa
import "stdlib/math.hexa" as math      // 풀 형태 — P1 호환
import "stdlib/math"      as math      // 확장자만 생략
import  stdlib/math.hexa  as math      // 따옴표만 생략
import  stdlib/math       as math      // 표준형 (권장)
```

선택 규칙:
- **따옴표가 없으면** 식별자/슬래시 시퀀스 (`/[A-Za-z_][A-Za-z0-9_/]*/`) 로 lex.
- **확장자가 없으면** resolve 시 `<path>.hexa` 와 `<path>/mod.hexa` 순으로 시도 (P2 §3.2 참조).
- 따옴표 형태는 향후 비-식별자 문자(공백·점·하이픈) 가 들어간 경로를 위해 보존 — 평소엔 쓰지 않는다.

`from ... import ...` 도 동일 규칙:
```hexa
from stdlib/math import abs, max         // 권장
from "stdlib/math.hexa" import abs, max  // 동등
```

P4 의 Python module 명도 동일:
```hexa
import py math as math                   // 권장
import py "numpy.linalg" as la           // 점 포함 → 따옴표 필요
```

이로써 사용자는 99% 케이스에서 따옴표·확장자 없이 짧게 적고, 특수 문자가 필요한 1% 만 따옴표로 회귀한다.

### 비목표
- Python 의 동적 `import` (`importlib`) 호환은 다루지 않는다.
- 순환 import 그래프는 현재처럼 **에러로 거부** (module_loader.hexa 가 이미 visited 셋으로 detect).
- 패키지(다중 파일 디렉토리)는 P5+ 로 별도 RFC.

---

## 3. 설계 (How)

### 3.1 파서·렉서 변경

#### 현재 (parser.hexa:1027)
```hexa
if tok.value == "import" {
    p_advance()
    if p_peek_kind() == "StringLit" { path = ... }
    return ImportStmt { name: path }
}
```

#### P1 추가
```hexa
if tok.value == "import" {
    p_advance()
    let path = ""
    let alias = ""
    if p_peek_kind() == "StringLit" { path = p_advance().value }
    else if p_peek_kind() == "Ident" { path = p_advance().value }   // P2
    if p_peek().value == "as" { p_advance(); alias = p_advance().value }
    return ImportStmt { name: path, alias: alias, kind_tag: "module" }
}
```

#### P3 추가 — `from ... import ...`
- `from` 토큰을 `lexer.hexa` 키워드 표에 추가 (`from` → `From`)
- 파서: `From StringLit/Ident Import IdentList` → `ImportStmt { kind_tag: "selective", items: [...] }`

#### P4 추가 — `import py "math" as math`
- `py` 는 contextual keyword (식별자로도 쓸 수 있도록 `import` 직후 위치만 인정)
- `kind_tag: "python"` 으로 표시

### 3.2 module_loader.hexa 변경

현재 동작 (`self/module_loader.hexa`):
1. AST 진입 전 **소스 텍스트 단위**에서 `import "..."` 라인을 찾음
2. 해당 파일을 leaves-first 로 모은 뒤
3. `pub` 접두사 떼고 한 덩어리로 concat
4. 원래 `import` 라인을 코멘트 처리

P1 (별칭) 구현:
- 별칭이 있는 import 는 **인라인 시 심볼에 prefix 추가**:
  - 모듈 `math.hexa` 안의 `pub fn abs(x)` → 평탄화 후 `__mod_math__abs(x)` 로 rename
  - 호출부 `math.abs(x)` → `__mod_math__abs(x)` 로 rewrite
- 단순 텍스트 치환은 위험(주석/문자열 안의 `math.abs` 오인식). **AST 기반** 으로 옮긴다.
  - 작업: `module_loader.hexa` 가 lexer 토큰 스트림까지 내려가서 식별자 단위로 prefix 적용.
  - 비용: 파일 평탄화 후 1회 추가 토큰 패스. 자가호스트 빌드 기준 < 5% 추정 (parser_throughput 벤치로 검증).

P2 (HEXA_PATH 검색):
- `ml_resolve_full(rel)` 에 검색 경로 시퀀스 추가:
  1. 호출 파일 디렉토리 (현재 동작)
  2. `$HEXA_PATH` 콜론 구분 디렉토리
  3. `$HEXA_HOME/stdlib` (기본 `/usr/local/share/hexa/stdlib` 혹은 `~/core/hexa-lang/stdlib`)
- 따옴표 없는 `import math` 는 위 경로 각각에 `math.hexa`, `math/mod.hexa` 순으로 시도.
- 캐시: 한 빌드 내에서 동일 경로 재해소를 막기 위해 `g_resolve_cache: map<string,string>` 추가.

P3 (선택적):
- 인라인 시 import 된 심볼만 골라서 emit, 나머지는 폐기.
- 기존 단순 concat → AST-aware 절단 필요. P1 의 토큰 패스에 흡수.

P4 (Python):
- 컴파일 시 `import py "math" as math` 를 다음 hexa 코드로 desugar:
  ```hexa
  let __py_math_initted = false
  fn __py_math_init() {
      if !__py_math_initted { py_init(); py_eval("import math"); __py_math_initted = true }
  }
  // math.sqrt(x) 호출은 다음으로 rewrite:
  //   { __py_math_init(); py_call("math", "sqrt", to_string(x)) }
  ```
- 시그니처 정보가 없으므로 P4 v1 은 **문자열 in/out 만**. 텐서 인자는 `stdlib/python_ffi.hexa::py_tensor_from_buffer` 를 직접 호출하도록 안내(별도 RFC 가 타입 추론 도입 시 자동화).

### 3.3 새 stdlib 정리 (P1 직후)

P1 도입 즉시 갈 수 있는 청소:
- `stdlib/math.hexa` (함수 평면) ↔ `stdlib/math/eigen.hexa`, `stdlib/math/rng.hexa` 등 (디렉토리 분할) — 두 형태가 공존하며 충돌. P1 별칭 도입 후 다음으로 통일:
  ```hexa
  import "stdlib/math.hexa" as math      // 평면 유틸 (abs/min/max/clamp/gcd)
  import "stdlib/math/rng.hexa" as rng   // RNG
  ```
- 사용자 코드는 `math.abs(x)`, `rng.next_u64(s)` 로 호출.

---

## 4. 단계별 마일스톤

| 단계 | 변경 파일 | 테스트 추가 | 위험 |
|---|---|---|---|
| P1 — 별칭 | `parser.hexa`(+5줄), `module_loader.hexa`(토큰 prefix 패스, +50줄) | `self/test_module_alias.hexa`, `bench/import_chain.hexa` 갱신 | 평면 import 와의 호환성 — 별칭 없는 기존 import 는 prefix 적용 안 함 |
| P2 — HEXA_PATH | `module_loader.hexa::ml_resolve_full` (+30줄) | `self/test_module_search_path.hexa` | 검색 경로 우선순위 충돌 — 단계별 fallback 명시 |
| P3 — 선택적 | `parser.hexa`, `lexer.hexa`(`from` 키워드), `module_loader.hexa` | `self/test_from_import.hexa` | AST 절단 시 부수효과 import (top-level let) 누락 — 명시 거부 |
| P4 — Python | `parser.hexa`(`py` ctx kw), 새 패스 `module_loader.hexa::expand_py_imports` | `stdlib/test/test_import_py_math.hexa` | py_init 부팅 비용 — 첫 호출에 lazy init |

각 단계 끝마다:
- `bench/import_chain.hexa` 의 wall-clock 회귀 < 5%
- `self/test_*` 통과
- `state/hx_blockers.json` 에 단계 종료 마커

---

## 5. 호환성

- 기존 `import "x.hexa"` (별칭 없음) 는 **변경 없이** 동일하게 평탄·전역 동작 유지. P1 의 prefix는 `as` 가 있을 때만 적용.
- `python_ffi.hexa` 의 `py_call` / `py_eval` API 는 변경 없음. P4 는 그 위 syntactic sugar.
- module_loader 의 출력 텍스트(평탄화된 `.hexa`)는 P1 도입 후에도 valid hexa source — 외부 도구가 그 산출물을 직접 보고 있지 않다는 가정. (확인됨: `ai_native_lint`, AOT compile 모두 평탄화된 파일을 통째로 받음.)

---

## 6. 미결 질문

1. **P1 의 prefix 인코딩**: `__mod_<alias>__<sym>` vs `<alias>$<sym>` 같이 코드젠 친화적 형태? — 후자는 외부 C 식별자 충돌이 적지만 디버그 출력에서 읽기 어렵다.
2. **P2 의 `$HEXA_HOME` 기본값**: 환경변수 미설정 시 어디를 봐야 하나? `$HOME/core/hexa-lang/stdlib` 는 모노레포 가정, 배포 환경에는 부적합. installer (`install.hexa`) 가 절대경로를 stdlib 어딘가에 적어두는 방식 검토.
3. **P4 의 타입 정보**: 시그니처 없이 모든 인자를 `to_string` 하면 numpy 배열 등 비-스트링 입력은 `py_tensor_from_buffer` 수동 호출이 여전히 필요. P4 v2 에서 `extern py fn math.sqrt(x: float) -> float` 같은 선언적 시그니처 도입을 별도 RFC로.
4. **hexa-bio 모듈 28개 마이그레이션 경로**: P4 시점에 `import py "quantum_h_molecule" as q; q.build_hamiltonian(...)` 형태로 호출부를 일괄 변환하는 codemod 도구 필요?

---

## 7. 첫 번째 PR 의 범위

- P1 만 (별칭 + AST 기반 prefix 패스)
- 새 테스트 1개 (`self/test_module_alias.hexa`)
- `stdlib/math.hexa` 호출하는 기존 사용자 코드 회귀 없음 검증
- `bench/import_chain.hexa` 회귀 측정값 RFC에 후속 추가

---

## 부록 A — 모듈 작성 / 매핑 목록

### A.1 이미 파일 존재 → P1 별칭만 (회귀 위험 ~0)

| Python | hexa stdlib | 비고 |
|---|---|---|
| `math` | `stdlib/math` | abs/min/max/clamp/gcd |
| `json` | `stdlib/json` | dump/load |
| `re` | `stdlib/regex` | `regex/mod.hexa` 통합 필요 |
| `subprocess` | `stdlib/proc` | spawn_supervised |
| `time` / `datetime` | `stdlib/time` | tz/format 부족분 별도 RFC |
| `sqlite3` | `stdlib/sqlite` | |
| `urllib` / `requests` | `stdlib/http`, `stdlib/http2` | |
| `websocket` | `stdlib/websocket` | client only |
| `hashlib` | `stdlib/hash` | `hash/mod.hexa` 통합 필요 |
| `base64` | `stdlib/bytes` | bytes_to_b64 류 |
| `csv` | `stdlib/parse` | 부분 |
| `yaml` | `stdlib/yaml` | |
| `tokenize` | `stdlib/tokenize` | |
| `socket` 저수준 | `stdlib/net` | |
| `numpy.ndarray` | `stdlib/tensor` | `tensor/mod.hexa` |
| `numpy.linalg` | `stdlib/linalg` | |
| `torch.nn` | `stdlib/nn` | |
| `torch.optim` | `stdlib/optim` | `optim/mod.hexa` |
| `torch.autograd` | `stdlib/autograd` | |
| `safetensors` | `stdlib/safetensors` | |

**1차 PR 청소 대상 (회귀 0):**
`math, json, regex, http, time, sqlite, hash, websocket` → `as <name>` 부여 + 호출부 grep 치환.

### A.2 현재 부재 → 신규 작성 필요

| Python | hexa 위치(제안) | 우선 | 동기 |
|---|---|---|---|
| `sys` (argv/stdin/stderr/version) | `stdlib/sys` | **High** | self-host CLI 가 ad-hoc |
| `os.path` / `pathlib` | `stdlib/path` | **High** | `ml_dirname/ml_join_path` 가 매 파일에 재구현 |
| `argparse` | `stdlib/argparse` | **High** | `cli/*.hexa` 들이 손파싱 |
| `logging` | `stdlib/log` | **High** | `println("[XYZ] ...")` 산재 |
| `random` (top-level) | `stdlib/random` (= `math/rng` 재export) | Medium | |
| `tempfile` | `stdlib/tempfile` | Medium | proc/test 가 `/tmp/...` 직조 |
| `glob` | `stdlib/glob` | Medium | |
| `functools` / `itertools` | `stdlib/iter`, `stdlib/funcs` | Medium | |
| `dataclasses` | `stdlib/record` | Low | `struct` 키워드와 겹침 |
| `tomllib` | `stdlib/toml` | Low | yaml로 임시 대체 가능 |

**2차(P2 직후) 신규 4개:** `sys, path, argparse, log` — 자가호스트가 가장 많이 재발명.

### A.3 Python 1급(P4)으로만 흡수 — hexa 재구현 비현실적

P4 도입 시 `import py X as X` 로 직접 호출:
- 사이언스 스택: `numpy`, `scipy`, `pandas`, `torch`, `transformers`
- 양자: `qiskit`, `qiskit-nature`, `pyscf`
- 비전/오디오: `cv2`, `mediapipe`, `librosa`
- hexa-bio `_python_bridge/module/*.py` 28개 — 그대로 유지, 호출부만 P4 문법으로 단순화

이들은 hexa stdlib 으로 재작성하지 않는다 (RFC §2 비목표).

### A.4 의도적 제외 (언어 차원에서 제공)

- `typing` — hexa 의 정적 타입 시스템이 직접 제공
- `asyncio` — `async fn` / `spawn` / `await` 키워드 존재
- `unittest` — `stdlib/test` 이미 존재
