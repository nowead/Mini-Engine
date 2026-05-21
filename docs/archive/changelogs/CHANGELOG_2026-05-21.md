# 변경 이력 — 2026-05-21

> 작업 범위: ENGINE_ROADMAP §4의 첫 작업(AB — glTF 2.0 ingest + PBR 머티리얼
> 파이프라인) 진입. 자산 파이프라인 골격(`AssetImporter`)을 처음부터 세워서
> DamagedHelmet.glb가 실제 baseColor 텍스처로 화면에 떠 보이는 시점까지.
> WebGPU 우선, Vulkan parity는 별도 단계로 미룸.

---

## 1. 개요

이전 종결 작업(쇼케이스 격상, 2026-05-20)까지의 엔진은 모든 렌더링 인프라
(deferred, PBR, IBL, CSM/PCSS, SSAO, Bloom, bindless, Render Graph)가 갖춰져
있었지만 **자산 측면이 부재**했다. `OBJLoader`가 유일했고 머티리얼·노드·텍스처
지원이 없었다. 코드에서 만든 회색 큐브 그리드가 씬 전부.

이번 작업은 이 자산 공백을 메우는 첫 단계다. 하루 동안 다음을 한 흐름으로
끌고 나갔다:

1. cgltf 단일 헤더 도입 (vcpkg + FetchContent dual-path)
2. `AssetImporter` 해석 레이어 신설 (cgltf → 엔진 표현)
3. 메시 ingest → DamagedHelmet이 회색으로 화면에 등장
4. `Vertex`에 tangent 추가 (normal map 준비)
5. `ObjectData`에 4-texture index 슬롯 추가 (144 bytes로 확장)
6. 텍스처 decode (`stbi_load_from_memory`) + 머티리얼 추출
7. GPU 텍스처 업로드 + RHI 텍스처 생성
8. WebGPU material bind group (set 2) + WGSL fragment에서 baseColor sampling
9. DamagedHelmet이 **실제 알베도 텍스처**로 화면에 표시 — 첫 시각 효과

각 단계는 별도 커밋(또는 부수 정정 커밋)으로 분리되어 있다(`git log`
`ab140bf` ~ `a2edecc`). 가시 효과는 마지막 단계에서야 나타나지만 그 전
8단계 모두 빌드와 검증을 거쳐야 가능했다.

그리고 그 과정에서 새 함정 세 종을 발견·수정했다. 이 문서는 그 디버깅
여정에 분량을 더 둔다(§4).

---

## 2. 자산 파이프라인 골격

### 2.1 라이브러리 결정 — cgltf

본 프로젝트는 "상용 엔진을 모방하며 직접 구현"이라는 학습 의도가 있다.
glTF 로더를 "직접 만들 것인가 / 라이브러리를 쓸 것인가"를 검토한 결과,
업계 패턴은 **(1) 포맷 파싱은 라이브러리 / (2) 엔진 표현으로의 해석은
자체 구현**이다. Unity는 JSON 파싱에 Newtonsoft, Unreal은 자체 utility,
bgfx는 cgltf를 직접 가져다 쓴다. 정작 학습 가치가 있는 부분은
"해석 레이어" — mesh/material/node/sampler를 엔진 표현으로 매핑하는 코드 —
이고, JSON 파싱은 그래픽스 학습에 0이다.

→ **cgltf 채택**. 의존성 0, 단일 헤더, bgfx/sokol/Wicked Engine과 같은
   규모 프로젝트 다수가 채택.

### 2.2 빌드 시스템 통합

기존 `tinyobjloader` 패턴을 그대로 미러링:

- 네이티브: vcpkg 포트 (`find_package(cgltf CONFIG REQUIRED)` — 첫 시도)
- WASM: `FetchContent_Declare(cgltf ... v1.14)` + `INTERFACE` 타겟

**함정 #1 발견** — vcpkg의 cgltf 포트는 **header-only**라서 CMake config
파일을 제공하지 않는다. `find_package(cgltf CONFIG REQUIRED)`가 실패하고
`Could not find a package configuration file` 에러. 메시지를 읽어 보면
힌트가 있다:

```text
cgltf is header-only and can be used from CMake via:
  find_path(CGLTF_INCLUDE_DIRS "cgltf.h")
  target_include_directories(main PRIVATE ${CGLTF_INCLUDE_DIRS})
```

→ 네이티브도 `find_path` + 직접 `INTERFACE` 타겟 생성으로 통일. 두 경로가
   동일한 `cgltf` 링크 타겟 이름을 노출하므로 `target_link_libraries`
   부분은 backend-agnostic.

### 2.3 모듈 설계

