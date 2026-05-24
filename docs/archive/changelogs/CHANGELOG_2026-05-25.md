# 변경 이력 — 2026-05-25

> 작업 범위: sub-task 8(다중 메시) 커밋 직전에 발견된 **네이티브 렌더링
> 깜빡임 두 종류**와 그 뒤따른 **그림자 peter-panning 틈**의 디버깅 여정.
> 셋 다 sub-task 8 / Vulkan parity와 무관한 기존 버그였고, 네이티브
> Vulkan 경로에 잠복해 있다가 이번에 표면화됐다.

---

## 1. 증상

1. **스트레스 테스트 깜빡임**: 건물 수를 늘리면(슬라이더 16→100K) 건물이
   보였다 안 보였다 함.
2. **바닥 사각형 그림자 깜빡임**: 평범한 상태(건물 16개)에서도 카메라를
   돌리면 지면의 일부 사각형 영역에 검은 그림자가 드리웠다 정상이다를 빠르게
   반복.

두 증상 모두 "프레임 간 불안정"이라 처음엔 동기화/컬링 경쟁을 의심했다.

---

## 2. 틀린 가설들 (체계적 배제)

- **gBuffer2.a 의미 변경(Vulkan parity, 1.0→emissive.b)이 마스크를 깬다?**
  → `gBuffer2.gba`는 deferred_lighting에서 emissive로만 읽힘. 다른 패스(SSAO
  등)가 마스크로 쓰지 않음. ✗
- **비동기 컴퓨트 큐 간 동기화 경쟁?** → 로그 확인 결과 이 머신은
  `timeline_semaphores=no`라 **async compute 비활성**, 인라인 동기 컬 경로
  사용. 동기 경로의 host→compute / compute→indirect·vertex 배리어는 정상. ✗
- **프러스텀 평면 추출(Gribb-Hartmann) 부호/클립공간 오류?** → GLM은
  `GLM_FORCE_DEPTH_ZERO_TO_ONE` 미정의(OpenGL z∈[-1,1])라 `row3+row2` near
  추출이 그 컨벤션과 일치. 카메라 Y-flip(`proj[1][1]*=-1`)은 bottom/top 평면을
  라벨만 맞바꿀 뿐 프러스텀 볼륨은 동일. 컬링 수학 정상. ✗
- **그림자 맵 스위밍(매 프레임 light matrix 재계산)?** → 그림자 시스템은 이미
  카메라 독립적인 단일 scene-fit 행렬로 재작업돼 있었음(카메라 위치 무시,
  매 프레임 동일). 행렬·그림자 맵 내용 모두 프레임 간 안정. ✗

---

## 3. 진짜 원인 (둘은 별개)

### 3.1 스트레스 깜빡임 — visibleIndices 버퍼 오버플로

프러스텀 컬 출력 버퍼(`visibleIndicesBuffers`)의 용량이
`MAX_CULL_OBJECTS = 4096`인데, 스트레스 슬라이더는 **최대 100,000**(버튼
16/1K/10K/100K)까지 간다. 객체 수가 4096을 넘으면 컬 컴퓨트 셰이더의
`atomicAdd(instanceCount,1)`이 4096+ 슬롯을 반환하고
`visibleIndices[slot] = objectIndex`가 **버퍼 끝을 넘어 기록**한다. 인다이렉트
드로우의 instanceCount도 4096을 넘어, 드로우가 범위 밖 visibleIndices에서
**쓰레기 인덱스**를 읽어 엉뚱한/없는 ObjectData를 참조 → 건물이 깜빡인다.

기본 씬은 16건물+지면=17개라 4096 미만 → 평범한 상태에선 이 오버플로가
발생하지 않음. 그래서 두 증상이 별개임이 확인됐다.

### 3.2 바닥 사각형 깜빡임 — 지면의 자기 그림자 acne

`shadow.vert.glsl`(Vulkan)은 `objects[gl_InstanceIndex]`를 직접 인덱싱해
**지면(instance 0)을 포함한 모든 객체를 그림자 맵에 렌더**한다. 지면은 AABB
span ~100000짜리 거대한 평면이고, 태양이 저각도(sunDir.y≈0.28)라 이 평면이
그림자 맵에 들어가면 **수신면 전체가 자기 그림자(acne)** 로 덮인다 — 그림자 맵
footprint(원점 중심 한 변 2R의 정사각형)만큼.

왜 카메라 움직일 때 "깜빡"이는가: deferred의 그림자 비교
`currentDepth - bias > pcfDepth`에서 `currentDepth`는 **카메라 깊이 버퍼에서
역재구성한 worldPos**를 light space로 투영한 값이다. 카메라가 움직이면 이
역재구성 정밀도가 흔들려 bias 경계에 걸친 픽셀 집합이 매 프레임 바뀐다 →
acne 패턴(정사각형 footprint)이 켜졌다 꺼졌다 → "바닥 사각형 깜빡".

