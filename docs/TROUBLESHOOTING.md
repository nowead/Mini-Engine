# Troubleshooting Guide

Common issues and solutions when building or running Mini-Engine.

---

## Table of Contents

- [Build Issues](#build-issues)
- [Runtime Issues](#runtime-issues)
- [Shader Compilation](#shader-compilation)
- [Platform-Specific Issues](#platform-specific-issues)
- [Performance Issues](#performance-issues)
- [Debugging Tools](#debugging-tools)
- [RHI (Render Hardware Interface) Issues](#rhi-render-hardware-interface-issues)

---

## Build Issues

### CMake Configuration Fails

**Error:**
```
CMake Error: Could not find VCPKG_ROOT
```

**Solution:**
```bash
# Set VCPKG_ROOT environment variable
export VCPKG_ROOT=/path/to/vcpkg  # Linux/macOS
# or
set VCPKG_ROOT=C:\vcpkg  # Windows

# Verify it's set
echo $VCPKG_ROOT  # Linux/macOS
echo %VCPKG_ROOT%  # Windows
```

---

### Library Not Found

**Error:**
```
CMake Error: Could not find glfw3, glm, stb, or tinyobjloader
```

**Solution:**

1. **Install dependencies via vcpkg:**
   ```bash
   $VCPKG_ROOT/vcpkg install glfw3 glm stb tinyobjloader
   ```

2. **Verify installation:**
   ```bash
   $VCPKG_ROOT/vcpkg list
   # Should show: glfw3, glm, stb, tinyobjloader
   ```

3. **Reconfigure CMake:**
   ```bash
   rm -rf build  # Clean previous build
   cmake --preset=default
   ```

---

### C++20 Compiler Not Found

**Error:**
```
CMake Error: C++ compiler does not support C++20
```

**Solution:**

**Linux:**
```bash
# Install newer GCC or Clang
sudo apt install g++-11  # Ubuntu/Debian
# or
sudo dnf install gcc-c++  # Fedora/RHEL

# Set as default compiler
export CXX=g++-11
```

**macOS:**
```bash
# Update Xcode Command Line Tools
xcode-select --install

# Verify Clang version (should be 14+)
clang++ --version
```

**Windows:**
- Install Visual Studio 2022 (includes C++20 support)
- Or update existing installation via Visual Studio Installer

---

### Debug Callback Type Mismatch (Cross-Platform)

**Error:**
```
error: cannot initialize a member subobject of type 'vk::PFN_DebugUtilsMessengerCallbackEXT'
with an rvalue of type 'VkBool32 (*)(VkDebugUtilsMessageSeverityFlagBitsEXT, ...)'
type mismatch at 1st parameter ('vk::DebugUtilsMessageSeverityFlagBitsEXT' vs 'VkDebugUtilsMessageSeverityFlagBitsEXT')
```

**Cause:**
- Vulkan-Hpp (C++ wrapper) requires C++ wrapper types (`vk::`) on all platforms

**Solution:**

Use C++ Vulkan-Hpp types for all platforms:

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

**Note:** Vulkan-Hpp requires C++ types even on Linux with llvmpipe.

---

### ImGui RenderPass Casting Error

**Error:**
```
error: cannot cast from type 'struct VkRenderPass_T' to pointer type 'VkRenderPass' (aka 'VkRenderPass_T *')
```

**Cause:**
- `swapchain.getRenderPass()` already returns `vk::RenderPass` (value type)
- Adding dereference (`*`) attempts to cast the struct to a pointer, causing an error

**Solution:**

Remove unnecessary dereference and use C-style cast:

**Incorrect Code:**
```cpp
// ❌ Wrong: getRenderPass() already returns a value, don't dereference
initInfo.RenderPass = static_cast<VkRenderPass>(*swapchain.getRenderPass());
```

**Correct Code:**
```cpp
// ✅ Correct: Use C-style cast without dereference
initInfo.RenderPass = (VkRenderPass)swapchain.getRenderPass();
```

**Note:** When converting from `vk::RenderPass` to `VkRenderPass`, C-style cast `(VkRenderPass)` is safer than `static_cast`.

---

### Segmentation Fault with ImGui Rendering on Linux

**Error:**
```
validation layer: VUID-vkCmdDrawIndexed-renderpass
vkCmdDrawIndexed(): Rendering commands must occur inside a render pass.
Segmentation fault (core dumped)
```

**Cause:**

- Linux uses traditional render pass, but `Renderer::recordCommandBuffer` starts the render pass, renders the main mesh, then **immediately ends it**
- ImGui rendering is called outside the render pass, causing validation errors and segfault
- macOS/Windows use dynamic rendering, so each rendering stage can start/end its own rendering session independently

**Solution:**

On Linux, ImGui must render within the same render pass:

**1. Renderer.cpp - Don't end render pass immediately:**

**Incorrect Code:**
```cpp
// src/rendering/Renderer.cpp
#ifdef __linux__
    commandManager->getCommandBuffer(currentFrame).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Draw main mesh
    primaryMesh->draw(commandManager->getCommandBuffer(currentFrame));

    // ❌ Problem: Ending render pass here means ImGui renders outside the pass
    commandManager->getCommandBuffer(currentFrame).endRenderPass();
#endif
```

**Correct Code:**
```cpp
// src/rendering/Renderer.cpp
#ifdef __linux__
    commandManager->getCommandBuffer(currentFrame).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Draw main mesh
    primaryMesh->draw(commandManager->getCommandBuffer(currentFrame));

    // ✅ Correct: Keep render pass open - ImGui will render in the same pass
    // Note: endRenderPass() will be called after ImGui rendering
#endif
```

**2. ImGuiManager.cpp - End render pass after ImGui rendering:**

**Incorrect Code:**
```cpp
// src/ui/ImGuiManager.cpp
#ifdef __linux__
    VkCommandBuffer vkCmdBuffer = static_cast<VkCommandBuffer>(*commandBuffer);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);
    // ❌ Problem: Render pass never ends
#endif
```

**Correct Code:**
```cpp
// src/ui/ImGuiManager.cpp
#ifdef __linux__
    VkCommandBuffer vkCmdBuffer = static_cast<VkCommandBuffer>(*commandBuffer);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);

    // ✅ Correct: End render pass after ImGui rendering
    commandBuffer.endRenderPass();
#endif
```

**Rendering Flow Comparison:**

**Incorrect Flow (Causes Segfault):**
```
1. beginRenderPass()
2. Draw main mesh
3. endRenderPass()          ← Ended too early
4. ImGui draw commands      ← Executes outside render pass → CRASH!
5. end command buffer
```

**Correct Flow:**
```
1. beginRenderPass()
2. Draw main mesh
3. ImGui draw commands      ← Executes inside the same render pass
4. endRenderPass()          ← Ended after ImGui
5. end command buffer
```

**Note:**

- macOS/Windows use dynamic rendering, so each rendering stage can have independent `beginRendering()/endRendering()` pairs
- Linux (Vulkan 1.1) uses traditional render pass, so all rendering must occur within a single render pass

---

## Runtime Issues

### Vulkan Device Not Found

**Error:**
```
Failed to find GPUs with Vulkan support
```

**Possible Causes:**
1. Graphics drivers not updated
2. Vulkan drivers not installed
3. Running in virtual machine without GPU passthrough

**Solutions:**

**1. Update Graphics Drivers:**

- **NVIDIA**: Download latest drivers from [nvidia.com](https://www.nvidia.com/Download/index.aspx)
- **AMD**: Download from [amd.com](https://www.amd.com/en/support)
- **Intel**: Download from [intel.com](https://www.intel.com/content/www/us/en/download-center/home.html)

**2. Verify Vulkan Support:**
```bash
vulkaninfo  # Should display GPU capabilities
```

**3. Linux-Specific: Install Vulkan Drivers**

**Ubuntu/Debian:**
```bash
# For Intel/AMD
sudo apt install mesa-vulkan-drivers

# For NVIDIA (proprietary)
sudo ubuntu-drivers autoinstall
```

**Arch Linux:**
```bash
# For Intel
sudo pacman -S vulkan-intel

# For AMD
sudo pacman -S vulkan-radeon

# For NVIDIA
sudo pacman -S nvidia vulkan-nvidia
```

**4. Virtual Machine Users:**
- Vulkan requires GPU access
- Enable GPU passthrough if possible
- Or run on host system instead

---

### Application Crashes on Startup

**Error:**
```
Segmentation fault (core dumped)
```

**Solution:**

1. **Enable Vulkan Validation Layers** for detailed error messages:

   Edit `src/Application.cpp`:
   ```cpp
   static constexpr bool ENABLE_VALIDATION = true;
   ```

   Rebuild and run:
   ```bash
   make build && make run
   ```

2. **Check Validation Layer Output:**
   - Look for `VUID-` error codes in output
   - Search for the code at [Vulkan Spec](https://registry.khronos.org/vulkan/)

3. **Common Causes:**
   - Missing shader files (`shaders/slang.spv` not found)
   - Missing model/texture files
   - Incompatible Vulkan version

---

### Validation Layer Errors

**Error:**
```
Validation Error: [VUID-xxxxx] ...
```

**Solution:**

1. **Read the error message carefully** - it usually indicates the exact problem

2. **Common Validation Errors:**

   **Missing Memory Barrier:**
   ```
   VUID-vkCmdDraw-None-02859: Image layout mismatch
   ```
   - Fix: Add proper pipeline barriers for image layout transitions

   **Buffer/Image Not Bound:**
   ```
   VUID-vkCmdDraw-None-02697: Descriptor set not bound
   ```
   - Fix: Ensure `vkCmdBindDescriptorSets` is called before draw

   **Synchronization Error:**
   ```
   VUID-vkQueueSubmit-pWaitSemaphores-xxxxx
   ```
   - Fix: Check semaphore and fence usage in `SyncManager`

3. **Disable Validation for Release Builds:**

   Edit `src/Application.cpp`:
   ```cpp
   static constexpr bool ENABLE_VALIDATION = false;
   ```

---

## Shader Compilation

### Shader Compilation Fails

**Error:**
```
slangc: command not found
```

**Solution:**

1. **Verify Vulkan SDK Installation:**
   ```bash
   echo $VULKAN_SDK  # Should point to Vulkan SDK directory
   ```

2. **Check if slangc exists:**
   ```bash
   ls $VULKAN_SDK/bin/slangc  # Should exist
   ```

3. **Add to PATH:**

   **Linux/macOS** (`~/.bashrc` or `~/.zshrc`):
   ```bash
   export PATH=$VULKAN_SDK/bin:$PATH
   ```

   **Windows** (PowerShell as Administrator):
   ```powershell
   [Environment]::SetEnvironmentVariable(
       "Path",
       "$env:Path;$env:VULKAN_SDK\bin",
       "User"
   )
   ```

4. **Verify slangc works:**
   ```bash
   slangc --version
   ```

---

### SPIR-V Compilation Error

**Error:**
```
Error compiling shader.slang: unknown type 'XXX'
```

**Solution:**

1. **Check Slang syntax** in `shaders/shader.slang`
2. **Common issues:**
   - Incorrect uniform buffer layout
   - Missing `[[vk::binding(N)]]` annotations
   - Incompatible SPIR-V version for platform

3. **Platform-Specific SPIR-V Versions:**
   - Linux: SPIR-V 1.3 (Vulkan 1.1 compatibility)
   - macOS/Windows: SPIR-V 1.4 (Vulkan 1.3)

---

### Slang Warning: Additional Capabilities (warning 41012)

**Warning:**
```
warning 41012: entry point uses additional capabilities
that are not part of the specified profile 'spirv_1_3'
```

**Cause:**
Implicit resource types like `Sampler2D` cause Slang to auto-add extension capabilities (SPV_KHR_non_semantic_info).

**Solution:**
Suppress warning in CMakeLists.txt:

```cmake
# Add -Wno-41012 to slangc compilation command
${SLANGC_EXECUTABLE} ${SHADER_SOURCES} ... -Wno-41012 -o slang.spv
```

**Notes:**

- Warning is informational - functionality works correctly
- Alternative: Use explicit bindings (`[[vk::binding(1, 0)]] Sampler2D texture`) to eliminate warning
- For simple projects, suppressing the warning is more straightforward

---

## Platform-Specific Issues

### Linux: lavapipe Software Renderer Warning

**Warning:**
```
WARNING: lavapipe is not a conformant vulkan implementation, testing use only.
```

**Cause:**
Missing GPU drivers - Vulkan is using software renderer (lavapipe).

**Solution:**
Install Vulkan drivers for your GPU:

```bash
# NVIDIA
sudo apt install nvidia-driver-535  # or latest version

# AMD/Intel
sudo apt install mesa-vulkan-drivers

# Verify
vulkaninfo --summary  # Should show GPU (not lavapipe)
```

**Notes:**

- lavapipe is very slow (testing only)
- WSL2 requires GPU passthrough configuration
- Can be ignored for development/testing

---

### macOS: Validation Layer Not Found

**Error:**
```
Required layer not supported: VK_LAYER_KHRONOS_validation
Context::createInstance: ErrorLayerNotPresent
```

**Cause:**
- Vulkan SDK installed via Homebrew uses different paths than standalone SDK
- macOS SIP (System Integrity Protection) blocks `DYLD_LIBRARY_PATH`

**Solution:**

1. **Update Makefile environment setup** to use Homebrew paths:
   ```makefile
   HOMEBREW_PREFIX := $(shell brew --prefix)
   VULKAN_LAYER_PATH := $(HOMEBREW_PREFIX)/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d
   ```

2. **Use `DYLD_FALLBACK_LIBRARY_PATH` instead of `DYLD_LIBRARY_PATH`** (SIP allows this):
   ```makefile
   export VK_LAYER_PATH="$(VULKAN_LAYER_PATH)"
   export DYLD_FALLBACK_LIBRARY_PATH="$(HOMEBREW_PREFIX)/opt/vulkan-validationlayers/lib:$(HOMEBREW_PREFIX)/lib:/usr/local/lib:/usr/lib"
   ```

3. **Verify layer detection:**
   ```bash
   export VK_LAYER_PATH="/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d"
   vulkaninfo --summary | grep -A5 "Instance Layers"
   # Should show: VK_LAYER_KHRONOS_validation
   ```

**Key Points:**
- `VK_LAYER_PATH`: Points to `.json` manifest files
- `DYLD_FALLBACK_LIBRARY_PATH`: Points to `.dylib` library files
- Never use `DYLD_LIBRARY_PATH` on macOS (SIP blocks it)

---

### macOS: Window Surface Creation Fails

**Error:**
```
failed to create window surface!
```

**Cause:**
- Setting `VULKAN_SDK` environment variable incorrectly can interfere with MoltenVK
- Incorrect `DYLD_LIBRARY_PATH` settings

**Solution:**

Only set minimal required environment variables:
```bash
export VK_LAYER_PATH="/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d"
export DYLD_FALLBACK_LIBRARY_PATH="/opt/homebrew/opt/vulkan-validationlayers/lib:/opt/homebrew/lib:/usr/local/lib:/usr/lib"
```

Do NOT set:
- ❌ `VULKAN_SDK` (unless specifically needed)
- ❌ `DYLD_LIBRARY_PATH` (use `DYLD_FALLBACK_LIBRARY_PATH`)

---

### macOS: MoltenVK Errors

**Error:**
```
MoltenVK does not support feature: XXX
```

**Solution:**

1. **Check feature compatibility** at [MoltenVK Features](https://github.com/KhronosGroup/MoltenVK#moltenvk-feature-support)

2. **Common Unsupported Features:**
   - Some Vulkan 1.3 features
   - Certain descriptor indexing features
   - Ray tracing (not supported on Metal)

3. **Workaround:**
   - Use Vulkan 1.1 compatible features only
   - Conditional compilation for macOS-specific code

---

### Linux: llvmpipe (Software Rendering)

**Warning:**
```
WARNING: lavapipe is not a conformant Vulkan implementation
```

**Context:**
- `llvmpipe` is a software Vulkan renderer (CPU-based)
- Used when no GPU drivers available
- Performance will be significantly slower

**Solution (if you have a GPU):**

1. **Install proper drivers** (see [Vulkan Device Not Found](#vulkan-device-not-found))

2. **Force discrete GPU (laptops with hybrid graphics):**
   ```bash
   DRI_PRIME=1 ./build/vulkanGLFW
   ```

3. **Verify GPU is being used:**
   ```bash
   vulkaninfo | grep deviceName
   # Should show your actual GPU, not llvmpipe
   ```

---

### Windows: Missing DLL

**Error:**
```
The code execution cannot proceed because VCRUNTIME140.dll was not found
```

**Solution:**

1. **Install Visual C++ Redistributable:**
   - Download from [Microsoft](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)
   - Install both x64 and x86 versions

2. **Or install Visual Studio 2022** (includes redistributables)

---

## Performance Issues

### Low Frame Rate

**Symptoms:**
- Application runs but FPS < 30
- Stuttering or lag

**Diagnostics:**

1. **Enable Performance Monitoring:**

   Add FPS counter to `Application.cpp` main loop:
   ```cpp
   // Print FPS every second
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

2. **Check if running on integrated GPU:**
   ```bash
   vulkaninfo | grep deviceName
   ```

3. **Optimize:**
   - Ensure Release build: `cmake --build build --config Release`
   - Disable validation layers (see above)
   - Check for excessive draw calls

---

### Memory Leaks

**Symptoms:**
- Memory usage increases over time
- Application crashes after running for a while

**Diagnostics:**

1. **Enable Vulkan validation layers** (catches many resource leaks)

2. **Use memory profilers:**
   - **Linux**: `valgrind --leak-check=full ./build/vulkanGLFW`
   - **macOS**: Instruments (Xcode → Open Developer Tool → Instruments)
   - **Windows**: Visual Studio Diagnostic Tools

3. **Common Causes:**
   - Forgot to destroy Vulkan objects
   - RAII destructors not called (check move semantics)
   - Circular references with smart pointers

---

## Debugging Tools

### RenderDoc (Frame Capture)

**Install:**
- Download from [renderdoc.org](https://renderdoc.org/)

**Usage:**
1. Launch RenderDoc
2. Set executable: `build/vulkanGLFW`
3. Click "Launch"
4. Press F12 in running app to capture frame
5. Analyze draw calls, pipeline state, buffers, textures

---

### NVIDIA Nsight Graphics

**Install:**
- Download from [NVIDIA Developer](https://developer.nvidia.com/nsight-graphics)

**Usage:**
- Launch Nsight
- Attach to `vulkanGLFW` process
- Profile GPU performance, shader execution

---

### Vulkan Validation Layers

**Enable:**
```cpp
// src/Application.cpp
static constexpr bool ENABLE_VALIDATION = true;
```

**Output:**
- Detailed error messages with `VUID-` codes
- Performance warnings
- Best practices suggestions

**Search VUID codes:**
- [Vulkan Specification](https://registry.khronos.org/vulkan/)
- [Vulkan Validation Layers Guide](https://github.com/KhronosGroup/Vulkan-ValidationLayers)

---

## RHI (Render Hardware Interface) Issues

### ErrorIncompatibleDriver on macOS (MoltenVK)

**Error:**
```
vk::SystemError: ErrorIncompatibleDriver
```

**Cause:**
- macOS uses MoltenVK (Vulkan-to-Metal translation layer)
- MoltenVK requires the `VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME` instance extension
- Instance must be created with `vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR` flag
- Device must enable `VK_KHR_portability_subset` extension

**Solution:**

1. **Add portability flag to instance creation:**
```cpp
vk::InstanceCreateInfo createInfo{
    .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
    // ... other fields
};
```

2. **Add required instance extension:**
```cpp
requiredExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
```

3. **Add device extension:**
```cpp
#ifdef __APPLE__
std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    "VK_KHR_portability_subset",
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
};
#endif
```

---

### VMA (Vulkan Memory Allocator) Segfault on macOS

**Error:**
```
Segmentation fault in vmaCreateAllocator()
Crash in vkGetDeviceProcAddr -> loader_lookup_device_dispatch_table
```

**Cause:**
- VMA configured with `VMA_DYNAMIC_VULKAN_FUNCTIONS 1` tries to use global `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`
- vulkan-hpp uses its own dispatch mechanism, making global function pointers unreliable with MoltenVK

**Solution:**

Change VMA to use static Vulkan functions:

**VulkanMemoryAllocator.cpp:**
```cpp
#define VMA_IMPLEMENTATION
// Use static Vulkan functions - more reliable with vulkan-hpp and MoltenVK
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>
```

**VulkanCommon.hpp:**
```cpp
// VMA (Vulkan Memory Allocator)
// Use static Vulkan functions - more reliable with vulkan-hpp and MoltenVK
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>
```

**VMA Allocator creation (no need for pVulkanFunctions):**
```cpp
void createVmaAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = *m_physicalDevice;
    allocatorInfo.device = *m_device;
    allocatorInfo.instance = *m_instance;
    // Using VMA_STATIC_VULKAN_FUNCTIONS=1, so no need to provide pVulkanFunctions

    VkResult result = vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);
}
```

---

### Swapchain Window Handle is Null

**Error:**
```
VulkanRHISwapchain: Window handle is null
```

**Cause:**
- `SwapchainDesc` passed to `createSwapchain()` does not include `windowHandle`
- The RHI layer needs the window handle to create a Vulkan surface

**Solution:**

Always set `windowHandle` in `SwapchainDesc`:

```cpp
void RendererBridge::createSwapchain(uint32_t width, uint32_t height, bool vsync) {
    rhi::SwapchainDesc desc;
    desc.width = width;
    desc.height = height;
    desc.presentMode = vsync ? rhi::PresentMode::Fifo : rhi::PresentMode::Mailbox;
    desc.bufferCount = MAX_FRAMES_IN_FLIGHT + 1;
    desc.windowHandle = m_window;  // ← Don't forget this!

    m_swapchain = m_device->createSwapchain(desc);
}
```

---

## Getting More Help

If none of these solutions work:

1. **Check Documentation:**
   - [Build Guide](BUILD_GUIDE.md)
   - [Cross-Platform Guide](CROSS_PLATFORM_RENDERING.md)
   - [Refactoring Docs](refactoring/monolith-to-layered/)

2. **Open an Issue:**
   - Include OS, GPU, Vulkan SDK version
   - Attach full error message
   - Describe steps to reproduce

3. **Useful Resources:**
   - [Vulkan Tutorial](https://vulkan-tutorial.com/)
   - [Vulkan Spec](https://registry.khronos.org/vulkan/)
   - [Khronos Forums](https://community.khronos.org/)

---

*Last Updated: 2025-12-19*
