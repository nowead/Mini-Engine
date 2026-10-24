# 코드 지도

폴더마다 무엇이 들어 있는지, 새 코드를 어디에 두는지 정리합니다. 설계 배경은 [아키텍처](../ARCHITECTURE.md)를 봅니다.

## 폴더

| 폴더 | 내용 | 새로 추가하는 것 |
| --- | --- | --- |
| [src/rhi/include/rhi/](../../src/rhi/include/rhi/) | 백엔드 공통 인터페이스 (`RHIDevice`, `RHIBuffer`, `RHITypes` 등) | API에 독립적인 자원·명령 개념 |
| [src/rhi/backends/vulkan/](../../src/rhi/backends/vulkan/), [webgpu/](../../src/rhi/backends/webgpu/) | 인터페이스의 백엔드별 구현 | 인터페이스를 바꿨다면 두 백엔드 모두 |
| [src/rendering/](../../src/rendering/) | `Renderer`, 패스(G-buffer, deferred, shadow, skybox, IBL), `VolumeRenderer`, `BrickedVolume` | 렌더 패스, GPU 자원 관리 |
| [src/rendering/graph/](../../src/rendering/graph/) | 렌더 그래프: 자원 사용 선언 → 배리어 → 실행 (Vulkan 전용) | 그래프에 올리는 패스의 의존성 |
| [src/scene/](../../src/scene/) | 카메라, AABB/frustum, scene graph, quadtree, mesh, material | 씬 자료구조, 컬링 |
| [src/assets/](../../src/assets/) | DICOM, NIfTI, raw 볼륨 로더, glTF importer | 새 파일 형식 로더 |
| [src/loaders/](../../src/loaders/) | OBJ 로더 (예전 경로) | 새 로더는 `src/assets/`에 |
| [src/resources/](../../src/resources/) | 텍스처 로딩·캐시 (`ResourceManager`) | |
| [src/utils/](../../src/utils/) | 로거, 스레드 풀, mmap, GPU 타이머 | 렌더링과 무관한 공용 유틸 |
| [src/ui/](../../src/ui/) | Dear ImGui (native) | native 디버그 UI |
| [src/wasm/](../../src/wasm/) | 브라우저 UI와 연결하는 WASM 바인딩 | JS에서 부르는 설정 함수 |
| [src/effects/](../../src/effects/) | 파티클 | |
| [src/game/](../../src/game/), [src/examples/](../../src/examples/) | 초기 도시 빌딩 데모, 인스턴싱 예제 (`MiniEngine`·`instancing_test`에 컴파일됨) | 새 기능은 넣지 않음 |
| [shaders/](../../shaders/) | GLSL(Vulkan)과 WGSL(WebGPU) 소스. `.spv`는 빌드 산출물 | 셰이더 쌍 |
| [tests/](../../tests/) | CTest 회귀 테스트와 창을 여는 데모·뷰어 | [테스트 가이드](../../tests/README.md) 참고 |
| [scripts/](../../scripts/) | 샘플 데이터 다운로드·생성, 문서·셰이더 검사, WASM 래퍼 | |

## 새 코드 추가 체크리스트

- **소스 파일**: [CMakeLists.txt](../../CMakeLists.txt)에서 그 코드를 쓰는 모든 타깃의 소스 목록에 추가합니다. native `MiniEngine`, WASM `MiniEngine`, `showcase_demo`는 각각 `Renderer.cpp`를 직접 나열합니다.
- **셰이더**: Vulkan용 GLSL과 WebGPU용 WGSL을 함께 작성하고 C++ 쪽 레이아웃과 맞춥니다. 새 WGSL은 CMake의 복사 목록에 추가합니다.
- **테스트**: GPU 없이 종료 코드로 판정할 수 있는 로직이면 `tests/`에 `*_test.cpp`를 추가하고 [tests/CMakeLists.txt](../../tests/CMakeLists.txt)에 등록합니다.
