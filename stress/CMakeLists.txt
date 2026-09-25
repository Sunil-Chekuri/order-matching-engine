# The stress harness is a standalone executable, deliberately not
# registered with ctest: it runs hot for seconds to minutes and checks
# invariants that only fail probabilistically, which is the opposite of
# what belongs in a suite that must be fast and deterministic. Run it by
# hand (or on a schedule in CI) with a round count chosen for the time
# available.
add_executable(engine_stress engine_stress.cpp)
target_link_libraries(engine_stress PRIVATE engine_lib)
