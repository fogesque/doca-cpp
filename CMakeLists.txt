cmake_minimum_required(VERSION 3.20)
project(doca-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TARGET_NAME doca-cpp)

# Add FindDOCA.cmake to module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Find DOCA (system dependency)
find_package(DOCA REQUIRED)

# Find errors
find_package(errors CONFIG REQUIRED)

# Find kvalog
find_package(kvalog CONFIG REQUIRED)

# Library sources
set(DOCA_CPP_SOURCES
    # Core
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/device.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/buffer.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/context.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/mmap.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/progress_engine.cpp
    # RDMA
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_server.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_client.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_endpoint.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_executor.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/rdma_buffer.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/internal/rdma_connection.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/internal/rdma_engine.cpp
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/rdma/internal/rdma_task.cpp
)

# Library target
add_library(${TARGET_NAME}
    ${DOCA_CPP_SOURCES}
)

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
        PRIVATE
            DOCA_CPP_ENABLE_LOGGING
    )
endif()

# Build samples and tests (optional, controlled by option)
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_SAMPLES "Build samples" OFF)

if(BUILD_TESTS)
    add_subdirectory(doca-cpp/tests)
endif()

if(BUILD_SAMPLES)
    add_subdirectory(doca-cpp/samples)
endif()
