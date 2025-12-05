cmake_minimum_required(VERSION 3.28)

project(vulkanGLFW CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Stb REQUIRED)
find_package(tinyobjloader REQUIRED)

message(STATUS "--- Vulkan Debug Info ---")
message(STATUS "Vulkan_FOUND: ${Vulkan_FOUND}")
message(STATUS "VULKAN_INCLUDE_DIR: ${VULKAN_INCLUDE_DIR}")
message(STATUS "VULKAN_LIBRARY: ${VULKAN_LIBRARY}")
message(STATUS "VULKAN_SDK (env var from CMake): $ENV{VULKAN_SDK}")
message(STATUS "-------------------------")

add_executable(vulkanGLFW
    src/main.cpp
    # Application layer
    src/Application.cpp
    src/Application.hpp
    # Core classes
    src/core/VulkanDevice.cpp
    src/core/VulkanDevice.hpp
    src/core/CommandManager.cpp
    src/core/CommandManager.hpp
    # Resource classes
    src/resources/VulkanBuffer.cpp
    src/resources/VulkanBuffer.hpp
    src/resources/VulkanImage.cpp
    src/resources/VulkanImage.hpp
    src/resources/ResourceManager.cpp
    src/resources/ResourceManager.hpp
    # Rendering classes
    src/rendering/Renderer.cpp
    src/rendering/Renderer.hpp
    src/rendering/SyncManager.cpp
    src/rendering/SyncManager.hpp
    src/rendering/VulkanSwapchain.cpp
    src/rendering/VulkanSwapchain.hpp
    src/rendering/VulkanPipeline.cpp
    src/rendering/VulkanPipeline.hpp
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
    # Utility headers (header-only)
    src/utils/VulkanCommon.hpp
    src/utils/Vertex.hpp
    src/utils/FileUtils.hpp
)

# Add include directories for the project
target_include_directories(vulkanGLFW PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vulkanGLFW PRIVATE
    glfw
    glm::glm
    Vulkan::Vulkan
    Stb::stb
    Vulkan::cppm
    tinyobjloader::tinyobjloader
)

find_package (Vulkan REQUIRED)

# set up Vulkan C++ module
add_library(VulkanCppModule)
add_library(Vulkan::cppm ALIAS VulkanCppModule)

target_compile_definitions(VulkanCppModule PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)
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