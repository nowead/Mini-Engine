# Emscripten Toolchain for WebAssembly Builds

set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_C_COMPILER emcc)
set(CMAKE_CXX_COMPILER em++)
set(EMSCRIPTEN ON)

# On Windows, emar/emranlib are .bat wrappers — cmake_link_script (CreateProcess)
# cannot run extension-less script names directly, but CMake wraps .bat paths in
# "cmd /c" automatically. Test CMAKE_HOST_WIN32, NOT WIN32: this toolchain sets
# CMAKE_SYSTEM_NAME=Emscripten (cross-compiling), so WIN32 reflects the TARGET
# (Emscripten → false) and the Windows host would wrongly fall to the plain
# `emar`, which Windows cannot execute → fresh configure fails the compiler test.
if(CMAKE_HOST_WIN32 AND DEFINED ENV{EMSDK})
    set(_em "$ENV{EMSDK}/upstream/emscripten")
    set(CMAKE_AR     "${_em}/emar.bat"     CACHE FILEPATH "Emscripten ar"     FORCE)
    set(CMAKE_RANLIB "${_em}/emranlib.bat"  CACHE FILEPATH "Emscripten ranlib" FORCE)
else()
    set(CMAKE_AR     emar     CACHE FILEPATH "Emscripten ar")
    set(CMAKE_RANLIB emranlib CACHE FILEPATH "Emscripten ranlib")
endif()

# Point CMake to our Platform/Emscripten.cmake so it knows the compiler feature set.
# Without this, CMake reports "System is unknown" and target_compile_features fails.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# Cross-compilation: use STATIC_LIBRARY for try_compile so CMake can detect
# compiler ABI without needing to run (execute) the resulting binary.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Set C++20 globally as a fallback — CMakeLists.txt also guards with EMSCRIPTEN.
set(CMAKE_CXX_STANDARD 20 CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "" FORCE)

# Note: emdawnwebgpu is specified per-target via --use-port=emdawnwebgpu
# Global flags here run during CMake's compiler test and must not include port flags.

# Optimization
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

# Disable C++20 module scanning for Emscripten compatibility
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL "" FORCE)
