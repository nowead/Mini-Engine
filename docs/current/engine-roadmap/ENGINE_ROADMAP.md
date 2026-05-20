# Mini-Engine — 다음 단계 계획서

**작성일**: 2026-05-20
**관점**: 커리어 마일스톤이 아니라 **엔진 자체의 성숙도**. 무엇이 만들어졌고,
무엇이 만들어진 것을 가리고 있는가, 다음에 무엇을 짚어야 가장 큰 잠금이 풀리는가.

---

## 1. 현재 진단 — 만들어진 것 vs 보이지 않는 것

쇼케이스 격상까지 끝낸 시점에서 엔진 자체의 그림은 다음과 같다.

### 강력한 기반 — 이미 있는 것

| 항목 | 상태 | 한 줄 |
| --- | --- | --- |
| RHI 추상화 (Vulkan + WebGPU) | ✅ | 한 인터페이스로 두 백엔드 구동, 인스턴싱·indirect·bindless 모두 매핑됨 |
| Render Graph + sync2 배리어 자동 추론 | ✅ | 패스 추가가 선언적 — `compile()`이 의존성 그래프에서 배리어 생성 |
| Deferred + PBR (Cook-Torrance) + IBL | ✅ | G-Buffer 3-target, Vulkan/WebGPU 동등 품질 |
| CSM (Vulkan 4-cascade) / PCSS (WebGPU 단일 맵) | ✅ | 두 백엔드의 그림자 솔루션 분리·동작 |
| SSAO + bilateral blur + Bloom + ACES + FXAA | ✅ | 통합 PostProcess 패스 (WebGPU) / 분리 패스 (Vulkan) |
| GPU 프러스텀 컬링 + Indirect Draw | ✅ | 컴퓨트 셰이더가 가시 인스턴스만 indirect 버퍼에 기록 |
| Bindless Textures | ✅ | 4096 슬롯, `nonuniformEXT`, partially-bound + UAB |
| GPU Timestamp Profiler | ✅ | Vulkan 7-pass + WebGPU 5-pass, async readback ring |
| 데모 UI (네이티브 ImGui / 브라우저 HTML) | ✅ | A/B 분할 비교 · 가이드 투어 · 패스 타이밍 모두 노출 |
| 쇼케이스 데모(엔드-투-엔드 시연 가능) | ✅ | 브라우저에서 즉시 실행, 깊이 자료 링크 포함 |

### 그러나 — 엔진을 끌어내리는 한 가지 사실

위 강력한 기반에도 불구하고, **엔진이 만들어내는 그림이 단조롭다**. 진단 결과
원인은 분명하다.

**머티리얼이 사실상 없다.** `src/rendering/InstancedRenderData.hpp`의
`ObjectData`(128B)를 보면:

```cpp
glm::vec4 colorAndMetallic;  // rgb=albedo, a=metallic
glm::vec4 roughnessAOPad;    // r=roughness, g=ao, b=bindless texture index, a=pad
```

오브젝트당 **단일 알베도 텍스처 인덱스 하나**, 나머지는 전부 스칼라 상수. 즉:

- **노멀 맵 없음** — 표면이 항상 매끄러움. 폴리곤 노멀이 곧 픽셀 노멀. PBR이
  표현할 수 있는 디테일의 80%가 막혀 있다.
- **메탈릭/러프니스 텍스처 없음** — 오브젝트 하나의 표면 전체가 동일한 metalness
  / roughness. "녹슨 금속에 칠해진 페인트가 일부 벗겨진" 같은 표현 불가.
- **이미시브 맵 없음** — 가로등의 발광부만 빛나게 하는 게 안 됨. 가로등이
  빛나려면 그 가로등 전체가 빛나야 함.
- **AO 맵 없음** — 디테일 셀프 섀도잉 부재. SSAO가 매크로 AO를 잡지만 미세
  AO(주름·홈)는 텍스처에서 와야 함.

