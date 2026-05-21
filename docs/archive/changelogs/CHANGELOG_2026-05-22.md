# 변경 이력 — 2026-05-22

> 작업 범위: 어제(2026-05-21) baseColor sampling으로 시작한 step 6의 나머지
> PBR slot 완성 — normal map + TBN, metallic-roughness, occlusion, emissive.
> 셰이더 두 개(`gbuffer.wgsl`, `deferred_lighting.wgsl`)와 material bind
> group 한 슬롯 확장이 변경의 대부분. C++ 측 변경 면적은 작지만 PBR ingest
> 의 시각적 완성도가 단숨에 올라간 날.

---

## 1. 개요

어제 종결 시점 헬멧은 **baseColor 텍스처만 적용된 평면** 상태였다. 노멀
맵·금속/거칠기·AO·자체 발광이 전부 비활성이라 PBR이라기보다는 "스티커
붙인 매끈한 헬멧"에 가까웠다. 이번 작업은 세 단계로 그 격차를 메웠다:

1. **Step 6b** — 노멀 맵 + TBN. 헬멧 표면의 패널 분할선·찌그러진 부위·
   리벳 등이 픽셀별 음영으로 나타남.
2. **Step 6c** — Metallic/Roughness 텍스처 + Occlusion. 금속 영역과
   비금속 영역, 광택·마모 차이, 패널 사이 음영이 분리되어 표현됨. 동시에
   기존 더미 MR 텍스처 값에 latent bug가 있었음(metallic=0 강제)을 발견·
   수정.
3. **Step 6d** — Emissive. 헬멧의 visor·사이드 램프가 자체 발광. 새로운
   render target 추가 없이 `gBuffer2`의 사용되지 않던 `.gba` 채널 3개에
   pack해 처리.

각 단계가 별도 commit이 아니라 단일 work batch로 들어가는 이유: 셰이더가
일관된 상태에 도달해야 시각 검증이 의미를 가짐. 중간 상태(예: emissive
없이 MR만)는 깨지지 않지만 어색.

WebGPU 우선. Vulkan parity는 step 7(별도 작업)로 미룸.

---

## 2. Step 6b — Normal map + TBN

### 2.1 변경 면적

오직 `shaders/gbuffer.wgsl`만 수정. C++ 측 변경 0 — tangent stream은
2026-05-21에 이미 vertex 입력에 흐르고 있었음.

- `VertexOutput`: `tangent: vec3<f32>` (location 7) 추가
- `vs_main`: tangent를 `normalMat`(world matrix upper-3×3)으로 변환,
  **정규화 없이** 통과
- `fs_main`: normal map sampling + TBN 구성

### 2.2 zero-tangent fallback이 핵심

```wgsl
var N: vec3<f32>;
if (length(input.tangent) > 0.01) {
    let T      = normalize(input.tangent);
    let Tortho = normalize(T - Nvert * dot(Nvert, T));  // Gram-Schmidt
    let B      = cross(Nvert, Tortho);
    let TBN    = mat3x3<f32>(Tortho, B, Nvert);
    N = normalize(TBN * nMapTangentSpace);
} else {
    N = Nvert;
}
```

빌딩(procedural cube)은 tangent가 `(0,0,0)`이라 `length()` 분기로 fallback
→ vertex normal 그대로 사용. 더미 normal 텍스처는 `(0.5, 0.5, 1.0)` 디코드
시 `(0,0,1)` (tangent space) = vertex normal in world space와 등가이므로
TBN 적용해도 같은 결과지만 zero-tangent에서 TBN 행렬이 degenerate가 되는
경우를 명시적으로 회피.

vertex shader에서 **tangent를 정규화하지 않고** 통과하는 게 의도적이다.
`(0,0,0)`이 rasterizer interpolation을 거치며 정상화되면 sentinel이 사라짐.
unnormalized 통과 → fragment에서 `length()` 체크 살아남음.

### 2.3 Gram-Schmidt 재직교화

