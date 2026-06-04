# 변경 이력 — 2026-06-04

> 작업 범위: M3-3 v1-α streaming의 v1-1 → v1-2 → v1-3 atomic 단계 완료. 그
> 과정에서 v1-3 검증으로 표면화된 **atlas thrashing 함정** 진단 + 해결책
> (memory-budget 기반 auto-size 재작성 + WARN 로그).

---

## 1. 개요

[M3-3 v1-α 계획서](../../current/medical-volume/M3-3_V1_STREAMING_PLAN.md)
대로 atomic 4단계 (v1-1 frustum cull diagnostic → v1-2 streaming mode auto-
entry + 빈 atlas → v1-3 LRU + incremental upload → v1-4 마무리) 진행. v1-3
초기 구현 검증 단계에서 **streaming 메커니즘은 정확히 작동하지만 atlas 크기
가 가시 brick 수보다 작으면 시각적으로 hole이 발생하는 본질적 한계**를 발견
하고 두 가지 대응을 추가했다.

오늘 끝낸 항목:

- **v1-1 frustum cull diagnostic** — visibleBricks/visibleNonEmpty 카운팅
  (`5df02c2`)
- **v1-2 streaming mode auto-entry** — 비어 있는 atlas 진입 + 원본 데이터 보관
  (`6d9c3f9`)
- **v1-3 LRU + incremental upload** — 매 프레임 K=8 brick upload + LRU eviction
  + auto-size 재작성 + WARN (`d748814`)
- v1-α baseline 재측정 ([BASELINE_2026-06-04_V1_ALPHA.md](
  ../../current/medical-volume/BASELINE_2026-06-04_V1_ALPHA.md))

---

## 2. atlas thrashing 함정 — 진단 여정

### 2.1 증상

v1-3 LRU 구현 후 verification 단계에서:

```powershell
.\volume_viewer.exe test_1024.nii --atlas-cap 2
```

- `Atlas: 2x2x2, 8/8 slots (100%)`
- `resident: 8, missing: 296`
- `this frame: +0 uploaded / -0 evicted` ← **stat 자체는 정상적인 LRU 동작**
- 화면: 가시 영역 대부분 검은 hole, 흐릿한 평면만 뜸

사용자가 "**내가 안 보이는 곳의 brick이 빠지던가 해야 하는데, 내가 보이는
곳의 brick이 빠져있다**" 지적 → 의료영상에선 가시 부분이 결손되면 진단
가치 자체가 사라짐.

### 2.2 1차 가설 — 모두 폐기

- "LRU 정책 버그" → 코드 확인. `lastFrameUsed == frameIdx`인 slot은 eviction
  후보에서 제외. **정책은 정확**.
- "초기 frame 8개만 채우고 멈춤" → 처음 8 upload 후 모든 slot이 가시 brick
  → 매 프레임 bumped → eviction 후보 0 → `+0/-0`. **메커니즘은 의도대로**.
- "atlas-cap 2가 너무 작아서 그런 것뿐, 실 사용 시엔 큼" → 그러나 default
  `--atlas-cap` 없을 때도 2 GB dense 1024³처럼 자연스럽게 발생 가능.

### 2.3 진짜 원인 — 모델 가정 위반

Streaming 모델의 핵심 가정: **"매 시점 가시 brick 수 << atlas slot 수"**.
이 가정은 줌인 워크플로(대용량 볼륨의 작은 region을 보는 경우)에서는 자연스
럽게 성립. 그러나 **줌아웃 전체 보기에선 가시 brick == 전체 non-empty
brick**이라 가정이 깨진다.

가정이 깨지면:
- LRU가 가시 brick 보호 (정책상 옳음)
- 새 brick 들어올 자리 없음 → upload 멈춤
- 가시 영역 일부가 영원히 missing → 시각 hole

→ **메커니즘 버그가 아니라 모델 한계**. atlas 크기 정책이 잘못된 것.

### 2.4 해결 1 — memory-budget 기반 auto-size

기존 auto-size: `min(pageGrid, 8) per axis` — 고정 cap. nonEmpty 수와 무관.

v1-α 신규 auto-size:
1. `axisGuess = ceil(cbrt(nonEmpty))` 시작점 — 가능한 한 모든 brick fit
2. pageGrid로 axis 별 clamp
3. atlas 메모리가 `kAutoAtlasBudgetBytes = 512 MB` 초과하면 가장 큰 axis를
   하나씩 줄여 fit

결과:
- 1024³ default(304 non-empty): atlas (7,7,7) = 343 slots = **188 MB**
  (이전 (8,8,8) = 292 MB → -33%)
