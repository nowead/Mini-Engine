cmake_minimum_required(VERSION 3.28)
project(MiniEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# C++ module scanning (Emscripten's Unix Makefiles doesn't support it)
if(EMSCRIPTEN)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
else()
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
endif()

# =============================================================================
# RHI Backend Configuration
# =============================================================================
option(RHI_BACKEND_VULKAN "Enable Vulkan RHI backend" ON)
option(RHI_BACKEND_WEBGPU "Enable WebGPU RHI backend" OFF)
option(BUILD_TESTS "Build test executables" ON)

if(EMSCRIPTEN)
    set(RHI_BACKEND_WEBGPU ON CACHE BOOL "" FORCE)
    set(RHI_BACKEND_VULKAN OFF CACHE BOOL "" FORCE)

    # Global WASM optimization flags (prevents wasm-ld "section too large" error on macOS arm64)
    # -ffunction-sections/-fdata-sections: Applied globally to affect all targets including FetchContent deps.
    # Per-target compile_options intentionally repeat these flags for explicit control across build types.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS_RELEASE "-Oz -g0 -DNDEBUG -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g1 -ffunction-sections -fdata-sections")
endif()

# =============================================================================
# Dependencies
# =============================================================================
if(NOT EMSCRIPTEN)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(glm CONFIG REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(Stb REQUIRED)
    find_package(tinyobjloader REQUIRED)
    find_package(imgui CONFIG REQUIRED)
else()
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
    )
    set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
    set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glm)

    # stb for image loading (WASM)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(stb)
    add_library(stb_image INTERFACE)
    target_include_directories(stb_image INTERFACE ${stb_SOURCE_DIR})

    # tinyobjloader for OBJ loading (WASM)
    FetchContent_Declare(
        tinyobjloader
        GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
        GIT_TAG v2.0.0rc13
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tinyobjloader)
endif()