`Tortho = normalize(T - N * dot(N, T))`는 보간 후 T가 N에 완전히 수직이
아닐 수 있는 drift를 보정. cross(N, T)만 쓰면 TBN이 약간 비뚤어진 frame이
되어 노멀 맵 결과가 미세하게 어긋남. 이 한 줄로 안정성 ↑.

### 2.4 알려진 한계

glTF의 `TANGENT` accessor는 `vec4` — xyz는 tangent, w는 **bitangent
handedness sign**. 현재 `Vertex.tangent`는 `vec3`라 w를 저장하지 않는다.
DamagedHelmet 같은 비-mirror UV 자산에는 영향 없음. 거울 대칭 UV가 있는
자산(예: 인체 모델의 좌우 대칭 텍스처)에서 한쪽 normal이 뒤집힐 수
있음. 그 자산을 다룰 때 `Vertex.tangent`를 `vec4`로 승격하면 됨 — 차후
작업.

---

## 3. Step 6c — Metallic/Roughness + Occlusion sampling

### 3.1 머티리얼 bind group 슬롯 확장 4 → 5

기존 4 텍스처(baseColor / normal / mr / emissive)에 occlusion(AO)을
별도 슬롯으로 추가. glTF 2.0 스펙은 `occlusionTexture`를 **`metallicRoughnessTexture`와
분리된 텍스처**로 정의(R 채널만 사용). 많은 자산이 ORM(Occlusion-Roughness-Metallic)
packing으로 한 텍스처에 묶지만, 표준 경로를 따르려면 별도 슬롯이 필요.

- Layout entries: 5 textures + 1 sampler (binding 0~5)
- Default dummy: `R=1` identity (`(255, 0, 0, 255)`) — sample × factor에서
  factor 통과
- `ShowcaseAsset::aoView`: `material.occlusionTextureIndex`에서 resolve

### 3.2 더미 MR 텍스처 identity bug fix

이전 default MR는 `(0, 255, 0, 255)`로 만들어져 있었는데, 코드 주석엔
"metallic=0 roughness=1"이라 적혀 있었다. 의도는 "neutral PBR" — 비금속
디퓨즈. 그러나 셰이더가 `metallic = input.metallic * mrSample.b` 형식의
multiply semantics를 따르면, **B=0인 dummy가 빌딩의 metallic factor를
모두 0으로 강제**한다는 사실이 step 6c 작업 중 드러남.

PBR multiply 시맨틱의 일관된 컨벤션은:

- factor가 PRIMARY (텍스처 없을 때 그대로 통과해야 함)
- 텍스처 sample이 multiply (텍스처 있을 때 factor와 곱해져 픽셀별 값
  생성)
- 더미는 IDENTITY (값 1 — 곱해도 factor 그대로)

→ MR dummy를 `(0, 255, 255, 255)` (G=1, B=1)로 교정. 같은 원칙으로 AO
dummy = `(255, 0, 0, 255)` (R=1).

자세한 PBR multiply semantics는 `Renderer::createMaterialBindGroupInfrastructure`
의 주석 블록에 적었다. 모든 dummy 색 선택의 근거 한곳에 집중.

### 3.3 WGSL fragment

```wgsl
let mrSample  = textureSample(mrTex, materialSampler, input.texCoord);
let roughness = input.roughness * mrSample.g;
let metallic  = input.metallic  * mrSample.b;

let aoSample = textureSample(aoTex, materialSampler, input.texCoord).r;
let ao       = input.ao * aoSample;
```

`gBuffer0.w = roughness`, `gBuffer1.a = metallic`, `gBuffer2.r = ao`로
기존 G-Buffer 레이아웃 그대로 활용. 추가 render target 없음.

---

## 4. Step 6d — Emissive packing into gBuffer2.gba

### 4.1 통찰 — free space가 있었다

emissive를 위해 4번째 G-Buffer attachment를 만드는 게 직관적이지만,
기존 `gBuffer2`를 자세히 보면:

```wgsl
out.gBuffer2 = vec4<f32>(input.ao, 0.0, 0.0, 1.0);  // 이전
```