```text
src/assets/
├── ImportedAsset.hpp        — engine-side data structures (POD)
├── AssetImporter.hpp/.cpp   — cgltf → ImportedAsset interpretation layer
```

cgltf의 C API는 `AssetImporter.cpp` 내부에만 노출. 그 이외 엔진 코드는
`ImportedAsset` (mesh / material / texture / node 묶음)만 본다. 라이브러리
격리 원칙.

`ImportedAsset`의 구조체들은 glTF 2.0의 metallic-roughness 모델을 그대로
1:1 미러링한다. baseColorFactor, metallicFactor, roughnessFactor,
emissiveFactor, doubleSided + 5개 텍스처 인덱스. 엔진이 외부 표준을
역으로 받아들이는 형태 — 머티리얼 시스템 설계를 우리가 임의로 하지 않고
glTF 컨벤션을 그대로 수용.

---

## 3. 단계별 진행 (커밋 단위)

### 3.1 `ab140bf` cgltf 스캐폴드

cgltf 단일 헤더 통합 + `AssetImporter::load()` 빈 구현이 빌드까지 되는
것 확인. 의도적으로 `std::nullopt` 반환 (다음 커밋에서 실제 변환). 이
스캐폴드 단계가 별도 커밋인 이유는 빌드 시스템 통합과 코드 구현의 위험을
분리해서 검증하기 위함.

### 3.2 `204fd1c` 메시 ingest + showcase 경로

`translatePrimitive` 본격 구현. cgltf primitive를 walk하며 POSITION /
NORMAL / TEXCOORD_0를 `cgltf_accessor_read_float`로 추출, 인덱스는
`cgltf_accessor_read_uint`. 비-삼각형 / POSITION 누락 / accessor 차원
불일치는 명시적으로 skip + 로그.

씬 wiring: `Renderer::setShowcaseMesh(verts, indices, worldMatrix)` +
`ShowcaseAsset` 내부 구조체(Mesh + 1-entry ObjectData SSBO + visibleIndices
+ set 1 bind group). `GBufferPass::execute`에 optional showcase 4개
파라미터 추가 — 빌딩 indirect draw 직후 같은 렌더 패스에서 set 1만 swap
후 `drawIndexed(count, 1, 0, 0, 0)`. 같은 파이프라인·set 0·G-Buffer
attachment·deferred lighting 경로를 그대로 재사용.

이 시점에서 헬멧은 **회색**으로 등장 (텍스처 sampling 없음).

### 3.3 `161ada6` ObjectData 144B 확장

`ObjectData`에 `glm::uvec4 textureIndices` (16B) 추가, 128B → 144B로 확장.
sentinel `0xFFFFFFFF`. C++ 측 `static_assert(sizeof(ObjectData) == 144)`
가드. 셰이더 8개(building/gbuffer/shadow/frustum_cull × glsl/wgsl)에 동시
반영 — **2026-05-19 shadow stride 사고**의 직접 교훈. 모든 사본을 한 커밋
안에서 동기.

AssetImporter는 같은 커밋에서 `ImportedTexture` (RGBA8 픽셀 디코드, stb_image)
+ `ImportedMaterial` (PBR-MR factors + 4 텍스처 인덱스) 추출까지 처리.
WASM 측 stb는 별도 FetchContent로 추가 (vcpkg의 `Stb::stb` alias로
이름 통일).

### 3.4 `32c3bdb` 텍스처 GPU 업로드

`ResourceManager::uploadRGBA8FromMemory(pixels, w, h, format)` public
메서드 신설. `Renderer::uploadShowcaseMaterialTextures(asset, materialIndex)`
가 머티리얼이 참조하는 텍스처 인덱스를 walk하면서 색공간 자동 분기:
baseColor/emissive = `RGBA8UnormSrgb`, normal/MR/occlusion = `RGBA8Unorm`.
이 시점에는 GPU에 텍스처는 있지만 셰이더가 sampling은 안 함.

이 커밋에 부수적으로 `ResourceManager.cpp`의 `STB_IMAGE_IMPLEMENTATION`
가드 제거. 이전엔 `#ifndef __EMSCRIPTEN__`으로 막혀 있었는데, AssetImporter
가 WASM에서도 `stbi_load_from_memory`를 호출하면서 undefined symbol
에러가 발생. `-s ERROR_ON_UNDEFINED_SYMBOLS=0`이 link-time 에러를 가려서
런타임에야 터지는 latent bug였음 — 가드 제거로 양 백엔드에서 stb_image
구현이 살아 있음을 보장.

### 3.5 `a2edecc` Step 6a — baseColor sampling + 함정 3종 동반 수정

