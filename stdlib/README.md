# stdlib/ — 사용자용 라이브러리

사용자가 `import "../stdlib/xxx.hexa"` 로 쓰는 고수준 모듈.

| 파일 | 역할 |
|---|---|
| collections.hexa | 컬렉션 (List, Set, Map 확장) |
| math.hexa | 수학 함수 |
| nn.hexa | 신경망 |
| optim.hexa | 옵티마이저 |
| string.hexa | 문자열 유틸 |
| consciousness.hexa | anima 의식 모듈 |

`self/lib/` 와 구분:
- **stdlib/** = 사용자 import (public API)
- **self/lib/** = 컴파일러 내부 유틸 (fraction, simd, sieve, tensor_ops 등)

병합 금지 — 역할 다름.