PBR 수학·G-Buffer 포맷·Deferred 라이팅 셰이더는 전부 **이미 노멀 맵·MR 맵·
이미시브 맵을 받을 준비가 되어 있다**(노멀은 G-Buffer0에 들어가고, MR은
G-Buffer1, 이미시브는 별도 채널에 들어가도록 자리가 있음). 단지 인풋이 텍스처
대신 스칼라 상수로 들어가고 있을 뿐이다.

이게 풀리지 않으면 다른 어떤 개선(글TF 로딩, TAA, 멀티스레드)을 해도 **데모
스크린샷이 여전히 "회색 큐브 격자"** 다. 그래서 머티리얼이 다음 단일 최대 잠금
해제 지점.

### 부차적이지만 진단해 둘 항목

| 항목 | 상태 | 한 줄 |
| --- | --- | --- |
| 자산 로더 | ◐ | OBJ만(tinyobjloader). 글TF/GLB 없음 → 외부 모델 도입 불가, 머티리얼 정의도 표준 없음 |
| 씬 정의 | ◐ | `WorldManager::createDefaultSectors`로 하드코딩(NASDAQ/KOSDAQ — 과거 도메인 잔해). 씬 파일 파싱 없음 |
| G-Buffer 채널 | ◐ | 노멀+러프네스 / 알베도+메탈릭 / AO. **velocity 없음** → TAA·모션 블러·디노이즈 reflection 차단 |
| 투명 패스 | ✗ | 없음. 디퍼드 단독 — 유리·연기·UI in-world 불가. 알파-컷아웃은 discard로 가능하지만 시연 안 됨 |
| 멀티스레드 커맨드 레코딩 | ✗ | 메인 스레드만 사용. Vulkan의 핵심 병렬 역량 미시연 |
| TAA / 시간적 슈퍼샘플링 | ✗ | FXAA만. 모션이 있는 씬에서 안정성 부재 |
| 메시 셰이더 / GPU-driven 클러스터 컬링 | ✗ | 전통 vertex 파이프라인 + 프러스텀 컬링 한 단계만 |
| Reflection (SSR 또는 planar) | ✗ | IBL만 — 동적 거울·물웅덩이 반사 없음 |

---

## 2. 다음 작업 후보 비교

위 진단에서 끌어낼 수 있는 작업 단위 5개:

### A. 머티리얼 텍스처 확장 (권장)

**작업 내용**: `ObjectData`에 `normalIdx / mrIdx / emissiveIdx / aoIdx` 슬롯 추가
(이미 있는 단일 알베도 인덱스를 4개로 확장). 셰이더에서 노멀 맵 → TBN으로
변환 → G-Buffer에 픽셀 노멀로 기록. MR 맵 → 픽셀별 metalness/roughness로 G-Buffer
기록. 이미시브 맵 → HDR 색에 추가. AO 맵 → SSAO와 합성.

**작업량**: 1~2주.

**얻는 것**: 모든 데모 스크린샷이 즉시 달라짐. PBR 수학이 그제서야 보이게 됨.
"PBR 구현했다"고 면접에서 말하려면 노멀 맵부터는 있어야 함.

**의존성**: 없음 — 기존 인프라(bindless 4096 슬롯, G-Buffer, PBR 셰이더) 위에서
인덱스 슬롯 늘리고 셰이더 샘플 4번 추가만. 가장 깨끗한 작업.

**리스크**: 낮음. 셰이더 코드와 SSBO 레이아웃만 손대고 RHI는 안 건드림.

### B. glTF 2.0 로더

**작업 내용**: tinygltf 또는 cgltf 도입. glTF의 mesh / material / texture / node
계층을 엔진 내부 표현(BuildingEntity는 더 이상 적절치 않으니 `SceneNode` 신설
필요)으로 매핑. 머티리얼 텍스처(A 단계의 산물)와 자연스럽게 결합.

**작업량**: 2~3주. 노드 계층 / 인덱스 버퍼 단위 / 머티리얼 베이크 등 분량이 큼.

**얻는 것**: 외부 어셋 라이브러리(Sketchfab, glTF-Sample-Models) 직접 사용
가능. 데모 다양성 폭발적 증가.

