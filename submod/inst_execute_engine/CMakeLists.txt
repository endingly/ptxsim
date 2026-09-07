find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PTXSIM_ENGINE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PTXSIM_ENGINE_DISPATCH "${PTXSIM_ENGINE_GENERATED_DIR}/engine_dispatch.gen.inc")
set(PTXSIM_ENGINE_COVERAGE "${PTXSIM_ENGINE_GENERATED_DIR}/engine_coverage.json")
set(PTXSIM_ENGINE_DEPFILE "${PTXSIM_ENGINE_GENERATED_DIR}/engine_dispatch.d")
set(PTXSIM_ENGINE_BINDINGS "${CMAKE_CURRENT_SOURCE_DIR}/instructions/bindings.yaml")
file(GLOB_RECURSE PTXSIM_ENGINE_CODEGEN_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../exec_ir/python/codegen/*.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../exec_ir/python/instructions/*.yaml")

# The depfile also tracks the installed frontend specification and Python code.
add_custom_command(
    OUTPUT "${PTXSIM_ENGINE_DISPATCH}" "${PTXSIM_ENGINE_COVERAGE}"
    COMMAND "${Python3_EXECUTABLE}" -m ptxsim_exec_ir_codegen.gen_engine
            --bindings "${PTXSIM_ENGINE_BINDINGS}"
            --output "${PTXSIM_ENGINE_DISPATCH}"
            --coverage "${PTXSIM_ENGINE_COVERAGE}"
            --depfile "${PTXSIM_ENGINE_DEPFILE}"
    DEPENDS "${PTXSIM_ENGINE_BINDINGS}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../exec_ir/python/setup.cfg"
            ${PTXSIM_ENGINE_CODEGEN_FILES}
    DEPFILE "${PTXSIM_ENGINE_DEPFILE}"
    COMMENT "Generating PTXSim instruction execution wiring"
    VERBATIM)
add_custom_target(ptxsim_engine_codegen
    DEPENDS "${PTXSIM_ENGINE_DISPATCH}" "${PTXSIM_ENGINE_COVERAGE}")

file(GLOB_RECURSE ptxsim_inst_execute_engine_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_inst_execute_engine ${ptxsim_inst_execute_engine_srcs}
            "${PTXSIM_ENGINE_DISPATCH}")
add_library(ptxsim::inst_execute_engine ALIAS ptxsim_inst_execute_engine)
set_target_properties(ptxsim_inst_execute_engine PROPERTIES EXPORT_NAME inst_execute_engine)
target_compile_features(ptxsim_inst_execute_engine PUBLIC cxx_std_23)
target_link_libraries(ptxsim_inst_execute_engine PUBLIC ptxsim::arith ptxsim::exec_ir ptxsim::runtime)
add_dependencies(ptxsim_inst_execute_engine ptxsim_engine_codegen)
target_include_directories(ptxsim_inst_execute_engine PRIVATE "${PTXSIM_ENGINE_GENERATED_DIR}")
register_headers(ptxsim_inst_execute_engine inst_execute_engine PUBLIC
                 "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_inst_execute_engine)

if(BUILD_TESTING)
    add_test(NAME ptxsim_engine_codegen
        COMMAND "${CMAKE_COMMAND}" -E env
                "PTXSIM_ENGINE_BINDINGS=${PTXSIM_ENGINE_BINDINGS}"
                "${Python3_EXECUTABLE}" -m unittest ptxsim_exec_ir_codegen.test_engine)
    file(GLOB_RECURSE test_ptxsim_inst_execute_engine_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_ptxsim_inst_execute_engine
                   ${test_ptxsim_inst_execute_engine_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_inst_execute_engine PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::inst_execute_engine)
    ptxsim_enable_sanitizers(test_ptxsim_inst_execute_engine)
    gtest_discover_tests(test_ptxsim_inst_execute_engine)
endif()
