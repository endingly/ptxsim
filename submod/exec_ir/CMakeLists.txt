add_library(ptxsim_exec_ir INTERFACE)
add_library(ptxsim::exec_ir ALIAS ptxsim_exec_ir)
set_target_properties(ptxsim_exec_ir PROPERTIES EXPORT_NAME exec_ir)
target_compile_features(ptxsim_exec_ir INTERFACE cxx_std_23)
target_link_libraries(ptxsim_exec_ir INTERFACE ptxsim::common)
register_headers(ptxsim_exec_ir exec_ir INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_exec_ir_srcs "test/*.cpp")
    add_executable(test_ptxsim_exec_ir ${test_ptxsim_exec_ir_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_exec_ir PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::exec_ir)
    ptxsim_enable_sanitizers(test_ptxsim_exec_ir)
    gtest_discover_tests(test_ptxsim_exec_ir)
endif()
