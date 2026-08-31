file(GLOB_RECURSE ptxsim_state_srcs "src/*.cpp")
add_library(ptxsim_state STATIC ${ptxsim_state_srcs})
add_library(ptxsim::state ALIAS ptxsim_state)
set_target_properties(ptxsim_state PROPERTIES EXPORT_NAME state)
target_compile_features(ptxsim_state PUBLIC cxx_std_23)
target_link_libraries(ptxsim_state PUBLIC ptxsim::common)
register_headers(ptxsim_state state PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_state)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_state_srcs "test/*.cpp")
    add_executable(test_ptxsim_state ${test_ptxsim_state_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_state PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::state
        ptxsim::program)
    target_include_directories(test_ptxsim_state PRIVATE src)
    ptxsim_enable_sanitizers(test_ptxsim_state)
    gtest_discover_tests(test_ptxsim_state)
endif()
