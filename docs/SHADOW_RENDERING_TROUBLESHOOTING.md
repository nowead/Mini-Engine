# Shadow Rendering Troubleshooting

## 문서 목적
이 문서는 Mini-Engine의 그림자 렌더링 시스템에서 발생한 문제들과 시도한 해결책들을 기록하고, 새로운 설계 방향을 제시합니다.

---

## 1. 현재 증상

### 1.1 주요 문제
1. **건물 높이 변화가 그림자에 반영되지 않음**
   - 건물이 애니메이션으로 높이가 변해도 그림자는 초기 상태 유지
   - 메인 렌더링에서는 높이 변화가 정상적으로 보임

2. **그림자가 두 개의 삼각형 형태로 렌더링됨**
   - 하나의 건물 그림자가 중앙이 비어있는 두 개의 뾰족한 삼각형으로 나타남
   - 그림자의 중간 부분이 빛을 받은 것처럼 표시됨

3. **Shadow map이 scene의 절반만 커버함**
   - clear value를 0.0으로 설정했을 때 바닥의 절반만 어두워짐
   - Sun azimuth/elevation 조정 시 어두운 영역이 회전하지만 항상 절반만 영향 받음

4. **카메라 이동 시에만 그림자가 변화**
   - 카메라를 움직일 때만 그림자가 업데이트되는 것처럼 보임
   - 다른 파라미터 변경 시에는 그림자가 정적으로 유지

---

## 2. 시도한 해결책들

### 2.1 GPU 메모리 동기화 시도

#### 인스턴스 버퍼 메모리 배리어 추가
```cpp
// Renderer.cpp - Shadow pass 전에 추가
vulkanEncoder->getCommandBuffer().pipelineBarrier(
    vk::PipelineStageFlagBits::eHost,
    vk::PipelineStageFlagBits::eVertexInput,
    {},
    {},
    vk::BufferMemoryBarrier{
        .srcAccessMask = vk::AccessFlagBits::eHostWrite,
        .dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead,
        ...
    },
    {}
);
```
**결과**: 효과 없음

#### Uniform 버퍼 flush 수정
```cpp
// getMappedData() + memcpy 대신 write() 사용
buffer->write(&ubo, sizeof(LightSpaceUBO));
```
**결과**: 효과 없음

#### 매 프레임 새 인스턴스 버퍼 생성
```cpp
// 버퍼 재사용 대신 매 프레임 새로 생성
instanceBuffers[currentBufferIndex] = rhiDevice->createBuffer(bufferDesc);
```
**결과**: 효과 없음

### 2.2 Light Space Matrix 수정 시도

#### Y축 Flip 추가
```cpp
// Vulkan Y축 보정
lightProj[1][1] *= -1.0f;
```
**결과**: 그림자가 두 갈래로 갈라지는 문제 발생/해결 반복

#### 매 프레임 Jitter 추가
```cpp
// 드라이버 캐싱 방지용 jitter
static uint32_t frameCounter = 0;
frameCounter++;
float jitter = static_cast<float>(frameCounter % 1000) * 0.00001f;
m_lightSpaceMatrix[3][3] += jitter;
```
**결과**: 효과 없음

### 2.3 셰이더 수정 시도

#### Shadow 셰이더에 고정 높이 테스트
```glsl
// 고정 높이 200으로 테스트
vec3 debugScale = vec3(instanceScale.x, 200.0, instanceScale.z);
vec3 worldPos = inPosition * debugScale + instancePosition;
```
**결과**: 그림자 크기 변화 없음 → 셰이더가 실행되지 않거나 다른 문제 있음

### 2.4 Shadow Map Clear Value 테스트

#### Clear value를 0.0으로 변경
```cpp
depthAttachment.depthClearValue = 0.0f;
```
**결과**: 바닥의 절반만 어두워짐 → Light frustum이 scene 절반만 커버 확인

---

## 3. 구조적 문제 분석

### 3.1 Light Position 계산 방식의 근본적 오류

**현재 구현**:
```cpp
// ShadowRenderer.cpp - updateLightMatrix()
glm::vec3 lightPos = sceneCenter + normalizedLightDir * sceneRadius * 2.0f;
glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));
```

**문제점**:
- 태양이 `sceneCenter` (0,0,0)를 **바라보며** 회전함
- 이는 point light처럼 동작하여 월드 중앙에서 방사형으로 빛이 나감
- 실제 태양은 **무한히 먼 곳에서 평행광**으로 들어와야 함

**시각적 비교**:
```
현재 (잘못된 방식):              올바른 방식 (Directional Light):

      ☀️ (sceneCenter 주위 회전)         ☀️ → → → → → →
       ↘                                  ↘ ↘ ↘ ↘ ↘ ↘ (평행광)
        ↘                                  ↘ ↘ ↘ ↘ ↘ ↘
    [🏢 🏢 🏢]                            [🏢 🏢 🏢]
         ↙                                  ↘ ↘ ↘ ↘ ↘ ↘
        ↙
      빛이 중앙으로 수렴              빛이 평행하게 진행
```

