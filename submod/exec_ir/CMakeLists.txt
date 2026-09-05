find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PTXSIM_EXEC_IR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PTXSIM_EXEC_IR_GENERATED_PUBLIC_DIR "${PTXSIM_EXEC_IR_GENERATED_DIR}/public")
set(PTXSIM_EXEC_IR_GENERATED_HEADER
    "${PTXSIM_EXEC_IR_GENERATED_PUBLIC_DIR}/ptxsim/exec_ir/exec_ir.gen.hpp")
set(PTXSIM_EXEC_IR_GENERATED_SOURCE
    "${PTXSIM_EXEC_IR_GENERATED_DIR}/exec_ir.gen.cpp")
file(GLOB_RECURSE PTXSIM_EXEC_IR_CODEGEN_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/python/codegen/*.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/python/instructions/*.yaml")

add_custom_command(
    OUTPUT "${PTXSIM_EXEC_IR_GENERATED_HEADER}"
           "${PTXSIM_EXEC_IR_GENERATED_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}" -m ptxsim_exec_ir_codegen
            --output "${PTXSIM_EXEC_IR_GENERATED_HEADER}"
            --source-output "${PTXSIM_EXEC_IR_GENERATED_SOURCE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/python/setup.cfg"
            ${PTXSIM_EXEC_IR_CODEGEN_FILES}
    COMMENT "Generating PTXSim execution IR"
    VERBATIM)
add_custom_target(ptxsim_exec_ir_codegen DEPENDS
    "${PTXSIM_EXEC_IR_GENERATED_HEADER}" "${PTXSIM_EXEC_IR_GENERATED_SOURCE}")

file(GLOB_RECURSE ptxsim_exec_ir_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_exec_ir
    ${ptxsim_exec_ir_srcs}
    "${PTXSIM_EXEC_IR_GENERATED_SOURCE}")
add_library(ptxsim::exec_ir ALIAS ptxsim_exec_ir)
set_target_properties(ptxsim_exec_ir PROPERTIES EXPORT_NAME exec_ir)
target_compile_features(ptxsim_exec_ir PUBLIC cxx_std_23)
find_package(fmt CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
target_link_libraries(ptxsim_exec_ir
    PUBLIC ptxsim::common
    PRIVATE fmt::fmt magic_enum::magic_enum)
add_dependencies(ptxsim_exec_ir ptxsim_exec_ir_codegen)
target_include_directories(ptxsim_exec_ir PUBLIC
    $<BUILD_INTERFACE:${PTXSIM_EXEC_IR_GENERATED_PUBLIC_DIR}>
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
register_headers(ptxsim_exec_ir exec_ir PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
install(FILES "${PTXSIM_EXEC_IR_GENERATED_HEADER}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ptxsim/exec_ir")
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
