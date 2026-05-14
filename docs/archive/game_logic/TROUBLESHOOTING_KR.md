# 트러블슈팅 가이드

Mini-Engine 빌드 및 실행 시 발생하는 일반적인 문제와 해결 방법입니다.

---

## 목차

- [빌드 문제](#빌드-문제)
- [런타임 문제](#런타임-문제)
- [셰이더 컴파일](#셰이더-컴파일)
- [플랫폼별 문제](#플랫폼별-문제)
- [성능 문제](#성능-문제)
- [디버깅 도구](#디버깅-도구)

---

## 빌드 문제

### CMake 설정 실패

**오류:**
```
CMake Error: Could not find VCPKG_ROOT
```

**해결 방법:**
```bash
# VCPKG_ROOT 환경변수 설정
export VCPKG_ROOT=/path/to/vcpkg  # Linux/macOS
# 또는
set VCPKG_ROOT=C:\vcpkg  # Windows

# 설정 확인
echo $VCPKG_ROOT  # Linux/macOS
echo %VCPKG_ROOT%  # Windows
```

---

### 라이브러리를 찾을 수 없음

**오류:**
```
CMake Error: Could not find glfw3, glm, stb, or tinyobjloader
```

**해결 방법:**

1. **vcpkg를 통해 의존성 설치:**
   ```bash
   $VCPKG_ROOT/vcpkg install glfw3 glm stb tinyobjloader
   ```

2. **설치 확인:**
   ```bash
   $VCPKG_ROOT/vcpkg list
   # 다음이 표시되어야 함: glfw3, glm, stb, tinyobjloader
   ```

3. **CMake 재설정:**
   ```bash
   rm -rf build  # 이전 빌드 정리
   cmake --preset=default
   ```

---

### C++20 컴파일러를 찾을 수 없음

**오류:**
```
CMake Error: C++ compiler does not support C++20
```

**해결 방법:**

**Linux:**
```bash
# 최신 GCC 또는 Clang 설치
sudo apt install g++-11  # Ubuntu/Debian
# 또는
sudo dnf install gcc-c++  # Fedora/RHEL

# 기본 컴파일러로 설정
export CXX=g++-11
```

**macOS:**
```bash
# Xcode Command Line Tools 업데이트
xcode-select --install

# Clang 버전 확인 (14 이상이어야 함)
clang++ --version
```

**Windows:**
- Visual Studio 2022 설치 (C++20 지원 포함)
- 또는 Visual Studio Installer를 통해 기존 설치 업데이트

---

### 디버그 콜백 타입 불일치 (크로스 플랫폼)

**오류:**
```
error: cannot initialize a member subobject of type 'vk::PFN_DebugUtilsMessengerCallbackEXT'
with an rvalue of type 'VkBool32 (*)(VkDebugUtilsMessageSeverityFlagBitsEXT, ...)'
type mismatch at 1st parameter ('vk::DebugUtilsMessageSeverityFlagBitsEXT' vs 'VkDebugUtilsMessageSeverityFlagBitsEXT')
```

**원인:**
- Vulkan-Hpp (C++ 래퍼)는 모든 플랫폼에서 C++ 래퍼 타입(`vk::`)을 요구합니다

**해결 방법:**

모든 플랫폼에서 C++ Vulkan-Hpp 타입 사용:

**VulkanDevice.hpp:**
```cpp
// Use C++ Vulkan-Hpp types for all platforms
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);
```

**VulkanDevice.cpp:**
```cpp
// Use C++ Vulkan-Hpp types for all platforms
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*)
{
    if (static_cast<uint32_t>(severity) & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) {
        std::cerr << "validation layer: type 0x" << std::hex << static_cast<uint32_t>(type)
                  << std::dec << " msg: " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}
```

**참고:** Linux에서 llvmpipe를 사용하는 경우에도 Vulkan-Hpp는 C++ 타입을 요구합니다.

---

### ImGui RenderPass 캐스팅 오류

**오류:**
```
error: cannot cast from type 'struct VkRenderPass_T' to pointer type 'VkRenderPass' (aka 'VkRenderPass_T *')
```

**원인:**
- `swapchain.getRenderPass()`가 이미 `vk::RenderPass` (값 타입)을 반환함
- 역참조(`*`)를 추가로 하면 구조체를 포인터로 캐스팅하려고 시도하여 오류 발생

**해결 방법:**

불필요한 역참조를 제거하고 C 스타일 캐스트 사용:

**잘못된 코드:**
```cpp
// ❌ 잘못됨: getRenderPass()가 이미 값을 반환하는데 역참조를 추가
initInfo.RenderPass = static_cast<VkRenderPass>(*swapchain.getRenderPass());
```

**올바른 코드:**
```cpp
// ✅ 올바름: 역참조 없이 C 스타일 캐스트 사용
initInfo.RenderPass = (VkRenderPass)swapchain.getRenderPass();
```

**참고:** `vk::RenderPass`에서 `VkRenderPass`로 변환 시 C 스타일 캐스트 `(VkRenderPass)`를 사용하는 것이 `static_cast`보다 안전합니다.

---

### Linux에서 ImGui 렌더링 시 Segmentation Fault

**오류:**
```
validation layer: VUID-vkCmdDrawIndexed-renderpass
vkCmdDrawIndexed(): Rendering commands must occur inside a render pass.
Segmentation fault (core dumped)
```

**원인:**
- Linux는 traditional render pass를 사용하는데, `Renderer::recordCommandBuffer`에서 render pass를 시작하고 메인 메시 렌더링 후 **즉시 종료**함
- ImGui 렌더링이 render pass 외부에서 호출되어 validation 오류 및 segfault 발생
- macOS/Windows는 dynamic rendering을 사용하므로 각 렌더링 단계마다 별도의 rendering 세션을 시작/종료할 수 있음

**해결 방법:**

Linux에서는 ImGui도 같은 render pass 내에서 렌더링되어야 합니다:

**1. Renderer.cpp - render pass를 즉시 종료하지 않기:**

**잘못된 코드:**
```cpp
// src/rendering/Renderer.cpp
#ifdef __linux__
    commandManager->getCommandBuffer(currentFrame).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Draw main mesh
    primaryMesh->draw(commandManager->getCommandBuffer(currentFrame));

    // ❌ 문제: 여기서 render pass를 종료하면 ImGui가 render pass 외부에서 렌더링됨
    commandManager->getCommandBuffer(currentFrame).endRenderPass();
#endif
```

**올바른 코드:**
```cpp
// src/rendering/Renderer.cpp
#ifdef __linux__
    commandManager->getCommandBuffer(currentFrame).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Draw main mesh
    primaryMesh->draw(commandManager->getCommandBuffer(currentFrame));

    // ✅ 올바름: render pass를 열어둠 - ImGui가 같은 pass에서 렌더링
    // Note: endRenderPass()는 ImGui 렌더링 후에 호출됨
#endif
```

**2. ImGuiManager.cpp - ImGui 렌더링 후 render pass 종료:**

**잘못된 코드:**
```cpp
// src/ui/ImGuiManager.cpp
#ifdef __linux__
    VkCommandBuffer vkCmdBuffer = static_cast<VkCommandBuffer>(*commandBuffer);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);
    // ❌ 문제: render pass가 종료되지 않음
#endif
```

**올바른 코드:**
```cpp
// src/ui/ImGuiManager.cpp
#ifdef __linux__
    VkCommandBuffer vkCmdBuffer = static_cast<VkCommandBuffer>(*commandBuffer);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);

    // ✅ 올바름: ImGui 렌더링 후 render pass 종료
    commandBuffer.endRenderPass();
#endif
```

**렌더링 흐름 비교:**

**잘못된 흐름 (Segfault 발생):**
```
1. beginRenderPass()
2. Draw main mesh
3. endRenderPass()          ← 너무 빨리 종료
4. ImGui draw commands      ← render pass 외부에서 실행 → CRASH!
5. end command buffer
```

**올바른 흐름:**
```
1. beginRenderPass()
2. Draw main mesh
3. ImGui draw commands      ← 같은 render pass 내부에서 실행
4. endRenderPass()          ← ImGui 후에 종료
5. end command buffer
```

**참고:**
- macOS/Windows는 dynamic rendering을 사용하므로 각 렌더링 단계가 독립적인 `beginRendering()/endRendering()` 쌍을 가질 수 있음
- Linux (Vulkan 1.1)는 traditional render pass를 사용하므로 모든 렌더링이 하나의 render pass 내에서 이루어져야 함

---

## 런타임 문제

### Vulkan 디바이스를 찾을 수 없음

**오류:**
```
Failed to find GPUs with Vulkan support
```

**가능한 원인:**
1. 그래픽 드라이버 업데이트 필요
2. Vulkan 드라이버 미설치
3. GPU 패스스루 없이 가상 머신에서 실행

**해결 방법:**

**1. 그래픽 드라이버 업데이트:**

- **NVIDIA**: [nvidia.com](https://www.nvidia.com/Download/index.aspx)에서 최신 드라이버 다운로드
- **AMD**: [amd.com](https://www.amd.com/en/support)에서 다운로드
- **Intel**: [intel.com](https://www.intel.com/content/www/us/en/download-center/home.html)에서 다운로드

**2. Vulkan 지원 확인:**
```bash
vulkaninfo  # GPU 기능이 표시되어야 함
```

**3. Linux 전용: Vulkan 드라이버 설치**

**Ubuntu/Debian:**
```bash
# Intel/AMD용
sudo apt install mesa-vulkan-drivers

# NVIDIA용 (proprietary)
sudo ubuntu-drivers autoinstall
```

**Arch Linux:**
```bash
# Intel용
sudo pacman -S vulkan-intel

# AMD용
sudo pacman -S vulkan-radeon

# NVIDIA용
sudo pacman -S nvidia vulkan-nvidia
```

**4. 가상 머신 사용자:**
- Vulkan은 GPU 접근 필요
- 가능하면 GPU 패스스루 활성화
- 또는 호스트 시스템에서 실행

---

### 시작 시 애플리케이션 크래시

**오류:**
```
Segmentation fault (core dumped)
```

**해결 방법:**

1. **자세한 오류 메시지를 위해 Vulkan Validation Layer 활성화:**

   `src/Application.cpp` 편집:
   ```cpp
   static constexpr bool ENABLE_VALIDATION = true;
   ```

   재빌드 및 실행:
   ```bash
   make build && make run
   ```

2. **Validation Layer 출력 확인:**
   - 출력에서 `VUID-` 오류 코드 찾기
   - [Vulkan Spec](https://registry.khronos.org/vulkan/)에서 코드 검색

3. **일반적인 원인:**
   - 셰이더 파일 누락 (`shaders/slang.spv` 없음)
   - 모델/텍스처 파일 누락
   - 호환되지 않는 Vulkan 버전

---

### Validation Layer 오류

**오류:**
```
Validation Error: [VUID-xxxxx] ...
```

**해결 방법:**

1. **오류 메시지를 주의 깊게 읽기** - 대부분 정확한 문제를 나타냄

2. **일반적인 Validation 오류:**

   **메모리 배리어 누락:**
   ```
   VUID-vkCmdDraw-None-02859: Image layout mismatch
   ```
   - 해결: 이미지 레이아웃 전환을 위한 적절한 파이프라인 배리어 추가

   **버퍼/이미지 바인딩 안 됨:**
   ```
   VUID-vkCmdDraw-None-02697: Descriptor set not bound
   ```
   - 해결: 드로우 전에 `vkCmdBindDescriptorSets` 호출 확인

   **동기화 오류:**
   ```
   VUID-vkQueueSubmit-pWaitSemaphores-xxxxx
   ```
   - 해결: `SyncManager`에서 세마포어 및 펜스 사용 확인

3. **릴리스 빌드에서 Validation 비활성화:**

   `src/Application.cpp` 편집:
   ```cpp
   static constexpr bool ENABLE_VALIDATION = false;
   ```

---

## 셰이더 컴파일

### 셰이더 컴파일 실패

**오류:**
```
slangc: command not found
```

**해결 방법:**

1. **Vulkan SDK 설치 확인:**
   ```bash
   echo $VULKAN_SDK  # Vulkan SDK 디렉토리를 가리켜야 함
   ```

2. **slangc 존재 확인:**
   ```bash
   ls $VULKAN_SDK/bin/slangc  # 존재해야 함
   ```

3. **PATH에 추가:**

   **Linux/macOS** (`~/.bashrc` 또는 `~/.zshrc`):
   ```bash
   export PATH=$VULKAN_SDK/bin:$PATH
   ```

   **Windows** (관리자 권한 PowerShell):
   ```powershell
   [Environment]::SetEnvironmentVariable(
       "Path",
       "$env:Path;$env:VULKAN_SDK\bin",
       "User"
   )
   ```

4. **slangc 작동 확인:**
   ```bash
   slangc --version
   ```

---

### SPIR-V 컴파일 오류

**오류:**
```
Error compiling shader.slang: unknown type 'XXX'
```

**해결 방법:**

1. **`shaders/shader.slang`에서 Slang 문법 확인**
2. **일반적인 문제:**
   - 잘못된 uniform 버퍼 레이아웃
   - `[[vk::binding(N)]]` 어노테이션 누락
   - 플랫폼에 호환되지 않는 SPIR-V 버전

3. **플랫폼별 SPIR-V 버전:**
   - Linux: SPIR-V 1.3 (Vulkan 1.1 호환성)
   - macOS/Windows: SPIR-V 1.4 (Vulkan 1.3)

---

### Slang 경고: 추가 기능 사용 (warning 41012)

**경고:**
```
warning 41012: entry point uses additional capabilities
that are not part of the specified profile 'spirv_1_3'
```

**원인:**
`Sampler2D` 같은 암시적 리소스 타입 사용 시 Slang이 자동으로 확장 기능(SPV_KHR_non_semantic_info)을 추가함.

**해결:**
CMakeLists.txt에서 경고 무시 플래그 추가:
```cmake
# slangc 컴파일 명령에 -Wno-41012 추가
${SLANGC_EXECUTABLE} ${SHADER_SOURCES} ... -Wno-41012 -o slang.spv
```

**참고:**

- 경고는 정보성이며 기능은 정상 작동함
- 대안: 명시적 바인딩 사용 (`[[vk::binding(1, 0)]] Sampler2D texture`)하면 경고 해결 가능
- 단순한 프로젝트에서는 경고 무시가 더 간단함

---

## 플랫폼별 문제

### Linux: lavapipe 소프트웨어 렌더러 경고

**경고:**
```
WARNING: lavapipe is not a conformant vulkan implementation, testing use only.
```

**원인:**
GPU 드라이버 미설치로 Vulkan이 소프트웨어 렌더러(lavapipe)를 사용함.

**해결:**
GPU별 Vulkan 드라이버 설치:

```bash
# NVIDIA
sudo apt install nvidia-driver-535  # 또는 최신 버전

# AMD/Intel
sudo apt install mesa-vulkan-drivers

# 확인
vulkaninfo --summary  # GPU가 표시되어야 함 (lavapipe 아님)
```

**참고:**

- lavapipe은 매우 느린 테스트용 렌더러
- WSL2는 GPU 패스스루 설정 필요
- 개발/테스트 목적이면 무시 가능

---

### macOS: Validation Layer를 찾을 수 없음

**오류:**
```
Required layer not supported: VK_LAYER_KHRONOS_validation
Context::createInstance: ErrorLayerNotPresent
```

**원인:**
- Homebrew를 통해 설치된 Vulkan SDK는 독립 실행형 SDK와 다른 경로 사용
- macOS SIP (System Integrity Protection)이 `DYLD_LIBRARY_PATH` 차단

**해결 방법:**

1. **Homebrew 경로를 사용하도록 Makefile 환경 설정 업데이트:**
   ```makefile
   HOMEBREW_PREFIX := $(shell brew --prefix)
   VULKAN_LAYER_PATH := $(HOMEBREW_PREFIX)/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d
   ```

2. **`DYLD_LIBRARY_PATH` 대신 `DYLD_FALLBACK_LIBRARY_PATH` 사용** (SIP 허용):
   ```makefile
   export VK_LAYER_PATH="$(VULKAN_LAYER_PATH)"
   export DYLD_FALLBACK_LIBRARY_PATH="$(HOMEBREW_PREFIX)/opt/vulkan-validationlayers/lib:$(HOMEBREW_PREFIX)/lib:/usr/local/lib:/usr/lib"
   ```

3. **레이어 감지 확인:**
   ```bash
   export VK_LAYER_PATH="/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d"
   vulkaninfo --summary | grep -A5 "Instance Layers"
   # 다음이 표시되어야 함: VK_LAYER_KHRONOS_validation
   ```

**핵심 사항:**
- `VK_LAYER_PATH`: `.json` manifest 파일을 가리킴
- `DYLD_FALLBACK_LIBRARY_PATH`: `.dylib` 라이브러리 파일을 가리킴
- macOS에서는 절대 `DYLD_LIBRARY_PATH` 사용 금지 (SIP가 차단함)

---

### macOS: 윈도우 서페이스 생성 실패

**오류:**
```
failed to create window surface!
```

**원인:**
- `VULKAN_SDK` 환경변수를 잘못 설정하면 MoltenVK 간섭 가능
- 잘못된 `DYLD_LIBRARY_PATH` 설정

**해결 방법:**

최소한의 필요한 환경변수만 설정:
```bash
export VK_LAYER_PATH="/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d"
export DYLD_FALLBACK_LIBRARY_PATH="/opt/homebrew/opt/vulkan-validationlayers/lib:/opt/homebrew/lib:/usr/local/lib:/usr/lib"
```

설정하지 말아야 할 것:
- ❌ `VULKAN_SDK` (특별히 필요하지 않는 한)
- ❌ `DYLD_LIBRARY_PATH` (`DYLD_FALLBACK_LIBRARY_PATH` 사용)

---

### macOS: MoltenVK 오류

**오류:**
```
MoltenVK does not support feature: XXX
```

**해결 방법:**

1. **기능 호환성 확인:** [MoltenVK Features](https://github.com/KhronosGroup/MoltenVK#moltenvk-feature-support)

2. **일반적으로 지원되지 않는 기능:**
   - 일부 Vulkan 1.3 기능
   - 특정 descriptor indexing 기능
   - 레이 트레이싱 (Metal에서 지원 안 됨)

3. **해결 방법:**
   - Vulkan 1.1 호환 기능만 사용
   - macOS 전용 코드에 조건부 컴파일 사용

---

### Linux: llvmpipe (소프트웨어 렌더링)

**경고:**
```
WARNING: lavapipe is not a conformant Vulkan implementation
```

**설명:**
- `llvmpipe`는 소프트웨어 Vulkan 렌더러 (CPU 기반)
- GPU 드라이버가 없을 때 사용됨
- 성능이 상당히 느림

**해결 방법 (GPU가 있는 경우):**

1. **적절한 드라이버 설치** ([Vulkan 디바이스를 찾을 수 없음](#vulkan-디바이스를-찾을-수-없음) 참조)

2. **하이브리드 그래픽이 있는 노트북에서 전용 GPU 강제 사용:**
   ```bash
   DRI_PRIME=1 ./build/vulkanGLFW
   ```

3. **GPU가 사용되고 있는지 확인:**
   ```bash
   vulkaninfo | grep deviceName
   # llvmpipe가 아닌 실제 GPU가 표시되어야 함
   ```

---

### Windows: DLL 누락

**오류:**
```
The code execution cannot proceed because VCRUNTIME140.dll was not found
```

**해결 방법:**

1. **Visual C++ 재배포 가능 패키지 설치:**
   - [Microsoft](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)에서 다운로드
   - x64 및 x86 버전 모두 설치

2. **또는 Visual Studio 2022 설치** (재배포 가능 패키지 포함)

---

## 성능 문제

### 낮은 프레임 레이트

**증상:**
- 애플리케이션이 실행되지만 FPS < 30
- 끊김 또는 지연

**진단:**

1. **성능 모니터링 활성화:**

   `Application.cpp` 메인 루프에 FPS 카운터 추가:
   ```cpp
   // 매초 FPS 출력
   static auto lastTime = std::chrono::high_resolution_clock::now();
   static int frameCount = 0;
   frameCount++;

   auto now = std::chrono::high_resolution_clock::now();
   auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);
   if (delta.count() >= 1000) {
       std::cout << "FPS: " << frameCount << std::endl;
       frameCount = 0;
       lastTime = now;
   }
   ```

2. **내장 GPU에서 실행 중인지 확인:**
   ```bash
   vulkaninfo | grep deviceName
   ```

3. **최적화:**
   - Release 빌드 확인: `cmake --build build --config Release`
   - Validation layer 비활성화 (위 참조)
   - 과도한 드로우 콜 확인

---

### 메모리 누수

**증상:**
- 시간이 지남에 따라 메모리 사용량 증가
- 한동안 실행 후 애플리케이션 크래시

**진단:**

1. **Vulkan validation layer 활성화** (많은 리소스 누수 포착)

2. **메모리 프로파일러 사용:**
   - **Linux**: `valgrind --leak-check=full ./build/vulkanGLFW`
   - **macOS**: Instruments (Xcode → Open Developer Tool → Instruments)
   - **Windows**: Visual Studio Diagnostic Tools

3. **일반적인 원인:**
   - Vulkan 객체 파괴 누락
   - RAII 소멸자 호출 안 됨 (이동 시맨틱 확인)
   - 스마트 포인터로 순환 참조

---

## 디버깅 도구

### RenderDoc (프레임 캡처)

**설치:**
- [renderdoc.org](https://renderdoc.org/)에서 다운로드

**사용법:**
1. RenderDoc 실행
2. 실행 파일 설정: `build/vulkanGLFW`
3. "Launch" 클릭
4. 실행 중인 앱에서 F12를 눌러 프레임 캡처
5. 드로우 콜, 파이프라인 상태, 버퍼, 텍스처 분석

---

### NVIDIA Nsight Graphics

**설치:**
- [NVIDIA Developer](https://developer.nvidia.com/nsight-graphics)에서 다운로드

**사용법:**
- Nsight 실행
- `vulkanGLFW` 프로세스에 연결
- GPU 성능, 셰이더 실행 프로파일링

---

### Vulkan Validation Layers

**활성화:**
```cpp
// src/Application.cpp
static constexpr bool ENABLE_VALIDATION = true;
```

**출력:**
- `VUID-` 코드가 포함된 자세한 오류 메시지
- 성능 경고
- 모범 사례 제안

**VUID 코드 검색:**
- [Vulkan Specification](https://registry.khronos.org/vulkan/)
- [Vulkan Validation Layers Guide](https://github.com/KhronosGroup/Vulkan-ValidationLayers)

---

## RHI (Render Hardware Interface) 문제

### Linux: Dynamic Rendering 미지원 (VK_KHR_dynamic_rendering)

**오류:**
```
Validation Error: [ VUID-vkCmdBeginRendering-dynamicRendering-06446 ]
vkCmdBeginRendering requires VK_KHR_dynamic_rendering or Vulkan 1.3
```

또는:

```
Assertion `("dynamicRendering is not enabled on the device", false)` failed.
```

**원인:**
- Linux (특히 WSL2의 lavapipe/llvmpipe)는 Vulkan 1.1 사용
- Dynamic rendering (`vkCmdBeginRendering`/`vkCmdEndRendering`)은 Vulkan 1.3 기능
- macOS (MoltenVK)와 최신 GPU가 있는 Windows는 Vulkan 1.3 지원
- lavapipe (소프트웨어 렌더러)는 Vulkan 1.1만 지원

**해결 방법:**

Linux에서 전통적인 render pass 사용:

**1. VulkanRHICommandEncoder - 플랫폼별 render pass 사용:**

```cpp
// VulkanRHICommandEncoder.cpp
void VulkanRHICommandEncoder::beginRenderPass(const RenderPassDesc& desc) {
#ifdef __linux__
    // Linux (Vulkan 1.1): 전통적인 render pass 사용
    if (desc.nativeRenderPass && desc.nativeFramebuffer) {
        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = static_cast<vk::RenderPass>(
            reinterpret_cast<VkRenderPass>(desc.nativeRenderPass));
        renderPassInfo.framebuffer = static_cast<vk::Framebuffer>(
            reinterpret_cast<VkFramebuffer>(desc.nativeFramebuffer));
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = vk::Extent2D{desc.width, desc.height};

        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue{...};
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        m_commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
        m_usesTraditionalRenderPass = true;
    }
#else
    // macOS/Windows (Vulkan 1.3): dynamic rendering 사용
    vk::RenderingInfo renderingInfo{...};
    m_commandBuffer.beginRendering(renderingInfo);
#endif
}

void VulkanRHICommandEncoder::endRenderPass() {
#ifdef __linux__
    if (m_usesTraditionalRenderPass) {
        m_commandBuffer.endRenderPass();
        m_usesTraditionalRenderPass = false;
        return;
    }
#endif
    m_commandBuffer.endRendering();
}
```

**2. RenderPassDesc에 nativeRenderPass/nativeFramebuffer 추가:**

```cpp
// RHIRenderPass.hpp
struct RenderPassDesc {
    // ... 기존 필드 ...
    void* nativeRenderPass = nullptr;   // Linux: VkRenderPass
    void* nativeFramebuffer = nullptr;  // Linux: VkFramebuffer
};
```

**3. VulkanRHISwapchain에서 framebuffer 생성:**

```cpp
// VulkanRHISwapchain.cpp
void VulkanRHISwapchain::createFramebuffers() {
    m_framebuffers.resize(m_imageViews.size());
    for (size_t i = 0; i < m_imageViews.size(); i++) {
        std::array<vk::ImageView, 2> attachments = {
            m_imageViews[i],
            m_depthImageView
        };
        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_extent.width;
        framebufferInfo.height = m_extent.height;
        framebufferInfo.layers = 1;
        m_framebuffers[i] = m_device.createFramebuffer(framebufferInfo);
    }
}
```

---

### Linux: renderPass = NULL로 파이프라인 생성

**오류:**
```
Validation Error: [ VUID-VkGraphicsPipelineCreateInfo-dynamicRendering-06576 ]
If the dynamicRendering feature is not enabled, renderPass must not be VK_NULL_HANDLE
```

**원인:**
- Linux에서 파이프라인 생성 시 유효한 VkRenderPass 필요
- dynamic rendering이 있는 macOS/Windows는 `renderPass = VK_NULL_HANDLE` 사용 가능
- lavapipe는 dynamic rendering을 지원하지 않아 전통적인 render pass 필요

**해결 방법:**

**1. RenderPipelineDesc에 nativeRenderPass 추가:**

```cpp
// RHIPipeline.hpp
struct RenderPipelineDesc {
    // ... 기존 필드 ...
    void* nativeRenderPass = nullptr;  // Linux: 파이프라인 생성용 VkRenderPass
};
```

**2. 조건부 파이프라인 생성:**

```cpp
// VulkanRHIPipeline.cpp
void VulkanRHIPipeline::createGraphicsPipeline(const RenderPipelineDesc& desc) {
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    // ... 파이프라인 스테이지, 레이아웃 등 설정 ...

#ifdef __linux__
    // Linux: 전통적인 render pass 사용
    if (desc.nativeRenderPass) {
        pipelineInfo.renderPass = static_cast<vk::RenderPass>(
            reinterpret_cast<VkRenderPass>(desc.nativeRenderPass));
        pipelineInfo.subpass = 0;
    }
#else
    // macOS/Windows: dynamic rendering 사용
    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &swapchainFormat;
    renderingInfo.depthAttachmentFormat = vk::Format::eD32Sfloat;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.renderPass = nullptr;
#endif

    m_pipeline = m_device.createGraphicsPipeline(pipelineCache, pipelineInfo).value;
}
```

**3. 파이프라인 생성 전에 스왑체인 먼저 생성:**

```cpp
// Renderer.cpp
void Renderer::createRHIPipeline() {
    // 스왑체인 먼저 생성 (Linux에서 render pass 필요)
    if (!m_rhiSwapchain) {
        createSwapchain();
    }

    rhi::RenderPipelineDesc desc{};
    // ... 파이프라인 설명 설정 ...

#ifdef __linux__
    auto* vulkanSwapchain = dynamic_cast<VulkanRHISwapchain*>(m_rhiSwapchain.get());
    if (vulkanSwapchain) {
        desc.nativeRenderPass = reinterpret_cast<void*>(
            static_cast<VkRenderPass>(vulkanSwapchain->getRenderPass()));
    }
#endif

    m_rhiPipeline = m_rhiDevice->createRenderPipeline(desc);
}
```

---

### Linux: 이미지 레이아웃 전환 오류

**오류:**
```
Validation Error: [ VUID-VkImageMemoryBarrier-oldLayout-01197 ]
oldLayout must be VK_IMAGE_LAYOUT_UNDEFINED or the current layout of the image
```

**원인:**
- 수동 이미지 레이아웃 배리어가 render pass의 자동 전환과 충돌
- 전통적인 render pass (`initialLayout`/`finalLayout`)가 레이아웃 전환을 자동으로 처리
- 명시적 `pipelineBarrier` 추가 시 중복/충돌 전환 발생

**해결 방법:**

전통적인 render pass 사용 시 Linux에서 수동 배리어 건너뛰기:

```cpp
// VulkanRHICommandEncoder.cpp
void VulkanRHICommandEncoder::pipelineBarrier(...) {
#ifdef __linux__
    // Linux의 전통적인 render pass에서는 레이아웃 전환이
    // renderPass의 initialLayout/finalLayout에 의해 자동으로 처리됨
    // 충돌을 피하기 위해 수동 배리어 건너뛰기
    return;
#endif
    // macOS/Windows: 배리어 정상 적용
    m_commandBuffer.pipelineBarrier(...);
}
```

**참고:** 이는 임시 해결책입니다. 더 견고한 솔루션은 활성 render pass를 추적하고 충돌하는 배리어만 건너뛰는 것입니다.

---

### Phase 8: 레거시 코드 제거 후 세그멘테이션 폴트

**오류:**
```
[Vulkan] Validation Error: [ VUID-VkFramebufferCreateInfo-attachmentCount-00876 ]
pCreateInfo->attachmentCount 1 does not match attachmentCount of 2

[Vulkan] Validation Error: [ VUID-VkClearDepthStencilValue-depth-00022 ]
pRenderPassBegin->pClearValues[1].depthStencil.depth is invalid

[Vulkan] Validation Error: [ VUID-VkRenderPassBeginInfo-clearValueCount-00902 ]
clearValueCount is 1 but there must be at least 2 entries

Segmentation fault (core dumped)
```

**원인:**

- 레거시 래퍼 클래스(VulkanSwapchain, VulkanImage 등)를 삭제한 후 초기화 순서가 잘못됨
- Depth 리소스(`createRHIDepthResources()`)가 swapchain 생성 전에 호출됨
- `createRHIDepthResources()`가 호출되었을 때 `rhiBridge->getSwapchain()`이 null이었음
- 이로 인해 depth image가 생성되지 않음
- 나중에 framebuffer 생성 시 depth attachment를 기대했지만 없었음

**잘못된 초기화 순서:**

```cpp
Renderer::Renderer(...) {
    device = std::make_unique<VulkanDevice>(...);
    rhiBridge = std::make_unique<rendering::RendererBridge>(...);

    // ❌ Swapchain이 아직 생성되지 않음!
    createRHIDepthResources();  // 조기 반환 - swapchain이 null
    createRHIUniformBuffers();
    createRHIBindGroups();
    createRHIPipeline();        // Depth attachment 없이 framebuffer 생성
}
```

**해결책:**

Depth 리소스 **전에** swapchain을 생성:

```cpp
Renderer::Renderer(...) {
    device = std::make_unique<VulkanDevice>(...);
    rhiBridge = std::make_unique<rendering::RendererBridge>(...);

    // ✅ Swapchain을 먼저 생성 (depth 리소스에 필요)
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);

    // 이제 depth 리소스가 swapchain 크기를 가져올 수 있음
    createRHIDepthResources();
    createRHIUniformBuffers();
    createRHIBindGroups();
    createRHIPipeline();
}
```

**주요 변경사항:**

1. RendererBridge 생성 직후 `rhiBridge->createSwapchain()` 호출
2. `createRHIDepthResources()`가 이제 swapchain 크기를 쿼리할 수 있음
3. `createRHIPipeline()`에서 생성된 framebuffer가 depth attachment를 올바르게 포함

---

### Phase 8: 세마포어 시그널링 경고 (심각하지 않음)

**경고:**
```
[Vulkan] Validation Error: [ VUID-vkQueueSubmit-pCommandBuffers-00065 ]
vkQueueSubmit(): pSubmits[0].pSignalSemaphores[0] is being signaled by VkQueue,
but it was previously signaled by VkQueue and has not since been waited on.
```

**원인:**

- 세마포어가 적절한 동기화 없이 프레임 간에 재사용됨
- 새 작업을 제출하기 전에 fence를 대기하지 않았을 수 있음
- 엄격한 validation에서 감지되지만 런타임 문제를 일으키지 않음

**영향:**

- ⚠️ **비차단** - 애플리케이션이 정상적으로 렌더링됨
- 성능 영향 없음
- Validation layer 경고만 발생

**임시 해결책:**

지금은 이 경고를 무시하세요. 세마포어 동기화는 validation 경고에도 불구하고 올바르게 작동합니다.

**향후 수정 (선택사항):**

RendererBridge에서 fence 대기 최적화:

```cpp
void RendererBridge::beginFrame() {
    // 타임아웃과 함께 fence 대기
    m_inFlightFences[m_currentFrame]->wait(UINT64_MAX);
    m_inFlightFences[m_currentFrame]->reset();  // Fence 리셋

    // 이제 세마포어를 안전하게 재사용할 수 있음
    // ...
}
```

---

## 추가 도움 받기

이러한 해결 방법으로 문제가 해결되지 않으면:

1. **문서 확인:**
   - [빌드 가이드](BUILD_GUIDE.md)
   - [크로스 플랫폼 가이드](CROSS_PLATFORM_RENDERING.md)
   - [리팩토링 문서](refactoring/monolith-to-layered/)

2. **이슈 열기:**
   - OS, GPU, Vulkan SDK 버전 포함
   - 전체 오류 메시지 첨부
   - 재현 단계 설명

3. **유용한 리소스:**
   - [Vulkan Tutorial](https://vulkan-tutorial.com/)
   - [Vulkan Spec](https://registry.khronos.org/vulkan/)
   - [Khronos Forums](https://community.khronos.org/)

---

*마지막 업데이트: 2025-12-22*
