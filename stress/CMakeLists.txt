# Not registered with ctest on purpose: it runs hot and its invariants
# fail probabilistically. Run it by hand or from CI.
add_executable(engine_stress engine_stress.cpp)
target_link_libraries(engine_stress PRIVATE engine_lib)
engine_set_warnings(engine_stress)
