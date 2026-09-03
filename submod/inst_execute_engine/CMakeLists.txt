file(GLOB_RECURSE ptxsim_inst_execute_engine_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_inst_execute_engine ${ptxsim_inst_execute_engine_srcs})
add_library(ptxsim::inst_execute_engine ALIAS ptxsim_inst_execute_engine)
target_compile_features(ptxsim_inst_execute_engine PUBLIC cxx_std_23)
target_link_libraries(ptxsim_inst_execute_engine PUBLIC ptxsim::arith ptxsim::exec_ir ptxsim::runtime)
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
endif()
