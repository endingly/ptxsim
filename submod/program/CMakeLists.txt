add_library(ptxsim_program STATIC src/program_image.cpp)
add_library(ptxsim::program ALIAS ptxsim_program)
set_target_properties(ptxsim_program PROPERTIES EXPORT_NAME program)
target_compile_features(ptxsim_program PUBLIC cxx_std_23)
target_link_libraries(ptxsim_program PUBLIC ptxsim::common ptxsim::exec_ir)
register_headers(ptxsim_program program PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

file(GLOB ptxsim_program_public_headers CONFIGURE_DEPENDS include/*.hpp)
set(ptxsim_program_header_check_sources)
foreach(ptxsim_program_header IN LISTS ptxsim_program_public_headers)
    get_filename_component(ptxsim_program_header_name "${ptxsim_program_header}" NAME)
    string(MAKE_C_IDENTIFIER "${ptxsim_program_header_name}" ptxsim_program_header_id)
    set(ptxsim_program_header_check
        "${CMAKE_CURRENT_BINARY_DIR}/header-check/${ptxsim_program_header_id}.cpp")
    file(GENERATE OUTPUT "${ptxsim_program_header_check}"
         CONTENT "#include <ptxsim/program/${ptxsim_program_header_name}>\n")
    list(APPEND ptxsim_program_header_check_sources "${ptxsim_program_header_check}")
endforeach()
add_library(ptxsim_program_public_headers OBJECT ${ptxsim_program_header_check_sources})
target_link_libraries(ptxsim_program_public_headers PRIVATE ptxsim::program)
ptxsim_enable_sanitizers(ptxsim_program_public_headers)
add_custom_target(ptxsim_program_public_header_check ALL
    DEPENDS ptxsim_program_public_headers)

if(BUILD_TESTING)
    add_executable(test_ptxsim_program test/test_program_image.cpp)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_program PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::program)
    ptxsim_enable_sanitizers(test_ptxsim_program)
    gtest_discover_tests(test_ptxsim_program)
endif()