`.r`만 ao로 쓰이고 `.gba` 3 채널은 **0/1 패딩**. DeferredLighting reader도
`gb2.r`만 읽었다. **3 채널이 통째로 free space** — emissive RGB(3 채널)와
정확히 일치.

→ `gBuffer2`를 emissive 운반에 재활용. 새 attachment, 새 view, 새 binding,
   format 결정 등의 인프라 변경 0.

### 4.2 셰이더 변경

`gbuffer.wgsl`:

```wgsl
let emissive = textureSample(emissiveTex, materialSampler, input.texCoord).rgb;
out.gBuffer2 = vec4<f32>(ao, emissive);  // .r=ao, .gba=emissive RGB
```

emissive 텍스처의 view format이 `RGBA8UnormSrgb`라 sample 시점에 자동
sRGB→linear 변환됨. `gBuffer2`는 `RGBA8Unorm`이라 linear → linear 저장.
색공간 일관.

`deferred_lighting.wgsl`:

```wgsl
let emissive = gb2.gba;  // 추출
// ... 모든 라이팅 계산 ...
color += emissive;       // shadow/AO/light에 영향 안 받는 self-emission
```

가산 위치가 중요. 모든 라이팅(ambient + sun shadow + point lights) **이후에**
더한다 — emissive는 shadow에도 영향 안 받고 AO로 어두워지지도 않는다.
bloom 패스가 임계 휘도를 넘는 픽셀을 자동으로 픽업하므로 visor 발광에
halo 효과가 자연스럽게 따라옴.

### 4.3 LDR 한계 (의도된 trade-off)

`gBuffer2`가 `RGBA8Unorm` (0–1 LDR)이라 emissive 값도 0–1로 제한된다.
HDR-bright emissive (intensity > 1)는 표현 불가. DamagedHelmet의 emissive
값은 모두 LDR 범위라 문제 없음.

HDR emissive가 필요해지는 시점(예: 매우 밝은 네온 사인 같은 자산)에는
`RGBA16Float` 4번째 G-Buffer attachment를 추가하는 게 자연스러운 진화
경로. 그 시점까지는 packing이 우월(메모리·attachment count 절약).

`gbuffer.wgsl`과 `deferred_lighting.wgsl` 양쪽 주석에 명시.

### 4.4 사용자 검증과 진단 도구 추가

본격 검증 시 사용자가 "visor 부근 발광부가 어디가 빛나는지 모르겠다"고
보고. 가능 원인 셋:

1. material에 emissive 텍스처 인덱스가 실제로 해석됐는지 불명
2. shader가 sampling/적용 잘 하는지 불명
3. DamagedHelmet의 emissive가 본래 얼마나 밝은지 불명

→ **진단 도구 3종** 추가:

**Slot resolution 로그** (`Renderer.cpp`):

```cpp
LOG_INFO("Renderer") << "Showcase material slot resolution: "
                     << "baseColor=" << slotTag(showcaseAsset.baseColorView)
                     << " normal="    << slotTag(showcaseAsset.normalView)
                     << " mr="        << slotTag(showcaseAsset.mrView)
                     << " emissive="  << slotTag(showcaseAsset.emissiveView)
                     << " ao="        << slotTag(showcaseAsset.aoView);
```

각 슬롯이 `real` (실제 glTF 텍스처) vs `dummy` (엔진 기본값)로 어디로
가는지 한 줄로 노출. AssetImporter의 인덱스 해석을 의심해야 할 시점을
즉시 판별 가능.

**Debug View 9 = Emissive** (`deferred_lighting.wgsl`):

```wgsl
} else if (ubo.debugView == 9) {
    dbgColor = emissive;  // raw view of gBuffer2.gba
}
```

전부 검정 → 바인딩 실패. 작은 영역에 밝은 점들 → 데이터 정상 (DamagedHelmet
의 emissive가 본래 visor·사이드 램프 등 작은 영역에만 분포).

**HTML 라디오 버튼** (`wasm_shell.html`): Debug Controls의 G-Buffer View
섹션에 "Emissive" 옵션(value=9) 추가.

사용자 확인 결과 emissive 정상 작동 (debug view에서 visor·램프 영역의
픽셀들이 밝게 보임).

