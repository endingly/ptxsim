file(GLOB_RECURSE ptxsim_arith_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(ptxsim_arith ${ptxsim_arith_srcs})
add_library(ptxsim::arith ALIAS ptxsim_arith)
register_headers(ptxsim_arith arith PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package(softfloat CONFIG REQUIRED)
get_target_property(ptxsim_softfloat_definitions softfloat::softfloat
                    INTERFACE_COMPILE_DEFINITIONS)
set(ptxsim_softfloat_tls_configured FALSE)
foreach(ptxsim_softfloat_definition IN LISTS ptxsim_softfloat_definitions)
    if(ptxsim_softfloat_definition STREQUAL "THREAD_LOCAL=thread_local" OR
       ptxsim_softfloat_definition MATCHES "THREAD_LOCAL=thread_local")
        set(ptxsim_softfloat_tls_configured TRUE)
    endif()
endforeach()
if(NOT ptxsim_softfloat_tls_configured)
    message(FATAL_ERROR "softfloat::softfloat must export THREAD_LOCAL=thread_local")
endif()
target_link_libraries(
    ptxsim_arith
    PRIVATE
    softfloat::softfloat
)
target_compile_definitions(ptxsim_arith PRIVATE PTXSIM_SOFTFLOAT_TLS_CONFIGURED=1)

if(BUILD_TESTING)
    file(GLOB ptxsim_arith_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    add_executable(test_ptxsim_arith ${ptxsim_arith_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
        test_ptxsim_arith
        PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim_arith
        softfloat::softfloat
    )
    gtest_discover_tests(test_ptxsim_arith)
endif()
