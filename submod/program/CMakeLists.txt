file(GLOB_RECURSE ptxsim_program_srcs "src/*.cpp")
add_library(ptxsim_program STATIC ${ptxsim_program_srcs})
add_library(ptxsim::program ALIAS ptxsim_program)
set_target_properties(ptxsim_program PROPERTIES EXPORT_NAME program)
target_compile_features(ptxsim_program PUBLIC cxx_std_23)
target_link_libraries(ptxsim_program PUBLIC ptxsim::common ptxsim::exec_ir)
register_headers(ptxsim_program program PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(BUILD_TESTING)
    file(GLOB_RECURSE test_ptxsim_program_srcs "test/*.cpp")
    add_executable(test_ptxsim_program ${test_ptxsim_program_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_program PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::program)
    ptxsim_enable_sanitizers(test_ptxsim_program)
    gtest_discover_tests(test_ptxsim_program)
endif()
