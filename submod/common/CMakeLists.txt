add_library(ptxsim_common INTERFACE)
add_library(ptxsim::common ALIAS ptxsim_common)
set_target_properties(ptxsim_common PROPERTIES EXPORT_NAME common)
target_compile_features(ptxsim_common INTERFACE cxx_std_23)
register_headers(ptxsim_common common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(sul-dynamic_bitset CONFIG REQUIRED)
target_link_libraries(ptxsim_common INTERFACE sul::dynamic_bitset)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_common_srcs "test/*.cpp")
    add_executable(test_ptxsim_common ${test_ptxsim_common_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
        test_ptxsim_common 
        PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::common
    )
    ptxsim_enable_sanitizers(test_ptxsim_common)
    gtest_discover_tests(test_ptxsim_common)
endif()
