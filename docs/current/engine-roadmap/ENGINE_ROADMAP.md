# Mini-Engine — 다음 단계 계획서

**작성일**: 2026-05-20
**최종 수정일**: 2026-05-22 (AB sub-task 1~7 종결, 시퀀스 재정렬)
**관점**: 커리어 마일스톤이 아니라 **엔진 자체의 성숙도**. 무엇이 만들어졌고,
무엇이 만들어진 것을 가리고 있는가, 다음에 무엇을 짚어야 가장 큰 잠금이 풀리는가.

---

## 0. 현재 위치 (2026-05-26)

| 영역 | 상태 |
| --- | --- |
| §4.4 sub-task 1–7 (cgltf → glTF 풀 PBR sampling) | ✅ WebGPU에서 완료. commit `4f809dd` |
| §4.4 sub-task 9 (A/B 모드 머티리얼 토글) | ✅ WebGPU에서 완료 (2026-05-24) |
| Vulkan parity — 헬멧 네이티브 풀 PBR | ✅ bindless 경로로 완료 (2026-05-24). 초기화 순서 버그 + 네이티브 resize 크래시 동시 수정 |
| §4.4 sub-task 8 (SceneNode 최소 구현 / 노드 트리 ingest) | ✅ 노드 트리 평탄화 + showcase 서브메시 리스트로 완료 (2026-05-24). BoxAnimated로 다중 메시/계층 검증 |
| 네이티브 그림자/컬 깜빡임 3종 | ✅ 지면 그림자 acne·컬 버퍼 오버플로·peter-panning 수정 (2026-05-25) |
| **C. TAA (Temporal AA)** | ✅ Vulkan 네이티브 완료 (2026-05-26). 모션 벡터 G-Buffer + Halton 지터 + 리졸브(리프로젝션 + variance clipping). 건물/헬멧 계단현상 해소 |
| 그림자 품질 (그리드/계단) | ✅ ortho 축소(낭비 해소) + 하드웨어 PCF 비교 샘플러 (2026-05-26) |
| §4.5 잔여 한계 | 3개 — tangent.w, emissive HDR, ORM-packed MR fallback |

**다음 시퀀스**: ~~9~~ ✅ → ~~Vulkan parity~~ ✅ → ~~8~~ ✅ → ~~C (TAA)~~ ✅ →
~~그림자 품질~~ ✅ → **다음 후보**: 로드맵 D(멀티스레드 커맨드 레코딩) 또는
WebGPU TAA 포팅. TAA로 AB 위에 시간적 안정성, 하드웨어 PCF로 그림자 품질 확보.

상세 디버깅 여정: [`CHANGELOG_2026-05-21.md`](../../archive/changelogs/CHANGELOG_2026-05-21.md)
(자산 파이프라인 구축 + 함정 3종) ·
[`CHANGELOG_2026-05-22.md`](../../archive/changelogs/CHANGELOG_2026-05-22.md)
(normal/MR/AO/emissive + 진단 도구) ·
[`CHANGELOG_2026-05-24.md`](../../archive/changelogs/CHANGELOG_2026-05-24.md)
(A/B 머티리얼 토글 — frame-state UBO + WGSL select 분기).

§1 이하 진단은 **작성 시점(2026-05-20) 기준**이며 역사적 컨텍스트로 보존.
구체 진척은 §4.4 표를 보면 됨.

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

### AB. glTF 2.0 ingest + PBR 머티리얼 파이프라인 (권장 — 첫 작업)

> **2026-05-20 갱신**: 원안의 A(머티리얼 텍스처 확장)와 B(glTF 로더)를 단일
> 작업 단위로 합친다. 이유는 §3 시퀀스 근거 참조.

