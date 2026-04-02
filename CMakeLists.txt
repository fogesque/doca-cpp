cmake_minimum_required(VERSION 3.20)
project(doca-cpp VERSION 0.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TARGET_NAME doca-cpp)

# Add FindDOCA.cmake to module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Find DOCA (system dependency)
find_package(DOCA REQUIRED COMPONENTS common rdma)

# Find Asio
find_package(asio CONFIG REQUIRED)

# Find errors
find_package(errors CONFIG REQUIRED)

# Find kvalog
find_package(kvalog CONFIG REQUIRED)

# Library sources
set(DOCA_CPP_SOURCES
    # Core
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/buffer.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/context.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/device.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/mmap.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/progress_engine.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/resource_scope.cpp
    # RDMA Internal (shared between old and new)
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/internal/rdma_connection.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/internal/rdma_engine.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/internal/rdma_task.cpp
    # RDMA Streaming (new)
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_stream_config.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_buffer_view.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_stream_server.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_stream_client.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_stream_chain.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/internal/rdma_session_manager.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/internal/rdma_buffer_pool.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/internal/rdma_pipeline.cpp
)

# Library target
add_library(${TARGET_NAME}
    ${DOCA_CPP_SOURCES}
)

add_library(${TARGET_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})

# Require C++23 minimum
target_compile_features(doca-cpp INTERFACE cxx_std_23)

target_include_directories(${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/doca-cpp/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${DOCA_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        asio::asio
        errors::errors
        kvalog::kvalog
        DOCA::common
        DOCA::rdma
        # Add other DOCA libs as needed
)

# Enable logging option
option(ENABLE_LOGGING "Enable logging" ON)

if(ENABLE_LOGGING)
    target_compile_definitions(${TARGET_NAME}
        PUBLIC
            DOCA_CPP_ENABLE_LOGGING
    )
endif()

# GPUNetIO module (requires CUDA and DOCA GPUNetIO)
option(BUILD_GPUNETIO "Build GPUNetIO module for GPU RDMA streaming" OFF)

if(BUILD_GPUNETIO)
    enable_language(CUDA)
    find_package(CUDAToolkit 13.0 REQUIRED)

    find_package(DOCA REQUIRED COMPONENTS common gpunetio rdma gpunetio_device)

    # Define DOCA_CPP_ENABLE_GPUNETIO on the main doca-cpp target so headers
    # can conditionally expose GPU-related APIs (e.g. SetDataPathOnGpu)
    target_compile_definitions(${TARGET_NAME}
        PUBLIC
            DOCA_CPP_ENABLE_GPUNETIO
    )

    # CUDA configuration
    set(CUDA_ARCH_LIST 75 80 90)

    if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS "13.0")
        list(PREPEND CUDA_ARCH_LIST 70)
    endif()

    if(CMAKE_CUDA_COMPILER_VERSION VERSION_GREATER_EQUAL "12.8")
        list(APPEND CUDA_ARCH_LIST 100)
    endif()

    set(CMAKE_CUDA_ARCHITECTURES ${CUDA_ARCH_LIST})

    # Add GPU sources to the main doca-cpp target
    target_sources(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_buffer_view.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_buffer_pool.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_device.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_manager.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_memory_region.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_buffer_array.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_rdma_handler.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_rdma_pipeline.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_rdma_server.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/gpu_rdma_client.cpp
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/kernels/server_kernel.cu
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/kernels/client_kernel.cu
    )

    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wno-missing-braces>
    )

    set_target_properties(${TARGET_NAME} PROPERTIES
        CUDA_ARCHITECTURES "${CUDA_ARCH_LIST}" 
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_RESOLVE_DEVICE_SYMBOLS ON
        CUDA_STANDARD 20
    )

    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/kernels/server_kernel.cu
        ${CMAKE_SOURCE_DIR}/doca-cpp/src/gpunetio/kernels/client_kernel.cu
        TARGET_DIRECTORY ${TARGET_NAME}
        PROPERTIES LANGUAGE CUDA
    )

    target_link_libraries(${TARGET_NAME}
        PRIVATE
            DOCA::gpunetio
    )

    target_link_libraries(${TARGET_NAME}
        PUBLIC
            -Wl,--whole-archive
            DOCA::gpunetio_device
            -Wl,--no-whole-archive
    )

    target_link_libraries(${TARGET_NAME}
        PUBLIC
            CUDA::cudart
            CUDA::cuda_driver
    )
endif()

# Build samples and tests (optional, controlled by option)
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_SAMPLES "Build samples" OFF)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

#  Installation 
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS ${TARGET_NAME}
    EXPORT ${TARGET_NAME}-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY doca-cpp/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ${TARGET_NAME}-targets
    FILE ${TARGET_NAME}-targets.cmake
    NAMESPACE ${TARGET_NAME}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME}
)

install(FILES cmake/FindDOCA.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${TARGET_NAME}-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME}
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${TARGET_NAME}
)