- 1024³ dense(408 non-empty): 위와 동일 (cube root 8) = (8,8,8) = 292 MB
- 1024×1024×1024 dense(2728 non-empty): `ceil(cbrt(2728)) = 14` →
  (14,14,14)=1504 MB → budget 초과 → shrink → **(9,10,10) = 493 MB**.
  2728 > 900 → Streaming **자동 진입**.

핵심 효과: **"내가 안 만진 데이터인데 굳이 가시-가능 안 되는 atlas 받는" 케이스
회피**. 메모리 budget 안에서 최대한 가시 가능하게 사이즈.

### 2.5 해결 2 — Streaming 진입 시 WARN + 권장값

자동 사이징이라도 atlas < nonEmpty가 강제될 수 있음 (4 GB+ 볼륨). 사용자가
즉시 인지하고 결정할 수 있도록:

```text
[WARN][BrickedVolume] streaming mode (atlas too small): 2728 non-empty
bricks vs 900 atlas slots. When the camera sees more than 900 bricks at
once, the excess will render as empty. Pass atlasGrid (14,14,14) = 2744
slots (~1504 MB) for guaranteed Static rendering. Streaming is best when
total bricks > atlas but visible-set << atlas (zoom-in workflows on large
volumes).
```

- 진단: "이 atlas 크기는 이 볼륨의 가시 케이스 못 받는다"
- 권장값: 구체적 atlasGrid + 메모리 추정
- 사용 가이드: streaming의 적합·부적합 케이스 명시

LOG_INFO가 아닌 **LOG_WARN**으로 격상 — 정상 로그 stream에서 즉시 눈에 띄는
것이 의도.

### 2.6 교훈

- **메커니즘 검증 ≠ 정책 검증**. v1-3의 LRU 코드는 첫날부터 정확히 동작했지
  만 사용자가 "보이는 것이 사라진다" 지적할 때까지 정책상 한계를 인식 못
  함. **시각 검증을 사용자 워크플로 관점으로 수행**해야 함.
- **메모리 절감 ≠ 가치**. atlas 99.6% 절감했어도 가시 영역 결손이면 의료
  진단 가치 0. 절감은 가시 영역 보장 위에서 의미.
- **데이터-aware auto-size**. pageGrid나 axis만 보지 말고 nonEmpty 수 +
  메모리 예산 두 축으로 사이징. 단순 cap은 데이터 종류에 따라 너무 작거나
  너무 큼.

memory에 새 노트로는 안 남김 — 함정 자체가 plan §9.2에 예고되어 있었고
이번 작업으로 해결됨.

---

## 3. v1-1 → v1-3 atomic 진행 — 짧은 회고

3 단계 atomic 구분이 효과적이었다.

- **v1-1 diagnostic only**: 진단 도구만 추가, 시각 영향 0. 빠르게 검증
  가능했고 사용자가 frustum cull이 정확히 동작함을 즉시 확인.
- **v1-2 빈 atlas + 모드 분기**: 시각적으로 "검은 화면" 임시 회귀 발생.
  사전 합의된 임시 상태라 사용자 혼란 없음. **여기서 어디까지 갈지 미리
  합의가 중요**.
- **v1-3 본체**: LRU + upload + auto-size + WARN. 가장 큰 작업이지만 v1-1/2
  로 기반이 깔려 있어 명확하게 구분 가능.

v1-2의 의도된 임시 회귀를 사용자가 받아들이려면 **계획서가 미리 그것을 명시
적으로 설명**해야 했고, 실제로 [PLAN §8 v1-2](../../current/medical-volume/M3-3_V1_STREAMING_PLAN.md)
가 "**셰이더는 그대로. 첫 프레임 atlas 비어 있어서 화면이 다 검정. 이 시점은
의도된 임시 상태**" 명시한 덕에 검증 단계에서 매끄럽게 넘어갔다.

---

## 4. 잔여 작업 — v1-β로 분리

v1-α의 정직한 한계는 BASELINE_2026-06-04_V1_ALPHA.md §6에 정리. 가장 큰
3가지:

1. **CPU brick pack 최적화** — Case C에서 12.6 FPS 봐서 명백히 v1-α 다음
   순위. row-memcpy + SIMD로 10× 개선 여지.
2. **LOD (multi-resolution brick)** — visible > atlas 케이스를 hole 없이
   표현하는 본질적 해결책. 의료영상 진단 워크플로에서 가장 가치.
3. **디스크 페이징** — 4 GB+ 임상 데이터의 RAM 한계 해소.

각각 별도 트랙. v1-β 마일스톤 (시기 미정).
