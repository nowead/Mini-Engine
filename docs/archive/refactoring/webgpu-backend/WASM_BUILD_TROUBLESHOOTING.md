# WebAssembly Build Troubleshooting Guide

This document describes common issues encountered when building the WebAssembly version of the Mini-Engine and their solutions.

## Table of Contents
- [macOS-Specific Issues](#macos-specific-issues)
- [Section Too Large Error](#section-too-large-error)
- [PATH Configuration Issues](#path-configuration-issues)
- [C++ Module Scanning](#c-module-scanning)

---

## macOS-Specific Issues

### Problem: `nproc: command not found`

**Error Message:**
```
make: nproc: Command not found
```

**Cause:**
The `nproc` command is Linux-specific and doesn't exist on macOS.

**Solution:**
The Makefile now uses OS detection to choose the appropriate command:
- macOS: `sysctl -n hw.ncpu`
- Linux: `nproc`

**Files Modified:**
- [Makefile](../../Makefile#L220-L224) - Added OS-specific CPU count detection

**Relevant Code:**
```makefile
ifeq ($(DETECTED_OS),macOS)
    @bash -c "... && emmake make -j$$(sysctl -n hw.ncpu)"
else
    @bash -c "... && emmake make -j$$(nproc)"
endif
```

---

## Section Too Large Error

### Problem: `wasm-ld: error: WebGPUCommon.cpp.obj: section too large`

**Error Message:**
```
wasm-ld: error: WebGPUCommon.cpp.obj: section too large
em++: error: '/Users/.../wasm-ld ... failed (returned 1)
```

**Cause:**
This error occurs due to excessive code bloat from:
1. **Inline function duplication**: All inline functions in headers are duplicated in every translation unit that includes them
2. **Insufficient optimization**: Debug builds or insufficient size optimization
3. **Large template instantiations**: Heavy use of C++ templates without optimization

**Root Cause in This Project:**
The [WebGPUCommon.hpp](../../src/rhi-webgpu/include/rhi-webgpu/WebGPUCommon.hpp) header contained ~25 inline conversion functions that were being duplicated across 13+ .cpp files, resulting in massive code bloat.

**Solution:**

### 1. Move Inline Functions to Implementation Files

**Before** (WebGPUCommon.hpp):
```cpp
// Header file with inline implementations
inline WGPUTextureFormat ToWGPUFormat(rhi::TextureFormat format) {
    switch (format) {
        case rhi::TextureFormat::R8Unorm: return WGPUTextureFormat_R8Unorm;
        // ... 50+ more cases
    }
}
// ... 24 more inline functions
```

**After** (WebGPUCommon.hpp):
```cpp
// Header file with declarations only
WGPUTextureFormat ToWGPUFormat(rhi::TextureFormat format);
rhi::TextureFormat FromWGPUFormat(WGPUTextureFormat format);
// ... 24 more declarations
```

**Implementation** ([WebGPUCommon.cpp](../../src/rhi-webgpu/src/WebGPUCommon.cpp)):
```cpp
// Single implementation in .cpp file
WGPUTextureFormat ToWGPUFormat(rhi::TextureFormat format) {
    switch (format) {
        case rhi::TextureFormat::R8Unorm: return WGPUTextureFormat_R8Unorm;
        // ... implementation
    }
}
```

### 2. Enable Maximum Size Optimization

**CMake Configuration** ([CMakeLists.txt](../../CMakeLists.txt#L315-L320)):
```cmake
# Emscripten-specific settings
target_compile_options(rhi_smoke_test PRIVATE
    -Oz        # Maximum size optimization (better than -Os)
    -g0        # Strip all debug information
    -flto      # Link-Time Optimization
)

target_link_options(rhi_smoke_test PRIVATE
    "SHELL:-Oz"
    "SHELL:-flto"
    "SHELL:-s INITIAL_MEMORY=134217728"   # 128 MB initial memory
    "SHELL:-s STACK_SIZE=16777216"        # 16 MB stack
)
```

### 3. Results

**Before:**
- Build failed with "section too large" error
- Estimated final size: >500 KB (if it had succeeded)

**After:**
- Build successful
- Final WASM size: **156 KB**
- JavaScript size: 154 KB
- HTML size: 3.1 KB

**Files Modified:**
- [src/rhi-webgpu/include/rhi-webgpu/WebGPUCommon.hpp](../../src/rhi-webgpu/include/rhi-webgpu/WebGPUCommon.hpp) - Converted to declarations only
- [src/rhi-webgpu/src/WebGPUCommon.cpp](../../src/rhi-webgpu/src/WebGPUCommon.cpp) - Added all function implementations
- [CMakeLists.txt](../../CMakeLists.txt#L315-L320) - Added `-Oz` and `-flto` flags
- [src/rhi-webgpu/CMakeLists.txt](../../src/rhi-webgpu/CMakeLists.txt#L45-L49) - Added optimization flags for rhi_webgpu library

---

## PATH Configuration Issues

### Problem: `cmake: command not found` or Permission Denied

**Error Message:**
```
emcmake: error: 'cmake ...' failed: [Errno 13] Permission denied: 'cmake'
```

**Cause:**
Emscripten's `emsdk_env.sh` sets its own PATH that may override system paths, preventing access to system tools like `cmake`.

**Solution:**
Explicitly set PATH before sourcing Emscripten environment:

**Makefile Configuration** ([Makefile](../../Makefile#L209-L211)):
```makefile
configure-wasm: check-emscripten
    @bash -c "export PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin && \
              source $(EMSCRIPTEN_ENV) && \
              cd $(WASM_BUILD_DIR) && \
              emcmake cmake .."
```

**Key Points:**
- Set PATH **before** sourcing `emsdk_env.sh`
- Include common installation paths:
  - `/opt/homebrew/bin` - Homebrew on Apple Silicon Macs
  - `/usr/local/bin` - Homebrew on Intel Macs
  - `/usr/bin:/bin:/usr/sbin:/sbin` - System paths

---

## C++ Module Scanning

### Problem: CMake Error - Modules Not Supported by Generator

**Error Message:**
```
CMake Error in CMakeLists.txt:
  The target named "rhi_smoke_test" has C++ sources that may use modules, but
  modules are not supported by this generator:

    Unix Makefiles

  Modules are supported only by Ninja, Ninja Multi-Config, and Visual Studio
  generators for VS 17.4 and newer.
```

**Cause:**
`CMAKE_CXX_SCAN_FOR_MODULES` is enabled globally, but Emscripten uses Unix Makefiles generator which doesn't support C++20 modules.

**Important Note:**
`CMAKE_CXX_SCAN_FOR_MODULES` is for **C++20 modules** (`import std;`, `export module`), **NOT** for RAII or object lifetime management. RAII is a fundamental C++ feature that works regardless of this setting.

**Solution:**

**CMake Configuration** ([CMakeLists.txt](../../CMakeLists.txt#L8-L13)):
```cmake
# Disable C++ module scanning for Emscripten (Unix Makefiles doesn't support it)
if(EMSCRIPTEN)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
else()
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
endif()
```

**What This Does:**
- ✅ RAII still works perfectly (automatic destructors, smart pointers, etc.)
- ✅ Regular `#include` headers work normally
- ❌ C++20 module syntax (`import`, `export module`) not available in WASM builds
- ✅ C++20 module syntax still available in native builds (Vulkan backend)

---

## Build Optimization Summary

### Recommended WASM Build Flags

**Compiler Flags:**
```cmake
-Oz        # Optimize for size (more aggressive than -Os)
-g0        # Remove all debug information
-flto      # Enable Link-Time Optimization
```

**Linker Flags:**
```cmake
-s USE_WEBGPU=1              # Enable WebGPU
-s USE_GLFW=3                # Enable GLFW
-s ALLOW_MEMORY_GROWTH=1     # Allow dynamic memory growth
-s INITIAL_MEMORY=134217728  # 128 MB initial
-s MAXIMUM_MEMORY=4294967296 # 4 GB maximum
-s STACK_SIZE=16777216       # 16 MB stack
```

### Size Optimization Best Practices

1. **Avoid inline functions in headers**
   - Move implementations to .cpp files
   - Only inline trivial one-liners (getters/setters)

2. **Use `-Oz` instead of `-Os`**
   - `-Oz`: Maximum size optimization
   - `-Os`: Balanced size/speed optimization
   - `-O3`: Maximum speed (much larger size)

3. **Enable LTO (Link-Time Optimization)**
   - Allows cross-module optimizations
   - Reduces code duplication across translation units
   - Requires `-flto` on both compile and link steps

4. **Strip debug information**
   - Use `-g0` to remove all debug symbols
   - Debug builds are typically 2-5x larger

---

## Quick Reference: Build Commands

```bash
# Clean WASM build
make clean-wasm

# Build WASM (configure + build)
make build-wasm

# Build WASM without reconfiguring
make build-wasm-only

# Build and serve on http://localhost:8000
make serve-wasm

# Clean and rebuild from scratch
make re-wasm
```

---

## Additional Resources

- [Emscripten Optimizing Code](https://emscripten.org/docs/optimizing/Optimizing-Code.html)
- [WebAssembly Code Size Optimization](https://v8.dev/blog/webassembly-code-caching)
- [CMake C++20 Modules](https://www.kitware.com/import-cmake-c20-modules/)

---

## Changelog

### 2024-12-30
- Fixed macOS `nproc` command issue
- Resolved "section too large" error by moving inline functions to .cpp
- Added `-Oz` and `-flto` optimization flags
- Fixed PATH configuration for Emscripten builds
- Disabled C++ module scanning for WASM builds
- Final WASM size: 156 KB