**의존성**: A를 먼저 끝내야 의미 있음(머티리얼이 없으면 glTF 머티리얼 정보를
받아도 쓸 데가 없음).

**리스크**: 중. 노드 트리 처리와 머티리얼 변환에서 코너 케이스 다수.

### C. TAA (Temporal Anti-Aliasing)

**작업 내용**: G-Buffer에 velocity 채널 추가(전 프레임 vs 현 프레임 클립 공간
차분). 히스토리 컬러 버퍼 신설. 리프로젝션 + neighborhood clamping + variance
clipping. FXAA를 옵션으로 유지.

**작업량**: 2~3주.

**얻는 것**: 정적 카메라에서도 안티앨리어싱 품질 격상. 동적 카메라에서 미세
디테일 안정. SSAO/SSR 디노이즈 기반 마련.

**의존성**: G-Buffer 포맷 확장 필요(현재 3-target → velocity 추가로 4-target).
Render Graph 의존성 변경 자동 처리되지만 셰이더 다수 수정.

**리스크**: 중. 모션 벡터 정확도가 잘못되면 고스팅이 심각. 비교 토글 UI 같이
가야 디버깅 가능.

### D. 멀티스레드 커맨드 레코딩

**작업 내용**: 스레드 풀(C++20 `std::jthread`) + 스레드별 `VkCommandPool` +
RHI에 Secondary CB 타입 추가. Render Graph 스케줄러가 의존성 없는 패스를
워커 스레드에 배정. WebGPU는 멀티스레드 명령 기록을 지원하지 않으므로
이 작업은 Vulkan 전용.

**작업량**: 3~4주.

**얻는 것**: Vulkan 면접 단골 주제 정복. 4코어에서 CPU 프레임 시간 30%대 감소
가능. 진짜 "엔진의 코어 활용" 시연.

**의존성**: Render Graph 위에서 작업 — 이미 깔끔히 깔린 기반. 단 RHI 인터페이스
확장 필요.

**리스크**: 중-상. Secondary CB의 `VkCommandBufferInheritanceInfo` 핸들링, 멀티
스레드 디버깅, 두 백엔드 격차 처리.

### E. 메시 셰이더 + GPU-driven 클러스터 컬링

**작업 내용**: `VK_EXT_mesh_shader` 도입. 메시를 클러스터로 사전 분할 → 메시
셰이더에서 클러스터 단위 컬링(프러스텀 + 백페이스 + 어큐ㅡ전). Indirect 호환.

**작업량**: 4~6주.

**얻는 것**: 모던 GPU의 최전선 기능 시연. 수천 개 오브젝트 씬에서 GPU 측 컬링
효율 극대화.

**의존성**: 메시 데이터 파이프라인 재설계(클러스터 분할 사전 처리). A·B가
끝나야 효과 측정 가능한 씬이 만들어짐.

**리스크**: 상. WebGPU는 메시 셰이더 미지원 → Vulkan 단독. 메시 셰이더 자체가
새 멘탈 모델.

---

## 3. 권장 시퀀스와 근거

**A → B → C → D → E** 순서가 의존성·작업량·시연 임팩트 모두를 만족.

근거:

1. **A를 먼저 하는 이유** — 가장 작고 가장 임팩트가 큼. 인프라가 이미
   준비되어 있고 (bindless · G-Buffer · PBR 셰이더) 셰이더 일부 + SSBO 슬롯
   확장만 손대면 됨. 모든 후속 작업의 시각적 결과를 풍성하게 만듦.
2. **B를 두 번째로** — A 없이는 glTF 머티리얼이 도착해도 받을 그릇이 없음.
   A가 끝나면 glTF 머티리얼 → ObjectData 매핑이 직선적.
3. **C(TAA)를 세 번째로** — 풍부한 머티리얼이 있어야 안티앨리어싱 차이가
   드러남. velocity 채널 추가는 G-Buffer 포맷 확장이라 D·E 이전이 깔끔.
