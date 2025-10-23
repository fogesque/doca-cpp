cmake_minimum_required(VERSION 3.22)
project(device_discovery_sample)

set(TARGET_NAME ${PROJECT_NAME})

# Find errors
find_package(errors CONFIG REQUIRED)

add_executable(${TARGET_NAME} device_discovery.cpp)

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

install(TARGETS ${TARGET_NAME} DESTINATION ${CMAKE_BINARY_DIR}/bin/examples)