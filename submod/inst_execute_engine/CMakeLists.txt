find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PTXSIM_INST_EXECUTE_ENGINE_GENERATED_HEADER
    "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR}/instruction_preparation.gen.hpp")
set(PTXSIM_INST_EXECUTE_ENGINE_GENERATED_SOURCE
    "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR}/instruction_preparation.gen.cpp")
file(GLOB_RECURSE PTXSIM_INST_EXECUTE_ENGINE_CODEGEN_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/python/ptxsim_codegen/*.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/codegen/*.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/codegen/templates/*.j2"
    "${PROJECT_SOURCE_DIR}/submod/exec_ir/python/codegen/*.py"
    "${PROJECT_SOURCE_DIR}/submod/exec_ir/python/instructions/*.py"
    "${PROJECT_SOURCE_DIR}/submod/exec_ir/python/instructions/*.yaml")
add_custom_command(
    OUTPUT "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_HEADER}"
           "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}" -m ptxsim_codegen.inst_execute_engine
            --output "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_HEADER}"
            --source-output "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_SOURCE}"
    DEPENDS "${PROJECT_SOURCE_DIR}/setup.cfg"
            "${PROJECT_SOURCE_DIR}/pyproject.toml"
            ${PTXSIM_INST_EXECUTE_ENGINE_CODEGEN_FILES}
    COMMENT "Generating PTXSim ValueALU execution preparation"
    VERBATIM)
add_custom_target(ptxsim_inst_execute_engine_codegen DEPENDS
    "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_HEADER}"
    "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_SOURCE}")

file(GLOB_RECURSE ptxsim_inst_execute_engine_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_inst_execute_engine
    ${ptxsim_inst_execute_engine_srcs}
    "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_SOURCE}")
add_library(ptxsim::inst_execute_engine ALIAS ptxsim_inst_execute_engine)
set_target_properties(ptxsim_inst_execute_engine PROPERTIES EXPORT_NAME inst_execute_engine)
target_compile_features(ptxsim_inst_execute_engine PUBLIC cxx_std_23)
target_link_libraries(ptxsim_inst_execute_engine PUBLIC ptxsim::arith ptxsim::exec_ir ptxsim::runtime)
add_dependencies(ptxsim_inst_execute_engine ptxsim_inst_execute_engine_codegen)
target_include_directories(ptxsim_inst_execute_engine PRIVATE
    "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
register_headers(ptxsim_inst_execute_engine inst_execute_engine PUBLIC
                 "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_inst_execute_engine)

if(BUILD_TESTING)
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

    add_test(NAME InstExecuteEngineSemanticsLinkContract
             COMMAND "${CMAKE_COMMAND}"
                     "-DPTXSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                     "-DPTXSIM_PACKAGE_BUILD_DIR=${PROJECT_BINARY_DIR}"
                     "-DPTXSIM_CONSUMER_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/link-contract"
                     "-DPTXSIM_GENERATED_HEADER=${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_HEADER}"
                     "-DPTXSIM_GENERATED_SOURCE=${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_SOURCE}"
                     "-DPTXSIM_GENERATOR=${CMAKE_GENERATOR}"
                     "-DPTXSIM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                     "-DPTXSIM_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                     "-DPTXSIM_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
                     "-DPTXSIM_BUILD_CONFIG=$<CONFIG>"
                     "-DPTXSIM_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
                     "-DPTXSIM_ENABLE_ASAN=${PTXSIM_ENABLE_ASAN}"
                     "-DPTXSIM_ENABLE_UBSAN=${PTXSIM_ENABLE_UBSAN}"
                     "-DPTXSIM_SOFTFLOAT_DIR=${softfloat_DIR}"
                     "-DPTXSIM_DYNAMIC_BITSET_DIR=${sul-dynamic_bitset_DIR}"
                     "-DPTXSIM_FMT_DIR=${fmt_DIR}"
                     "-DPTXSIM_MAGIC_ENUM_DIR=${magic_enum_DIR}"
                     "-DPTXSIM_PTX_FRONTEND_DIR=${ptx_frontend_DIR}"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/run_link_contract.cmake")
    set_tests_properties(InstExecuteEngineSemanticsLinkContract PROPERTIES
                         RUN_SERIAL TRUE)
endif()
