add_library(ptxsim_bootstrap STATIC src/thread_bootstrap.cpp)
add_library(ptxsim::bootstrap ALIAS ptxsim_bootstrap)
set_target_properties(ptxsim_bootstrap PROPERTIES EXPORT_NAME bootstrap)
target_compile_features(ptxsim_bootstrap PUBLIC cxx_std_23)
target_link_libraries(ptxsim_bootstrap PUBLIC ptxsim::program ptxsim::state)
register_headers(ptxsim_bootstrap bootstrap PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_bootstrap)

file(GLOB ptxsim_bootstrap_public_headers CONFIGURE_DEPENDS include/*.hpp)
set(ptxsim_bootstrap_header_check_sources)
foreach(ptxsim_bootstrap_header IN LISTS ptxsim_bootstrap_public_headers)
    get_filename_component(ptxsim_bootstrap_header_name
        "${ptxsim_bootstrap_header}" NAME)
    string(MAKE_C_IDENTIFIER "${ptxsim_bootstrap_header_name}"
        ptxsim_bootstrap_header_id)
    set(ptxsim_bootstrap_header_check
        "${CMAKE_CURRENT_BINARY_DIR}/header-check/${ptxsim_bootstrap_header_id}.cpp")
    file(GENERATE OUTPUT "${ptxsim_bootstrap_header_check}"
         CONTENT "#include <ptxsim/bootstrap/${ptxsim_bootstrap_header_name}>\n")
    list(APPEND ptxsim_bootstrap_header_check_sources
        "${ptxsim_bootstrap_header_check}")
endforeach()
add_library(ptxsim_bootstrap_public_headers OBJECT
    ${ptxsim_bootstrap_header_check_sources})
target_link_libraries(ptxsim_bootstrap_public_headers PRIVATE ptxsim::bootstrap)
ptxsim_enable_sanitizers(ptxsim_bootstrap_public_headers)
add_custom_target(ptxsim_bootstrap_public_header_check ALL
    DEPENDS ptxsim_bootstrap_public_headers)

if(BUILD_TESTING)
    add_executable(test_ptxsim_bootstrap test/test_thread_bootstrap.cpp)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_bootstrap PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::bootstrap)
    ptxsim_enable_sanitizers(test_ptxsim_bootstrap)
    gtest_discover_tests(test_ptxsim_bootstrap)
endif()
