# 아키텍처

Mini-Engine은 그래픽스 API를 감싸는 RHI, 이를 사용하는 렌더링 기능, 데이터·입력·UI를 연결하는 실행 프로그램으로 구성됩니다. 현재는 실행 타깃이 필요한 `.cpp`를 선택해 링크하는 구조이며, 별도로 배포되는 엔진 SDK는 아닙니다.

```text
Application / MiniEngine          volume_viewer / volume_viewer_wasm
glTF 장면 · 입력 · UI              NIfTI/DICOM 로더 · 카메라 · UI
             |                                  |
Renderer · Render Graph                VolumeRenderer · BrickedVolume
             +----------------+-----------------+
                         RendererBridge
                              |
                 RHI: Device / Buffer / Texture /
                 Pipeline / CommandEncoder / Queue
                       /                  \
              Vulkan + VMA          WebGPU + Emscripten
              GLSL → SPIR-V          WGSL
```

## 책임과 경계

| 영역 | 책임 | 코드 |
| --- | --- | --- |
| 실행 프로그램 | 윈도우·입력·UI, 시나리오 선택, 데이터 로딩 연결 | [Application](../src/Application.cpp), [뷰어](../tests/volume_viewer.cpp) |
| 로더 | 파일 형식 해석, 강도·spacing·머티리얼 데이터 구성 | [assets](../src/assets/) |
| 렌더러 | 패스·셰이더·GPU 자원 관리, draw/dispatch 기록 | [rendering](../src/rendering/) |
| RHI | 공통 자원·명령 인터페이스와 백엔드 팩토리 | [인터페이스](../src/rhi/include/rhi/), [구현](../src/rhi/backends/) |

의료 파일 형식은 `VolumeRenderer` 내부에 넣지 않습니다. 로더가 `Volume3D`, mmap 소스 또는 슬라이스 callback으로 데이터를 넘기고 렌더러는 강도 데이터와 GPU 저장 방식을 다룹니다. 공유 코드에도 capability 확인과 백엔드별 분기가 남아 있습니다.

## 장면 렌더링

`Renderer`는 G-buffer, deferred lighting, 그림자, SSAO, Bloom, tone mapping과 anti-aliasing을 조합합니다. GPU frustum culling은 오브젝트 버퍼를 읽고 visible index와 indirect draw 데이터를 구성합니다. glTF 파싱은 cgltf가 담당하고, 엔진의 `AssetImporter`가 메시·머티리얼·텍스처 의미를 해석합니다.

`rendering/graph`는 패스의 자원 사용을 선언하고 배리어와 실행을 관리합니다. Vulkan sync2 경로와 WebGPU의 동기화 모델은 같지 않으므로 API 간 동일한 저수준 동작을 가정하지 않습니다.

## 볼륨 데이터 흐름

1. **입력**: NIfTI/raw 또는 단일 DICOM 시리즈에서 차원, spacing, 스칼라 강도를 읽습니다. DICOM은 rescale과 슬라이스 정렬을 적용합니다.
2. **CPU 저장과 변환**: 일반 float/half 경로, native int16/uint16 NIfTI mmap 경로, DICOM progressive callback 경로를 선택합니다.
3. **GPU 저장**: `BrickedVolume`이 halo를 포함한 brick atlas와 page table을 만듭니다. atlas 예산에 따라 Static 또는 Streaming/LOD 경로를 사용합니다.
4. **샘플링과 렌더링**: GLSL/WGSL의 `sampleVolume`이 page table을 경유합니다. Ray march는 transfer function과 occupancy를 사용하고, path trace는 산란을 샘플링한 뒤 누적·denoise·display를 거칩니다.

Progressive DICOM은 슬라이스를 ring buffer에 넣고 완성된 브릭 행을 staging slab으로 업로드하여 전체 float/half 중간 버퍼를 피합니다. 이 경로는 현재 Static atlas 전용입니다. 브라우저에서는 소비한 가상 파일을 해제하며, native 원본 파일은 유지합니다.

Native NIfTI mmap은 CPU의 전체 데이터 복제를 줄입니다. GPU atlas 크기, 실제 resident working set, 브라우저 WASM heap은 서로 다른 비용이므로 하나의 메모리 지표로 합쳐 해석하지 않습니다.

## 백엔드 차이

| 항목 | Vulkan native | WebGPU browser |
| --- | --- | --- |
| 셰이더 | GLSL을 `glslc`로 SPIR-V 컴파일 | 대응하는 WGSL 소스 로드 |
| UI | Dear ImGui | HTML/JavaScript + WASM 바인딩 |
| 그림자 | Cascaded shadow maps | 단일 shadow map 경로 |
| Bindless 텍스처 | 지원 경로 있음 | 명시적 material bind group |
| 병렬 커맨드 기록 | ThreadPool 기반 셰도우 기록 | 해당 경로 없음 |
| 파일 접근 | 로컬 파일·OS mmap | preload·업로드·MEMFS |
| 프레임 수명 | 명시적 acquire/submit/present | 브라우저 프레임·ASYNCIFY 제약 |

GLSL/WGSL은 직접 유지하는 소스 쌍입니다. 런타임 SPIR-V→WGSL 변환을 전제로 하지 않습니다. 데이터 구조를 바꿀 때는 C++와 대응하는 모든 셰이더 레이아웃을 함께 확인해야 합니다.

## 설계의 검증 범위

자동 테스트는 스레드 풀, CPU 데이터 로딩, AABB/frustum 컬링을 다루고, smoke run(`make smoke`)이 Vulkan validation 오류를 검사합니다. GPU 이미지 정확성, 동기화, 모바일 수명·메모리 한계는 별도 수동 검증과 측정 기록이 필요합니다. Linux/macOS용 구성은 저장소에 있지만, 이번 정리에서 모든 플랫폼을 다시 검증한 것은 아닙니다.

- [테스트와 수동 검증](../tests/README.md)
- [볼륨 렌더링 개념과 코드 설명](current/medical-volume/learning/VOLUME_RENDERING_PRIMER.md)
- [엔진 로드맵: 병렬 기록 구현과 측정](current/engine-roadmap/ENGINE_ROADMAP.md)
- [볼륨 설계·측정 인덱스](current/medical-volume/README.md)
