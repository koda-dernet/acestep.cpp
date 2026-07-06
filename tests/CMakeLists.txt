# test-philox: Philox RNG reference dumper.
add_executable(test-philox test-philox.cpp)
target_include_directories(test-philox PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT MSVC)
    target_link_libraries(test-philox PRIVATE m)
endif()

# test-model-store: exercise ModelStore load/evict policies against real GGUFs.
add_executable(test-model-store test-model-store.cpp)
target_link_libraries(test-model-store PRIVATE acestep-core)
link_ggml_backends(test-model-store)

# test-lm-prompt: byte for byte check of Phase 2 LM prompt builders
# against the Qwen3 chat template reference.
add_executable(test-lm-prompt test-lm-prompt.cpp)
target_link_libraries(test-lm-prompt PRIVATE acestep-core)
link_ggml_backends(test-lm-prompt)

# test-request-solver: request-level solver migration and STORM defaults.
add_executable(test-request-solver test-request-solver.cpp)
target_link_libraries(test-request-solver PRIVATE acestep-core)
link_ggml_backends(test-request-solver)

# test-storm-solver: synthetic STORM solver determinism without model weights.
add_executable(test-storm-solver test-storm-solver.cpp)
target_include_directories(test-storm-solver PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT MSVC)
    target_link_libraries(test-storm-solver PRIVATE m)
endif()
