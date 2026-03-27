cmake_minimum_required(VERSION 3.20)

# 4-Channel FBLMS Streaming Chain Sample
add_executable(streaming_chain_server server.cpp)
target_link_libraries(streaming_chain_server PRIVATE doca-cpp)
target_compile_features(streaming_chain_server PRIVATE cxx_std_23)

add_executable(streaming_chain_client client.cpp)
target_link_libraries(streaming_chain_client PRIVATE doca-cpp)
target_compile_features(streaming_chain_client PRIVATE cxx_std_23)