**작업 내용**: cgltf 단일 헤더 도입으로 glTF/glb 파싱 + 바이너리 버퍼 디코딩
처리. 그 위에 **AssetImporter 해석 레이어**를 직접 작성 — glTF의 mesh /
material / texture / sampler / node를 엔진 표현으로 매핑. 머티리얼 시스템은
glTF 컨벤션(pbrMetallicRoughness + normalTexture + occlusionTexture +
emissiveTexture)을 그대로 1:1 수용하도록 설계 — 외부 표준에서 우리 표현을
역추론하는 방식.

수반되는 변경:

- `ObjectData`(현재 단일 알베도 인덱스만 보유)를 4-텍스처 슬롯(albedo / normal /
  metallicRoughness / emissive)으로 확장. AO는 glTF 컨벤션대로 occlusion 텍스처의
  R 채널을 별도 슬롯으로 받거나 MR 텍스처 R에 통합 — 데이터를 보고 결정.
- `Vertex`에 tangent 추가. glTF 자산은 vertex tangent를 자산 측에서 제공하는
  경우가 많아 첫 작업이 자연스러움.
- G-Buffer 프래그먼트 셰이더에서 4-텍스처 분기 처리 — 텍스처 있으면 픽셀별
  값, 없으면 기존 상수.
- G-Buffer에 이미시브 채널 자리 확보(현재 GBuffer2의 g/b/a 빈 슬롯에 패킹).
- `SceneNode` 신설 — glTF 노드 계층을 받기 위한 최소한의 트리 표현. 첫
  마일스톤은 깊이 1(단일 메시 자산)이라도 무방.

**작업량**: 3~4주. 분할 시 합보다 짧음(서로의 정답이 같은 자산에서 나옴).

**얻는 것**:

- Khronos Sample Models(DamagedHelmet, Sponza, FlightHelmet 등) 직접 로딩·렌더링.
- 데모 스크린샷이 즉시 "회색 큐브 격자"에서 "실제 PBR 자산"으로 변환.
- 머티리얼 시스템이 임의의 자체 설계가 아니라 **업계 표준(glTF) 컨벤션을 그대로
  수용**하는 형태로 정착 — 후속 자산은 별도 변환 없이 들어옴.
- 향후 작업(TAA·메시 셰이더 등)의 시연이 풍부한 자산 위에서 측정 가능.

**의존성**: 없음 — 기존 인프라(bindless 4096 슬롯, G-Buffer, PBR 셰이더, RHI
sampler) 위에서 SSBO 슬롯 확장 + 셰이더 분기 + 신규 `AssetImporter` /
`SceneNode` 모듈만 추가.

**리스크**: 중. glTF 스펙의 코너 케이스(sparse accessor, non-interleaved
attribute, double-sided, alphaMode=MASK/BLEND 등) 전부 처리하면 작업 폭증.
**최소 viable 경로를 먼저 — 단일 메시 / opaque / PBR-MR만** 우선 처리하고
나머지는 후속.

**라이브러리 선택**: cgltf (단일 헤더, 의존성 0, bgfx·sokol 채택). 파싱·바이너리
디코딩만 위임하고 해석 레이어는 전부 직접 작성 — 상업 엔진의 실제 패턴과 일치
(Unity는 Newtonsoft.Json, bgfx는 cgltf, Unreal은 자체 utility 위에서 glTF 전용
해석 레이어만 자체 작성).

### C. TAA (Temporal Anti-Aliasing) — ✅ 완료 (2026-05-26, Vulkan)

> 완료: 모션 벡터 G-Buffer(4th RG16Float) + Halton 지터 + 컴퓨트 리졸브
> (리프로젝션 + 3×3 variance clipping + 0.9 블렌드). 핵심 난제는 persistent
> 히스토리를 RenderGraph에 넣기(추적 레이아웃 import로 내용 보존) + velocity를
> 지터-free로 두기(정적 카메라 선명 수렴). 자세한 내용:
> [`CHANGELOG_2026-05-26.md`](../../archive/changelogs/CHANGELOG_2026-05-26.md).
> WebGPU 포팅 + per-object 모션 벡터는 후속.

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

**AB → C → D → E** 순서.

