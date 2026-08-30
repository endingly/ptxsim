add_library(ptxsim_common INTERFACE)
add_library(ptxsim::common ALIAS ptxsim_common)
set_target_properties(ptxsim_common PROPERTIES EXPORT_NAME common)
target_compile_features(ptxsim_common INTERFACE cxx_std_23)
register_headers(ptxsim_common common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(ptxsim_common_header_check
    "${CMAKE_CURRENT_BINARY_DIR}/header-check/ids.cpp")
file(GENERATE OUTPUT "${ptxsim_common_header_check}"
     CONTENT "#include <ptxsim/common/ids.hpp>\n")
add_library(ptxsim_common_public_headers OBJECT "${ptxsim_common_header_check}")
target_link_libraries(ptxsim_common_public_headers PRIVATE ptxsim::common)
ptxsim_enable_sanitizers(ptxsim_common_public_headers)
add_custom_target(ptxsim_common_public_header_check ALL
    DEPENDS ptxsim_common_public_headers)

if(BUILD_TESTING)
    add_executable(test_ptxsim_common test/test_ids.cpp)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_common PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::common)
    ptxsim_enable_sanitizers(test_ptxsim_common)
    gtest_discover_tests(test_ptxsim_common)
endif()
