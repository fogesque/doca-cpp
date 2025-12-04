cmake_minimum_required(VERSION 3.22)
project(doca-cpp-samples)

set(TARGET_PREFIX sample_)

# Find errors
find_package(errors CONFIG REQUIRED)

# ======================================================================
# Sample: device_discovery
# ======================================================================
set(TARGET_NAME ${TARGET_PREFIX}device_discovery)

add_executable(${TARGET_NAME} ${CMAKE_CURRENT_LIST_DIR}/device_discovery.cpp)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        doca-cpp
        errors::errors
)

target_include_directories(${TARGET_NAME}
    PRIVATE 
        ${CMAKE_SOURCE_DIR}/doca-cpp/include
        ${DOCA_INCLUDE_DIRS} 
)

install(TARGETS ${TARGET_NAME} DESTINATION ${CMAKE_BINARY_DIR}/bin/samples)