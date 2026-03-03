# Emscripten Toolchain for WebAssembly Builds

set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_C_COMPILER emcc)
set(CMAKE_CXX_COMPILER em++)
set(CMAKE_AR emar CACHE FILEPATH "Emscripten ar")
set(CMAKE_RANLIB emranlib CACHE FILEPATH "Emscripten ranlib")
set(EMSCRIPTEN 1)

# Note: USE_WEBGPU=1 removed — emdawnwebgpu is now specified per-target via --use-port=emdawnwebgpu
# Global linker flags here are applied during CMake's compiler test and must not include port flags

# Optimization
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

# Disable C++20 module scanning for Emscripten compatibility
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL "" FORCE)
