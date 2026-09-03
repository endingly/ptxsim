find_package(ptx_frontend 0.0.1 CONFIG REQUIRED COMPONENTS codegen ptx_spec)

set(PTXSIM_EXEC_IR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PTXSIM_EXEC_IR_GENERATED_PUBLIC_DIR "${PTXSIM_EXEC_IR_GENERATED_DIR}/public")
set(PTXSIM_EXEC_IR_GENERATED_HEADER
    "${PTXSIM_EXEC_IR_GENERATED_PUBLIC_DIR}/ptxsim/exec_ir/exec_ir.gen.hpp")
set(PTXSIM_EXEC_IR_BACKEND
    "${CMAKE_CURRENT_SOURCE_DIR}/python/instructions/backend.yaml")
file(GLOB_RECURSE PTXSIM_EXEC_IR_CODEGEN_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/python/codegen/*.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/instructions/*.yaml")
file(GLOB_RECURSE PTXSIM_EXEC_IR_FRONTEND_SPEC_FILES CONFIGURE_DEPENDS
    "${ptx_frontend_PTX_SPEC_DIR}/*.yaml")
file(GLOB_RECURSE PTXSIM_EXEC_IR_FRONTEND_CODEGEN_FILES CONFIGURE_DEPENDS
    "${ptx_frontend_CODEGEN_PYTHON_ROOT}/base/*.py"
    "${ptx_frontend_CODEGEN_PYTHON_ROOT}/code_gen/*.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PTXSIM_EXEC_IR_BACKEND}"
    ${PTXSIM_EXEC_IR_CODEGEN_FILES}
    ${PTXSIM_EXEC_IR_FRONTEND_SPEC_FILES}
    ${PTXSIM_EXEC_IR_FRONTEND_CODEGEN_FILES})

add_custom_command(
    OUTPUT "${PTXSIM_EXEC_IR_GENERATED_HEADER}"
    COMMAND "${ptx_frontend_CODEGEN_PYTHON_EXECUTABLE}" -m ptxsim_exec_ir_codegen
            --backend "${PTXSIM_EXEC_IR_BACKEND}"
            --spec-dir "${ptx_frontend_PTX_SPEC_DIR}"
            --output "${PTXSIM_EXEC_IR_GENERATED_HEADER}"
    DEPENDS "${PTXSIM_EXEC_IR_BACKEND}"
            ${PTXSIM_EXEC_IR_CODEGEN_FILES}
            ${PTXSIM_EXEC_IR_FRONTEND_SPEC_FILES}
            ${PTXSIM_EXEC_IR_FRONTEND_CODEGEN_FILES}
    COMMENT "Generating PTXSim execution IR"
    VERBATIM)
add_custom_target(ptxsim_exec_ir_codegen DEPENDS
    "${PTXSIM_EXEC_IR_GENERATED_HEADER}")

file(GLOB_RECURSE ptxsim_exec_ir_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_exec_ir ${ptxsim_exec_ir_srcs})
add_library(ptxsim::exec_ir ALIAS ptxsim_exec_ir)
target_compile_features(ptxsim_exec_ir PUBLIC cxx_std_23)
target_link_libraries(ptxsim_exec_ir PUBLIC ptxsim::common)
add_dependencies(ptxsim_exec_ir ptxsim_exec_ir_codegen)
target_include_directories(ptxsim_exec_ir PUBLIC
    $<BUILD_INTERFACE:${PTXSIM_EXEC_IR_GENERATED_PUBLIC_DIR}>)
register_headers(ptxsim_exec_ir exec_ir PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_exec_ir)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_exec_ir_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_ptxsim_exec_ir ${test_ptxsim_exec_ir_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_exec_ir PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::exec_ir)
    ptxsim_enable_sanitizers(test_ptxsim_exec_ir)
    gtest_discover_tests(test_ptxsim_exec_ir)
endif()
