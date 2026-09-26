# The depth viewer is a client, not part of the engine, so it gets its
# own executable rather than another mode on engine.exe. It links
# engine_lib only for TcpClient and the view helpers in src/cli.
add_executable(book_view depth_cli.cpp)
target_link_libraries(book_view PRIVATE engine_lib)
