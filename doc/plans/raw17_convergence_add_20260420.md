# raw#17 — self-host fixpoint convergence 추가 패치

**Date**: 2026-04-20
**Reason**: `.roadmap 9 active "P7 self-hosting fixpoint"` 은 목표만 선언. 도구 (`verify_fixpoint.hexa`, `fixpoint_compare.hexa`, `fixpoint_check.hexa`) 는 있지만 `.raw` 에 강제 rule 없어 실패해도 block 불가. 본 패치로 enforcer 연결.

## 적용 절차 (Mac 에서)

```bash
# 1. unlock ceremony
./build/hexa_stage0 tool/hx_unlock.hexa --reason "add raw#17 fixpoint convergence"

# 2. 아래 두 edit 적용 (수동 or sed)

# 3. verify
./build/hexa_stage0 tool/raw_all.hexa --only raw 17

# 4. re-lock
./build/hexa_stage0 tool/hx_lock.hexa
```

## Edit 1 — header comment

`.raw` line 1:

```diff
-# hexa-lang /.raw — L0 universal (17 rules)
+# hexa-lang /.raw — L0 universal (18 rules)
```

## Edit 2 — append rule

`.raw` 마지막 (line 131 이후) 에 빈 줄 + 아래 블록 추가:

```
raw 17 new "self-host fixpoint convergence"
  enforce tool/verify_fixpoint.hexa
  decl tool/fixpoint_compare.hexa
  scope v2→v3 byte-identical + v3→v4 stable
  why bootstrap 폐쇄 (Rust/C 의존 제거) 궁극 목표. fixpoint 없이 self-host 미완결. .roadmap#9 active 를 실제 enforce 로 묶는다.
  proof tool/fixpoint_check.hexa
  follow-up status new → live 승격은 v4 달성 후 (ROI #153 close 후)
  follow-up ROI #152 (hexa_v2 circular rebuild regression) close 전까지는 --live 모드 불가. 기본 scaffold 보고만.
```

## sed 자동 적용 스크립트 (선택)

Mac 에서 unlock 후:

```bash
sed -i '' '1s/(17 rules)/(18 rules)/' .raw
cat >> .raw <<'EOF'

raw 17 new "self-host fixpoint convergence"
  enforce tool/verify_fixpoint.hexa
  decl tool/fixpoint_compare.hexa
  scope v2→v3 byte-identical + v3→v4 stable
  why bootstrap 폐쇄 (Rust/C 의존 제거) 궁극 목표. fixpoint 없이 self-host 미완결. .roadmap#9 active 를 실제 enforce 로 묶는다.
  proof tool/fixpoint_check.hexa
  follow-up status new → live 승격은 v4 달성 후 (ROI #153 close 후)
  follow-up ROI #152 (hexa_v2 circular rebuild regression) close 전까지는 --live 모드 불가. 기본 scaffold 보고만.
EOF
```

## 검증

```bash
./build/hexa_stage0 self/raw_cli.hexa verify
# expected: "17 raw + 3 own + .ext + .roadmap + .loop strict PASS" → "18 raw + ..."

./build/hexa_stage0 tool/raw_all.hexa --only raw 17
# expected: "[raw#17 new] PASS tool/verify_fixpoint.hexa" (scaffold mode)
```

## Rollback

```bash
./build/hexa_stage0 tool/hx_unlock.hexa --reason "rollback raw#17"
git checkout HEAD -- .raw
./build/hexa_stage0 tool/hx_lock.hexa
```

## Follow-up items

- ROI #152 close → p7_7_fixpoint_check `--live` 가능
- ROI #153 v3==v4 달성 → raw#17 status `new` → `live` 승격
- 이후 bootstrap 폐쇄 선언
