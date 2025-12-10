# ImGui Integration Guide

## 개요 (Overview)

이 문서는 Vulkan FdF 프로젝트에 Dear ImGui를 통합하는 과정에서 발생한 문제들과 해결 방법을 기록합니다.

### 목표 (Goals)
- FdF 기능들을 UI를 통해 표시
- 기존 4-layer RAII 아키텍처 유지
- vcpkg를 통한 의존성 관리
- 크로스 플랫폼 지원 (macOS, Linux)

### 결과 (Results)
- ImGui 성공적으로 통합
- 4-layer 아키텍처 유지
- Pure RAII 리소스 관리
- 플랫폼별 최적화 (Dynamic Rendering on macOS, RenderPass on Linux)

---

## 통합 과정 (Integration Process)

### 1. 의존성 추가 (Adding Dependencies)

#### vcpkg.json
```json
{
  "name": "my-vulkan-project",
  "version-string": "0.1.0",
  "dependencies": [
    "glfw3",
    "glm",
    "stb",
    "tinyobjloader",
    "volk",  // Critical: Resolves Vulkan header version mismatch
    {
      "name": "imgui",
      "features": ["glfw-binding", "vulkan-binding"]
    }
  ]
}
```

**주요 포인트:**
- `volk` 의존성은 Vulkan 헤더 버전 불일치 문제 해결에 필수
- ImGui는 `glfw-binding`과 `vulkan-binding` feature 필요

#### CMakeLists.txt 수정
```cmake
# Vulkan SDK 헤더가 최우선 순위를 갖도록 설정
target_include_directories(vulkanGLFW BEFORE PRIVATE
    ${Vulkan_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ImGui 링크
target_link_libraries(vulkanGLFW PRIVATE
    # ... other libraries ...
    imgui::imgui
)

# Vulkan C++ 모듈 경고 억제
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(VulkanCppModule PRIVATE -Wno-missing-declarations)
endif()
```

### 2. ImGuiManager 클래스 생성

#### 파일 구조
- [src/ui/ImGuiManager.hpp](../src/ui/ImGuiManager.hpp)
- [src/ui/ImGuiManager.cpp](../src/ui/ImGuiManager.cpp)

#### 주요 기능
```cpp
class ImGuiManager {
public:
    ImGuiManager(GLFWwindow* window,
                 VulkanDevice& device,
                 VulkanSwapchain& swapchain,
                 CommandManager& commandManager);
    ~ImGuiManager();

    void newFrame();
    void renderUI(Camera& camera, bool isFdfMode,
                  std::function<void()> onModeToggle,
                  std::function<void(const std::string&)> onFileLoad);
    void render(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);
    void handleResize();

private:
    VulkanDevice& device;
    VulkanSwapchain& swapchain;
    CommandManager& commandManager;
    vk::raii::DescriptorPool imguiPool = nullptr;
};
```

### 3. Application Layer 통합

#### Application.hpp 수정
```cpp
class Application {
private:
    // IMPORTANT: 멤버 변수 선언 순서가 destruction 순서를 결정
    GLFWwindow* window = nullptr;
    std::unique_ptr<Camera> camera;              // 1st destroyed
    std::unique_ptr<Renderer> renderer;          // 2nd destroyed (calls waitIdle)
    std::unique_ptr<ImGuiManager> imguiManager;  // 3rd destroyed (depends on Renderer)
};
```

**주요 변경사항:**
- `cleanup()` 함수 제거 (Pure RAII)
- 멤버 변수 선언 순서 조정
- ImGuiManager를 Application layer에 배치

---

## 발생한 문제와 해결 방법 (Problems & Solutions)

### 문제 1: Vulkan RAII Dispatcher 버전 불일치

#### 에러 메시지
```
Assertion failed: (m_dispatcher->getVkHeaderVersion() == VK_HEADER_VERSION)
```

