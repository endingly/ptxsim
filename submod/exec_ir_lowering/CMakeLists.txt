find_package(Python3 3.14 REQUIRED COMPONENTS Interpreter)
set(PTXSIM_EXEC_IR_LOWERING_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GLOB_RECURSE PTXSIM_EXEC_IR_LOWERING_CODEGEN_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/model/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/frontend/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/resources/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/code_gen/lowering/*.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/binding/*.py")
list(APPEND PTXSIM_EXEC_IR_LOWERING_CODEGEN_FILES
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/resources/backend.yaml"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/__init__.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/artifacts.py"
    "${PROJECT_SOURCE_DIR}/python/src/ptxsim_codegen/code_gen/__init__.py")
set(PTXSIM_EXEC_IR_LOWERING_PLAN "${CMAKE_CURRENT_BINARY_DIR}/lowering_artifacts.cmake")
include(plan_codegen)
ptxsim_plan_codegen(ptxsim_codegen.code_gen.lowering
    "${PTXSIM_EXEC_IR_LOWERING_GENERATED_DIR}" "${PTXSIM_EXEC_IR_LOWERING_PLAN}"
    "${PROJECT_SOURCE_DIR}/python/pyproject.toml"
    ${PTXSIM_EXEC_IR_LOWERING_CODEGEN_FILES})
include("${PTXSIM_EXEC_IR_LOWERING_PLAN}")
add_custom_command(
    OUTPUT ${PTXSIM_EXEC_IR_LOWERING_GENERATED_HEADERS}
           ${PTXSIM_EXEC_IR_LOWERING_GENERATED_SOURCES}
    BYPRODUCTS "${PTXSIM_EXEC_IR_LOWERING_GENERATED_DIR}/artifact_plan.json"
    COMMAND "${Python3_EXECUTABLE}" -m ptxsim_codegen.code_gen.lowering
            --output-dir "${PTXSIM_EXEC_IR_LOWERING_GENERATED_DIR}"
    DEPENDS "${PROJECT_SOURCE_DIR}/python/pyproject.toml"
            ${PTXSIM_EXEC_IR_LOWERING_CODEGEN_FILES}
    COMMENT "Generating PTXSim execution IR lowering"
    VERBATIM)
add_custom_target(ptxsim_exec_ir_lowering_codegen DEPENDS
    ${PTXSIM_EXEC_IR_LOWERING_GENERATED_HEADERS}
    ${PTXSIM_EXEC_IR_LOWERING_GENERATED_SOURCES})

file(GLOB_RECURSE ptxsim_exec_ir_lowering_srcs CONFIGURE_DEPENDS src/*.cpp)
find_package(ptx_frontend 0.8.0 EXACT CONFIG REQUIRED COMPONENTS resolved_ir)
add_library(ptxsim_exec_ir_lowering
    ${ptxsim_exec_ir_lowering_srcs}
    ${PTXSIM_EXEC_IR_LOWERING_GENERATED_SOURCES})
add_library(ptxsim::exec_ir_lowering ALIAS ptxsim_exec_ir_lowering)
set_target_properties(ptxsim_exec_ir_lowering PROPERTIES EXPORT_NAME exec_ir_lowering)
target_compile_features(ptxsim_exec_ir_lowering PUBLIC cxx_std_23)
target_link_libraries(ptxsim_exec_ir_lowering PUBLIC
    ptxsim::exec_ir
    ptx_frontend::resolved_ir)
if(BUILD_SHARED_LIBS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   PTXSIM_DEFAULT_INSTALL_RPATH AND CMAKE_INSTALL_RPATH_USE_LINK_PATH)
    foreach(dependency IN ITEMS ptx_frontend::resolved_ir fmt::fmt)
        get_target_property(ptxsim_dependency_type ${dependency} TYPE)
        if(ptxsim_dependency_type STREQUAL "SHARED_LIBRARY")
            # These managed dependencies may live below the source/build tree.
            set_property(TARGET ptxsim_exec_ir_lowering APPEND PROPERTY INSTALL_RPATH
                "$<TARGET_FILE_DIR:${dependency}>")
        endif()
    endforeach()
endif()
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