### 왜 A와 B를 합쳤나 (2026-05-20 결정)

원안은 A(머티리얼) → B(glTF)였다. 그러나 사용자 지적:

> "오브젝트 로더를 직접 구현하는 것보다 glTF 로더를 갖고 오는 것이 합리적인지
> 고려하고 문제 없다면 진행하자."

이 지적의 정답을 따라가면 A·B 분리가 인위적이라는 결론에 도달한다:

1. **A 단독 진행 시 테스트 자산의 빈곤** — 머티리얼 텍스처 시스템을 만들어도
   적용할 자산이 "코드에서 만든 큐브 + 손으로 PBR 텍스처 묶음"뿐. 시스템 설계의
   타당성이 실제 자산에서 검증되지 않음.
2. **glTF 자산은 vertex tangent를 자산 측에서 제공** — A의 첫 서브태스크였던
   "tangent 직접 계산"이 B와 함께면 자연 해결.
3. **머티리얼 시스템 설계의 표준 입력** — 임의의 자체 설계가 아니라 glTF의
   pbrMetallicRoughness 모델을 그대로 받는 형태로 정착 → 후속 자산은 별도
   변환 없이 들어옴. A를 먼저 하면 "우리 표현"이 먼저 굳어버려 glTF 매핑에서
   불필요한 어댑터 코드가 생김.
4. **합치면 두 번 안 손댐** — 분리 시 머티리얼 정의를 두 번(A에서 자체 설계 →
   B에서 glTF 매핑) 손대야 함. 합치면 한 번에 정답.

→ **AB 단일 작업 단위로 통합**, 첫 작업으로 진입.

### 이후 시퀀스

1. **AB 닫기 (2026-05-22 추가 단계)** — sub-task 1~7이 완료된 후, 다음
   순서로 AB를 깨끗히 봉인:
   - **(a) sub-task 9: 시연 통합** — A/B 분할에 머티리얼 토글 추가 (좌측
     sentinel 강제, 우측 정상). 작업량 작고 시각 효과 즉시. P1.1 인프라
     재사용.
   - **(b) Vulkan parity** — 네이티브 빌드도 헬멧을 풀 PBR로 렌더링하도록
     material bind group 경로를 Vulkan에 이식. 핵심 설계 결정: 현재 set 2
     를 점유 중인 bindless texture array와의 충돌 해소 방안(set 3로 이동 /
     bindless 내부 통합 / 별도 파이프라인 가지). 양 백엔드 동등성 확보.
   - **(c) sub-task 8: SceneNode 최소 구현** — Sponza 같은 다중 메시 자산
     ingest를 위한 인프라. AB 작업 단위의 마지막.
2. **C(TAA)를 그 다음** — AB로 풍부한 머티리얼이 들어와야 안티앨리어싱
   차이가 시각적으로 드러남. velocity 채널 추가는 G-Buffer 포맷 확장이라
   D·E 이전이 깔끔.
3. **D(멀티스레드)는 그 다음** — 시각 작업이 끝난 뒤 성능 작업으로 자연스럽게
   전환. AB로 들어온 풍부한 씬에서 CPU 측 병목이 비로소 측정 가능.
4. **E(메시 셰이더)는 마지막** — 가장 큰 작업량, 가장 모던. AB~D가 만든 풍부한
   씬에서 컬링 비교 시연이 단단해짐.

**AB만 하더라도** 엔진의 데모 인상이 즉시 격상되며 외부 자산 라이브러리를 받을
수 있게 됨. 그 뒤 어디까지 진행할지는 그때 다시 판단.

---

## 4. 첫 작업 — AB (glTF ingest + PBR 머티리얼 파이프라인) 구체화

### 4.1 목표 정의

