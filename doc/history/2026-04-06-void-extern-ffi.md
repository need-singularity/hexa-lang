# VOID extern FFI — Phase 1 완성

**날짜:** 2026-04-06
**영향:** hexa-lang 컴파일러 핵심 기능 추가

## 요약

VOID 터미널 에뮬레이터의 기반인 extern FFI 시스템을 완성했다. hexa 코드에서 `extern fn` 키워드로 C 라이브러리 함수를 직접 호출할 수 있게 되었다.

## 변경 사항

### 버그 수정
- **`interpreter.rs`**: `env.set()` → `env.define()` — extern fn 등록 시 새 변수를 만드는 올바른 메서드 사용
- **`compiler.rs`**: VM에서 extern 발견 시 컴파일 에러 반환 → interpreter로 깔끔한 fallback
- **`env.rs`**: FFI 헬퍼 빌트인 6개 등록 (`str`, `cstring`, `from_cstring`, `ptr_null`, `ptr_addr`, `deref`)

### 기존 구현 (이미 존재했던 것)
- `parser.rs`: `extern fn` 파싱 + `@link()` 어트리뷰트 + 포인터 타입
- `ast.rs`: `ExternFnDecl` 구조체 + `Stmt::Extern` variant
- `type_checker.rs`: extern fn 타입 스코프 등록
- `interpreter.rs`: dlopen/dlsym 기반 FFI 호출 (0~6 인자)
- `jit.rs`: Cranelift 심볼 등록 + extern 함수 JIT 컴파일

### 추가된 빌트인
| 이름 | 기능 |
|------|------|
| `str(val)` | `to_string()` 별칭 |
| `cstring(s)` | hexa String → null-terminated C 문자열 포인터 |
| `from_cstring(p)` | C 문자열 포인터 → hexa String |
| `ptr_null()` | null 포인터 반환 |
| `ptr_addr(p)` | 포인터 주소를 Int로 반환 |
| `deref(p)` | 포인터 역참조 |

## 테스트 결과

### test_extern.hexa — 6/6 PASS
- getpid, getenv, strlen, time, write(stdout), malloc/free

### test_extern_pty.hexa — 전체 PASS
- openpty, fork, setsid, dup2, execvp, read/write, fcntl, kill, waitpid
- PTY 열기 → 셸 스폰 → 명령 실행 → 출력 수신 → 정리

## 실행 경로

```
JIT 가능 (StringLit/Assert 없음) → JIT에서 extern 직접 실행
JIT 불가 → VM에서 extern 감지 → 컴파일 실패 → Interpreter fallback → dlopen/dlsym FFI
```

## 근본 원인 분석

`env.set()`은 기존 바인딩만 업데이트하고 새 변수를 만들지 않음. extern fn 등록 시 `env.define()`을 사용해야 새 변수로 환경에 추가됨. 이 1줄 버그로 interpreter 경로의 extern FFI 전체가 작동하지 않았다.

## 다음 단계 (Phase 2)

- PTY 래퍼 모듈 (`mk2_hexa/native/sys/pty.hexa`)
- Cocoa/AppKit 윈도우 생성 (macOS)
- Metal GPU 렌더링 파이프라인
