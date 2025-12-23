cmake_minimum_required(VERSION 3.22)
project(doca-cpp-samples)

set(TARGET_PREFIX sample_)

add_subdirectory(device_discovery)
add_subdirectory(rdma_client_server)