file(GLOB_RECURSE ptxsim_simulator_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_simulator ${ptxsim_simulator_srcs})
add_library(ptxsim::simulator ALIAS ptxsim_simulator)
set_target_properties(ptxsim_simulator PROPERTIES EXPORT_NAME simulator)
target_compile_features(ptxsim_simulator PUBLIC cxx_std_23)
target_link_libraries(ptxsim_simulator PUBLIC ptxsim::inst_execute_engine)
register_headers(ptxsim_simulator simulator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_simulator)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_simulator_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_ptxsim_simulator ${test_ptxsim_simulator_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_simulator PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::simulator
        ptxsim::exec_ir_lowering)
    ptxsim_enable_sanitizers(test_ptxsim_simulator)
    gtest_discover_tests(test_ptxsim_simulator)
endif()
