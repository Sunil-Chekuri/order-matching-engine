cmake_minimum_required(VERSION 3.14)

project(OrderMatchingEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

file(GLOB_RECURSE LIB_SOURCES
    src/core/*.cpp
    src/engine/*.cpp
    src/api/*.cpp
    src/utils/*.cpp
)

add_library(engine_lib ${LIB_SOURCES})
target_include_directories(engine_lib PUBLIC include)

add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE engine_lib)

enable_testing()
add_subdirectory(tests)
