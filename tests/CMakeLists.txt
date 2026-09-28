cmake_minimum_required(VERSION 3.28)

# Also usable without configuring the renderer, Vulkan SDK or vcpkg.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MiniEngineTests LANGUAGES CXX)
    enable_testing()
    set(_dicom_default OFF)
else()
    set(_dicom_default ON)
endif()

get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
option(MINIENGINE_TEST_DICOM "Test DICOM loading (requires GLM, OpenJPEG, libjpeg-turbo 3.x)" ${_dicom_default})
find_package(Threads REQUIRED)

function(configure_cpu_test target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE "${ENGINE_ROOT}")
    set_target_properties(${target} PROPERTIES
        CXX_SCAN_FOR_MODULES OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "cpu" TIMEOUT 30)
endfunction()

add_executable(threadpool_test threadpool_test.cpp "${ENGINE_ROOT}/src/utils/ThreadPool.cpp")
target_link_libraries(threadpool_test PRIVATE Threads::Threads)
configure_cpu_test(threadpool_test)

add_executable(volume_loader_test volume_loader_test.cpp
    "${ENGINE_ROOT}/src/assets/NiftiFile.cpp"
    "${ENGINE_ROOT}/src/assets/VolumeFile.cpp"
    "${ENGINE_ROOT}/src/utils/MmappedFile.cpp")
configure_cpu_test(volume_loader_test)

if(MINIENGINE_TEST_DICOM)
    if(NOT TARGET glm::glm)
        find_package(glm CONFIG REQUIRED)
    endif()
    if(NOT TARGET openjp2)
        find_package(OpenJPEG CONFIG REQUIRED)
    endif()
    if(NOT TARGET JPEG::JPEG)
        find_package(JPEG REQUIRED)
    endif()
    add_executable(dicom_loader_test dicom_loader_test.cpp "${ENGINE_ROOT}/src/assets/DicomFile.cpp")
    target_link_libraries(dicom_loader_test PRIVATE glm::glm openjp2 JPEG::JPEG)
    configure_cpu_test(dicom_loader_test)
endif()
