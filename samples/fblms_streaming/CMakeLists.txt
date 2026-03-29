cmake_minimum_required(VERSION 3.22)

find_package(CUDAToolkit REQUIRED)
find_package(errors CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

# ======================================================================
# Sample: CPU clients (4 sensor channels)
# ======================================================================
set(TARGET_NAME_CLIENT ${TARGET_PREFIX}fblms_client)

add_executable(${TARGET_NAME_CLIENT}
    client.cpp
)

target_link_libraries(${TARGET_NAME_CLIENT}
    PRIVATE
        doca-cpp
        errors::errors
        yaml-cpp::yaml-cpp
)

target_include_directories(${TARGET_NAME_CLIENT}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_SOURCE_DIR}/doca-cpp/include
        ${DOCA_INCLUDE_DIRS}
)

install(TARGETS ${TARGET_NAME_CLIENT} DESTINATION ${CMAKE_BINARY_DIR}/bin/samples)

# ======================================================================
# Sample: GPU server (4 channels with StreamChain + FBLMS)
# ======================================================================
set(TARGET_NAME_SERVER ${TARGET_PREFIX}fblms_server)

add_executable(${TARGET_NAME_SERVER}
    server.cpp
)

target_link_libraries(${TARGET_NAME_SERVER}
    PRIVATE
        doca-cpp
        doca-cpp-gpunetio
        CUDA::cudart
        CUDA::cufft
        errors::errors
        yaml-cpp::yaml-cpp
)

target_include_directories(${TARGET_NAME_SERVER}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_SOURCE_DIR}/doca-cpp/include
        ${DOCA_INCLUDE_DIRS}
)

install(TARGETS ${TARGET_NAME_SERVER} DESTINATION ${CMAKE_BINARY_DIR}/bin/samples)