가장 큰 커밋. **세 가지 함정**이 같은 흐름에서 발현돼 한 번에 잡았다(§4
참조). 핵심 변경:

- **Material bind group (set 2, WebGPU only)**:
  `createMaterialBindGroupInfrastructure()`가 4-텍스처+sampler layout +
  4개 1×1 dummy 텍스처 (white / flat normal / MR / black) + 공유 sampler
  + default bind group을 생성. 빌딩은 default를 쓰고, 헬멧은 `uploadShowcaseMaterialTextures`
  결과로 자기 bind group을 갖는다.
- **GBufferPass**: pipeline layout에 set 2 추가 (WebGPU only — Vulkan은
  bindless 점유), `execute()`가 default + showcase material BG 2개를
  추가 인자로 받음.
- **`setShowcaseMesh`**: 선택적 `ImportedMaterial*` 인자 — baseColorFactor,
  metallicFactor, roughnessFactor가 ObjectData로 흘러 들어감. 텍스처가
  multiply되는 시맨틱과 일관.
- **`gbuffer.wgsl`**: set 2 binding 5개 선언, VertexOutput에 texCoord
  pass-through, fragment에서
  `textureSample(baseColorTex, ...).rgb * input.albedo`. 빌딩은 dummy
  white × grey factor = grey (변화 없음), 헬멧은 sample × white factor
  = sample (실제 텍스처 표시).

이 커밋 후 헬멧이 처음으로 실제 albedo 텍스처(주황·빨강 패널)로 화면에
등장.

---

## 4. 디버깅 여정 — 같은 흐름에서 발견한 함정 3종

step 6a를 마치기까지 빌드는 매번 깨끗했지만 브라우저에서 처음 띄웠을 때
**세 번 연속 다른 에러**가 떴다. 셋 다 WebGPU 측 특수성에서 비롯한 것이라
한 자리에 묶어 둔다.

### 4.1 함정 #1 — bytesPerRow=0이 WebGPU에서 거부됨

증상: 콘솔에 다음이 떴다.

```text
[WebGPU Error] Validation: The byte size of each row (4) is > bytesPerRow (0).
 - While [encoding ...CopyBufferToTexture(...)].
 - While finishing [CommandEncoder ...].
```

(4)는 1×1 dummy 텍스처 업로드 시점, (8192)는 2048×2048 헬멧 텍스처
업로드 시점. 9개 텍스처 업로드가 전부 실패하며 cascade로 ASYNCIFY abort.

**근본 원인**: RHI 추상화의 inconsistency. `BufferTextureCopyInfo::bytesPerRow`
필드 이름은 "bytes"지만:

- Vulkan 백엔드: `VkBufferImageCopy::bufferRowLength`에 TEXELS 단위로 전달
  (0 = tightly packed, Vulkan의 컨벤션)
- WebGPU 백엔드: `WGPUTexelCopyBufferLayout::bytesPerRow`에 BYTES 단위로
  전달. **0은 거부됨** — 실제 row size 이상이어야 valid.

기존 `ResourceManager::uploadTexture`는 Vulkan 시절 코드라 `bytesPerRow = 0`
("tightly packed" 의미)으로 호출. Vulkan에선 통과, WebGPU에선 실패.

**수정 #1**: `uploadRGBA8FromMemory`에서 `#ifdef __EMSCRIPTEN__`로 분기 —
WebGPU는 `width * 4`, Vulkan은 0 유지. RHI 자체의 정규화는 차후 작업으로
미루고 callsite에서 우회.

### 4.2 함정 #2 — Dawn은 1×1 텍스처에도 256-alignment 강제

함정 #1을 잡고 다시 띄웠더니 새 에러:

```text
[WebGPU Error] Validation: bytesPerRow (4) is not a multiple of 256.
```

WebGPU 스펙은 `bytesPerRow`가 256의 배수여야 한다고 명시하는데, 명목상
조건은 "image height > 1". 1×1 텍스처는 height = 1이므로 정렬 불필요해
보인다. **그러나 Dawn (emdawnwebgpu)는 스펙보다 엄격하게 height와 무관하게
항상 256 정렬을 요구**한다. 4 bytes로 호출한 1×1 dummy 업로드가 거부됨.

**수정 #2**: staging 버퍼 행 stride를 256-byte로 패딩. 1×1 → 256 bytes/row,
2048×2048(이미 8192 = 32×256) → 그대로. 임의 너비도 자동:

```cpp
const uint32_t kRowAlignment    = 256u;
const uint32_t paddedBytesPerRow = (tightBytesPerRow + kRowAlignment - 1u)
                                    & ~(kRowAlignment - 1u);
```

