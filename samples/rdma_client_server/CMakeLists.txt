cmake_minimum_required(VERSION 3.22)

# Find errors
find_package(errors CONFIG REQUIRED)

# Find yaml-cpp
find_package(yaml-cpp CONFIG REQUIRED)

# ======================================================================
# Sample: server
# ======================================================================
set(TARGET_NAME_SERVER ${TARGET_PREFIX}rdma_server)

set(SERVER_SOURCES 
    ${CMAKE_SOURCE_DIR}/samples/rdma_client_server/server.cpp 
    ${CMAKE_SOURCE_DIR}/samples/rdma_client_server/service.cpp     
)

add_executable(${TARGET_NAME_SERVER} ${SERVER_SOURCES})

target_link_libraries(${TARGET_NAME_SERVER}
    PRIVATE
        doca-cpp
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

# ======================================================================
# Sample: client
# ======================================================================
set(TARGET_NAME_CLIENT ${TARGET_PREFIX}rdma_client)

set(CLIENT_SOURCES 
    ${CMAKE_SOURCE_DIR}/samples/rdma_client_server/client.cpp 
    ${CMAKE_SOURCE_DIR}/samples/rdma_client_server/service.cpp     
)

add_executable(${TARGET_NAME_CLIENT} ${CLIENT_SOURCES})

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