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

# Library sources
set(DOCA_CPP_SOURCES
    ${CMAKE_SOURCE_DIR}/doca-cpp/src/core/device.cpp
)

# Library target
add_library(${TARGET_NAME}
    ${DOCA_CPP_SOURCES}
)

# Require C++23 minimum
target_compile_features(errors INTERFACE cxx_std_23)

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
include(GNUInstallDirs)
install(TARGETS ${TARGET_NAME} 
    EXPORT ${TARGET_NAME}-targets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install headers and FindDOCA.cmake
install(DIRECTORY doca-cpp/include/ DESTINATION include)
install(FILES cmake/FindDOCA.cmake DESTINATION share/doca-cpp/cmake)

# Install CMake config files
install(EXPORT errorsTargets
    FILE ${TARGET_NAME}-targets.cmake
    NAMESPACE doca-cpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/doca-cpp
)

# Create and install Config files
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/doca-cppConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/doca-cppConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/doca-cppConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/doca-cpp
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/doca-cppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/doca-cppConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/doca-cpp
)


# Build samples and tests (optional, controlled by option)
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_SAMPLES "Build samples" OFF)

if(BUILD_TESTS)
    add_subdirectory(doca-cpp/tests)
endif()

if(BUILD_SAMPLES)
    add_subdirectory(doca-cpp/samples)
endif()