per-row `memcpy`로 패딩 stride에 복사 (행 사이 trailing bytes는 GPU가 읽지
않으므로 미정의 상태 유지 무방). claude.md §9에 "1×1에도 256 정렬 강제,
스펙보다 엄격" 사실을 기록 — 다음 세션에서 재발견 막기 위함.

### 4.3 함정 #3 — JS setInterval이 emscripten_sleep 중 wasm 진입

함정 #2를 잡고 또 띄웠더니 이번엔:

```text
Aborted(Assertion failed: Cannot have multiple async operations in flight at once)
```

스택을 보면 `setInterval → Module.getPassTimeGBuffer() → ASYNCIFY abort`.
패스 타이밍 폴러가 wasm 함수를 호출했는데 wasm이 다른 곳에서 suspend
중이라 ASYNCIFY가 abort. 이전(2026-05-20)의 **WebGPUTimer mapAsync 콜백**
사고와 같은 클래스이지만 **트리거가 JS 측 타이머**라는 점이 다르다.

왜 이번에 생겼나: 9개 텍스처 업로드 × 각 `queue->waitIdle()`이 fence
wait의 `emscripten_sleep` 윈도우를 거친다. 누적 시간이 ~150ms로 늘어
500ms setInterval 첫 발이 그 안에 들어갈 확률이 급등.

**수정 #3**: wasm-side busy flag를 JS에 노출. RAII 가드 패턴:

```cpp
struct BusyFlagGuard {
    BusyFlagGuard()  { EM_ASM({ Module._wasmBusy = true;  }); }
    ~BusyFlagGuard() { EM_ASM({ Module._wasmBusy = false; }); }
};
BusyFlagGuard _busyGuard;  // drawFrame 진입부에 — 모든 early-return 자동 처리
```

JS 측:

```javascript
var Module = { _wasmBusy: true, ... };  // init 동안 기본 busy
setInterval(function() {
    if (Module._wasmBusy) return;       // 폴링 skip
    // ... wasm calls ...
}, 500);
```

claude.md §9에 "WebGPU 콜백뿐 아니라 모든 JS→wasm 진입에 적용되는 더 넓은
규칙"으로 일반화하여 기록. 향후 새 `EMSCRIPTEN_BINDINGS` 추가 시 동일
gate를 적용하라는 지침.

---

## 5. 부수 수정 — 같은 흐름에서 정리한 작은 것들

### 5.1 WASD/QE 카메라 조작

`Camera::translate(deltaX, deltaY)`가 두 인자 모두 부호 반전해 ↑↓← →가
뒤집혀 있던 점을 사용자가 step 6a 검증 중 발견. 시그니처를 의미 명시형
3축으로 재설계:

```cpp
void translate(float deltaRight, float deltaForward, float deltaUp);
//                ^A/D            ^W/S              ^Q/E
```

Forward는 view 방향의 **수평 투영**을 사용해 카메라가 아래를 보고 있을
때 W/S가 카메라를 기울이지 않음. Up은 world Y. Q/E 추가로 수직 이동.

### 5.2 헬멧 "안쪽이 보이는" 현상 — winding 불일치

step 6a 검증 시 사용자가 "헬멧 표면 아닌 내부에 텍스처가 보이는 듯해"라고
보고. 가설 추적:

- glTF 2.0 스펙: **CCW front face** mandatory
- 엔진 건물 파이프라인: `CullMode::Back + FrontFace::Clockwise`
  (BuildingManager의 procedural cube가 CW로 authored됨)
- 결과: 헬멧의 진짜 외곽 삼각형이 BACK으로 분류되어 culling → 내부 면만 보임

**수정**: `AssetImporter::translatePrimitive`에서 각 삼각형의 인덱스 1·2를
swap. **geometric mirror가 아니라 winding만 반전**하므로 메시 자체는 그대로,
외곽이 엔진의 CW front 컨벤션과 매칭. 빌딩 큐브에는 영향 0 (AssetImporter
경로만 통과). `TODO(engine-roadmap)` 주석으로 "큐브 + 파이프라인을 CCW로
마이그레이션하면 이 asset-side 재작성이 사라짐" 명시.

### 5.3 커밋 메시지 워크플로우 함정 두 번 더

`__pycache__` (이전 commit `8e2a299`)에 이어, **`.commit_msg.tmp`가
`git add -A`로 추적되는 사고**가 commit `161ada6`에서 발생. `git rm
--cached` + `.gitignore`에 `*.tmp` 추가로 chore commit `b21d930` 수행.

