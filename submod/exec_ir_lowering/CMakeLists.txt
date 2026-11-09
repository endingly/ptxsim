find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PTXSIM_EXEC_IR_LOWERING_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PTXSIM_EXEC_IR_LOWERING_GENERATED_HEADER
    "${PTXSIM_EXEC_IR_LOWERING_GENERATED_DIR}/exec_ir_lowering.gen.hpp")
set(PTXSIM_EXEC_IR_LOWERING_GENERATED_SOURCE
    "${PTXSIM_EXEC_IR_LOWERING_GENERATED_DIR}/exec_ir_lowering.gen.cpp")
file(GLOB_RECURSE PTXSIM_EXEC_IR_LOWERING_CODEGEN_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/model/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/frontend/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/resources/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/resources/*.yaml"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/code_gen/lowering/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/binding/*.py")
list(APPEND PTXSIM_EXEC_IR_LOWERING_CODEGEN_FILES
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/__init__.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/artifacts.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/code_gen/__init__.py")
add_custom_command(
    OUTPUT "${PTXSIM_EXEC_IR_LOWERING_GENERATED_HEADER}"
           "${PTXSIM_EXEC_IR_LOWERING_GENERATED_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}" -m ptxsim_codegen.code_gen.lowering
            --output
            "${PTXSIM_EXEC_IR_LOWERING_GENERATED_HEADER}"
            --source-output
            "${PTXSIM_EXEC_IR_LOWERING_GENERATED_SOURCE}"
    DEPENDS "${PROJECT_SOURCE_DIR}/python/pyproject.toml"
            ${PTXSIM_EXEC_IR_LOWERING_CODEGEN_FILES}
    COMMENT "Generating PTXSim execution IR lowering"
    VERBATIM)
add_custom_target(ptxsim_exec_ir_lowering_codegen DEPENDS
    "${PTXSIM_EXEC_IR_LOWERING_GENERATED_HEADER}"
    "${PTXSIM_EXEC_IR_LOWERING_GENERATED_SOURCE}")

file(GLOB_RECURSE ptxsim_exec_ir_lowering_srcs CONFIGURE_DEPENDS src/*.cpp)
find_package(ptx_frontend 0.0.1 CONFIG REQUIRED COMPONENTS resolved_ir)
add_library(ptxsim_exec_ir_lowering
    ${ptxsim_exec_ir_lowering_srcs}
    "${PTXSIM_EXEC_IR_LOWERING_GENERATED_SOURCE}")
add_library(ptxsim::exec_ir_lowering ALIAS ptxsim_exec_ir_lowering)
set_target_properties(ptxsim_exec_ir_lowering PROPERTIES EXPORT_NAME exec_ir_lowering)
target_compile_features(ptxsim_exec_ir_lowering PUBLIC cxx_std_23)
target_link_libraries(ptxsim_exec_ir_lowering PUBLIC
    ptxsim::exec_ir
    ptx_frontend::resolved_ir)
add_dependencies(ptxsim_exec_ir_lowering ptxsim_exec_ir_lowering_codegen)
target_include_directories(ptxsim_exec_ir_lowering PRIVATE
    "${PTXSIM_EXEC_IR_LOWERING_GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
register_headers(ptxsim_exec_ir_lowering exec_ir_lowering PUBLIC
                 "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_exec_ir_lowering)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_exec_ir_lowering_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_ptxsim_exec_ir_lowering ${test_ptxsim_exec_ir_lowering_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_exec_ir_lowering PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::exec_ir_lowering)
    ptxsim_enable_sanitizers(test_ptxsim_exec_ir_lowering)
    gtest_discover_tests(test_ptxsim_exec_ir_lowering)
endif()
