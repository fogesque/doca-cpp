cmake_minimum_required(VERSION 3.22)

# Find errors
find_package(errors CONFIG REQUIRED)

# ======================================================================
# Sample: server
# ======================================================================
set(TARGET_NAME_SERVER ${TARGET_PREFIX}rdma_server)

add_executable(${TARGET_NAME_SERVER} ${CMAKE_CURRENT_LIST_DIR}/server.cpp)

target_link_libraries(${TARGET_NAME_SERVER}
    PRIVATE
        doca-cpp
        errors::errors
)

target_include_directories(${TARGET_NAME_SERVER}
    PRIVATE 
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_SOURCE_DIR}/doca-cpp/include
        ${DOCA_INCLUDE_DIRS} 
)

install(TARGETS ${TARGET_NAME_SERVER} DESTINATION ${CMAKE_BINARY_DIR}/bin/samples)

# ======================================================================
# Sample: client
# ======================================================================
set(TARGET_NAME_CLIENT ${TARGET_PREFIX}rdma_client)

add_executable(${TARGET_NAME_CLIENT} ${CMAKE_CURRENT_LIST_DIR}/client.cpp)

target_link_libraries(${TARGET_NAME_CLIENT}
    PRIVATE
        doca-cpp
        errors::errors
)

target_include_directories(${TARGET_NAME_CLIENT}
    PRIVATE 
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_SOURCE_DIR}/doca-cpp/include
        ${DOCA_INCLUDE_DIRS} 
)

install(TARGETS ${TARGET_NAME_CLIENT} DESTINATION ${CMAKE_BINARY_DIR}/bin/samples)