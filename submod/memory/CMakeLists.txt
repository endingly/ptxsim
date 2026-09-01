file(GLOB_RECURSE ptxsim_memory_srcs "src/*.cpp")
add_library(ptxsim_memory ${ptxsim_memory_srcs})
add_library(ptxsim::memory ALIAS ptxsim_memory)
set_target_properties(ptxsim_memory PROPERTIES EXPORT_NAME memory)
target_compile_features(ptxsim_memory PUBLIC cxx_std_23)
target_link_libraries(ptxsim_memory PUBLIC ptxsim::common)
register_headers(ptxsim_memory memory PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_memory)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_memory_srcs "test/*.cpp")
    add_executable(test_ptxsim_memory ${test_ptxsim_memory_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
        test_ptxsim_memory 
        PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::memory)
    ptxsim_enable_sanitizers(test_ptxsim_memory)
    gtest_discover_tests(test_ptxsim_memory)
endif()
