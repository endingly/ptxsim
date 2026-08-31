file(GLOB_RECURSE ptxsim_bootstrap_srcs "src/*.cpp")
add_library(ptxsim_bootstrap STATIC ${ptxsim_bootstrap_srcs})
add_library(ptxsim::bootstrap ALIAS ptxsim_bootstrap)
set_target_properties(ptxsim_bootstrap PROPERTIES EXPORT_NAME bootstrap)
target_compile_features(ptxsim_bootstrap PUBLIC cxx_std_23)
target_link_libraries(ptxsim_bootstrap PUBLIC ptxsim::program ptxsim::state)
register_headers(ptxsim_bootstrap bootstrap PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_bootstrap)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_bootstrap_srcs "test/*.cpp")
    add_executable(test_ptxsim_bootstrap ${test_ptxsim_bootstrap_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_bootstrap PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::bootstrap
    )
    ptxsim_enable_sanitizers(test_ptxsim_bootstrap)
    gtest_discover_tests(test_ptxsim_bootstrap)
endif()
