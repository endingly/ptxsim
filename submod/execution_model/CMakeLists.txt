file(GLOB_RECURSE ptxsim_execution_model_srcs "src/*.cpp")
add_library(ptxsim_execution_model ${ptxsim_execution_model_srcs})
add_library(ptxsim::execution_model ALIAS ptxsim_execution_model)
set_target_properties(ptxsim_execution_model PROPERTIES EXPORT_NAME execution_model)
target_compile_features(ptxsim_execution_model PUBLIC cxx_std_23)
target_link_libraries(ptxsim_execution_model PUBLIC ptxsim::common)
register_headers(ptxsim_execution_model execution_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_execution_model)

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_execution_model_srcs "test/*.cpp")
    add_executable(test_ptxsim_execution_model ${test_ptxsim_execution_model_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
        test_ptxsim_execution_model 
        PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::execution_model)
    ptxsim_enable_sanitizers(test_ptxsim_execution_model)
    gtest_discover_tests(test_ptxsim_execution_model)
endif()
