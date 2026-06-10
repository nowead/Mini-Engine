# Mini-Engine: Architecture Evolution

> Vulkan Tutorial의 단일 파일에서 PBR & GPU-Driven 렌더링 엔진까지의 발전 기록

---

## 목차

- [개요](#개요)
- [Stage 1: Monolith에서 Layered Architecture로](#stage-1-monolith에서-layered-architecture로)
- [Stage 2: Layered Architecture에서 RHI Architecture로](#stage-2-layered-architecture에서-rhi-architecture로)
- [Stage 3: WebGPU 백엔드 구현](#stage-3-webgpu-백엔드-구현)
- [Stage 4: GPU Instancing & 게임 로직 도입](#stage-4-gpu-instancing--게임-로직-도입)
- [Stage 5: PBR & GPU-Driven 렌더링 고도화](#stage-5-pbr--gpu-driven-렌더링-고도화)
- [아키텍처 비교: Before vs After](#아키텍처-비교-before-vs-after)
- [타임라인](#타임라인)

---

## 개요

Mini-Engine은 [vulkan-tutorial.com](https://vulkan-tutorial.com/)의 학습 코드에서 시작하여, 5단계의 대규모 리팩터링과 기능 확장을 거쳐 현재의 모습이 되었다.

| Stage | 핵심 작업 | 동기 |
|-------|----------|------|
| **Stage 1** | Monolith → Layered Architecture | 467줄 단일 파일의 유지보수 불가능 |
| **Stage 2** | Layered → RHI Architecture | Vulkan 직접 호출의 플랫폼 종속성, 상용 엔진 아키텍처와의 괴리 |
| **Stage 3** | WebGPU 백엔드 추가 | RHI 아키텍처의 실증 — 웹 배포 지원 |
| **Stage 4** | GPU Instancing + 게임 로직 | 대량 오브젝트 렌더링 기반, 도메인 로직 분리 |
| **Stage 5** | PBR, GPU-Driven, Profiling | 렌더링 품질과 대규모 씬 처리 능력 |
| **Stage 6** | WASM 포스트프로세스 파이프라인 | WASM 렌더링 품질 — HDR, Tonemap, FXAA, 안정적 Resize |

---

## Stage 1: Monolith에서 Layered Architecture로

> 상세 문서: [docs/refactoring/monolith-to-layered/](refactoring/monolith-to-layered/REFACTORING_OVERVIEW.md)

### 시작점

vulkan-tutorial.com의 `HelloTriangleApplication` — 모든 로직이 `main.cpp` 하나에 집중된 모놀리식 구조.

```
main.cpp (467줄)
├── Window management
├── Vulkan instance/device/queue 생성
├── Swapchain, Pipeline, Command, Sync 관리
├── Buffer/Image 메모리 할당
├── Descriptor 관리
├── OBJ 로딩 + 텍스처 로딩
└── 렌더 루프
```

### Phase 1-7: 핵심 아키텍처 구축

**Phase 1 — Utility Layer 분리**
- `VulkanCommon.hpp`: Vulkan/GLM 헤더 중앙화, GLM 설정 불일치 방지
- `Vertex.hpp`: 정점 구조체 + 해시 함수 (OBJ 로딩 시 중복 제거 기반)
- `FileUtils.hpp`: 셰이더 파일 읽기 유틸리티

**Phase 2 — Device Management 캡슐화**
- `VulkanDevice` 클래스 생성: instance, physical/logical device, queue 관리
- 명시적 3단계 초기화 순서 확립 (device → surface → logical device)
- `findMemoryType`, `findSupportedFormat` 유틸리티 통합
- main.cpp에서 8개 멤버 변수 → `unique_ptr<VulkanDevice>` 1개로 축소

**Phase 3 — Resource Management RAII화**
- `VulkanBuffer`: 모든 버퍼 타입(vertex, index, uniform, staging) 통합 관리
- `VulkanImage`: image + view + sampler 자동 생성, RAII 정리
- 15개 멤버 변수 → 5개 `unique_ptr`로 축소
- `createBuffer()`, `copyBuffer()` 등 6개 헬퍼 함수 제거

**Phase 4 — Rendering Layer 분리**
- `SyncManager`: 세마포어/펜스 관리 (frames-in-flight ≠ swapchain images 핵심 인사이트)
- `CommandManager`: 커맨드 풀/버퍼 + single-time command 패턴
- `VulkanSwapchain`: 프레젠테이션 관리, recreation 단일 메서드
- `VulkanPipeline`: 그래픽스 파이프라인 상태 통합
- main.cpp에서 ~210줄 제거

**Phase 5 — Scene Layer 구축**
- `Mesh` 클래스: 지오메트리 + GPU 버퍼, `bind()`/`draw()` 인터페이스
- `OBJLoader`: tinyobjloader 기반, 정점 중복 제거 자동화
- main.cpp에서 ~96줄 제거

**Phase 6 — Renderer 통합**
- `Renderer` 클래스: 모든 Vulkan 서브시스템 소유, 5개 public 메서드
- `loadModel()`, `loadTexture()`, `drawFrame()`, `waitIdle()`, `handleFramebufferResize()`
- main.cpp: 467줄 → 93줄 (80% 감소)

**Phase 7 — Application Layer 완성**
- `Application` 클래스: 윈도우 + 메인 루프 관리
- **main.cpp: 18줄** — 순수 엔트리 포인트 달성

### Phase 8-11: 확장 기능

**Phase 8 — Cross-Platform 지원**
- Linux (Vulkan 1.1): Traditional Render Pass
- macOS/Windows (Vulkan 1.3): Dynamic Rendering
- 플랫폼별 확장/기능 분기 (`#ifdef __linux__`)
- 단일 코드베이스로 3개 플랫폼 지원

**Phase 9 — 서브시스템 분리**
- `ResourceManager`: 텍스처 로딩 + 캐싱
- `SceneManager`: 메시 + 씬 그래프 기반
- Renderer를 God Object에서 렌더링 조율자로 변환

**Phase 10 — FdF 와이어프레임 통합**
- `FDFLoader`: heightmap 파싱 + 와이어프레임 생성
- `Camera`: 구면 좌표계 기반 카메라 시스템
- `VulkanPipeline`에 TopologyMode 추가 (TriangleList/LineList)
- Core/Resource 레이어 **변경 없이** 새 렌더링 모드 추가 — 아키텍처 확장성 검증

**Phase 11 — ImGui 디버깅 UI**
- `ImGuiManager`: Dear ImGui 통합, 플랫폼별 렌더링 경로
- volk 메타 로더로 Vulkan 헤더 버전 불일치 해결
- RAII 소멸 순서 관리 (ImGui → Renderer → Camera → Window)

### Stage 1 성과

| 지표 | Before | After |
|------|--------|-------|
| main.cpp | 467줄 (모든 로직) | 18줄 (순수 엔트리 포인트) |
| 클래스 수 | 0 | 14+ |
| 파일 수 | 1 | 34+ |
| 아키텍처 | 모놀리식 | 4-Layer |
| 메모리 관리 | 수동 | RAII 100% |
| 플랫폼 | 단일 | Linux/macOS/Windows |

---

## Stage 2: Layered Architecture에서 RHI Architecture로

> 상세 문서: [docs/refactoring/layered-to-rhi/](refactoring/layered-to-rhi/RHI_MIGRATION_PRD.md)

### 동기

Stage 1의 Layered Architecture는 코드 구조는 깔끔해졌지만, 근본적인 한계가 있었다:

1. **Vulkan 종속성**: 상위 레이어(Renderer, ResourceManager)가 Vulkan API를 직접 호출
2. **멀티 플랫폼 불가**: WebGPU, D3D12, Metal 같은 다른 그래픽스 API 지원 불가
3. **객체지향 캐싱 문제**: 가상 함수 호출 체인이 실시간 렌더링 hot path에 비효율적
4. **상용 엔진과의 괴리**: Unreal Engine, Unity 등은 RHI 패턴으로 그래픽스 API를 추상화

이러한 문제를 해결하기 위해, 완전한 RHI (Render Hardware Interface) 아키텍처로의 전환을 시도했다.

### Phase 1 — RHI 인터페이스 설계

WebGPU 스타일 API를 기반으로 15개 추상 인터페이스를 설계했다.

```
RHITypes.hpp (Foundation)
    ↓
RHIBuffer, RHITexture, RHISampler, RHIShader (Resource)
    ↓
RHIBindGroup, RHIPipeline (Pipeline)
    ↓
RHICommandBuffer, RHICommandEncoder (Command)
    ↓
RHISwapchain, RHIQueue (Presentation)
    ↓
RHIDevice (Aggregator)
    ↓
RHI.hpp (Convenience Header)
```

핵심 설계 결정:
- **Command Encoder 패턴**: Vulkan의 즉시 커맨드 버퍼 대신 WebGPU식 인코더 채택
- **Bind Group 모델**: Vulkan descriptor set → WebGPU bind group 통합 추상화
- **Type Safety**: `enum class` + 비트 연산자 오버로드로 컴파일 타임 안전성 확보
- **RAII 유지**: `std::unique_ptr` 기반 소유권, 가상 소멸자

결과: 15개 헤더, 2,125줄

### Phase 2 — Vulkan 백엔드 구현

기존 Vulkan 래퍼 클래스를 RHI 인터페이스의 구현체로 변환했다.

| 구현 클래스 | 역할 |
|------------|------|
| `VulkanRHIDevice` | 디바이스 관리, VMA 통합 |
| `VulkanRHIBuffer` | VMA 기반 버퍼 할당 |
| `VulkanRHITexture` | VMA 기반 이미지 할당 |
| `VulkanRHICommandEncoder` | 커맨드 버퍼 레코딩 |
| `VulkanRHIPipeline` | 그래픽스/컴퓨트 파이프라인 |
| `VulkanRHISwapchain` | 프레젠테이션 관리 |
| `VulkanRHIBindGroup` | Descriptor Set 래핑 |
| `VulkanRHIShader` | SPIR-V 셰이더 모듈 |
| `VulkanRHIQueue` | 커맨드 제출 |
| `VulkanRHISync` | 펜스/세마포어 |

핵심 변경:
- **VMA (Vulkan Memory Allocator) 도입**: 수동 메모리 관리 → VMA 자동 관리
- **Wrapper 패턴**: 기존 코드 80-90% 재사용

결과: 12개 구현 클래스, 3,650줄

### Phase 3 — Factory & Bridge 패턴

```cpp
// 런타임 백엔드 선택
auto device = RHIFactory::createDevice(
    DeviceCreateInfo{}.setBackend(RHIBackendType::Vulkan)
);

// 점진적 마이그레이션을 위한 브릿지
class RendererBridge {
    std::unique_ptr<rhi::RHIDevice> m_rhiDevice;
    // 기존 레거시 코드와 새 RHI 코드 공존 지원
};
```

### Phase 4-6 — 상위 레이어 마이그레이션

- **Renderer**: `drawFrame()` → RHI 커맨드 인코더 기반으로 전면 전환
- **ResourceManager**: `VulkanBuffer/VulkanImage` → `rhi::RHIBuffer/RHITexture`로 교체
- **SceneManager/Mesh**: RHI 타입 사용으로 전환
- **ImGuiManager**: Adapter 패턴으로 RHI 통합
  ```cpp
  class ImGuiBackend {
      virtual void init(rhi::RHIDevice*, rhi::RHISwapchain*) = 0;
      virtual void render(rhi::RHICommandEncoder*) = 0;
  };
  class ImGuiVulkanBackend : public ImGuiBackend { /* ... */ };
  ```

### Phase 7-8 — 레거시 정리 & 모듈화

**삭제된 레거시 코드:**

| 컴포넌트 | 삭제 LOC | 대체 |
|----------|---------|------|
| VulkanBuffer | ~250 | rhi::RHIBuffer |
| VulkanImage | ~200 | rhi::RHITexture |
| VulkanPipeline | ~75 | rhi::RHIRenderPipeline |
| VulkanSwapchain | ~86 | rhi::RHISwapchain |
| SyncManager | ~140 | RHI 내부 동기화 |
| CommandManager | ~140 | RHI 커맨드 인코딩 |
| **합계** | **~890** | **100% RHI** |

**모듈 디렉토리 구조:**

```
src/
├── rhi/                    # RHI 추상 인터페이스
│   ├── include/rhi/        # 15개 public 헤더
│   └── src/RHIFactory.cpp  # 팩토리 구현
│
├── rhi-vulkan/             # Vulkan 백엔드
│   └── src/VulkanRHI*.cpp  # 12개 구현 클래스
│
└── rhi-webgpu/             # WebGPU 백엔드 (Stage 3)
```

### Stage 2 성과

| 지표 | Before (Layered) | After (RHI) |
|------|------------------|-------------|
| 그래픽스 API 종속성 | Vulkan 100% | 0% (상위 레이어) |
| 레거시 래퍼 클래스 | 6개 | 0개 |
| 중복 리소스 | 4개 | 0개 |
| 프레임 타임 오버헤드 | - | < 2% |
| 메모리 오버헤드 | - | ~0.5% (vtable) |
| Validation 에러 | 가변 | 0 |

---

## Stage 3: WebGPU 백엔드 구현

> 상세 문서: [docs/refactoring/webgpu-backend/](refactoring/webgpu-backend/)

RHI 아키텍처의 가치를 실증하기 위해, 두 번째 그래픽스 백엔드로 WebGPU를 구현했다.

### 핵심 과제와 해결

**1. SPIR-V → WGSL 셰이더 변환**
- Vulkan은 SPIR-V, WebGPU는 WGSL을 사용
- `WebGPURHIShader`에서 SPIR-V 바이너리를 WGSL 텍스트로 자동 변환

**2. 비동기 API 래핑**
- WebGPU API는 비동기(Promise 기반), RHI는 동기 인터페이스
- `wgpuBufferMapAsync` → 동기 대기 래퍼 구현

**3. 플랫폼별 백엔드 자동 선택**
```cpp
#if defined(__EMSCRIPTEN__)
    defaultBackend = rhi::RHIBackendType::WebGPU;
#else
    defaultBackend = rhi::RHIBackendType::Vulkan;
#endif
```

### 구현 결과

- 15개 WebGPU RHI 클래스 구현 (~6,500 LOC)
- Emscripten WASM 빌드 파이프라인 구축
- 웹 배포 아티팩트: `.html` (3.2KB) + `.js` (156KB) + `.wasm` (185KB)
- RHI 인터페이스 변경 없이 두 번째 백엔드 추가 — **아키텍처 설계의 정당성 입증**

---

## Stage 4: GPU Instancing & 게임 로직 도입

> 상세 문서: [docs/refactoring/aaa-upgrade/GPU_INSTANCING.md](refactoring/aaa-upgrade/GPU_INSTANCING.md)

RHI 아키텍처와 WebGPU 백엔드가 완성된 후, 실제 애플리케이션을 위한 대량 오브젝트 렌더링과 게임 로직 레이어를 구축했다.

### 배경

Stage 3까지 엔진은 단일 OBJ 모델을 렌더링하는 수준이었다. 실제 애플리케이션(도시 시뮬레이션, 데이터 시각화 등)을 위해서는 수천 개의 동일 메시를 각기 다른 위치/크기/색상으로 렌더링할 수 있어야 했다. 또한 렌더링 코드와 게임/도메인 로직의 분리가 필요했다.

### GPU Instancing 구현

**핵심 개념**: 동일 메시를 N번 그릴 때 N개의 draw call 대신, 단일 draw call에 `instanceCount=N`을 전달하여 GPU가 병렬로 처리한다.

**RHI API 지원**:
```cpp
// 이미 RHI 인터페이스에 instanceCount 파라미터 존재
encoder->drawIndexed(indexCount, instanceCount, 0, 0, 0);
```

**Per-Instance 데이터 전달**:

- Vertex Input Rate를 `Instance`로 설정한 두 번째 버퍼 바인딩
- Per-vertex 데이터 (위치, 법선, UV) + Per-instance 데이터 (월드 위치, 색상, 스케일) 분리
- 셰이더에서 `gl_InstanceIndex` / `@builtin(instance_index)`로 인스턴스 식별

```cpp
// Instance buffer layout (binding 1, per-instance rate)
rhi::VertexBufferLayout instanceLayout;
instanceLayout.stride = sizeof(InstanceData);
instanceLayout.inputRate = rhi::VertexInputRate::Instance;
```

**성능 효과**:

| 지표 | 개별 Draw Call | GPU Instancing |
|------|--------------|----------------|
| 1,000 오브젝트 | 1,000 draw calls, ~10 FPS | 1 draw call, 60 FPS |
| CPU-GPU 동기화 | 1,000회 | 1회 |
| Driver 오버헤드 | 1,000x | 1x |

### 게임 로직 레이어 구축

렌더링과 도메인 로직을 분리하기 위해 `game/` 디렉토리를 신설했다.

**Entity 시스템**:
- `BuildingEntity`: 위치, 높이, 색상, 티커 심볼 등 도메인 데이터
- `BuildingManager`: 빌딩 엔티티 CRUD, 인스턴스 버퍼 생성/갱신, dirty tracking
- `WorldManager`: 월드 상태 관리, 마켓 데이터 업데이트 조율

**데이터 동기화**:
- `PriceUpdate`: 실시간 가격 데이터 구조체
- `MockDataGenerator`: 테스트용 가격 변동 생성기
- `AnimationUtils`, `HeightCalculator`: 가격 → 빌딩 높이 변환 유틸리티

**렌더링 인터페이스 분리**:
```cpp
// Application이 게임 로직에서 렌더링 데이터를 추출하여 Renderer에 전달
rendering::InstancedRenderData renderData;
renderData.mesh = buildingManager->getBuildingMesh();
renderData.instanceBuffer = buildingManager->getInstanceBuffer();
renderData.instanceCount = buildingManager->getBuildingCount();
renderer->submitInstancedRenderData(renderData);
```

Renderer는 게임 엔티티를 알지 못하고, Application이 게임 로직 → 렌더링 데이터 변환을 담당한다.

### 부가 기능

**Shadow Mapping 도입**:
- `ShadowRenderer`: 태양 방향 기반 directional shadow
- Orthographic light projection + PCF 소프트 섀도
- 빌딩 그림자가 지면에 렌더링

**Skybox Rendering**:
- `SkyboxRenderer`: 환경맵 기반 배경 렌더링
- 카메라 위치와 무관한 무한 원거리 배경

**Particle System**:
- `ParticleSystem`: CPU 기반 파티클 시뮬레이션
- `ParticleRenderer`: GPU 빌보드 렌더링
- 이벤트(가격 급등/급락) 시 시각적 피드백

### Stage 4 성과

| 지표 | Before | After |
|------|--------|-------|
| 렌더링 오브젝트 | 단일 OBJ | 수천 개 인스턴스 |
| Draw Call 방식 | 1 object = 1 call | N objects = 1 call |
| 게임 로직 | 없음 | Entity/Manager 패턴 |
| 레이어 분리 | Renderer only | Renderer + Game Logic |
| 그림자 | 없음 | Directional Shadow + PCF |

---

## Stage 5: PBR & GPU-Driven 렌더링 고도화

RHI 아키텍처 위에 현대적인 렌더링 기법들을 순차적으로 구현했다.

### Week 1 — Cook-Torrance PBR & IBL

**PBR 파이프라인**
- Blinn-Phong → Cook-Torrance BRDF 전면 교체
- GGX (Trowbridge-Reitz) Normal Distribution Function
- Smith-Schlick Geometry Function (roughness 기반 self-shadowing)
- Fresnel-Schlick Approximation (금속/비금속 반사 차이)
- ACES Filmic Tone Mapping + sRGB 출력
- Per-object metallic/roughness 파라미터 (SSBO를 통한 전달)

**Image Based Lighting (IBL)**
- HDR equirectangular 환경맵 로딩
- Compute Shader 기반 전처리 파이프라인:
  - `equirect_to_cubemap.comp`: HDR → Cubemap 변환
  - `irradiance_map.comp`: Diffuse irradiance convolution
  - `prefilter_env.comp`: Specular prefiltered environment map (5 MIP levels)
  - `brdf_lut.comp`: BRDF integration LUT (Split-Sum Approximation)
- 런타임에 1회 계산 후 캐싱, 매 프레임 참조

### Week 2 — GPU-Driven Rendering

**Per-Instance Vertex Attribute → SSBO 전환**
- 기존: 인스턴스 데이터를 vertex attribute로 전달 (4x4 행렬 = 4개 attribute slot 소비)
- 변경: 128바이트 `ObjectData` 구조체를 SSBO에 저장, `gl_InstanceIndex`로 인덱싱
  ```glsl
  struct ObjectData {
      mat4 worldMatrix;       // 64 bytes
      vec4 boundingBoxMin;    // 16 bytes (AABB for culling)
      vec4 boundingBoxMax;    // 16 bytes
      vec4 materialParams;    // 16 bytes (metallic, roughness, ...)
      vec4 colorTint;         // 16 bytes
  };
  ```

**Compute Shader Frustum Culling**
- 각 오브젝트의 AABB를 카메라 6개 frustum plane과 비교
- Workgroup size 64, atomic counter 기반 visible index compaction
- 입력: ObjectData SSBO + CullUBO (frustum planes, object count)
- 출력: Indirect Draw Buffer + Visible Indices Buffer
- `frustum_cull.comp.glsl` / `frustum_cull.comp.wgsl` 듀얼 셰이더

**Indirect Draw**
- `drawIndexedIndirect`: GPU가 직접 draw call 파라미터 결정
- CPU에서 instance count를 몰라도 됨 — GPU가 culling 후 결정
- 100K+ 오브젝트를 단일 indirect draw call로 렌더링

### Week 3 — Memory Aliasing & Async Compute

**Memory Aliasing (Transient Resources)**
- 한 프레임 내에서만 사용되는 depth buffer → lazily allocated memory
- `VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT`로 물리 메모리 절약
- GPU 대역폭 감소 (tile-based GPU에서 특히 효과적)

**Async Compute (Timeline Semaphore)**
- Frustum culling compute shader를 별도 compute queue에서 비동기 실행
- `VK_KHR_timeline_semaphore`로 graphics ↔ compute 동기화
- 그래픽스 파이프라인과 컴퓨트 파이프라인 병렬 실행

### Week 4 — GPU Profiling & Stress Test

**GPU Profiler (`vkCmdWriteTimestamp`)**
- `GpuProfiler` 클래스: frame-in-flight별 Vulkan query pool 관리
- 3개 구간 타이밍: Frustum Culling, Shadow Pass, Main Render Pass
- `timestampPeriod`로 GPU tick → milliseconds 변환
- EMA (Exponential Moving Average) 평활화

**Stress Test UI**
- ImGui 로그 스케일 슬라이더: 16 → 100,000 오브젝트
- 프리셋 버튼 (16, 100, 1K, 10K, 100K)
- 오브젝트 수 변경 시 카메라 자동 조정, 그라운드 플레인 동적 스케일링

**측정 결과 (Vulkan, Desktop)**

| 오브젝트 수 | FPS | Frustum Cull | Shadow Pass | Main Pass | GPU Total |
|-----------|-----|-------------|-------------|-----------|-----------|
| 1,000 | 47 | 1 ms | 6 ms | 10 ms | 17 ms |
| 10,000 | 8 | 1 ms | 34 ms | 61 ms | 95 ms |
| 100,000 | 0.5 | 2 ms | 193 ms | 329 ms | 530 ms |

핵심 인사이트:
- Frustum Culling은 O(1) (1-2ms 일정) — compute shader의 병렬성 입증
- Shadow/Main Pass는 O(n) — visible 오브젝트 수에 비례
- 100K에서 CPU 병목 (Frame Time 2022ms >> GPU Total 530ms) — ObjectData 생성 부담

### 기타 렌더링 기능

**Shadow Mapping**
- 태양 방향 기반 Orthographic Projection
- PCF (Percentage Closer Filtering) 기반 소프트 섀도
- 동적 shadow scene radius (씬 크기에 따른 자동 조절)

**Skybox Rendering**
- HDR 환경맵 기반 스카이박스
- IBL 파이프라인과 cubemap 공유

**Particle System**
- CPU 시뮬레이션 기반 파티클 시스템
- GPU 빌보드 렌더링

---

## Stage 6: WASM 포스트프로세스 파이프라인

`finance-city-engine` fork와의 비교 분석을 통해 도출된 WASM 전용 렌더링 품질 개선.

### 배경

Stage 5까지 WASM/WebGPU 빌드는 씬 지오메트리를 스왑체인에 직접 렌더링하고 있었다. 이는 네이티브(Vulkan) 빌드 대비 두 가지 품질 열위가 있었다:

1. **HDR 부재**: 단순 LDR 렌더링 — 밝은 영역의 클리핑, 어두운 영역의 디테일 손실
2. **Anti-Aliasing 부재**: 특히 건물 엣지에서 두드러지는 계단 현상(aliasing)

또한 브라우저 뷰포트 크기 변경 시 스왑체인 재생성이 JS 이벤트 핸들러에서 직접 호출되어 Asyncify 코루틴과의 충돌로 불안정하게 동작했다.

### Phase 14 — 멀티패스 포스트프로세스 파이프라인

**렌더링 파이프라인 재설계 (WASM 전용)**:

```
[기존 WASM]
  Geometry → Swapchain (LDR, aliasing)

[개선 후 WASM]
  Geometry → HDR Texture (RGBA16Float)
           → Tonemap Pass (ACES + gamma → RGBA8Unorm LDR)
           → FXAA Pass (anti-aliasing → Swapchain)
```

**HDR 렌더 타겟**:
- `RGBA16Float` 포맷: 0.0~1.0 범위를 초과하는 밝기 정보 보존
- Geometry pass가 HDR 텍스처에 렌더링
- 스왑체인과 독립적으로 씬 해상도 유지

**ACES Filmic Tonemap (`tonemap.wgsl`)**:
- 알고리즘: `(x*(2.51x+0.03))/(x*(2.43x+0.59)+0.14)`
- HDR → LDR RGBA8Unorm 변환 (bright area compression, dark area detail preservation)
- WASM 빌드에서 명시적 gamma 2.2 적용 (Vulkan은 sRGB 스왑체인이 자동 처리)

**FXAA 3.11 Anti-Aliasing (`fxaa.wgsl`)**:
- Rec.601 luminance 기반 엣지 검출: `dot(color, vec3(0.299, 0.587, 0.114))`
- 12-step 엣지 워크로 엣지 방향/길이 추정
- 서브픽셀 블렌딩으로 계단 현상 완화
- LDR 중간 타겟에서 스왑체인으로 출력

**Fullscreen Triangle 기법**: 버텍스 버퍼 없이 3개 버텍스로 화면 전체를 덮음
```wgsl
var pos = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
```

**바인드 그룹 구성**:
- Tonemap: `set 0, binding 0` = HDR 텍스처, `binding 1` = 샘플러
- FXAA: `set 0, binding 0` = LDR 텍스처, `binding 1` = 샘플러, `binding 2` = 해상도 uniform

### Deferred Resize 메커니즘

**문제**: `emscripten_set_resize_callback`이 JS 이벤트 핸들러에서 실행된다. Asyncify 코루틴 외부에서 `waitIdle()` + 스왑체인 재생성을 호출하면 deadlock/corruption 가능.

**해결 패턴**:
```cpp
// 이벤트 핸들러 (안전)
app->m_pendingResize = true;
app->m_pendingWidth  = e->windowInnerWidth;
app->m_pendingHeight = e->windowInnerHeight;

// mainLoopFrame() 진입부 (Asyncify 코루틴 내 — 안전)
if (m_pendingResize && m_pendingWidth > 0 && m_pendingHeight > 0) {
    glfwSetWindowSize(window, m_pendingWidth, m_pendingHeight);
    renderer->handleFramebufferResize(m_pendingWidth, m_pendingHeight);
    camera->setAspectRatio(...);
    m_pendingResize = false;
}
```

`glfwSetWindowSize()` 먼저 호출 → GLFW 내부 상태와 캔버스 버퍼 크기 동기화 → 명시적 치수 전달 오버로드로 스왑체인 재생성 (GLFW 재쿼리 불필요, stale 데이터 방지).

### Stage 6 성과

| 지표 | Before (Stage 5) | After (Stage 6) |
|------|-----------------|----------------|
| WASM 색 정밀도 | LDR 직접 렌더 | RGBA16Float HDR 버퍼 |
| Tonemapping | 없음 (클리핑) | ACES Filmic (전체 범위) |
| Anti-Aliasing | 없음 | FXAA 3.11 (엣지 부드럽게) |
| Resize 안정성 | JS 핸들러 직접 재생성 (불안전) | Asyncify 코루틴 내 안전 적용 |
| 추가 드로우콜 | 0 | 2 (fullscreen tonemap + FXAA) |
| 메모리 추가 | 0 | RGBA16Float + RGBA8Unorm 각 1개 |

---

## Stage 7: 의료 볼륨 렌더링 (2026-05-29 ~ 2026-06-07)

### Stage 7 동기

PBR 쇼케이스가 끝난 시점, 엔진을 **WebGPU 기반 차세대 클라이언트 사이드 의료
볼륨 렌더러**로 심화. 차별화 명제: "브라우저에서, 서버 GPU 없이, 시네마틱
화질로, 대용량 실데이터를" — 네 가지가 동시에 성립하는 시연이 핵심 가치.

### M1 — 실데이터 기반 (2026-05-29)

- **R16Float 16비트** 강도 저장 (R8 → R16, Hounsfield 보존)
- **Window/level** UBO + 임상 4 프리셋 (bone / lung / soft-tissue / brain)
- **NIfTI 로더** + 물리 spacing 기반 AABB
- **DICOM Explicit VR LE** 로더 + 실 임상 코퍼스 4종 검증 (multi-frame +
  중첩 SQ skip 갭 발견·수정)

### M2 — 시네마틱 품질 (2026-05-29)

- **Gradient 셰이딩** (Lambert + Blinn-Phong)
- **볼류메트릭 소프트 섀도우** (보조 레이 self-shadowing) — WebGL이 어려운 영역

### M3 — 스케일 & 성능

- **M3-1 (2026-05-29)**: 컴퓨트 occupancy 그리드 + empty-space skip (양 백엔드)
- **M3-2 (2026-05-31)**: 측정. sparse 256³ 합성 +3~5% (셰이딩 우세 워크로드).
  per-cell-entry 변형은 GPU warp divergence로 회귀 → 되돌림
- **M3-3 v0 (2026-06-02)**: **Brick atlas + page table** 간접 참조 도입.
  64³ interior + 1-voxel halo = 66³ stored. 빈 brick 제거로 1024³ × 10% 점유
  시 2 GB → ~200 MB
- **M3-3 v1-α streaming (2026-06-04)**: LRU + incremental upload + memory-budget
  auto-size. 1024³ default atlas 280→188 MB(-33%), 2 GB dense 자동 streaming
  (493 MB). 한계: 가시 brick > slots면 시각 hole
- **M3-3 v1-β LOD (2026-06-07)**: mip chain → per-LOD atlas (L0..L3) →
  거리 임계 selection → multi-LOD streaming (no migration + fallback + K=64) →
  셰이더가 `(lod<<30)|slot` 디코드. 1024³ Case C zoom-out missing brick
  2320 → 326 (**-86%**). 알려진 한계: LOD 경계 seam

### M4 — Path-traced 산란

- **v0 (2026-05-31)**: Woodcock 자유경로 + Henyey-Greenstein 위상함수 +
  single-light NEE + inline SPP 평균 (양 백엔드)
- **v1 (2026-06-02)**: Progressive 누적 (RGBA16Float ping-pong, running-mean).
  카메라/파라미터 변경 시 N=0 reset

### DICOM 추가 지원 (2026-06-07)

- **Implicit VR LE** (임상 PACS 기본): file meta(Explicit) ↔ dataset 인코딩 분리,
  tag → VR dictionary. 합성 256×256×128 + 실 RT 파일 dispatch 검증
- **압축 transfer syntax 4 step**:
  - Step 1 — encapsulated PixelData walk + 3종 TS UID 인식
  - Step 2 — RLE Lossless 인트리 PackBits 디코더 (비트 동일성 검증)
  - Step 3 — OpenJPEG vcpkg 의존
  - Step 4 — JPEG 2000 Lossless/Lossy 디코더 (memory stream)

### Disk Paging (Steps 1-3, 2026-06-07)

- **Step 1**: HalfDataView 추상화 + `packBrickToStaging` signature 정리
- **Step 2**: `utils::MmappedFile` 포터블 wrapper (Windows MapViewOfFile +
  POSIX mmap) → NIfTI 본문 OS 페이지 캐시로 위임
- **Step 3**: `m_mipChain` 제거, `packBrickToStaging`이 LOD 박스 필터 즉석
  (mathematically equivalent: box(box(x)) = box(x))
- 1024³ 정착 working set 2.69 → 2.38 GB (-11%). 피크 6.57 GB는 여전히 float
  intermediate 지배 — Step 5+가 진짜 4 GB+ unlock

### 독립 WASM 볼륨 뷰어 (2026-05-29)

- 브라우저에서 볼륨만 풀스크린: `volume_viewer_wasm` 별도 실행 파일
- M1/M2/M3 전부 + HTML 컨트롤 + 랜딩 인덱스 진입

### Stage 7 성과

| 지표 | Before (Stage 6) | After (Stage 7) |
|------|-----------------|----------------|
| 강도 정밀도 | RGBA8 (퍼셉추얼) | R16Float (Hounsfield 단위 보존) |
| 입력 포맷 | OBJ / glTF | NIfTI · DICOM (5 transfer syntax) |
| 셰이딩 | PBR (불투명 표면) | Gradient + 볼류메트릭 섀도우 + Path tracing |
| 빈 공간 처리 | 없음 (모든 voxel 마칭) | 컴퓨트 occupancy + skipping |
| 대용량 데이터 | 단일 3D 텍스처 (~GPU VRAM 한계) | Brick atlas + LRU streaming + 4 LOD |
| 시각 차별화 | PBR 표면 | Cinematic VRT (path-traced 다중 산란) |
| 메모리 효율 | N/A | 1024³ atlas 280→188 MB(-33%), v1-β LOD hole -86% |
| 진단 RAM | N/A | NIfTI mmap + on-the-fly mip → 정착 -11% |

---

## 아키텍처 비교: Before vs After

### 시작점 (vulkan-tutorial.com)

```
main.cpp (467줄) — HelloTriangleApplication
├── createInstance()
├── setupDebugMessenger()
├── createSurface()
├── pickPhysicalDevice()
├── createLogicalDevice()
├── createSwapChain()
├── createGraphicsPipeline()
├── createCommandBuffers()
├── drawFrame()
└── ... (20+ 함수, 30+ 멤버 변수)
```

### 최종 아키텍처

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 1: Application                                         │
│   Application, Camera, Input System                          │
│   ImGuiManager (PBR controls, GPU timing, stress test UI)    │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│ Layer 2: High-Level Subsystems (API-Agnostic)                │
│   Renderer (PBR, Shadow, GPU Culling, Indirect Draw)         │
│   ShadowRenderer, SkyboxRenderer, IBLManager                 │
│   ParticleRenderer, BuildingManager, WorldManager            │
│   ResourceManager, SceneManager                              │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│ Layer 3: RHI (Render Hardware Interface)                      │
│   15개 추상 인터페이스 (pure virtual)                           │
│   RHIDevice, RHIBuffer, RHITexture, RHIPipeline, ...         │
│   RHIFactory (런타임 백엔드 선택)                               │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│ Layer 4: Backend Implementations                             │
│   ┌─────────────────┐  ┌─────────────────┐                  │
│   │  Vulkan Backend  │  │  WebGPU Backend  │                 │
│   │  (Desktop)       │  │  (Web/WASM)      │                 │
│   │  VMA, SPIR-V     │  │  Emscripten      │                 │
│   │  ~8,000 LOC      │  │  WGSL, ~6,500 LOC│                 │
│   └─────────────────┘  └─────────────────┘                  │
└─────────────────────────────────────────────────────────────┘
```

### 수치 변화

| 지표 | 시작점 | 최종 |
|------|--------|------|
| main.cpp | 467줄 | 18줄 |
| 총 코드 | ~467 LOC | ~25,000+ LOC |
| 클래스/모듈 | 0 | 50+ |
| 그래픽스 API | Vulkan only | Vulkan + WebGPU |
| 렌더링 | Blinn-Phong | Cook-Torrance PBR + IBL |
| 오브젝트 처리 | 단일 모델 | 100K+ GPU-Driven |
| Draw Call 방식 | CPU drawIndexed | GPU drawIndexedIndirect |
| Culling | 없음 | GPU Frustum Culling (Compute) |
| Shadow | 없음 | Directional Shadow Mapping + PCF |
| UI | 없음 | ImGui (PBR 파라미터, GPU 타이밍, 스트레스 테스트) |
| 프로파일링 | 없음 | vkCmdWriteTimestamp per-pass GPU timing |
| 메모리 관리 | 수동 vkAllocateMemory | VMA + Memory Aliasing |
| 플랫폼 | 단일 | Linux, macOS, Windows, Web |

---

## 타임라인

```
vulkan-tutorial.com 학습
        │
        ▼
  ┌─ Stage 1: Monolith → Layered ─────────────────────────────┐
  │  Phase 1: Utility Layer 분리                                │
  │  Phase 2: Device Management 캡슐화                          │
  │  Phase 3: Resource Management RAII                          │
  │  Phase 4: Rendering Layer (Sync, Command, Swapchain, Pipeline) │
  │  Phase 5: Scene Layer (Mesh, OBJLoader)                     │
  │  Phase 6: Renderer 통합 (main.cpp 80% 축소)                  │
  │  Phase 7: Application Layer (main.cpp 18줄)                 │
  │  Phase 8: Cross-Platform (Linux/macOS/Windows)              │
  │  Phase 9: 서브시스템 분리 (ResourceManager, SceneManager)      │
  │  Phase 10: FdF 와이어프레임 + Camera                         │
  │  Phase 11: ImGui 디버깅 UI                                  │
  └────────────────────────────────────────────────────────────┘
        │
        ▼  "객체지향 Vulkan 래퍼는 상용 엔진 수준이 아니다"
        │  "멀티 플랫폼 지원이 불가능하다"
        │
  ┌─ Stage 2: Layered → RHI ──────────────────────────────────┐
  │  Phase 1: RHI 인터페이스 설계 (15개 추상화, WebGPU 스타일)     │
  │  Phase 2: Vulkan 백엔드 구현 (12개 클래스, VMA 통합)          │
  │  Phase 3: Factory & Bridge 패턴                             │
  │  Phase 4: Renderer 마이그레이션                               │
  │  Phase 5: Scene/Resource 마이그레이션                         │
  │  Phase 6: ImGui Adapter 통합                                │
  │  Phase 7: CommandManager 삭제, 완전 RHI 전환                  │
  │  Phase 8: 레거시 정리 (~890 LOC 삭제), 모듈 디렉토리 구조       │
  └────────────────────────────────────────────────────────────┘
        │
        ▼  "RHI 아키텍처를 실제로 증명하자"
        │
  ┌─ Stage 3: WebGPU 백엔드 ──────────────────────────────────┐
  │  15개 WebGPU RHI 클래스 구현                                 │
  │  SPIR-V → WGSL 셰이더 변환                                  │
  │  Emscripten WASM 빌드 시스템                                 │
  │  웹 배포 달성 (Chrome/Edge WebGPU)                           │
  └────────────────────────────────────────────────────────────┘
        │
        ▼  "이제 렌더링 품질과 성능을 끌어올리자"
        │
  ┌─ Stage 4: PBR & GPU-Driven ───────────────────────────────┐
  │  Week 1: Cook-Torrance PBR + IBL (환경맵 기반 라이팅)        │
  │  Week 2: SSBO + Compute Frustum Culling + Indirect Draw    │
  │  Week 3: Memory Aliasing + Async Compute (Timeline Semaphore) │
  │  Week 4: GPU Profiling + 100K Stress Test + 문서화           │
  └────────────────────────────────────────────────────────────┘
        │
        ▼  "WASM 렌더링 품질을 네이티브 수준으로 끌어올리자"
        │
  ┌─ Stage 5: WASM 포스트프로세스 파이프라인 ─────────────────────┐
  │  HDR Render Target (RGBA16Float) 도입                       │
  │  ACES Filmic Tonemap Pass (HDR → LDR RGBA8Unorm)           │
  │  FXAA 3.11 Anti-Aliasing Pass (LDR → Swapchain)            │
  │  Deferred Resize (Asyncify-safe 브라우저 뷰포트 변경 처리)     │
  └────────────────────────────────────────────────────────────┘
        │
        ▼
   현재: PBR & GPU-Driven Rendering Engine
         Vulkan 1.3 + WebGPU (HDR+Tonemap+FXAA) + Modern C++20
```

---

## 핵심 교훈

### 아키텍처

1. **단계적 리팩터링**이 전면 재작성보다 안전하다. 매 Phase마다 빌드/실행 검증을 거쳤다.
2. **인터페이스 설계가 구현보다 중요하다.** RHI 인터페이스를 WebGPU 스타일로 설계한 덕분에 WebGPU 백엔드 추가가 자연스러웠다.
3. **추상화는 공짜가 아니다.** 하지만 vtable 오버헤드 < 2%는 허용 가능한 수준이었다.

### 렌더링

4. **GPU-Driven은 CPU 병목을 드러낸다.** GPU culling은 O(1)이지만, CPU의 ObjectData 생성이 100K에서 진짜 병목이다.
5. **Shadow Pass가 가장 큰 비용이다.** 라이트 시점에서는 frustum culling이 적용되지 않아 모든 오브젝트를 그린다.
6. **Compute Shader의 병렬성은 강력하다.** 1K → 100K에서도 frustum culling 비용은 1-2ms로 일정하다.

### 엔지니어링

7. **RAII는 타협하지 않는다.** 11개 Phase, 4개 Stage를 거치며 단 한 번도 메모리 누수가 없었다.
8. **플랫폼 분기는 최소화한다.** `#ifdef` 가드는 백엔드 내부에만 두고, 상위 레이어는 순수 RHI 인터페이스만 사용한다.
9. **문서화는 미래의 나를 위한 투자다.** 매 Phase의 문서가 이후 리팩터링에서 의사결정의 근거가 되었다.

---

*문서 최종 업데이트: 2026-02-08*
*관련 문서: [Monolith → Layered](refactoring/monolith-to-layered/REFACTORING_OVERVIEW.md) | [Layered → RHI](refactoring/layered-to-rhi/RHI_MIGRATION_PRD.md) | [WebGPU Backend](refactoring/webgpu-backend/) | [README](../README.md)*
