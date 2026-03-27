cmake_minimum_required(VERSION 3.20)

# CPU-CPU Streaming Sample
add_executable(streaming_server server.cpp)
target_link_libraries(streaming_server PRIVATE doca-cpp)
target_compile_features(streaming_server PRIVATE cxx_std_23)

add_executable(streaming_client client.cpp)
target_link_libraries(streaming_client PRIVATE doca-cpp)
target_compile_features(streaming_client PRIVATE cxx_std_23)