**최소 viable 마일스톤**: Khronos Sample Models의 [DamagedHelmet.glb](https://github.com/KhronosGroup/glTF-Sample-Models)
한 자산이 화면에 PBR 머티리얼(노멀 맵 · MR 맵 · 이미시브 맵 포함) 그대로 렌더링.
G-Buffer 디버그 뷰 다섯 채널(Normals/Albedo/Metallic/Roughness/AO)이 새 자산에서
픽셀별로 의도대로 다양해질 것.

**최종 마일스톤**: 다중 메시 자산(예: Sponza)이 노드 계층까지 받아져 렌더링.
A/B 분할에 "머티리얼 텍스처 on/off" 비교 모드 추가.

### 4.2 모듈 설계 (신규)

```text
src/assets/
├── AssetImporter.hpp / .cpp     ← cgltf → 엔진 표현 해석 레이어 (자체 구현)
├── ImportedAsset.hpp            ← 메시·머티리얼·노드 트리 묶음
└── MaterialDescriptor.hpp       ← PBR 머티리얼 정의 (glTF 컨벤션 그대로)

src/scene/
├── SceneNode.hpp / .cpp         ← 최소 노드 계층 (transform + children)
└── (장기) Scene.hpp             ← SceneNode 루트 보유, 카메라/조명 컨테이너

third_party/cgltf/
└── cgltf.h                       ← 단일 헤더 (드롭만, vcpkg 안 거침)
```

`AssetImporter`만이 cgltf의 C API를 인식. 그 이외 엔진 코드는 `ImportedAsset`
같은 엔진 표현으로만 상호작용 — 라이브러리 격리 원칙.

### 4.3 변경 면적 (예상)

**자산 파이프라인 (신규)**:

- `third_party/cgltf/cgltf.h` 드롭. CMakeLists.txt에 include path 한 줄.
- `src/assets/AssetImporter.{hpp,cpp}` 신설 — cgltf 호출 → ImportedAsset 반환.
- `ResourceManager`에 glTF 경로 추가(기존 OBJ 경로 옆에). OBJ 로더는 보조로 유지.

**SSBO 레이아웃 (ObjectData 확장)**:

- 현재 `ObjectData` 128B에 텍스처 인덱스가 알베도 하나만 패킹됨
  (`roughnessAOPad.b` 자리). 4-텍스처로 확장 필요.
- **선택지 1**: `glm::uvec4 textureIndices` (16B) 한 줄 추가 → 144B. 셰이더
  3곳 동기.
- **선택지 2**: 기존 `roughnessAOPad.b`의 단일 인덱스 패킹 폐기 + 빈 슬롯
  재배치 → 128B 유지 가능 여부 확인. 더 깨끗하지만 패킹 더 복잡.
- 결정은 구현 시 — 128B 유지 가능하면 그쪽, 아니면 144B 확장.

**셰이더**:

- `Vertex`에 `tangent: vec3` 추가 — `pos / normal / texCoord / tangent`.
- `gbuffer.vert.glsl` / `gbuffer.wgsl` vertex 출력에 tangent 추가, fragment에
  TBN 구성.
- `gbuffer.frag.glsl` / `gbuffer.wgsl` fragment에서 4-텍스처 분기:
  - normalIdx != sentinel → sample → TBN 변환 → G-Buffer normal에 픽셀별 노멀
  - mrIdx != sentinel → sample.b/g → G-Buffer metallic/roughness 픽셀별
  - emissiveIdx != sentinel → sample → G-Buffer 이미시브 채널에 기록
  - aoIdx != sentinel → sample.r → G-Buffer AO 채널에 기록
- G-Buffer 이미시브 위치: 현재 GBuffer2 `(ao, 0, 0, 1)`의 g/b/a 빈 슬롯 →
  `(ao, emissive.r, emissive.g, emissive.b)` 패킹. 4번째 G-Buffer 안 만들어도 됨.
- Deferred lighting에서 이미시브를 HDR 색에 가산.

**RHI**: 변경 없음.

**2026-05-19 스트라이드 사고 재발 방지**:

- `ObjectData` 변경 시 C++ 정의 → 셰이더 정의(gbuffer/shadow/frustum-cull)
  세 곳을 한 커밋에서 동기.
- 주석에 "Must match X bytes, fields listed in order" 명시.
- `static_assert(sizeof(ObjectData) == N)` 추가.

### 4.4 진행 순서 (서브 작업)

각 단계가 빌드·실행 가능한 단위가 되도록 분할. ✅ 표시는 WebGPU 측 완료.
Vulkan parity는 별도 항목.

1. ✅ **cgltf 도입 + AssetImporter 골격** (commit `ab140bf`) — `third_party/cgltf`
   대신 vcpkg 포트 + WASM FetchContent dual-path 채택(헤더만 노출). 빈
   `AssetImporter::load()`가 빌드 통과까지 확인.
2. ✅ **메시 ingest + showcase 경로** (commit `204fd1c`) — glTF accessor →
   엔진 Vertex/IndexBuffer 변환. `ShowcaseAsset` 슬롯 + `setShowcaseMesh` +
   `GBufferPass::execute` showcase params. 헬멧 첫 화면 등장 (회색).
3. ✅ **Vertex tangent** (commit `161ada6` 동봉) — `Vertex.tangent` 추가,
   해시·비교 갱신. AssetImporter는 glTF TANGENT(vec4) xyz 부분만 저장.
   **알려진 한계**: bitangent sign (`tangent.w`) 미저장 — 거울 대칭 UV 자산
   에서 노멀 맵 handedness 어긋남 가능 (§4.5).
4. ✅ **`ObjectData` 4-텍스처 슬롯** (commit `161ada6`) — `uvec4 textureIndices`
   추가, 128B → 144B. `static_assert(sizeof) == 144` + 셰이더 8개(building/
   gbuffer/shadow/frustum_cull × glsl/wgsl) 동시 동기. 2026-05-19 shadow stride
   사고 재발 방지.
5. ✅ **텍스처 ingest + GPU 업로드** (commits `161ada6`, `32c3bdb`) — 5a/5b
   (AssetImporter의 stb_image 디코드 + material extraction) + 5c
   (`ResourceManager::uploadRGBA8FromMemory` + 256-aligned per-row staging).
   디버깅 여정: bytesPerRow=0 거부 + Dawn-stricter-than-spec 1×1 256 정렬
   요구. 자세한 내용은 [`CHANGELOG_2026-05-21.md`](../../archive/changelogs/CHANGELOG_2026-05-21.md) §4.
6. ✅ **G-Buffer fragment baseColor + normal map** (commits `a2edecc`, `4f809dd`):
   - **6a** baseColor sampling: material bind group set 2 (WebGPU only) 신설.
     5 entries(baseColor + normal + MR + emissive + sampler — 6c에서 AO slot
     추가로 6개로 확장). WGSL fragment에서 sample × factor.
   - **6b** normal map + TBN: zero-tangent fallback, Gram-Schmidt 재직교화로
     interpolation drift 보정.
7. ✅ **MR + AO + emissive** (commit `4f809dd`):
   - **6c** MR + AO: bind group 5 → 6 entry. AO 별도 슬롯(glTF 스펙대로).
     **잠재 버그 수정**: 더미 MR `(0,255,0,255)` → `(0,255,255,255)` identity.
     이전 값은 빌딩의 metallic factor를 0으로 무력화하던 latent bug.
   - **6d** emissive: 새 attachment 없이 `gBuffer2.gba` free space에 pack.
     DeferredLighting이 `gb2.gba` 추출 후 모든 라이팅 뒤에 가산. Bloom이
     bright emissive 픽업. **LDR 한계** 명시(§4.5).
   - 진단 도구 3종 동봉: slot resolution 로그, WGSL debug view 9
     (Emissive), HTML 라디오. 자세한 내용은
     [`CHANGELOG_2026-05-22.md`](../../archive/changelogs/CHANGELOG_2026-05-22.md).
8. ✅ **노드 트리 ingest + 다중 메시 showcase** (2026-05-24) — AssetImporter를
   노드 중심 순회로 전환(`cgltf_node_transform_world`로 메시별 world 행렬),
   showcase를 서브메시 리스트로 일반화(`setShowcaseAsset`), GBufferPass가
   서브메시 전부 드로우. 라이브 `scene::SceneNode` 트리 대신 노드 트리를
   평탄화하는 최소 구현. BoxAnimated.glb(노드 2개/메시 2개)로 다중 메시·계층
   검증. 자세한 내용은 [`CHANGELOG_2026-05-24.md`](../../archive/changelogs/CHANGELOG_2026-05-24.md) 3부.
9. ✅ **시연 통합** (commit `<this>`, 2026-05-24) — A/B 분할에 "머티리얼 on/off"
   추가. gbuffer fragment가 화면 좌측에서 PBR 텍스처를 무시하고 ObjectData
   스칼라 factor로 fallback(= sentinel 강제), 우측은 풀 PBR. abSplitX를
   gbuffer에 전달하기 위해 material set 2 binding 6에 frame-state UBO 신설.
   기존 A/B 인프라(deferred 점광원 skip + postprocess SSAO=0/분할선)와 일관
   동작. 자세한 내용은 [`CHANGELOG_2026-05-24.md`](../../archive/changelogs/CHANGELOG_2026-05-24.md).

**비번호 항목 (sub-task 9 이후, §3 시퀀스 참조)**:

- ✅ **Vulkan parity** — 네이티브 빌드도 헬멧을 풀 PBR로 렌더링 (2026-05-24).
  set 2 충돌은 "bindless 재사용"으로 해소 — 헬멧 5개 텍스처를 bindless 배열에
  등록하고 인덱스를 ObjectData에 실음. baseColor는 `roughnessAOPad.b`(빌딩과
  공유), 나머지는 `textureIndices`. 자세한 내용 + 디버깅 여정(초기화 순서 버그,
  resize 크래시): [`CHANGELOG_2026-05-24.md`](../../archive/changelogs/CHANGELOG_2026-05-24.md) 2부.

### 4.5 알려진 위험과 대응

#### 작성 시점(2026-05-20) 예상 위험 — 현재 상태

- ✅ **텍스처 색 공간**: baseColor/emissive는 sRGB, normal/MR/occlusion은
  linear. `uploadShowcaseMaterialTextures`가 머티리얼 사용처에 따라 자동
  분기 — 해결됨.
- ✅ **2026-05-19 스트라이드 사고 재발 방지**: `ObjectData` 변경 시 C++ +
  셰이더 8개를 한 커밋에서 동기 + `static_assert(sizeof) == 144` 가드 —
  step 4에서 적용 완료.
- ⚠️ **TBN 정확도**: glTF tangent vec4 중 xyz만 저장. handedness sign (w)
  미저장 — 거울 대칭 UV 자산에서 어긋남 가능. 현재 영향 없음(DamagedHelmet
  은 비-mirror UV).
- ⬜ **KTX2/Draco**: 압축 텍스처/메시 미지원. 비압축 자산만 처리. 후속.
- ⬜ **glTF 코너 케이스**: sparse accessor, alphaMode=MASK/BLEND, KHR
  extensions, multi-primitive mesh 미처리. opaque + PBR-MR + 단일 primitive
  만 지원. 후속 자산이 등장하면 그때 확장.

#### 진행 중 새로 발견된 한계 (2026-05-22 시점)

자세한 후속 행동: [`CHANGELOG_2026-05-22.md`](../../archive/changelogs/CHANGELOG_2026-05-22.md) §7.

- ✅ **Vulkan parity** (2026-05-24 해소) — 충돌 옵션 중 "bindless 안에 material
  통합"을 채택: 헬멧 텍스처를 기존 bindless 배열에 등록하고 인덱스를
  ObjectData로 전달, set 2는 그대로 bindless가 점유. 부수적으로 bindless가
  애초에 꺼져 있던 초기화 순서 버그와 네이티브 resize 크래시도 수정.
- **Tangent.w (bitangent handedness) 미저장** — `Vertex.tangent`를 vec4로
  승격하면 해소. 거울 대칭 UV 자산이 등장할 때까지 미루기.
- **Emissive HDR 미지원** — `gBuffer2.gba`가 RGBA8Unorm이라 emissive 값이
  0-1로 제한. 매우 밝은 self-emission 자산이 등장하면 `RGBA16Float` 4번째
  attachment로 분리.
- **ORM-packed MR 자산 fallback 부재** — DamagedHelmet은 occlusion과 MR을
  별도 텍스처로 분리 제공해 별도 슬롯 접근이 맞음. 일부 자산은 한 텍스처에
  ORM(Occlusion-Roughness-Metallic) packing — occlusion 슬롯이 sentinel일
  때 MR 텍스처의 R 채널을 fallback으로 쓰는 처리가 필요.

### 4.6 측정·시연

- ✅ **G-Buffer 디버그 뷰 회귀 테스트**: Normals/Albedo/Metallic/Roughness/AO
  다섯 채널 + Emissive(view 9, 2026-05-22 추가)가 헬멧에서 픽셀별로 의도대로
  다양해짐. 사용자 직접 확인.
- ⬜ **A/B 분할에 새 모드**: 좌측 = 텍스처 없음(sentinel 강제), 우측 = 풀
  머티리얼. 동일 자산을 좌우로 나눠 보면 노멀 맵/MR/AO/emissive의 임팩트가
  즉시 가시화. **sub-task 9 (다음 작업)**.
- ⬜ **퍼포먼스**: 4-텍스처 sampling + tangent 추가가 G-Buffer 패스 시간에
  미치는 영향을 GPU 타이머로 측정. 헬멧 단일 추가 비용은 이미 미미(슬롯
  로그로 확인). 100 인스턴스 시나리오에서 측정 후 CHANGELOG 추가 예정.
- ⬜ **자산 검증 확장**: 현재 DamagedHelmet 1종만 검증. FlightHelmet, BoomBox
  등 다른 PBR 자산에서도 정상 렌더링 확인 — 다양한 머티리얼 조합 cover.
  sub-task 8 (SceneNode) 직전에 진행 권장.

---

## 5. 이후 단기·중기·장기 윤곽

이번 작업(AB) 완료 후의 큰 그림. AB는 sub-task 9 + Vulkan parity + sub-task 8
까지 묶어서 종결로 간주 (§3 시퀀스 참조).

- **단기 (AB 닫는 작업, ~2~3 세션)**:
  1. sub-task 9 — A/B 모드 머티리얼 토글 (작업량 작음)
  2. Vulkan parity — material bind group을 Vulkan에 이식
  3. sub-task 8 — SceneNode 최소 구현
- **그 다음 (1 작업)**: C(TAA). AB로 들어온 풍부한 머티리얼 위에서
  안티앨리어싱 품질 격차가 비로소 시각화됨. velocity 채널 추가는 G-Buffer
  포맷 확장.
- **중기**: D(멀티스레드 커맨드 레코딩). AB·C로 만든 풍부한 씬에서 CPU 측
  병목이 비로소 측정 가능해지므로 의미 있는 작업이 됨.
- **장기**: E(메시 셰이더 + GPU-driven 클러스터 컬링) 또는 [`CAREER_ROADMAP.md`
  Phase 5](../../roadmap/CAREER_ROADMAP.md)의 센서 시뮬레이션 분기. 시점에서
  다시 판단.

`CAREER_ROADMAP.md`는 그대로 유지(취업 관점의 또 다른 렌즈). 본 문서는 엔진
관점의 별개 트랙으로 공존시킴. 두 트랙이 일치하는 항목(예: D는 양쪽 다 권장)도
있고, 본 문서에만 있는 항목(AB, C)도 있음.
