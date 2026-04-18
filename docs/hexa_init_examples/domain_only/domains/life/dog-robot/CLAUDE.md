<!-- @readme(scope=domain) -->

# dog-robot — 개 로봇 도메인

**축**: life
**mk 목표**: mk1 → mk3
**외계인 지수**: 7 → 10 (목표)

## 요약

반려견 수준 동반 로봇. 4 다리 + 센서 8 종 + 음성 합성 기본. 감정 표현(mk2), 학습형 행동(mk3) 으로 확장.

## 선행 도메인

- `life/actuator-4leg` — 4 다리 액추에이터
- `signal/voice-synth` — 음성 합성
- `bio/pet-behavior` — 반려동물 행동 모델

## 빠른 참조

- seed: [`dog-robot.hexa`](./dog-robot.hexa)
- 봉인: [`.sealed.hash`](./.sealed.hash) (첫 @sealed 시 생성)
- 규칙: 상위 `project.hexa` 의 `@domain_rules` 참조
