find_package(Python3 3.14 REQUIRED COMPONENTS Interpreter)

set(PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GLOB_RECURSE PTXSIM_INST_EXECUTE_ENGINE_CODEGEN_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/model/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/frontend/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/resources/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/code_gen/engine/*.py")
list(APPEND PTXSIM_INST_EXECUTE_ENGINE_CODEGEN_FILES
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/resources/backend.yaml"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/resources/execution.yaml"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/__init__.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/artifacts.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/code_gen/__init__.py")
set(PTXSIM_INST_EXECUTE_ENGINE_PLAN "${CMAKE_CURRENT_BINARY_DIR}/engine_artifacts.cmake")
include(plan_codegen)
ptxsim_plan_codegen(ptxsim_codegen.code_gen.engine
    "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR}" "${PTXSIM_INST_EXECUTE_ENGINE_PLAN}"
    "${PROJECT_SOURCE_DIR}/python/pyproject.toml"
    ${PTXSIM_INST_EXECUTE_ENGINE_CODEGEN_FILES})
include("${PTXSIM_INST_EXECUTE_ENGINE_PLAN}")
add_custom_command(
    OUTPUT ${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_HEADERS}
           ${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_SOURCES}
    BYPRODUCTS "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR}/artifact_plan.json"
    COMMAND "${Python3_EXECUTABLE}" -m ptxsim_codegen.code_gen.engine
            --output-dir "${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR}"
    DEPENDS "${PROJECT_SOURCE_DIR}/python/pyproject.toml"
            ${PTXSIM_INST_EXECUTE_ENGINE_CODEGEN_FILES}
    COMMENT "Generating dense typed PTXSim issue groups"
    VERBATIM)
add_custom_target(ptxsim_inst_execute_engine_codegen DEPENDS
    ${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_HEADERS}
    ${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_SOURCES})

file(GLOB_RECURSE ptxsim_inst_execute_engine_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_inst_execute_engine
    ${ptxsim_inst_execute_engine_srcs}
    ${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_SOURCES})
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
                     "-DPTXSIM_GENERATED_HEADER=${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR}/instruction_preparation.gen.hpp"
                     "-DPTXSIM_GENERATED_PLAN=${PTXSIM_INST_EXECUTE_ENGINE_PLAN}"
                     "-DPTXSIM_GENERATOR=${CMAKE_GENERATOR}"
                     "-DPTXSIM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                     "-DPTXSIM_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                     "-DPTXSIM_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}"
                     "-DPTXSIM_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
                     "-DPTXSIM_BUILD_CONFIG=$<CONFIG>"
                     "-DPTXSIM_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
                     "-DPTXSIM_CHAINLOAD_TOOLCHAIN_FILE=${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}"
                     "-DPTXSIM_ENABLE_ASAN=${PTXSIM_ENABLE_ASAN}"
                     "-DPTXSIM_ENABLE_UBSAN=${PTXSIM_ENABLE_UBSAN}"
                     "-DPTXSIM_SOFTFLOAT_DIR=${softfloat_DIR}"
                     "-DPTXSIM_DYNAMIC_BITSET_DIR=${sul-dynamic_bitset_DIR}"
                     "-DPTXSIM_FMT_DIR=${fmt_DIR}"
                     "-DPTXSIM_MAGIC_ENUM_DIR=${magic_enum_DIR}"
                     "-DPTXSIM_PTX_FRONTEND_DIR=${ptx_frontend_DIR}"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/run_link_contract.cmake")
    add_test(NAME InstExecuteEngineMissingHandlerLinkContract
             COMMAND "${CMAKE_COMMAND}"
                     "-DPTXSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                     "-DPTXSIM_PACKAGE_BUILD_DIR=${PROJECT_BINARY_DIR}"
                     "-DPTXSIM_CONSUMER_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/missing-handler-contract"
                     "-DPTXSIM_LINK_CONTRACT_MODE=handler"
                     "-DPTXSIM_PYTHON_EXECUTABLE=${Python3_EXECUTABLE}"
                     "-DPTXSIM_GENERATED_HEADER=${PTXSIM_INST_EXECUTE_ENGINE_GENERATED_DIR}/instruction_preparation.gen.hpp"
                     "-DPTXSIM_GENERATED_PLAN=${PTXSIM_INST_EXECUTE_ENGINE_PLAN}"
                     "-DPTXSIM_GENERATOR=${CMAKE_GENERATOR}"
                     "-DPTXSIM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                     "-DPTXSIM_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                     "-DPTXSIM_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}"
                     "-DPTXSIM_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
                     "-DPTXSIM_BUILD_CONFIG=$<CONFIG>"
                     "-DPTXSIM_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
                     "-DPTXSIM_CHAINLOAD_TOOLCHAIN_FILE=${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}"
                     "-DPTXSIM_ENABLE_ASAN=${PTXSIM_ENABLE_ASAN}"
                     "-DPTXSIM_ENABLE_UBSAN=${PTXSIM_ENABLE_UBSAN}"
                     "-DPTXSIM_SOFTFLOAT_DIR=${softfloat_DIR}"
                     "-DPTXSIM_DYNAMIC_BITSET_DIR=${sul-dynamic_bitset_DIR}"
                     "-DPTXSIM_FMT_DIR=${fmt_DIR}"
                     "-DPTXSIM_MAGIC_ENUM_DIR=${magic_enum_DIR}"
                     "-DPTXSIM_PTX_FRONTEND_DIR=${ptx_frontend_DIR}"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/run_link_contract.cmake")
    set_tests_properties(InstExecuteEngineSemanticsLinkContract
                         InstExecuteEngineMissingHandlerLinkContract PROPERTIES
                         RUN_SERIAL TRUE)
endif()
