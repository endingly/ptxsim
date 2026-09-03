file(GLOB_RECURSE ptxsim_exec_ir_lowering_srcs CONFIGURE_DEPENDS src/*.cpp)
find_package(ptx_frontend 0.0.1 CONFIG REQUIRED COMPONENTS resolved_ir)
add_library(ptxsim_exec_ir_lowering ${ptxsim_exec_ir_lowering_srcs})
add_library(ptxsim::exec_ir_lowering ALIAS ptxsim_exec_ir_lowering)
target_compile_features(ptxsim_exec_ir_lowering PUBLIC cxx_std_23)
target_link_libraries(ptxsim_exec_ir_lowering PUBLIC
    ptxsim::exec_ir
    ptx_frontend::resolved_ir)
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