---

## 5. 수정 파일 요약

| 파일 | 변경 |
| --- | --- |
| `shaders/gbuffer.wgsl` | VertexOutput `tangent` (loc 7), normal map sampling + TBN + Gram-Schmidt, MR/AO sampling, emissive pack into `gBuffer2.gba` |
| `shaders/deferred_lighting.wgsl` | `gb2.gba`에서 emissive 추출, 모든 라이팅 후 `color += emissive`, debug view 9 추가 |
| `src/rendering/Renderer.hpp` | `defaultAOTex/View`, `ShowcaseAsset::aoView` |
| `src/rendering/Renderer.cpp` | `createMaterialBindGroupInfrastructure` 5 텍스처로 확장 + dummy 컬러 교정 (MR identity fix), `uploadShowcaseMaterialTextures`가 aoView 해석 + 6-entry showcase bind group + slot resolution diagnostic log, `clearShowcaseMesh`에 aoView reset |
| `tests/wasm_shell.html` | Debug View "Emissive" 라디오 추가 (value=9) |

C++ 헤더 변경량은 작지만 정합성 작업(view 멤버 추가, slot resolve 추가)이
정확해야 했다. shader 변경량이 본 작업의 핵심.

---

## 6. 결과

DamagedHelmet이 풀 PBR로 렌더링된다:

- baseColor 텍스처가 패널 색·로고를 그림
- normal map이 패널 분할선·찌그러진 부위를 음영으로 살림
- metallic 텍스처가 금속 가장자리와 비금속 패널을 구분
- roughness 텍스처가 광택·마모 차이를 만듦
- occlusion 텍스처가 패널 사이 음영을 짙게
- emissive 텍스처가 visor·램프에서 자체 발광 + bloom halo

빌딩 격자는 모두 더미 텍스처(identity) × ObjectData factor로 grey 유지
— 회귀 없음.

브라우저에서 Debug View 라디오 9개(Normal, Normals, Albedo, Metallic,
Roughness, AO, Depth, SSAO, Bloom, Emissive)로 각 채널 분리 검증 가능.

WebGPU 측 PBR ingest는 종결. 다음은 step 7(Vulkan parity) 또는 step 8
(SceneNode 최소 구현) — 어느 쪽이 우선인지는 다음 세션에서 결정.

---

## 7. 후속

| 단계 | 상태 |
| --- | --- |
| 6a baseColor sampling | ✅ (2026-05-21) |
| 6b normal map + TBN | ✅ |
| 6c MR + AO sampling | ✅ |
| 6d emissive | ✅ |
| Vulkan parity (step 7) | ⬜ 다음 후보 |
| SceneNode 최소 구현 (step 8) | ⬜ 후보 |
| 시연 통합 — A/B 모드에 머티리얼 토글 (step 9) | ⬜ 후보 |

Vulkan side는 여전히 미터치. 다음 단계에서는 bindless 점유 set 2를
어떻게 다룰지(WebGPU set 2와 다른 set 번호 / bindless 안에 통합 / 별도
파이프라인) 결정해야 한다.

알려진 한계 정리:

1. **Tangent w (bitangent sign) 미저장** — 거울 대칭 UV 자산에서 normal
   map handedness 어긋날 수 있음. `Vertex.tangent`를 vec4로 승격.
2. **Emissive HDR 미지원** — `gBuffer2.gba`가 8-bit LDR. 매우 밝은
   self-emission 자산이 등장하면 `RGBA16Float` 4번째 attachment로 분리.
3. **Vulkan parity 부재** — Vulkan 빌드는 헬멧이 회색으로 보임 (set 2
   material BG 없음). WebGPU에서만 풀 PBR.
4. **MR ORM-packed 자산** — DamagedHelmet은 occlusion과 MR을 별도 텍스처로
   분리 제공해 우리 별도 슬롯 접근이 맞음. 일부 자산은 한 텍스처에
   ORM packing하는데 그 경우 occlusion 슬롯이 sentinel이면 ORM 텍스처의
   R 채널 fallback 같은 처리가 필요. 후속.