**결정적 단서**: `shadow.wgsl`(WebGPU)에는 이미
```wgsl
let ext = obj.boundingBoxMax.xyz - obj.boundingBoxMin.xyz;
if (max(ext.x, ext.z) > 10000.0) {
    output.position = vec4<f32>(0.0, 0.0, -2.0, 1.0);  // NDC z<0 → 클립 → 미기록
    return output;
}
```
지면을 거대 AABB로 감지해 그림자 맵에서 제외하는 코드가 있었다(주석:
"the giant ground plane into the shadow map (whole receiver self-shadowed)").
**GLSL 사본엔 이 컬링이 빠져 있었다** — 전형적인 GLSL↔WGSL 드리프트.
(Renderer.cpp drawFrame의 그림자 드로우 주석 "shadow.wgsl culls the ground by
its huge AABB"가 이 불일치를 이미 경고하고 있었다.)

---

## 4. 수정

### 4.1 visibleIndices 버퍼 확대

`MAX_CULL_OBJECTS` 4096 → **131072** (슬라이더 최대 100K + 헤드룸,
131072×4B = 512KB/프레임, 무시 가능). 슬라이더 최대치가 버퍼 용량보다 작아
오버플로 불가. (셰이더 변경 불필요.)

### 4.2 shadow.vert.glsl에 지면 컬링 추가 (shadow.wgsl과 동기)

```glsl
vec3 ext = obj.boundingBoxMax.xyz - obj.boundingBoxMin.xyz;
if (max(ext.x, ext.z) > 10000.0) {
    gl_Position = vec4(0.0, 0.0, -2.0, 1.0);  // NDC z<0 → 클립
    return;
}
```
지면이 더 이상 그림자 맵에 안 들어가 자기 그림자 acne 소멸 → 깜빡임 사라짐.
건물 그림자는 정상 유지(안정적인 light matrix).

---

## 5. 후속 — peter-panning 틈 (지면 제외의 부수효과 드러남)

acne가 사라지자 건물 밑동과 그림자 사이 **약간의 틈**(peter-panning)이
눈에 띄었다. 원인은 **bias 기본값 불일치**:

- `Renderer.hpp`: `shadowBias = 0.0015f` (주석에 "0.008 caused peter-panning"
  이라 이미 낮춰둠)
- `ImGuiManager.hpp` LightingSettings: `shadowBias = 0.008f`

Application이 매 프레임 `renderer->setShadowBias(lighting.shadowBias)`로 **UI
값(0.008)을 Renderer에 덮어쓰기** 때문에, Renderer가 0.0015로 고쳐놨어도
실효 bias는 peter-panning 유발값 0.008이었다. UI 기본값이 권위값.

**수정**: UI 기본값 0.008 → **0.0015** (Renderer 의도값과 일치). 지면을
그림자 맵에서 제외했고 그림자 패스가 front-face 컬링(`CullMode::Front`)이라
낮은 bias로도 acne 재발 없이 틈만 줄어든다. 사용자 확인으로 틈 해소.

deferred_lighting.frag.glsl의 bias는 평면(flat) `shadowBias*0.01`로,
slope-scaled bias는 향후 개선 항목으로 남김.

---

## 6. 수정 파일 요약

| 파일 | 변경 |
| --- | --- |
| `src/rendering/Renderer.hpp` | `MAX_CULL_OBJECTS` 4096 → 131072 + 사유 주석 |
| `shaders/shadow.vert.glsl` | 거대 AABB 지면 컬링 추가 (shadow.wgsl과 동기) |
| `src/ui/ImGuiManager.hpp` | `shadowBias` 기본값 0.008 → 0.0015 (peter-panning) |

---

## 7. 교훈

- **GLSL↔WGSL 드리프트**: 그림자 지면 컬링이 WGSL에만 있고 GLSL엔 없었다.
  셰이더 로직(컬링·분기)도 구조체 레이아웃처럼 양 백엔드 동기 대상.
  Renderer 주석이 불일치를 이미 경고했으나 GLSL엔 반영 안 됨.
- **두 값 한 의미**: `shadowBias`가 Renderer와 UI 두 곳에 기본값이 있고 UI가
  매 프레임 덮어쓰는 구조였다. "권위 소스"가 어디인지 명확히 하고 기본값을
  한 곳에서만 두거나 동기화할 것.
- **스트레스 한계는 버퍼 용량과 함께 간다**: 슬라이더 상한(100K)이 컬 출력
  버퍼 용량(4096)을 조용히 넘었다. 사용자 노출 상한을 키울 땐 그 데이터가
  흐르는 모든 GPU 버퍼 용량을 같이 확인할 것.
