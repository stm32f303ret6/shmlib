cmake_minimum_required(VERSION 3.10)
project(shmlib C)

add_library(shmlib INTERFACE)
target_include_directories(shmlib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(shmlib INTERFACE rt)