4. **D(멀티스레드)는 그 다음** — 시각 작업이 끝난 뒤 성능 작업으로 자연스럽게
   전환. 면접 가치가 가장 큰 단일 작업이지만, "보이는 것"이 충분히 풍성한
   다음에 가야 시연이 살아남.
5. **E(메시 셰이더)는 마지막** — 가장 큰 작업량, 가장 모던, 그러나 D 없이도
   가능. A~D를 먼저 끝내면 메시 셰이더의 컬링 비교 시연이 단단해짐.

**A만 하더라도** 엔진의 데모 인상이 즉시 격상되며 면접·포트폴리오 모두 다음
대화 단계로 진입 가능. 그 뒤 어디까지 진행할지는 그때 다시 판단.

---

## 4. 첫 작업 — 머티리얼 텍스처 확장 구체화

### 4.1 목표 정의

`ObjectData`의 단일 알베도 인덱스를 **4개의 텍스처 인덱스**(albedo, normal,
metallicRoughness, emissive)로 확장하고, 각 셰이더에서 텍스처가 있으면 픽셀별
값을, 없으면 기존 상수를 사용하도록 분기. AO 맵은 별도 슬롯이 아닌 MR 텍스처의
R 채널(glTF 컨벤션)에 통합.

성공 기준:
- 노멀 맵을 적용한 오브젝트에서 디테일 음영이 보이고 G-Buffer 노멀 디버그
  뷰에 픽셀별 노멀이 나타날 것.
- 메탈릭/러프니스 텍스처를 적용한 오브젝트에서 metalness·roughness G-Buffer
  디버그 뷰가 균일하지 않을 것.
- 이미시브가 켜진 오브젝트가 Bloom 임계를 통과해 자체 발광으로 보일 것.
- 기존(텍스처 없는) 회색 큐브가 회귀 없이 동일하게 보일 것.

### 4.2 변경 면적 (예상)

**SSBO 레이아웃**:
- `ObjectData`에 텍스처 인덱스 4개 추가 — 현재 128B 한도가 깨지므로 16B 확장
  (`textureIndices: uvec4` 한 줄 추가 → 144B). 셰이더 측 `ObjectData`(GBuffer
  + Shadow + Frustum-cull) 동기, 그리고 **2026-05-19 셰도우 스트라이드 사고**의
  교훈을 살려 한 번에 세 셰이더 동시 갱신 + 주석 명시.
- 또는: 기존 `roughnessAOPad.b`에 알베도 인덱스 1개만 들어있는 구조를 폐기하고
  `uvec4 textureIndices`로 일원화 → 128B 유지 가능 여부 재검토.

**셰이더**:
- `gbuffer.frag.glsl` / `gbuffer.wgsl` — 노멀 맵 샘플 후 TBN 변환, MR 맵 샘플
  후 metalness·roughness 픽셀별 기록, 이미시브 맵 샘플 후 별도 채널에 기록
  (G-Buffer에 이미시브 채널이 이미 있는지 확인 필요).
- TBN 계산을 위해 vertex 셰이더에서 tangent를 출력해야 함. 현재 `Vertex` 구조
  체는 `pos / normal / texCoord`만 있어 **tangent 추가**가 선행.
- 이미시브가 G-Buffer에 자리가 없으면 G-Buffer 4-target으로 확장하거나, HDR
  버퍼에 직접 가산.

**자산 파이프라인**:
- 텍스처 로더는 이미 `ResourceManager`에 있음. 단일 텍스처 로딩 패턴을 4개로
  확장 + `BindlessTextureManager`에 등록. 단순 호출 추가 수준.
- 데모용 테스트 텍스처 한 세트 — 노멀 맵 + MR 맵 + 이미시브 맵 — 외부 자산
  필요(혹은 절차적 생성).

**RHI**: 변경 없음.

### 4.3 진행 순서 (서브 작업)

1. **Vertex tangent 출력 + TBN 구성** — vertex 구조체에 tangent 추가, OBJ
   로더에서 계산(없으면 derived), G-Buffer vertex 셰이더에 출력.
2. **`ObjectData` 텍스처 인덱스 4개 슬롯 확보** — 셰이더 세 곳 동기. 모든 슬롯
   기본값 `0xFFFFFFFF` = "없음" 센티넬.
