add_library(ptxsim_state STATIC
    src/register_file.cpp
    src/thread_state.cpp)
add_library(ptxsim::state ALIAS ptxsim_state)
set_target_properties(ptxsim_state PROPERTIES EXPORT_NAME state)
target_compile_features(ptxsim_state PUBLIC cxx_std_23)
target_link_libraries(ptxsim_state PUBLIC ptxsim::common)
register_headers(ptxsim_state state PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
ptxsim_enable_sanitizers(ptxsim_state)

file(GLOB ptxsim_state_public_headers CONFIGURE_DEPENDS include/*.hpp)
set(ptxsim_state_header_check_sources)
foreach(ptxsim_state_header IN LISTS ptxsim_state_public_headers)
    get_filename_component(ptxsim_state_header_name "${ptxsim_state_header}" NAME)
    string(MAKE_C_IDENTIFIER "${ptxsim_state_header_name}" ptxsim_state_header_id)
    set(ptxsim_state_header_check
        "${CMAKE_CURRENT_BINARY_DIR}/header-check/${ptxsim_state_header_id}.cpp")
    file(GENERATE OUTPUT "${ptxsim_state_header_check}"
         CONTENT "#include <ptxsim/state/${ptxsim_state_header_name}>\n")
    list(APPEND ptxsim_state_header_check_sources "${ptxsim_state_header_check}")
endforeach()
add_library(ptxsim_state_public_headers OBJECT ${ptxsim_state_header_check_sources})
target_link_libraries(ptxsim_state_public_headers PRIVATE ptxsim::state)
ptxsim_enable_sanitizers(ptxsim_state_public_headers)
add_custom_target(ptxsim_state_public_header_check ALL
    DEPENDS ptxsim_state_public_headers)

if(BUILD_TESTING)
    add_executable(test_ptxsim_state
        test/test_register_file.cpp
        test/test_thread_state.cpp)
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
