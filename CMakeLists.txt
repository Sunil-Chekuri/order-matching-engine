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

# Instrument for gcov. Kept off the fetched dependencies so their code
# does not land in the coverage report.
option(ENGINE_COVERAGE "Build with gcov instrumentation" OFF)

# Semicolon-separated: address, undefined, thread. address and thread are
# mutually exclusive. Unavailable on MinGW, which ships no sanitizer
# runtimes -- these are for the Linux CI jobs.
set(ENGINE_SANITIZE "" CACHE STRING "Sanitizers to enable")

# All first-party targets go through here so none gets forgotten.
function(engine_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)

        if(ENGINE_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE ${ENGINE_WARNING_FLAGS})
    endif()

    if(ENGINE_COVERAGE AND NOT MSVC)
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        target_link_options(${target} PRIVATE --coverage)
    endif()

    if(ENGINE_SANITIZE AND NOT MSVC)
        string(REPLACE ";" "," sanitize_list "${ENGINE_SANITIZE}")

        # Frame pointers and -g are what make a sanitizer report name a
        # line rather than an address.
        target_compile_options(${target} PRIVATE
            -fsanitize=${sanitize_list} -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=${sanitize_list})
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
engine_configure_target(engine_lib)

# Winsock on Windows; POSIX has sockets in libc. PUBLIC so consumers
# inherit it.
if(WIN32)
    target_link_libraries(engine_lib PUBLIC ws2_32)
endif()

target_link_libraries(engine_lib PUBLIC Threads::Threads)

add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE engine_lib)
engine_configure_target(engine)

enable_testing()
add_subdirectory(tests)

add_subdirectory(benchmarks)

add_subdirectory(stress)

add_subdirectory(cli)