### 3.2 Orthographic Projection Frustum 문제

**현재 구현**:
```cpp
float orthoSize = sceneRadius * 1.2f;  // 240
glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize,
                                 -orthoSize, orthoSize,
                                 nearPlane, farPlane);
```

**문제점**:
- Light view 방향에 따라 scene의 일부가 frustum 밖으로 나갈 수 있음
- `lookAt`이 sceneCenter를 바라보므로, 광원 위치에 따라 frustum coverage가 달라짐
- scene의 절반만 커버되는 현상의 원인

### 3.3 Shadow Pass와 Main Pass의 데이터 불일치 가능성

**의심되는 문제**:
1. Shadow pass와 main pass가 서로 다른 시점의 인스턴스 데이터를 사용
2. Vulkan command buffer 내에서 동기화 문제
3. MoltenVK (macOS)의 동적 렌더링 처리 방식 문제

### 3.4 Cube Mesh와 Shadow의 불일치

**두 삼각형으로 렌더링되는 원인 가설**:
1. Face winding과 cull mode 불일치
2. Depth comparison 방향 문제
3. NDC 변환 시 Y축 처리 불일치

---

## 4. 새로운 설계 방향

### 4.1 Directional Light Shadow Mapping 재설계

#### 핵심 원칙
1. **Light는 방향만 가짐** - 위치는 scene을 커버하도록 계산
2. **평행광 시뮬레이션** - Orthographic projection 사용
3. **Scene-aware frustum** - 전체 scene을 커버하는 frustum 계산

#### Light View Matrix 계산 (새로운 방식)
```cpp
void ShadowRenderer::updateLightMatrix(const glm::vec3& lightDir,
                                        const AABB& sceneBounds) {
    // 1. Light direction (정규화)
    glm::vec3 L = glm::normalize(lightDir);

    // 2. Light space 기준 축 계산
    glm::vec3 up = abs(L.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(up, L));
    up = glm::cross(L, right);

    // 3. Scene bounds를 light space로 변환하여 frustum 계산
    // ... (scene의 모든 corner를 light space로 변환)

    // 4. Light position: scene 뒤쪽에 배치
    glm::vec3 sceneCenter = sceneBounds.getCenter();
    float sceneRadius = sceneBounds.getRadius();
    glm::vec3 lightPos = sceneCenter - L * sceneRadius * 2.0f;

    // 5. View matrix: light position에서 light direction 방향으로
    glm::mat4 lightView = glm::lookAt(lightPos, lightPos + L, up);

    // 6. Projection: scene을 완전히 커버하는 orthographic
    // ... (light space에서 scene bounds 계산 후 ortho 생성)
}
```

### 4.2 World-Space Sun 이동 구현

**태양 경로 시뮬레이션**:
```
     정오 (Elevation 90°)
           ☀️
          / | \
         /  |  \
        /   |   \
  동쪽 ☀️   |    ☀️ 서쪽
  (Azimuth 0°)  (Azimuth 180°)
       ↘    ↓    ↙
    ━━━━━━━━━━━━━━━━━ 지평선
          지면
```

**구현 방향**:
```cpp
glm::vec3 calculateSunDirection(float azimuth, float elevation) {
    // Azimuth: 0° = 동쪽, 90° = 남쪽, 180° = 서쪽, 270° = 북쪽
    // Elevation: 0° = 지평선, 90° = 정오 (수직)

    float azimuthRad = glm::radians(azimuth);
    float elevationRad = glm::radians(elevation);

    // 태양 방향 계산 (태양이 있는 방향, TO the sun)
    return glm::vec3(
        cos(elevationRad) * sin(azimuthRad),  // X (동-서)
        sin(elevationRad),                      // Y (위)
        cos(elevationRad) * cos(azimuthRad)   // Z (남-북)
    );
}
```

### 4.3 Stable Shadow Mapping

**목표**: 카메라 이동/회전에 관계없이 안정적인 그림자

**주요 기법**:
1. **Texel-aligned light frustum**: Shadow map 텍셀에 정렬하여 shimmer 방지
2. **Fixed world-space light frustum**: 카메라 독립적인 frustum
3. **Depth bias 최적화**: Shadow acne과 peter panning 방지

### 4.4 디버깅 인프라 구축

**Shadow Map 시각화**:
- ImGui에 shadow map 텍스처 표시
- Light frustum wireframe 렌더링
- Shadow coordinates 디버그 출력

---

## 5. 구현 우선순위

### Phase 1: 기본 구조 수정
1. [ ] Light direction 기반 view matrix 계산 재구현
2. [ ] Scene bounds 기반 orthographic frustum 계산
3. [ ] Y축 처리 통일 (Vulkan 좌표계 고려)

### Phase 2: 동기화 문제 해결
1. [ ] Shadow pass와 main pass의 데이터 흐름 검증
2. [ ] 인스턴스 버퍼 업데이트 타이밍 확인
3. [ ] Frame-in-flight 동기화 검토

