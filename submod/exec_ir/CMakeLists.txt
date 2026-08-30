add_library(ptxsim_exec_ir INTERFACE)
add_library(ptxsim::exec_ir ALIAS ptxsim_exec_ir)
set_target_properties(ptxsim_exec_ir PROPERTIES EXPORT_NAME exec_ir)
target_compile_features(ptxsim_exec_ir INTERFACE cxx_std_23)
target_link_libraries(ptxsim_exec_ir INTERFACE ptxsim::common)
register_headers(ptxsim_exec_ir exec_ir INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

file(GLOB ptxsim_exec_ir_public_headers CONFIGURE_DEPENDS include/*.hpp)
set(ptxsim_exec_ir_header_check_sources)
foreach(ptxsim_exec_ir_header IN LISTS ptxsim_exec_ir_public_headers)
    get_filename_component(ptxsim_exec_ir_header_name "${ptxsim_exec_ir_header}" NAME)
    string(MAKE_C_IDENTIFIER "${ptxsim_exec_ir_header_name}" ptxsim_exec_ir_header_id)
    set(ptxsim_exec_ir_header_check
        "${CMAKE_CURRENT_BINARY_DIR}/header-check/${ptxsim_exec_ir_header_id}.cpp")
    file(GENERATE OUTPUT "${ptxsim_exec_ir_header_check}"
         CONTENT "#include <ptxsim/exec_ir/${ptxsim_exec_ir_header_name}>\n")
    list(APPEND ptxsim_exec_ir_header_check_sources "${ptxsim_exec_ir_header_check}")
endforeach()
add_library(ptxsim_exec_ir_public_headers OBJECT ${ptxsim_exec_ir_header_check_sources})
target_link_libraries(ptxsim_exec_ir_public_headers PRIVATE ptxsim::exec_ir)
ptxsim_enable_sanitizers(ptxsim_exec_ir_public_headers)
add_custom_target(ptxsim_exec_ir_public_header_check ALL
    DEPENDS ptxsim_exec_ir_public_headers)

if(BUILD_TESTING)
    add_executable(test_ptxsim_exec_ir
        test/test_instruction.cpp
        test/test_operand.cpp)
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(test_ptxsim_exec_ir PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::exec_ir)
    ptxsim_enable_sanitizers(test_ptxsim_exec_ir)
    gtest_discover_tests(test_ptxsim_exec_ir)
endif()
