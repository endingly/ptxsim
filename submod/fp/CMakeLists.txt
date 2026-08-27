file(GLOB_RECURSE ptxsim_fp_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_fp ${ptxsim_fp_srcs})
add_library(ptxsim::fp ALIAS ptxsim_fp)
register_headers(ptxsim_fp fp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package(softfloat CONFIG REQUIRED)
target_link_libraries(
    ptxsim_fp
    PRIVATE
    softfloat::softfloat
)

if(BUILD_TESTING)
    file(GLOB ptxsim_fp_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_ptxsim_fp ${ptxsim_fp_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_fp PRIVATE GTest::gtest GTest::gtest_main ptxsim_fp)
    gtest_discover_tests(test_ptxsim_fp)
endif()