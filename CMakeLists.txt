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

# Your library
add_library(${TARGET_NAME}
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/device.cpp
)

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
        DOCA::common
        # Add other DOCA libs as needed
)

# Install rules
install(TARGETS ${TARGET_NAME} EXPORT ${TARGET_NAME}-targets)
install(DIRECTORY doca-cpp/include/ DESTINATION include)
install(FILES cmake/FindDOCA.cmake DESTINATION share/doca-cpp/cmake)

option(BUILD_SAMPLES "Build doca-cpp usage examples" ON)
if(BUILD_SAMPLES)
    add_subdirectory(doca-cpp/samples)
endif()