# CMake platform definition for Emscripten/WebAssembly.
# CMake does not ship a built-in Platform/Emscripten.cmake, so
# target_compile_features() fails with "no known features for CXX compiler".
# This file tells CMake which C++ standards Emscripten (LLVM/Clang) supports.

set(CMAKE_C_COMPILE_FEATURES   c_std_99 c_std_11)
set(CMAKE_CXX_COMPILE_FEATURES cxx_std_11 cxx_std_14 cxx_std_17 cxx_std_20)

# Emscripten is Clang-based — mark it so that compiler-ID guards work.
set(CMAKE_CXX_COMPILER_ID "Clang" CACHE STRING "" FORCE)
set(CMAKE_C_COMPILER_ID   "Clang" CACHE STRING "" FORCE)

# Suppress "unused variable" from CMake's internal platform probing.
set(CMAKE_SYSTEM_PROCESSOR "wasm32")