#### 원인
vcpkg의 prebuilt ImGui가 프로젝트의 Vulkan 헤더와 다른 버전으로 컴파일됨

#### 해결 방법
`volk` 의존성을 vcpkg.json에 추가:

```json
"dependencies": [
    "volk",  // Meta-loader for Vulkan
    { "name": "imgui", "features": ["glfw-binding", "vulkan-binding"] }
]
```

**왜 이게 작동하는가?**
- volk는 Vulkan의 meta-loader로, 런타임에 Vulkan 함수를 동적으로 로드
- ImGui의 vcpkg 빌드가 volk를 사용하도록 구성되어 있음
- volk를 통해 헤더 버전 차이를 우회

#### 관련 파일
- [vcpkg.json:14](../vcpkg.json#L14)

---

### 문제 2: Command Buffer 상태 에러

#### 에러 메시지
```
vkCmdBindPipeline(): was called before vkBeginCommandBuffer()
vkEndCommandBuffer(): Cannot call End on VkCommandBuffer when not in the RECORDING state
```

#### 원인
Command buffer 생명주기 관리 문제:
1. `recordCommandBuffer()`가 `end()` 호출
2. ImGui 렌더링이 같은 command buffer 사용 시도
3. `drawFrame()`이 다시 `end()` 호출

#### 해결 방법
Command buffer를 한 번만 종료하도록 수정:

```cpp
// Renderer.cpp - recordCommandBuffer()
void Renderer::recordCommandBuffer(uint32_t imageIndex) {
    // ... 메인 렌더링 ...

    // commandManager->getCommandBuffer(currentFrame).end();  // REMOVED
    // Note: end() is called in drawFrame after ImGui rendering
}

// Renderer.cpp - drawFrame()
void Renderer::drawFrame(std::function<void(const vk::raii::CommandBuffer&, uint32_t)> imguiRenderCallback) {
    // ... acquire image ...
    recordCommandBuffer(imageIndex);

    // Render ImGui if callback is provided
    if (imguiRenderCallback) {
        imguiRenderCallback(commandManager->getCommandBuffer(currentFrame), imageIndex);
    }

    // End command buffer recording (ONCE)
    commandManager->getCommandBuffer(currentFrame).end();
    // ... submit and present ...
}
```

#### 관련 파일
- [src/rendering/Renderer.cpp:416](../src/rendering/Renderer.cpp#L416)
- [src/rendering/Renderer.cpp:74-105](../src/rendering/Renderer.cpp#L74-L105)

---

### 문제 3: ImGui Render Pass 검증 에러 (macOS)

#### 에러 메시지
```
vkCmdDrawIndexed(): This call must be issued inside an active render pass
```

#### 원인
macOS에서 dynamic rendering 사용 시, ImGui가 자체 render pass 없이 렌더링 시도

#### 해결 방법
플랫폼별 렌더링 구현:

```cpp
void ImGuiManager::render(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) {
    ImGui::Render();

#ifdef __linux__
    // Linux: ImGui renders within the main render pass
    VkCommandBuffer vkCmdBuffer = static_cast<VkCommandBuffer>(*commandBuffer);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);
#else
    // macOS/Windows: Use dynamic rendering for ImGui
    vk::RenderingAttachmentInfo colorAttachment{
        .imageView = swapchain.getImageViews()[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,  // Load existing content
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f)
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {0, 0},
            .extent = swapchain.getExtent()
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment
    };

    commandBuffer.beginRendering(renderingInfo);
    VkCommandBuffer vkCmdBuffer = static_cast<VkCommandBuffer>(*commandBuffer);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);
    commandBuffer.endRendering();
#endif
}
```

**핵심 포인트:**
- Linux: 기존 render pass 내에서 렌더링
- macOS: 별도의 dynamic rendering pass 생성
- `loadOp = eLoad`: 기존 렌더링 결과 위에 ImGui 오버레이

#### 관련 파일
- [src/ui/ImGuiManager.cpp:210-241](../src/ui/ImGuiManager.cpp#L210-L241)
- [src/ui/ImGuiManager.cpp:71-87](../src/ui/ImGuiManager.cpp#L71-L87)

---

### 문제 4: Segmentation Fault on Exit

#### 에러
프로그램 종료 시 segmentation fault 발생

#### 원인
리소스 정리 순서 문제:
1. Renderer가 ImGuiManager보다 먼저 파괴됨
2. ImGuiManager 파괴 시 Renderer의 Vulkan 리소스 접근 시도
3. 이미 파괴된 리소스에 접근하여 crash

#### 해결 방법

**Step 1: 멤버 변수 선언 순서 조정**

C++에서 멤버 변수는 **선언의 역순**으로 파괴됩니다:

```cpp
// Application.hpp
class Application {
private:
    // Destruction order: 4 → 3 → 2 → 1
    GLFWwindow* window = nullptr;                // 1
    std::unique_ptr<Camera> camera;              // 2 (no Vulkan dependencies)
    std::unique_ptr<Renderer> renderer;          // 3 (calls waitIdle)
    std::unique_ptr<ImGuiManager> imguiManager;  // 4 (destroyed FIRST, depends on Renderer)
};
```

**Step 2: Renderer에 waitIdle() 추가**

```cpp
// Renderer.hpp
class Renderer {
public:
    ~Renderer();  // Non-default destructor
};

// Renderer.cpp
Renderer::~Renderer() {
    // RAII: Wait for device idle before destroying Vulkan resources
    if (device) {
        device->getDevice().waitIdle();
    }
    // All other resources cleaned up by RAII in reverse declaration order
}
```

**Step 3: cleanup() 함수 제거**

Pure RAII 원칙에 따라 명시적 cleanup() 제거:

```cpp
// Application.cpp
Application::~Application() {
    // RAII cleanup: Members destroyed in reverse declaration order
    // 1. ~ImGuiManager() - cleans up ImGui resources
    // 2. ~Renderer() - calls waitIdle() and cleans up Vulkan resources
    // 3. ~Camera() - no special cleanup needed

    // Manual cleanup for raw pointers only
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}
```

#### 관련 파일
- [src/Application.hpp:70-74](../src/Application.hpp#L70-L74)
- [src/rendering/Renderer.cpp:65-71](../src/rendering/Renderer.cpp#L65-L71)

---

### 문제 5: Build Warnings

#### 경고 메시지
```
warning: declaration does not declare anything [-Wmissing-declarations]
```

#### 원인
Vulkan C++ module에서 발생하는 컴파일러 경고

#### 해결 방법
CMakeLists.txt에서 경고 억제:

```cmake
# Suppress Vulkan C++ module warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(VulkanCppModule PRIVATE -Wno-missing-declarations)
endif()
```

#### 관련 파일
- [CMakeLists.txt:106-108](../CMakeLists.txt#L106-L108)

---

### 문제 6: 아키텍처 구조 위반

#### 문제
초기 구현에서 Renderer가 ImGuiManager를 소유하려 했으나, 이는 layer 경계를 위반

#### 해결 방법
ImGuiManager를 Application layer에 배치:

```
Application Layer (최상위)
├── Application (window, main loop)
└── ImGuiManager (UI overlay) ← 여기에 배치

Rendering Layer
├── Renderer (orchestration)
├── VulkanSwapchain
├── VulkanPipeline
└── SyncManager

Resource Layer
├── ResourceManager
├── VulkanImage
└── VulkanBuffer

Core Layer (최하위)
├── VulkanDevice
└── CommandManager
```

**왜 Application Layer인가?**
- ImGui는 UI 오버레이로, 렌더링 파이프라인의 일부가 아님
- Application이 Renderer와 ImGuiManager를 조율
- Renderer는 ImGui를 모르며, 단지 accessor 제공

**Renderer의 역할:**
```cpp
// Renderer는 ImGui를 모름 - 단지 public accessor 제공
class Renderer {
public:
    VulkanDevice& getDevice() { return *device; }
    VulkanSwapchain& getSwapchain() { return *swapchain; }
    CommandManager& getCommandManager() { return *commandManager; }

    void drawFrame(std::function<void(const vk::raii::CommandBuffer&, uint32_t)> imguiRenderCallback = nullptr);
};
```

#### 관련 파일
- [src/Application.hpp:70-74](../src/Application.hpp#L70-L74)
- [src/rendering/Renderer.hpp:99-111](../src/rendering/Renderer.hpp#L99-L111)

---

## 최종 아키텍처 (Final Architecture)

### Layer 구조

```
┌─────────────────────────────────────────────────────────────┐
│ Application Layer                                           │
│ ┌─────────────────┐         ┌──────────────────┐            │
│ │   Application   │────────▶│  ImGuiManager    │            │
│ │  - Window mgmt  │         │  - UI overlay    │            │
│ │  - Main loop    │         │  - Platform code │            │
│ └────────┬────────┘         └────────┬─────────┘            │
│          │                           │                      │
└──────────┼───────────────────────────┼──────────────────────┘
           │                           │
           │ owns                      │ uses (via getters)
           ▼                           ▼
┌─────────────────────────────────────────────────────────────┐
│ Rendering Layer                                             │
│ ┌──────────────────────────────────────────────────────┐    │
│ │                     Renderer                         │    │
│ │  - Orchestration                                     │    │
│ │  - Descriptor management                             │    │
│ │  - Uniform buffers                                   │    │
│ │  - Public getters (getDevice, getSwapchain, etc.)    │    │
│ └──────────────────────────────────────────────────────┘    │
│          │           │            │            │            │
│          │           │            │            │            │
│    ┌─────▼────┐ ┌───▼──────┐ ┌──▼────────┐ ┌─▼──────────┐   │
│    │Swapchain │ │ Pipeline │ │   Sync    │ │ Command    │   │
│    │          │ │          │ │  Manager  │ │  (moved)   │   │
│    └──────────┘ └──────────┘ └───────────┘ └────────────┘   │
└─────────────────────────────────────────────────────────────┘
           │                                │
           │ uses                           │ uses
           ▼                                ▼
┌─────────────────────────────────────────────────────────────┐
│ Resource Layer                                              │
│ ┌────────────────┐  ┌──────────────┐  ┌────────────────┐    │
│ │ ResourceManager│  │ VulkanImage  │  │ VulkanBuffer   │    │
│ │ - Staging      │  │ - Texture    │  │ - Vertex/Index │    │
│ │ - Uploads      │  │ - Depth      │  │ - Uniform      │    │
│ └────────────────┘  └──────────────┘  └────────────────┘    │
└─────────────────────────────────────────────────────────────┘
           │                                │
           │ uses                           │ uses
           ▼                                ▼
┌─────────────────────────────────────────────────────────────┐
│ Core Layer                                                  │
│ ┌──────────────────┐              ┌────────────────────┐    │
│ │  VulkanDevice    │              │ CommandManager     │    │
│ │  - Instance      │              │ - Command pools    │    │
│ │  - Physical dev  │              │ - Command buffers  │    │
│ │  - Logical dev   │              │ - Single-time cmds │    │
│ │  - Queues        │              │                    │    │
│ └──────────────────┘              └────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### RAII 리소스 정리 순서

```
Application 종료 시:

1. ~ImGuiManager()
   - ImGui_ImplVulkan_Shutdown()
   - ImGui_ImplGlfw_Shutdown()
   - ImGui::DestroyContext()
   - ~imguiPool

2. ~Renderer()
   - device->getDevice().waitIdle()  ← 명시적
   - ~sceneManager
   - ~resourceManager
   - ~syncManager
   - ~commandManager
   - ~pipeline
   - ~swapchain
   - ~depthImage
   - ~uniformBuffers
   - ~descriptorSets
   - ~descriptorPool
   - ~device

3. ~Camera()
   - (no special cleanup)

4. Application::~Application()
   - glfwDestroyWindow(window)
   - glfwTerminate()
```

---

## 플랫폼별 고려사항 (Platform Considerations)

### macOS / Windows
- **Dynamic Rendering 사용** (Vulkan 1.3)
- ImGui가 별도의 dynamic rendering pass에서 렌더링
- `UseDynamicRendering = true`
- RenderPass 불필요

### Linux
- **Traditional RenderPass 사용**
- ImGui가 메인 render pass 내에서 렌더링
- `UseDynamicRendering = false`
- RenderPass 필요

### 플랫폼 감지 코드
```cpp
#ifdef __linux__
    // Linux path
#else
    // macOS/Windows path
#endif
```

---

## 체크리스트 (Checklist)

ImGui를 새 Vulkan 프로젝트에 통합할 때:

- [ ] vcpkg.json에 `volk` 의존성 추가
- [ ] vcpkg.json에 `imgui` 의존성 추가 (glfw-binding, vulkan-binding)
- [ ] CMakeLists.txt에서 Vulkan 헤더가 먼저 include되도록 설정
- [ ] ImGuiManager를 Application layer에 배치
- [ ] Application.hpp에서 멤버 변수 선언 순서 확인 (destruction 순서)
- [ ] Renderer에 ~Renderer() 추가하고 waitIdle() 호출
- [ ] Command buffer를 한 번만 end() 호출하도록 수정
- [ ] 플랫폼별 렌더링 경로 구현 (dynamic rendering vs render pass)
- [ ] cleanup() 함수 제거 (Pure RAII)
- [ ] Vulkan C++ module 경고 억제

---

## 참고 자료 (References)

### 관련 파일
- [vcpkg.json](../vcpkg.json) - 의존성 관리
- [CMakeLists.txt](../CMakeLists.txt) - 빌드 설정
- [src/ui/ImGuiManager.hpp](../src/ui/ImGuiManager.hpp) - ImGui 관리자 인터페이스
- [src/ui/ImGuiManager.cpp](../src/ui/ImGuiManager.cpp) - ImGui 관리자 구현
- [src/Application.hpp](../src/Application.hpp) - Application layer
- [src/Application.cpp](../src/Application.cpp) - Application 구현
- [src/rendering/Renderer.hpp](../src/rendering/Renderer.hpp) - Renderer 인터페이스
- [src/rendering/Renderer.cpp](../src/rendering/Renderer.cpp) - Renderer 구현

### 외부 문서
- [Dear ImGui GitHub](https://github.com/ocornut/imgui)
- [ImGui Vulkan Example](https://github.com/ocornut/imgui/blob/master/examples/example_glfw_vulkan/main.cpp)
- [Vulkan Dynamic Rendering](https://www.khronos.org/blog/streamlining-render-passes)
- [volk GitHub](https://github.com/zeux/volk)

---

## 결론 (Conclusion)

ImGui를 Vulkan RAII 프로젝트에 통합하는 것은 여러 도전 과제가 있었지만, 다음 원칙들을 지키면서 성공적으로 완료했습니다:

1. **Pure RAII**: 명시적 cleanup() 없이 자동 리소스 정리
2. **아키텍처 무결성**: 4-layer 구조 유지
3. **플랫폼 독립성**: 플랫폼별 최적화 구현
4. **의존성 관리**: vcpkg를 통한 깔끔한 관리

가장 중요한 교훈은 **멤버 변수 선언 순서가 RAII에서 매우 중요하다**는 점입니다. C++의 파괴 순서(선언의 역순)를 이해하고 활용하면 명시적 cleanup 코드 없이도 안전한 리소스 정리가 가능합니다.