3. **G-Buffer 프래그먼트에서 텍스처 분기 처리** — 노멀 맵부터 시작.
   `textureIndices.normalIdx != 0xFFFFFFFF`면 샘플 후 TBN 변환, 아니면 vertex
   노멀 사용. Vulkan + WebGPU 양쪽 동시.
4. **MR · 이미시브 · AO 동일 패턴으로 추가**.
5. **G-Buffer 이미시브 채널 처리** — 현재 G-Buffer 레이아웃에 자리가 있는지
   확인. 없으면 deferred lighting에서 별도 텍스처 추가 또는 HDR 버퍼 직접
   접근.
6. **테스트 자산 1개로 적용** — 가로등 또는 건물 한 개에 4-텍스처 머티리얼
   주입. 디버그 뷰로 채널별 확인.
7. **시연**: A/B 모드와 결합 — 좌측 = 텍스처 없음(현 베이스라인), 우측 =
   풀 머티리얼.

### 4.4 알려진 위험과 대응

- **glTF 컨벤션 vs 자체 컨벤션**: glTF는 MR을 `metallicRoughnessTexture`의 BG
  채널에 패킹(B=metallic, G=roughness), AO는 별도 텍스처 또는 occlusion 텍스처
  R채널. 처음부터 **glTF 컨벤션을 따르기로 결정** — B 단계 glTF 로더가 자연스럽게
  연결됨.
- **TBN 정확도**: 모델에 tangent가 없으면 derived가 부정확. OBJ 로더에서
  MikkTSpace 같은 도구 도입 또는 단순 화면 공간 derived(`derivative` 사용)로
  fallback. derived는 노멀 맵 품질 손실이지만 의존성 없음.
- **2026-05-19 스트라이드 사고 재발 방지**: ObjectData 변경 시 (a) C++ 측 정의
  → 셰이더 측 정의(gbuffer/shadow/frustum-cull) 세 곳을 한 커밋에서 동기,
  (b) 주석에 "Must match X bytes, fields listed in order" 명시, (c)
  `static_assert(sizeof(ObjectData) == N)` 추가.

### 4.5 측정·시연

- **G-Buffer 디버그 뷰 회귀 테스트**: Normals/Albedo/Metallic/Roughness/AO 다섯
  뷰가 새 머티리얼에서 의도대로 픽셀별로 다양해질 것.
- **A/B 분할**: 기존 A/B에 "머티리얼 텍스처 on/off" 비교 모드 추가도 자연
  스러움 — 동일 모델, 좌측 텍스처 없음 / 우측 풀 텍스처. P1.1 인프라 재사용.
- **퍼포먼스**: 4-텍스처 샘플이 G-Buffer 패스 시간을 얼마나 늘리는지 GPU 타이머로
  측정. 예상은 미미(bindless + cache hit) 하지만 측정값을 다음 작업의 참고로
  남김.

---

## 5. 이후 단기·중기·장기 윤곽

이번 작업(A) 완료 후의 큰 그림:

- **단기(다음 1~2 작업)**: B(glTF 로더) → C(TAA). 시각 품질·자산 다양성·AA를
  한 시즌에 끌어올림.
- **중기**: D(멀티스레드 커맨드 레코딩). 이때까지 만든 풍부한 씬에서 CPU 측
  병목이 비로소 측정 가능해지므로 의미 있는 작업이 됨.
- **장기**: E(메시 셰이더 + GPU-driven 클러스터 컬링) 또는 [`CAREER_ROADMAP.md`
  Phase 5](../../roadmap/CAREER_ROADMAP.md)의 센서 시뮬레이션 분기. 시점에서
  다시 판단.

`CAREER_ROADMAP.md`는 그대로 유지(취업 관점의 또 다른 렌즈). 본 문서는 엔진
관점의 별개 트랙으로 공존시킴. 두 트랙이 일치하는 항목(예: D는 양쪽 다 권장)도
있고, 본 문서에만 있는 항목(A, B, C)도 있음.
