# A client, not part of the engine, so it gets its own executable.
add_executable(book_view depth_cli.cpp)
target_link_libraries(book_view PRIVATE engine_lib)
engine_set_warnings(book_view)
