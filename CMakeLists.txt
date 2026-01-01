cmake_minimum_required(VERSION 3.28)

project(vulkanGLFW CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable C++ module scanning for Emscripten (Unix Makefiles doesn't support it)
if(EMSCRIPTEN)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
else()
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# =============================================================================
# RHI Backend Options
# =============================================================================
option(RHI_BACKEND_VULKAN "Enable Vulkan RHI backend" ON)
option(RHI_BACKEND_WEBGPU "Enable WebGPU RHI backend" OFF)

# Emscripten: Auto-enable WebGPU, disable Vulkan
if(EMSCRIPTEN)
    set(RHI_BACKEND_WEBGPU ON CACHE BOOL "" FORCE)
    set(RHI_BACKEND_VULKAN OFF CACHE BOOL "" FORCE)
    message(STATUS "Emscripten detected - forcing WebGPU backend")
endif()

message(STATUS "--- RHI Backend Configuration ---")
message(STATUS "RHI_BACKEND_VULKAN: ${RHI_BACKEND_VULKAN}")
message(STATUS "RHI_BACKEND_WEBGPU: ${RHI_BACKEND_WEBGPU}")
message(STATUS "EMSCRIPTEN: ${EMSCRIPTEN}")
message(STATUS "---------------------------------")

# Dependencies
if(NOT EMSCRIPTEN)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(glm CONFIG REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(Stb REQUIRED)
    find_package(tinyobjloader REQUIRED)
    find_package(imgui CONFIG REQUIRED)
else()
    # Emscripten: Use FetchContent for glm (header-only library)
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glm)
endif()

# =============================================================================
# Vulkan C++ module setup (Native builds only)
# =============================================================================
if(NOT EMSCRIPTEN)
    message(STATUS "--- Vulkan Debug Info ---")
    message(STATUS "Vulkan_FOUND: ${Vulkan_FOUND}")
    message(STATUS "VULKAN_INCLUDE_DIR: ${VULKAN_INCLUDE_DIR}")
    message(STATUS "VULKAN_LIBRARY: ${VULKAN_LIBRARY}")
    message(STATUS "VULKAN_SDK (env var from CMake): $ENV{VULKAN_SDK}")
    message(STATUS "-------------------------")

    add_library(VulkanCppModule)
    add_library(Vulkan::cppm ALIAS VulkanCppModule)

    target_compile_definitions(VulkanCppModule PUBLIC
            VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
            VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    )

    # Suppress Vulkan C++ module warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(VulkanCppModule PRIVATE -Wno-missing-declarations)
    endif()
    target_include_directories(VulkanCppModule
            PRIVATE
            "${Vulkan_INCLUDE_DIR}"
    )
    target_link_libraries(VulkanCppModule
            PUBLIC
            Vulkan::Vulkan
    )

    set_target_properties(VulkanCppModule PROPERTIES
        CXX_STANDARD 20
        CXX_SCAN_FOR_MODULES ON
    )

    target_sources(VulkanCppModule
            PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS
            "${Vulkan_INCLUDE_DIR}"
            FILES
            "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
    )
endif()

# Find WebGPU dependencies (native builds only)
if(RHI_BACKEND_WEBGPU AND NOT EMSCRIPTEN)
    find_package(dawn CONFIG REQUIRED)
endif()

# Add RHI modules
add_subdirectory(src/rhi)

if(RHI_BACKEND_VULKAN)
    add_subdirectory(src/rhi-vulkan)
endif()

if(RHI_BACKEND_WEBGPU)
    add_subdirectory(src/rhi-webgpu)
endif()

# Link rhi_factory to backend implementations (after both are defined)
if(RHI_BACKEND_VULKAN)
    target_link_libraries(rhi_factory PUBLIC rhi::vulkan)
endif()

if(RHI_BACKEND_WEBGPU)
    target_link_libraries(rhi_factory PUBLIC rhi::webgpu)
endif()

# Main application (skip for Emscripten - it uses Vulkan-specific code)
if(NOT EMSCRIPTEN)
add_executable(vulkanGLFW
    src/main.cpp
    # Application layer
    src/Application.cpp
    src/Application.hpp
    # Core classes
    src/core/VulkanDevice.cpp
    src/core/VulkanDevice.hpp
    # Phase 8: Legacy rendering classes removed - using RHI only
    # Resource classes
    src/resources/ResourceManager.cpp
    src/resources/ResourceManager.hpp
    # Rendering classes
    src/rendering/Renderer.cpp
    src/rendering/Renderer.hpp
    src/rendering/RendererBridge.hpp
    src/rendering/RendererBridge.cpp
    # Scene classes
    src/scene/Mesh.cpp
    src/scene/Mesh.hpp
    src/scene/SceneManager.cpp
    src/scene/SceneManager.hpp
    src/scene/Camera.cpp
    src/scene/Camera.hpp
    # Loader classes
    src/loaders/OBJLoader.cpp
    src/loaders/OBJLoader.hpp
    src/loaders/FDFLoader.cpp
    src/loaders/FDFLoader.hpp
    # UI classes (Phase 6: ImGui RHI migration)
    src/ui/ImGuiBackend.hpp
    src/ui/ImGuiVulkanBackend.hpp
    src/ui/ImGuiVulkanBackend.cpp
    src/ui/ImGuiManager.cpp
    src/ui/ImGuiManager.hpp
    # Utility headers (header-only)
    src/utils/VulkanCommon.hpp
    src/utils/Vertex.hpp
    src/utils/FileUtils.hpp
)

# Add include directories for the project
# IMPORTANT: Vulkan SDK BEFORE other includes to ensure ImGui uses correct headers
target_include_directories(vulkanGLFW BEFORE PRIVATE
    ${Vulkan_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vulkanGLFW PRIVATE
    glm::glm
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    Stb::stb
    Vulkan::cppm
    tinyobjloader::tinyobjloader
    imgui::imgui
    rhi::factory
    rhi::vulkan
)

# Force ImGui to NOT use volk - use standard Vulkan headers
target_compile_definitions(vulkanGLFW PRIVATE
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES=0
)

# RHI Backend compile definitions
if(RHI_BACKEND_VULKAN)
    target_compile_definitions(vulkanGLFW PRIVATE RHI_BACKEND_VULKAN=1)
endif()

if(RHI_BACKEND_WEBGPU)
    target_compile_definitions(vulkanGLFW PRIVATE RHI_BACKEND_WEBGPU=1)
endif()

find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
set(SHADER_SLANG_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.slang)

# Slang 셰이더를 컴파일하는 CMake 함수 정의
function (add_slang_shader_target TARGET)
  # 함수에 전달된 인자들을 파싱합니다. (SOURCES 키워드 뒤의 값들을 가져옴)
  cmake_parse_arguments ("SHADER" "" "" "SOURCES" ${ARGN})

  # 셰이더 파일이 있는 디렉토리 경로 설정
  set (SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)
  # 컴파일할 셰이더의 진입점(entry points) 설정
  set (ENTRY_POINTS -entry vertMain -entry fragMain)

  # 1. 셰이더 디렉토리가 없으면 생성하는 커스텀 명령어 추가
  add_custom_command (
          OUTPUT ${SHADERS_DIR}
          COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
  )

  # 2. 실제 셰이더를 컴파일하는 커스텀 명령어 추가
  # VULKAN_SDK가 설정되어 있으면 라이브러리 경로를 포함
  if(DEFINED ENV{VULKAN_SDK})
    set(SLANG_LIB_PATH "$ENV{VULKAN_SDK}/lib")
    if(UNIX AND NOT APPLE)
      # Linux: LD_LIBRARY_PATH 설정, SPIR-V 1.3 for Vulkan 1.1 compatibility
      add_custom_command (
              OUTPUT  ${SHADERS_DIR}/slang.spv
              COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${SLANG_LIB_PATH}:$ENV{LD_LIBRARY_PATH}" ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_3 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -Wno-41012 -o slang.spv
              WORKING_DIRECTORY ${SHADERS_DIR}
              DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
              COMMENT "Slang 셰이더 컴파일 중..."
              VERBATIM
      )
    elseif(APPLE)
      # macOS: DYLD_LIBRARY_PATH 설정
      add_custom_command (
              OUTPUT  ${SHADERS_DIR}/slang.spv
              COMMAND ${CMAKE_COMMAND} -E env "DYLD_LIBRARY_PATH=${SLANG_LIB_PATH}:$ENV{DYLD_LIBRARY_PATH}" ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -Wno-41012 -o slang.spv
              WORKING_DIRECTORY ${SHADERS_DIR}
              DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
              COMMENT "Slang 셰이더 컴파일 중..."
              VERBATIM
      )
    else()
      # Windows 또는 기타: 환경 변수 없이 실행
      add_custom_command (
              OUTPUT  ${SHADERS_DIR}/slang.spv
              COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -Wno-41012 -o slang.spv
              WORKING_DIRECTORY ${SHADERS_DIR}
              DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
              COMMENT "Slang 셰이더 컴파일 중..."
              VERBATIM
      )
    endif()
  else()
    # VULKAN_SDK가 설정되지 않은 경우
    add_custom_command (
            OUTPUT  ${SHADERS_DIR}/slang.spv
            COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -Wno-41012 -o slang.spv
            WORKING_DIRECTORY ${SHADERS_DIR}
            DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
            COMMENT "Slang 셰이더 컴파일 중..."
            VERBATIM
    )
  endif()

  # 3. 위 커스텀 명령어들을 실행하는 커스텀 타겟 생성
  add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
endfunction()

# 위에서 정의한 함수를 호출하여 'foo'라는 이름의 셰이더 컴파일 타겟 생성
add_slang_shader_target(foo SOURCES ${SHADER_SLANG_SOURCES})

# 실제 프로그램 실행 파일 타겟인 'bar'가 'foo' 타겟에 의존하도록 설정
# 이렇게 하면 'bar'를 빌드하기 전에 항상 셰이더('foo')가 먼저 컴파일됩니다.
add_dependencies(vulkanGLFW foo)
endif() # NOT EMSCRIPTEN

# =============================================================================
# Phase 3 Smoke Test
# =============================================================================
add_executable(rhi_smoke_test
    tests/rhi_smoke_test.cpp
    # Renderer Bridge
    src/rendering/RendererBridge.hpp
    src/rendering/RendererBridge.cpp
)

target_include_directories(rhi_smoke_test BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(NOT EMSCRIPTEN)
    target_include_directories(rhi_smoke_test BEFORE PRIVATE
        ${Vulkan_INCLUDE_DIR}
    )
    target_link_libraries(rhi_smoke_test PRIVATE
        glfw
        glm::glm
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        Vulkan::cppm
        rhi::factory
        rhi::vulkan
    )
else()
    target_link_libraries(rhi_smoke_test PRIVATE
        rhi::factory
    )
    # Emscripten-specific settings
    set_target_properties(rhi_smoke_test PROPERTIES
        SUFFIX ".html"
    )
    target_link_options(rhi_smoke_test PRIVATE
        "SHELL:-s USE_WEBGPU=1"
        "SHELL:-s USE_GLFW=3"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s WASM=1"
        "SHELL:-s INITIAL_MEMORY=134217728"
        "SHELL:-s MAXIMUM_MEMORY=4294967296"
        "SHELL:-s STACK_SIZE=16777216"
        "SHELL:-Oz"
        "SHELL:-s ERROR_ON_UNDEFINED_SYMBOLS=0"
        "SHELL:-flto"
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_shell.html"
    )
    # Optimize compile flags for WASM - use Oz for maximum size optimization
    target_compile_options(rhi_smoke_test PRIVATE
        -Oz
        -g0
        -flto
    )
endif()

target_compile_definitions(rhi_smoke_test PRIVATE
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES=0
)

if(RHI_BACKEND_VULKAN)
    target_compile_definitions(rhi_smoke_test PRIVATE RHI_BACKEND_VULKAN=1)
endif()

if(RHI_BACKEND_WEBGPU)
    target_compile_definitions(rhi_smoke_test PRIVATE RHI_BACKEND_WEBGPU=1)
endif()

# =============================================================================
# Instancing Test (Phase 1.1 Demo)
# =============================================================================
add_executable(instancing_test
    tests/instancing_test_main.cpp
    src/examples/InstancingTest.cpp
    src/examples/InstancingTest.hpp
    # Renderer Bridge
    src/rendering/RendererBridge.hpp
    src/rendering/RendererBridge.cpp
)

target_include_directories(instancing_test BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(NOT EMSCRIPTEN)
    target_include_directories(instancing_test BEFORE PRIVATE
        ${Vulkan_INCLUDE_DIR}
    )
    target_link_libraries(instancing_test PRIVATE
        glfw
        glm::glm
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        Vulkan::cppm
        rhi::factory
        rhi::vulkan
    )
else()
    target_link_libraries(instancing_test PRIVATE
        rhi::factory
        glm::glm
    )
    # Emscripten-specific settings
    set_target_properties(instancing_test PROPERTIES
        SUFFIX ".html"
    )
    target_link_options(instancing_test PRIVATE
        "SHELL:-s USE_WEBGPU=1"
        "SHELL:-s USE_GLFW=3"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s WASM=1"
        "SHELL:-s INITIAL_MEMORY=134217728"
        "SHELL:-s MAXIMUM_MEMORY=4294967296"
        "SHELL:-s STACK_SIZE=16777216"
        "SHELL:-Oz"
        "SHELL:-s ERROR_ON_UNDEFINED_SYMBOLS=0"
        "SHELL:-flto"
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_shell.html"
    )
    # Optimize compile flags for WASM
    target_compile_options(instancing_test PRIVATE
        -Oz
        -g0
        -flto
    )
endif()

target_compile_definitions(instancing_test PRIVATE
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES=0
)

if(RHI_BACKEND_VULKAN)
    target_compile_definitions(instancing_test PRIVATE RHI_BACKEND_VULKAN=1)
endif()

if(RHI_BACKEND_WEBGPU)
    target_compile_definitions(instancing_test PRIVATE RHI_BACKEND_WEBGPU=1)
endif()