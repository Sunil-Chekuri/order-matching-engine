cmake_minimum_required(VERSION 3.14)

project(OrderMatchingEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without this CMake leaves the build type empty, which means no
# optimisation flags are passed at all — every performance number this
# project reports would describe an unoptimised build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include_directories(include)

file(GLOB_RECURSE LIB_SOURCES
    src/core/*.cpp
    src/engine/*.cpp
    src/api/*.cpp
    src/utils/*.cpp
    src/persistence/*.cpp
    src/net/*.cpp
    src/cli/*.cpp
)

add_library(engine_lib ${LIB_SOURCES})
target_include_directories(engine_lib PUBLIC include)

# The TCP front end needs the platform socket library. On Windows that
# is Winsock (ws2_32); POSIX has sockets in libc, so nothing to add.
# Kept PUBLIC so every consumer of engine_lib links it transitively
# rather than each target rediscovering the dependency.
if(WIN32)
    target_link_libraries(engine_lib PUBLIC ws2_32)
endif()

add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE engine_lib)

enable_testing()
add_subdirectory(tests)

add_subdirectory(benchmarks)

add_subdirectory(stress)

add_subdirectory(cli)