이후 또 **`claude.md`가 같은 패턴**으로 commit `32c3bdb`에 들어갔다. chore
commit `04bab2f`로 추적 해제 + `.gitignore`에 `claude.md` 추가. 같은 함정에
세 번 빠짐 — 패턴은 항상 `git add -A`가 의도치 않은 파일을 잡아옴.

→ claude.md §11에 "`git add -A`로 staging 한 다음 commit하는 패턴은 위험.
   특정 파일을 명시적으로 stage하거나 `git add -u`(추적 중 파일만)를
   쓰라" 지침 추가. 또한 메시지 파일은 `*.tmp` 확장자(자동 ignore)로
   고정.

### 5.4 claude.md — AI agent context 신설

빈 파일이었던 프로젝트 루트의 `claude.md`를 본격 작성. 일반 AI agent
지침(§1~§4)은 보존, Mini-Engine 특화 컨텍스트(§5~§12)를 추가: 프로젝트 정체,
디렉터리 맵, 빌드 명령, 셰이더 동기 규칙, WebGPU 함정 모음, glTF 컨벤션,
커밋 hygiene, 진입점 문서. **이번 세션에서 발견한 모든 함정이 §9에 기록되어
다음 세션이 재발견하지 않게 함.** (그리고 claude.md 자체는 5.3의 두 번째
사고로 인해 `.gitignore`에 등록되어 추적 안 됨 — 로컬 전용.)

---

## 6. 수정 파일 요약

| 파일 | 변경 |
| --- | --- |
| `vcpkg.json`, `CMakeLists.txt` | cgltf 의존성 추가 (vcpkg + FetchContent dual-path), stb FetchContent for WASM |
| `src/assets/ImportedAsset.hpp` | 신규 — glTF 2.0 metallic-roughness 모델 1:1 미러링 |
| `src/assets/AssetImporter.{hpp,cpp}` | 신규 — cgltf 해석 레이어. mesh / material / texture decode, winding 반전, primitive skip 로그 |
| `src/utils/Vertex.hpp` | `tangent: vec3` 추가, 해시 갱신, 모든 비교 사이트 동기 |
| `src/rendering/InstancedRenderData.hpp` | `ObjectData` 128B → 144B (`uvec4 textureIndices`), `static_assert(sizeof) == 144` |
| 셰이더 8개 (`building`/`gbuffer`/`shadow`/`frustum_cull` × `glsl`/`wgsl`) | ObjectData 144B 동기, tangent input declaration |
| `src/rendering/Renderer.{hpp,cpp}` | `ShowcaseAsset` 슬롯, `setShowcaseMesh` overload (material factor), `uploadShowcaseMaterialTextures`, `createMaterialBindGroupInfrastructure`, drawFrame `BusyFlagGuard` |
| `src/rendering/GBufferPass.{hpp,cpp}` | material layout/bind group 파라미터 추가, set 2 pipeline layout (WebGPU only) |
| `src/resources/ResourceManager.{hpp,cpp}` | `uploadRGBA8FromMemory` public, 256-aligned per-row staging, `STB_IMAGE_IMPLEMENTATION` 가드 제거 |
| `src/scene/Camera.{hpp,cpp}` | `translate` 3축 시그니처, forward 수평 투영 |
| `src/Application.cpp` | `AssetImporter` 호출, helmet load → `setShowcaseMesh` + `uploadShowcaseMaterialTextures`, WASD/QE 입력 |
| `shaders/gbuffer.wgsl` | Step 6a: set 2 binding 5개, texCoord 출력, baseColor sampling |
| `tests/wasm_shell.html` | `Module._wasmBusy = true` 기본값, 타이밍 폴러 gate |
| `models/DamagedHelmet.glb` | 신규 — Khronos Sample Assets, 3.7 MB. WASM `--preload-file`로 묶임 |
| `.gitignore` | `*.tmp`, `claude.md` 추가 |
| `claude.md` | 신규 (로컬 전용) — AI agent 컨텍스트 + Mini-Engine 특화 함정 모음 |

---

## 7. 후속

| 단계 | 상태 |
| --- | --- |
| 6a baseColor sampling | ✅ |
| 6b normal map + TBN | 다음 |
| 6c MR + AO sampling | 다음 |
| 6d emissive | 다음 |
| Vulkan parity (step 7) | 후속 |
| SceneNode 최소 구현 (step 8) | 후속 |
| 시연 통합 — A/B 모드에 머티리얼 토글 추가 (step 9) | 후속 |

Vulkan side는 의도적으로 미터치. WebGPU 측이 안정된 후 별도 단계에서
파이프라인 layout/bindless 통합 방안을 결정.
