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

# Needed for std::thread on Linux; MinGW links pthreads by default.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Off locally so a compiler upgrade can't block a build; CI turns it on.
option(ENGINE_WERROR "Treat compiler warnings as errors" OFF)

set(ENGINE_WARNING_FLAGS -Wall -Wextra)

# Plain -Wshadow only flags the constructor param/member idiom here, so
# use the narrower local form. GCC-only spelling.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND ENGINE_WARNING_FLAGS -Wshadow=local)
endif()

if(ENGINE_WERROR)
    list(APPEND ENGINE_WARNING_FLAGS -Werror)
endif()

# All first-party targets go through here so none gets forgotten.
function(engine_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)

        if(ENGINE_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE ${ENGINE_WARNING_FLAGS})
    endif()
endfunction()

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
engine_set_warnings(engine_lib)

# Winsock on Windows; POSIX has sockets in libc. PUBLIC so consumers
# inherit it.
if(WIN32)
    target_link_libraries(engine_lib PUBLIC ws2_32)
endif()

target_link_libraries(engine_lib PUBLIC Threads::Threads)

add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE engine_lib)
engine_set_warnings(engine)

enable_testing()
add_subdirectory(tests)

add_subdirectory(benchmarks)

add_subdirectory(stress)

add_subdirectory(cli)
