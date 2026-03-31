cmake_minimum_required(VERSION 3.22)
project(doca-cpp-samples)

set(TARGET_PREFIX sample_)

add_subdirectory(device_discovery)
add_subdirectory(cpu_cpu_throughput)

if(BUILD_GPUNETIO)
    add_subdirectory(cpu_gpu_throughput)
    add_subdirectory(gpu_gpu_throughput)
    add_subdirectory(fblms_streaming)
endif()