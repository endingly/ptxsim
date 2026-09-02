file(GLOB_RECURSE ptxsim_runtime_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_runtime ${ptxsim_runtime_srcs})
add_library(ptxsim::runtime ALIAS ptxsim_runtime)
set_target_properties(ptxsim_runtime PROPERTIES EXPORT_NAME runtime)
target_compile_features(ptxsim_runtime PUBLIC cxx_std_23)
target_link_libraries(ptxsim_runtime PUBLIC ptxsim::execution_model ptxsim::memory)
register_headers(ptxsim_runtime runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_runtime)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_runtime_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_ptxsim_runtime ${test_ptxsim_runtime_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_runtime PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::runtime)
    ptxsim_enable_sanitizers(test_ptxsim_runtime)
    gtest_discover_tests(test_ptxsim_runtime)
endif()