# =============================================================================
# Vulkan C++ Module (Native only)
# =============================================================================
if(NOT EMSCRIPTEN)
    add_library(VulkanCppModule)
    add_library(Vulkan::cppm ALIAS VulkanCppModule)

    target_compile_definitions(VulkanCppModule PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(VulkanCppModule PRIVATE -Wno-missing-declarations)
    endif()

    target_include_directories(VulkanCppModule PRIVATE ${Vulkan_INCLUDE_DIR})
    target_link_libraries(VulkanCppModule PUBLIC Vulkan::Vulkan)

    set_target_properties(VulkanCppModule PROPERTIES
        CXX_STANDARD 20
        CXX_SCAN_FOR_MODULES ON
    )

    target_sources(VulkanCppModule
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${Vulkan_INCLUDE_DIR}
        FILES ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
    )
endif()

# =============================================================================
# RHI Module Setup
# =============================================================================
if(RHI_BACKEND_WEBGPU AND NOT EMSCRIPTEN)
    find_package(dawn CONFIG REQUIRED)
endif()

add_subdirectory(src/rhi)

if(RHI_BACKEND_VULKAN)
    add_subdirectory(src/rhi/backends/vulkan)
    target_link_libraries(rhi_factory PUBLIC rhi::vulkan)
endif()

if(RHI_BACKEND_WEBGPU)
    add_subdirectory(src/rhi/backends/webgpu)
    target_link_libraries(rhi_factory PUBLIC rhi::webgpu)
endif()

# =============================================================================
# Main Application (Native only)
# =============================================================================
if(NOT EMSCRIPTEN)
    add_executable(MiniEngine
        src/main.cpp
        src/Application.cpp
        src/Application.hpp
        src/resources/ResourceManager.cpp
        src/resources/ResourceManager.hpp
        src/rendering/Renderer.cpp
        src/rendering/Renderer.hpp
        src/rendering/RendererBridge.hpp
        src/rendering/RendererBridge.cpp
        src/rendering/BatchRenderer.cpp
        src/rendering/BatchRenderer.hpp
        src/scene/Mesh.cpp
        src/scene/Mesh.hpp
        src/scene/SceneManager.cpp
        src/scene/SceneManager.hpp
        src/scene/Camera.cpp
        src/scene/Camera.hpp
        src/scene/SceneNode.cpp
        src/scene/SceneNode.hpp
        src/scene/SceneGraph.cpp
        src/scene/SceneGraph.hpp
        src/scene/SectorNode.cpp
        src/scene/SectorNode.hpp
        src/scene/AABB.hpp
        src/scene/Quadtree.cpp
        src/scene/Quadtree.hpp
        src/scene/Frustum.hpp
        src/loaders/OBJLoader.cpp
        src/loaders/OBJLoader.hpp
        src/ui/ImGuiBackend.hpp
        src/ui/ImGuiVulkanBackend.hpp
        src/ui/ImGuiVulkanBackend.cpp
        src/ui/ImGuiManager.cpp
        src/ui/ImGuiManager.hpp
        src/utils/Vertex.hpp
        src/utils/FileUtils.hpp
        # Game Logic Layer
        src/game/entities/BuildingEntity.cpp
        src/game/entities/BuildingEntity.hpp
        src/game/managers/BuildingManager.cpp
        src/game/managers/BuildingManager.hpp
        src/game/managers/WorldManager.cpp
        src/game/managers/WorldManager.hpp
        src/game/world/Sector.hpp
        src/game/sync/PriceUpdate.hpp
        src/game/sync/MockDataGenerator.hpp
        src/game/utils/AnimationUtils.hpp
        src/game/utils/HeightCalculator.hpp
        # Effects Layer
        src/effects/Particle.hpp
        src/effects/ParticleSystem.cpp
        src/effects/ParticleSystem.hpp
        src/effects/ParticleRenderer.cpp
        src/effects/ParticleRenderer.hpp
        # Phase 3.3: Skybox
        src/rendering/SkyboxRenderer.cpp
        src/rendering/SkyboxRenderer.hpp
        # Phase 3.3: Shadow Mapping
        src/rendering/ShadowRenderer.cpp
        src/rendering/ShadowRenderer.hpp
        # Phase 1.2: IBL (Image Based Lighting)
        src/rendering/IBLManager.cpp
        src/rendering/IBLManager.hpp
    )

    target_include_directories(MiniEngine BEFORE PRIVATE
        ${Vulkan_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(MiniEngine PRIVATE
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

    target_compile_definitions(MiniEngine PRIVATE
        IMGUI_IMPL_VULKAN_NO_PROTOTYPES=0
        $<$<BOOL:${RHI_BACKEND_VULKAN}>:RHI_BACKEND_VULKAN=1>
        $<$<BOOL:${RHI_BACKEND_WEBGPU}>:RHI_BACKEND_WEBGPU=1>
    )

    # Slang shader compilation
    find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
    set(SHADER_SLANG_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.slang)

    function(add_slang_shader_target TARGET)
        cmake_parse_arguments(SHADER "" "" "SOURCES" ${ARGN})
        set(SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)
        set(ENTRY_POINTS -entry vertMain -entry fragMain)
        set(SLANG_FLAGS -target spirv -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -Wno-41012 -o slang.spv)

        add_custom_command(
            OUTPUT ${SHADERS_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
        )

        if(DEFINED ENV{VULKAN_SDK})
            set(SLANG_LIB_PATH "$ENV{VULKAN_SDK}/lib")
            if(UNIX AND NOT APPLE)
                set(SPIRV_PROFILE spirv_1_3)
                set(LIB_PATH_VAR "LD_LIBRARY_PATH=${SLANG_LIB_PATH}:$ENV{LD_LIBRARY_PATH}")
            elseif(APPLE)
                set(SPIRV_PROFILE spirv_1_4)
                set(LIB_PATH_VAR "DYLD_LIBRARY_PATH=${SLANG_LIB_PATH}:$ENV{DYLD_LIBRARY_PATH}")
            else()
                set(SPIRV_PROFILE spirv_1_4)
                set(LIB_PATH_VAR "")
            endif()
        else()
            set(SPIRV_PROFILE spirv_1_4)
            set(LIB_PATH_VAR "")
        endif()

        if(LIB_PATH_VAR)
            add_custom_command(
                OUTPUT ${SHADERS_DIR}/slang.spv
                COMMAND ${CMAKE_COMMAND} -E env "${LIB_PATH_VAR}"
                    ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -profile ${SPIRV_PROFILE} ${SLANG_FLAGS}
                WORKING_DIRECTORY ${SHADERS_DIR}
                DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
                COMMENT "Compiling Slang shaders..."
                VERBATIM
            )
        else()
            add_custom_command(
                OUTPUT ${SHADERS_DIR}/slang.spv
                COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -profile ${SPIRV_PROFILE} ${SLANG_FLAGS}
                WORKING_DIRECTORY ${SHADERS_DIR}
                DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
                COMMENT "Compiling Slang shaders..."
                VERBATIM
            )
        endif()

        add_custom_target(${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
    endfunction()

    add_slang_shader_target(shader_compilation SOURCES ${SHADER_SLANG_SOURCES})
    add_dependencies(MiniEngine shader_compilation)

    # Building shader GLSL compilation (REQUIRED for main application)
    find_program(GLSLC_EXECUTABLE glslc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
    set(BUILDING_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
    
    add_custom_command(
        OUTPUT ${BUILDING_SHADER_DIR}/building.vert.spv
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=vertex
                -o ${BUILDING_SHADER_DIR}/building.vert.spv
                ${BUILDING_SHADER_DIR}/building.vert.glsl
        DEPENDS ${BUILDING_SHADER_DIR}/building.vert.glsl
        COMMENT "Compiling building.vert.glsl -> SPIR-V"
    )
    
    add_custom_command(
        OUTPUT ${BUILDING_SHADER_DIR}/building.frag.spv
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=fragment
                -o ${BUILDING_SHADER_DIR}/building.frag.spv
                ${BUILDING_SHADER_DIR}/building.frag.glsl
        DEPENDS ${BUILDING_SHADER_DIR}/building.frag.glsl
        COMMENT "Compiling building.frag.glsl -> SPIR-V"
    )

    # Shadow shader compilation (Phase 3.3)
    add_custom_command(
        OUTPUT ${BUILDING_SHADER_DIR}/shadow.vert.spv
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=vertex
                -o ${BUILDING_SHADER_DIR}/shadow.vert.spv
                ${BUILDING_SHADER_DIR}/shadow.vert.glsl
        DEPENDS ${BUILDING_SHADER_DIR}/shadow.vert.glsl
        COMMENT "Compiling shadow.vert.glsl -> SPIR-V"
    )

    add_custom_command(
        OUTPUT ${BUILDING_SHADER_DIR}/shadow.frag.spv
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=fragment
                -o ${BUILDING_SHADER_DIR}/shadow.frag.spv
                ${BUILDING_SHADER_DIR}/shadow.frag.glsl
        DEPENDS ${BUILDING_SHADER_DIR}/shadow.frag.glsl
        COMMENT "Compiling shadow.frag.glsl -> SPIR-V"
    )

    # IBL compute shader compilation (Phase 1.2)
    add_custom_command(
        OUTPUT ${BUILDING_SHADER_DIR}/brdf_lut.comp.spv
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute
                -o ${BUILDING_SHADER_DIR}/brdf_lut.comp.spv
                ${BUILDING_SHADER_DIR}/brdf_lut.comp.glsl
        DEPENDS ${BUILDING_SHADER_DIR}/brdf_lut.comp.glsl
        COMMENT "Compiling brdf_lut.comp.glsl -> SPIR-V"
    )

    add_custom_command(
        OUTPUT ${BUILDING_SHADER_DIR}/equirect_to_cubemap.comp.spv
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute
                -o ${BUILDING_SHADER_DIR}/equirect_to_cubemap.comp.spv
                ${BUILDING_SHADER_DIR}/equirect_to_cubemap.comp.glsl
        DEPENDS ${BUILDING_SHADER_DIR}/equirect_to_cubemap.comp.glsl
        COMMENT "Compiling equirect_to_cubemap.comp.glsl -> SPIR-V"
    )

    add_custom_command(
        OUTPUT ${BUILDING_SHADER_DIR}/irradiance_map.comp.spv
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute
                -o ${BUILDING_SHADER_DIR}/irradiance_map.comp.spv
                ${BUILDING_SHADER_DIR}/irradiance_map.comp.glsl
        DEPENDS ${BUILDING_SHADER_DIR}/irradiance_map.comp.glsl
        COMMENT "Compiling irradiance_map.comp.glsl -> SPIR-V"
    )

    add_custom_command(
        OUTPUT ${BUILDING_SHADER_DIR}/prefilter_env.comp.spv
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute
                -o ${BUILDING_SHADER_DIR}/prefilter_env.comp.spv
                ${BUILDING_SHADER_DIR}/prefilter_env.comp.glsl
        DEPENDS ${BUILDING_SHADER_DIR}/prefilter_env.comp.glsl
        COMMENT "Compiling prefilter_env.comp.glsl -> SPIR-V"
    )

    add_custom_target(building_shaders DEPENDS
        ${BUILDING_SHADER_DIR}/building.vert.spv
        ${BUILDING_SHADER_DIR}/building.frag.spv
        ${BUILDING_SHADER_DIR}/shadow.vert.spv
        ${BUILDING_SHADER_DIR}/shadow.frag.spv
        ${BUILDING_SHADER_DIR}/brdf_lut.comp.spv
        ${BUILDING_SHADER_DIR}/equirect_to_cubemap.comp.spv
        ${BUILDING_SHADER_DIR}/irradiance_map.comp.spv
        ${BUILDING_SHADER_DIR}/prefilter_env.comp.spv
    )
    
    add_dependencies(MiniEngine building_shaders)
else()
    # =============================================================================
    # MiniEngine WASM Build (Emscripten/WebGPU)
    # =============================================================================
    add_executable(MiniEngine
        src/main.cpp
        src/Application.cpp
        src/Application.hpp
        src/resources/ResourceManager.cpp
        src/resources/ResourceManager.hpp
        src/rendering/Renderer.cpp
        src/rendering/Renderer.hpp
        src/rendering/RendererBridge.hpp
        src/rendering/RendererBridge.cpp
        src/rendering/BatchRenderer.cpp
        src/rendering/BatchRenderer.hpp
        src/scene/Mesh.cpp
        src/scene/Mesh.hpp
        src/scene/SceneManager.cpp
        src/scene/SceneManager.hpp
        src/scene/Camera.cpp
        src/scene/Camera.hpp
        src/scene/SceneNode.cpp
        src/scene/SceneNode.hpp
        src/scene/SceneGraph.cpp
        src/scene/SceneGraph.hpp
        src/scene/SectorNode.cpp
        src/scene/SectorNode.hpp
        src/scene/AABB.hpp
        src/scene/Quadtree.cpp
        src/scene/Quadtree.hpp
        src/scene/Frustum.hpp
        src/loaders/OBJLoader.cpp
        src/loaders/OBJLoader.hpp
        # Note: ImGui excluded from WASM build (WebGPU backend not yet implemented)
        src/utils/Vertex.hpp
        src/utils/FileUtils.hpp
        # Game Logic Layer
        src/game/entities/BuildingEntity.cpp
        src/game/entities/BuildingEntity.hpp
        src/game/managers/BuildingManager.cpp
        src/game/managers/BuildingManager.hpp
        src/game/managers/WorldManager.cpp
        src/game/managers/WorldManager.hpp
        src/game/world/Sector.hpp
        src/game/sync/PriceUpdate.hpp
        src/game/sync/MockDataGenerator.hpp
        src/game/utils/AnimationUtils.hpp
        src/game/utils/HeightCalculator.hpp
        # Effects Layer
        src/effects/Particle.hpp
        src/effects/ParticleSystem.cpp
        src/effects/ParticleSystem.hpp
        src/effects/ParticleRenderer.cpp
        src/effects/ParticleRenderer.hpp
        # Phase 3.3: Skybox
        src/rendering/SkyboxRenderer.cpp
        src/rendering/SkyboxRenderer.hpp
        # Phase 3.3: Shadow Mapping
        src/rendering/ShadowRenderer.cpp
        src/rendering/ShadowRenderer.hpp
        # Phase 1.2: IBL (Image Based Lighting)
        src/rendering/IBLManager.cpp
        src/rendering/IBLManager.hpp
    )

    target_include_directories(MiniEngine BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(MiniEngine PRIVATE
        glm::glm
        rhi::factory
        stb_image
        tinyobjloader
    )

    target_compile_definitions(MiniEngine PRIVATE
        $<$<BOOL:${RHI_BACKEND_WEBGPU}>:RHI_BACKEND_WEBGPU=1>
    )

    set_target_properties(MiniEngine PROPERTIES SUFFIX ".html")

    # Copy WGSL shaders for WebGPU
    set(MINIENGINE_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${MINIENGINE_SHADER_DIR})

    add_custom_command(
        TARGET MiniEngine PRE_LINK
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MINIENGINE_SHADER_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/building.wgsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.wgsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.wgsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.wgsl
            ${MINIENGINE_SHADER_DIR}/
        COMMENT "Copying WGSL shaders for MiniEngine WASM"
    )

    target_link_options(MiniEngine PRIVATE
        "SHELL:-s USE_WEBGPU=1"
        "SHELL:-s USE_GLFW=3"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s WASM=1"
        "SHELL:-s INITIAL_MEMORY=134217728"
        "SHELL:-s MAXIMUM_MEMORY=4294967296"
        "SHELL:-s STACK_SIZE=16777216"
        "SHELL:-s ASYNCIFY=1"
        "SHELL:-s ASYNCIFY_STACK_SIZE=16384"
        "SHELL:-s ASSERTIONS=1"
        "SHELL:-s ERROR_ON_UNDEFINED_SYMBOLS=0"
        "SHELL:-O2"
        "SHELL:--preload-file ${CMAKE_CURRENT_BINARY_DIR}/shaders@/shaders"
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_shell.html"
    )
    target_compile_options(MiniEngine PRIVATE -O2 -g0 -ffunction-sections -fdata-sections)
endif()

# =============================================================================
# Demo/Test Executables (Optional)
# =============================================================================
if(BUILD_TESTS)
    # Helper function for common test configuration
    function(configure_rhi_test TARGET)
        target_include_directories(${TARGET} BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

        target_compile_definitions(${TARGET} PRIVATE
            IMGUI_IMPL_VULKAN_NO_PROTOTYPES=0
            $<$<BOOL:${RHI_BACKEND_VULKAN}>:RHI_BACKEND_VULKAN=1>
            $<$<BOOL:${RHI_BACKEND_WEBGPU}>:RHI_BACKEND_WEBGPU=1>
        )

        if(NOT EMSCRIPTEN)
            target_include_directories(${TARGET} BEFORE PRIVATE ${Vulkan_INCLUDE_DIR})
            target_link_libraries(${TARGET} PRIVATE
                glfw glm::glm Vulkan::Vulkan
                GPUOpen::VulkanMemoryAllocator Vulkan::cppm
                rhi::factory rhi::vulkan
            )
        else()
            target_link_libraries(${TARGET} PRIVATE rhi::factory)
            set_target_properties(${TARGET} PROPERTIES SUFFIX ".html")
            target_link_options(${TARGET} PRIVATE
                "SHELL:-s USE_WEBGPU=1"
                "SHELL:-s USE_GLFW=3"
                "SHELL:-s ALLOW_MEMORY_GROWTH=1"
                "SHELL:-s WASM=1"
                "SHELL:-s INITIAL_MEMORY=134217728"
                "SHELL:-s MAXIMUM_MEMORY=4294967296"
                "SHELL:-s STACK_SIZE=16777216"
                "SHELL:-Oz"
                "SHELL:-s ERROR_ON_UNDEFINED_SYMBOLS=0"
                "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_shell.html"
            )
            target_compile_options(${TARGET} PRIVATE -Oz -g0 -ffunction-sections -fdata-sections)
        endif()
    endfunction()

    # RHI Smoke Test (interactive, run manually with: make demo-smoke)
    add_executable(rhi_smoke_test
        tests/rhi_smoke_test.cpp
        src/rendering/RendererBridge.hpp
        src/rendering/RendererBridge.cpp
    )
    configure_rhi_test(rhi_smoke_test)

    # Instancing Test (interactive, run manually with: make demo-instancing)
    add_executable(instancing_test
        tests/instancing_test_main.cpp
        src/examples/InstancingTest.cpp
        src/examples/InstancingTest.hpp
        src/rendering/RendererBridge.hpp
        src/rendering/RendererBridge.cpp
    )

    configure_rhi_test(instancing_test)

    # Compile instancing test shaders (GLSL -> SPIR-V)
    if(NOT EMSCRIPTEN)
        find_program(GLSLC_EXECUTABLE glslc HINTS $ENV{VULKAN_SDK}/bin)
        if(GLSLC_EXECUTABLE)
            set(INSTANCING_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
            
            add_custom_command(
                OUTPUT ${INSTANCING_SHADER_DIR}/instancing_test.vert.spv
                COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=vertex
                        -o ${INSTANCING_SHADER_DIR}/instancing_test.vert.spv
                        ${INSTANCING_SHADER_DIR}/instancing_test.vert.glsl
                DEPENDS ${INSTANCING_SHADER_DIR}/instancing_test.vert.glsl
                COMMENT "Compiling instancing_test.vert.glsl -> SPIR-V"
            )
            
            add_custom_command(
                OUTPUT ${INSTANCING_SHADER_DIR}/instancing_test.frag.spv
                COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=fragment
                        -o ${INSTANCING_SHADER_DIR}/instancing_test.frag.spv
                        ${INSTANCING_SHADER_DIR}/instancing_test.frag.glsl
                DEPENDS ${INSTANCING_SHADER_DIR}/instancing_test.frag.glsl
                COMMENT "Compiling instancing_test.frag.glsl -> SPIR-V"
            )
            
            add_custom_target(instancing_shaders DEPENDS
                ${INSTANCING_SHADER_DIR}/instancing_test.vert.spv
                ${INSTANCING_SHADER_DIR}/instancing_test.frag.spv
            )
            
            add_dependencies(instancing_test instancing_shaders)
        endif()
    endif()

    if(EMSCRIPTEN)
        target_link_libraries(instancing_test PRIVATE glm::glm)

        # Copy WGSL shaders for WebGPU
        set(INSTANCING_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
        file(MAKE_DIRECTORY ${INSTANCING_SHADER_DIR})
        file(GLOB WGSL_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.wgsl")

        foreach(SHADER ${WGSL_SHADERS})
            get_filename_component(SHADER_NAME ${SHADER} NAME)
            configure_file(${SHADER} ${INSTANCING_SHADER_DIR}/${SHADER_NAME} COPYONLY)
        endforeach()

        add_custom_command(
            TARGET instancing_test PRE_LINK
            COMMAND ${CMAKE_COMMAND} -E make_directory ${INSTANCING_SHADER_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/instancing_test.vert.wgsl
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/instancing_test.frag.wgsl
                ${INSTANCING_SHADER_DIR}/
            COMMENT "Copying WGSL shaders"
        )

        # Build settings for instancing test
        target_link_options(instancing_test PRIVATE
            "SHELL:-s ASYNCIFY=1"
            "SHELL:-s ASYNCIFY_STACK_SIZE=16384"
            "SHELL:-s ASSERTIONS=1"
            "SHELL:-O2"
            "SHELL:--preload-file ${CMAKE_CURRENT_BINARY_DIR}/shaders@/shaders"
        )
        target_compile_options(instancing_test PRIVATE -O2 -g0 -ffunction-sections -fdata-sections)
    endif()

    message(STATUS "Demo executables enabled: rhi_smoke_test, instancing_test")
else()
    message(STATUS "Demo executables disabled (BUILD_TESTS=OFF)")
endif()