### Phase 3: 디버깅 도구
1. [ ] Shadow map ImGui 표시
2. [ ] Light frustum 시각화
3. [ ] Shadow coordinates 디버그 모드

### Phase 4: 최적화
1. [ ] Texel alignment (shimmer 방지)
2. [ ] Proper depth bias
3. [ ] Optional: Cascaded Shadow Maps

---

## 6. 해결된 문제들 (2025-01-26)

### 6.1 OpenGL vs Vulkan Depth Range 문제

**문제**: glm::ortho는 OpenGL 스타일로 Z를 [-1, 1]로 매핑하지만, Vulkan은 Z를 [0, 1]로 기대함.
- 빛 가까운 쪽 건물들의 Z가 음수가 되어 클리핑됨
- 결과: 씬의 절반만 그림자가 렌더링됨

**해결책**: Projection matrix를 Vulkan depth range로 변환
```cpp
// ShadowRenderer.cpp - updateLightMatrix()
glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize,
                                 -orthoSize, orthoSize,
                                 nearPlane, farPlane);

// Convert from OpenGL depth [-1, 1] to Vulkan depth [0, 1]
// OpenGL ortho: [2][2] = -2/(f-n), [3][2] = -(f+n)/(f-n)
// Vulkan ortho: [2][2] = -1/(f-n), [3][2] = -n/(f-n)
lightProj[2][2] = -1.0f / (farPlane - nearPlane);
lightProj[3][2] = -nearPlane / (farPlane - nearPlane);
```

**Fragment shader 수정**:
```glsl
// building.frag.glsl - calculateShadow()
// Z는 이미 [0,1] 범위이므로 X/Y만 변환
projCoords.xy = projCoords.xy * 0.5 + 0.5;
// Z는 변환하지 않음 (이전: projCoords = projCoords * 0.5 + 0.5)
```

### 6.2 Orthographic Size 하드코딩 문제

**문제**: `orthoSize = 55.0f`가 하드코딩되어 씬 크기(sceneRadius=200, ground=300x300)를 커버하지 못함

**해결책**: sceneRadius 기반으로 동적 계산
```cpp
float orthoSize = sceneRadius * 1.2f;  // 20% 마진 포함
```

### 6.3 현재 작동하는 Light Matrix 계산

```cpp
void ShadowRenderer::updateLightMatrix(const glm::vec3& lightDir,
                                        const glm::vec3& sceneCenter,
                                        float sceneRadius) {
    glm::vec3 normalizedLightDir = glm::normalize(lightDir);

    // 평행광 시뮬레이션을 위해 멀리 배치
    float lightDistance = sceneRadius * 3.0f;
    glm::vec3 lightPos = sceneCenter + normalizedLightDir * lightDistance;

    glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));

    float orthoSize = sceneRadius * 1.2f;
    float nearPlane = 0.1f;
    float farPlane = lightDistance * 2.0f;

    glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize,
                                     -orthoSize, orthoSize,
                                     nearPlane, farPlane);

    // Vulkan depth range 변환
    lightProj[2][2] = -1.0f / (farPlane - nearPlane);
    lightProj[3][2] = -nearPlane / (farPlane - nearPlane);

    m_lightSpaceMatrix = lightProj * lightView;
}
```

---

## 7. 해결된 문제: Peter Panning (그림자 오프셋)

### 7.1 Sun Elevation 조정 시 그림자 오프셋

**증상**: Sun elevation을 조정할 때 건물 기저부와 그림자 시작점이 멀어짐 (peter panning)

**원인**: Shadow bias가 depth comparison을 너무 많이 밀어내어 건물 기저부 근처에서 그림자가 사라짐

**해결책 (2가지 조합)**:

1. **Shadow pass에서 front-face culling 적용**
   - Back face만 렌더링하여 shadow map에 물체 뒷면 depth를 기록
   - 그림자가 자연스럽게 물체에 붙음
```cpp
// ShadowRenderer.cpp - createPipeline()
pipelineDesc.primitive.cullMode = rhi::CullMode::Front;  // Back face만 렌더링
```

2. **Fragment shader bias 최소화**
   - Front-face culling으로 self-shadow 문제가 해결되므로 bias를 극소화
```glsl
// building.frag.glsl
float bias = ubo.shadowBias * 0.01;  // 최소 bias
```

---

## 9. 참고 자료

- [LearnOpenGL - Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping)
- [Vulkan Tutorial - Depth Buffering](https://vulkan-tutorial.com/Depth_buffering)
- [Microsoft - Cascaded Shadow Maps](https://docs.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps)
- [NVIDIA - Shadow Mapping](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing)

---

## 10. 변경 이력

| 날짜 | 내용 |
|------|------|
| 2025-01-25 | 초기 문서 작성, 문제 분석 및 새로운 설계 방향 제시 |
| 2025-01-26 | Vulkan depth range 문제 해결, 동적 orthoSize 적용, 전체 씬 그림자 렌더링 성공 |
| 2025-01-26 | Peter panning 해결: front-face culling + 최소 bias 적용 |
