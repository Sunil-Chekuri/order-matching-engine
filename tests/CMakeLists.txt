include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

file(GLOB TEST_SOURCES *.cpp)

add_executable(engine_tests ${TEST_SOURCES})
target_link_libraries(engine_tests PRIVATE engine_lib gtest_main)

include(GoogleTest)
gtest_discover_tests(engine_tests)
