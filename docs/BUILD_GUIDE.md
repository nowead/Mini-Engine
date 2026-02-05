# Build Guide

Detailed instructions for building Mini-Engine on Linux, macOS, and Windows.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [Platform-Specific Setup](#platform-specific-setup)
  - [Linux](#linux)
  - [macOS](#macos)
- [Building the Project](#building-the-project)
- [Running the Application](#running-the-application)
- [Build Options](#build-options)

---

## Prerequisites

### Required Software

1. **Vulkan SDK** 1.3 or higher
   - Download from [LunarG](https://vulkan.lunarg.com/)
   - **Important**: Must include the `slangc` shader compiler
   - Add Vulkan SDK to your system PATH

2. **CMake** 3.28 or higher
   - Download from [cmake.org](https://cmake.org/download/)

3. **C++20 Compatible Compiler**
   - **Linux**: GCC 11+ or Clang 14+
   - **macOS**: Xcode 14+ (includes Clang 14+)
   - **Windows**: Visual Studio 2022+ or Clang 14+

4. **vcpkg** Package Manager
   - Required for dependency management (GLFW, GLM, stb, tinyobjloader)

### Dependencies (Managed via vcpkg)

- **GLFW3**: Cross-platform window and input management
- **GLM**: OpenGL Mathematics library for 3D math
- **stb**: Image loading (stb_image.h)
- **tinyobjloader**: Wavefront OBJ file parsing

---

## Platform-Specific Setup

### Linux

#### 1. Install System Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install build-essential cmake git
sudo apt install vulkan-tools libvulkan-dev
sudo apt install libglfw3-dev  # optional, vcpkg will provide if not installed
```

**Fedora/RHEL:**
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install cmake git
sudo dnf install vulkan-tools vulkan-loader-devel
```

#### 2. Verify Vulkan Installation
```bash
vulkaninfo  # Should display Vulkan capabilities
```

#### 3. Install vcpkg
```bash
git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
```

#### 4. Set Environment Variables
Add to `~/.bashrc` or `~/.zshrc`:
```bash
export VCPKG_ROOT=~/vcpkg
export VULKAN_SDK=/path/to/vulkan/sdk  # if not in system PATH
export LD_LIBRARY_PATH=$VULKAN_SDK/lib:$LD_LIBRARY_PATH
```

Reload shell:
```bash
source ~/.bashrc  # or source ~/.zshrc
```

---

### macOS

#### 1. Install Xcode and Command Line Tools
```bash
xcode-select --install
```

#### 2. Install Homebrew (if not installed)
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

#### 3. Install CMake
```bash
brew install cmake
```

#### 4. Install Vulkan SDK
- Download from [LunarG - macOS](https://vulkan.lunarg.com/sdk/home)
- Install the .dmg package
- The SDK includes MoltenVK for Vulkan-to-Metal translation

#### 5. Install vcpkg
```bash
git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
```

#### 6. Set Environment Variables
Add to `~/.zshrc` or `~/.bash_profile`:
```bash
export VCPKG_ROOT=~/vcpkg
export VULKAN_SDK=~/VulkanSDK/1.3.xxx/macOS  # adjust version
export DYLD_LIBRARY_PATH=$VULKAN_SDK/lib:$DYLD_LIBRARY_PATH
export PATH=$VULKAN_SDK/bin:$PATH
```

Reload shell:
```bash
source ~/.zshrc
```

#### 7. Verify Vulkan Installation
```bash
vulkaninfo
```

**Note**: On macOS, Vulkan runs through MoltenVK, which translates Vulkan calls to Metal.

---

### Windows

#### 1. Install Visual Studio 2022
- Download from [Visual Studio](https://visualstudio.microsoft.com/)
- Select "Desktop development with C++" workload
- Ensure C++20 support is included

#### 2. Install CMake
- Download from [cmake.org](https://cmake.org/download/)
- During installation, select "Add CMake to system PATH"

#### 3. Install Vulkan SDK
- Download from [LunarG - Windows](https://vulkan.lunarg.com/sdk/home)
- Run the installer
- Ensure "Add to PATH" is selected during installation

#### 4. Install vcpkg
Open PowerShell or Command Prompt:
```powershell
cd C:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

#### 5. Set Environment Variables
**Using PowerShell (recommended):**
```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
[Environment]::SetEnvironmentVariable("VULKAN_SDK", "C:\VulkanSDK\1.3.xxx", "User")
```

**Or manually via System Properties:**
1. Search for "Environment Variables" in Windows search
2. Add user variables:
   - `VCPKG_ROOT` = `C:\vcpkg`
   - `VULKAN_SDK` = `C:\VulkanSDK\1.3.xxx` (adjust version)

#### 6. Verify Installation
```powershell
vulkaninfo  # Should display Vulkan capabilities
```

---

## Building the Project

### Step 1: Clone the Repository

```bash
git clone https://github.com/your-username/vulkan-fdf.git
cd vulkan-fdf
```

### Step 2: Install vcpkg Dependencies (First-Time Setup)

**Linux/macOS:**
```bash
$VCPKG_ROOT/vcpkg install glfw3 glm stb tinyobjloader
```

**Windows:**
```powershell
C:\vcpkg\vcpkg.exe install glfw3 glm stb tinyobjloader
```

**Note**: This step is only needed once. vcpkg will cache the dependencies.

### Step 3: Configure CMake

**Method 1: Using Makefile (Linux/macOS only)**
```bash
make configure
```

**Method 2: Using CMake directly (All platforms)**

**Linux/macOS:**
```bash
cmake --preset=default
```

**Windows:**
```powershell
cmake --preset=default
```

### Step 4: Build

**Method 1: Using Makefile (Linux/macOS only)**
```bash
make build
```

**Method 2: Using CMake directly (All platforms)**

**Linux/macOS:**
```bash
cmake --build build --config Release
```

**Windows:**
```powershell
cmake --build build --config Release
```

### Step 5: Verify Shader Compilation

The build process automatically compiles Slang shaders to SPIR-V. Check for:
```
shaders/slang.spv  # Generated during build
```

If shader compilation fails, see [Troubleshooting](TROUBLESHOOTING.md#shader-compilation-fails).

---

## Running the Application

### Linux/macOS

```bash
./build/vulkanGLFW
```

Or using Makefile:
```bash
make run
```

### Windows

**Using Command Prompt:**
```powershell
.\build\Release\vulkanGLFW.exe
```

**Or double-click** `vulkanGLFW.exe` in the `build\Release\` folder.

---

## Build Options

### Debug Build

**Linux/macOS:**
```bash
cmake --preset=default -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

**Windows:**
```powershell
cmake --preset=default
cmake --build build --config Debug
```

### Enable Vulkan Validation Layers

Edit [src/Application.cpp](../src/Application.cpp):
```cpp
static constexpr bool ENABLE_VALIDATION = true;  // Change to true
```

Then rebuild:
```bash
make build  # or cmake --build build
```

### Clean Build

**Using Makefile (Linux/macOS):**
```bash
make clean
```

**Using CMake (All platforms):**
```bash
rm -rf build  # Linux/macOS
# or
rmdir /s build  # Windows
```

Then reconfigure and rebuild.

---

## Makefile Commands (Linux/macOS)

Quick reference for Makefile targets:

| Command | Description |
|---------|-------------|
| `make` | Configure + Build + Run (one command) |
| `make configure` | Configure CMake |
| `make build` | Build the project |
| `make run` | Run the application |
| `make clean` | Clean build artifacts |

---

## WebAssembly (WASM) Build

Mini-Engine supports WebGPU/WASM builds via Emscripten. The WASM build uses the WebGPU backend instead of Vulkan.

### Prerequisites

1. **Emscripten SDK** (version **3.1.50** required)
   - Newer versions (3.1.60+) have a `wasm-ld` bug on macOS arm64 causing `section too large` linker errors
   - Emscripten 5.0.0+ deprecated `USE_WEBGPU=1` and is not compatible

```bash
# Install Emscripten SDK
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk
./emsdk install 3.1.50
./emsdk activate 3.1.50
source ./emsdk_env.sh
```

### Building for WASM

```bash
# Using Makefile
make build-wasm

# Or using CMake directly
mkdir -p build_wasm && cd build_wasm
emcmake cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/EmscriptenToolchain.cmake
emmake make -j$(nproc)
```

### Running the WASM Build

```bash
# Start a local HTTP server (WebGPU requires HTTPS or localhost)
make serve-instancing
# Open http://localhost:8000/instancing_test.html in Chrome 113+
```

### Platform Notes

| Platform | Emscripten Version | Status |
|----------|-------------------|--------|
| Linux x86_64 | 3.1.50 - 3.1.74 | Working |
| macOS arm64 (Apple Silicon) | **3.1.50 only** | Working |
| macOS arm64 | 3.1.60+ | `section too large` linker bug |
| Any | 5.0.0+ | `USE_WEBGPU=1` deprecated |

For WASM build issues, see [WASM Troubleshooting Guide](WASM_TROUBLESHOOTING.md).

---

## Next Steps

- **Build succeeded?** See [README.md](../README.md) for usage
- **Build failed?** Check [Troubleshooting Guide](TROUBLESHOOTING.md)
- **WASM build issues?** Check [WASM Troubleshooting Guide](WASM_TROUBLESHOOTING.md)
- **Want to modify code?** See [Refactoring Documentation](refactoring/monolith-to-layered/)

---

*Last Updated: 2026-02-03*
