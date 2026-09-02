include(CheckCXXSourceCompiles)
# The v0.1 arithmetic contract is Linux with GCC or Clang. __int128 is used
# only for exact integer widening; fail configuration rather than selecting a
# host-dependent fallback with different overflow behavior.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR
        "ptxsim::arith v0.1 requires GCC or Clang on Linux; its exact integer backend requires __int128")
endif()
check_cxx_source_compiles(
    "int main() { __int128 value = 1; return static_cast<int>(value - 1); }"
    PTXSIM_ARITH_HAS_INT128)
if(NOT PTXSIM_ARITH_HAS_INT128)
    message(FATAL_ERROR "ptxsim::arith requires compiler support for __int128")
endif()

file(GLOB_RECURSE ptxsim_arith_srcs CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM ptxsim_arith_srcs "${CMAKE_CURRENT_SOURCE_DIR}/src/validation.cpp")
add_library(ptxsim_arith ${ptxsim_arith_srcs})
add_library(ptxsim::arith ALIAS ptxsim_arith)
set_target_properties(ptxsim_arith PROPERTIES EXPORT_NAME arith)
target_compile_features(ptxsim_arith PUBLIC cxx_std_23)
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
target_link_libraries(ptxsim_arith PRIVATE softfloat::softfloat)
target_compile_definitions(ptxsim_arith PRIVATE PTXSIM_SOFTFLOAT_TLS_CONFIGURED=1)
ptxsim_enable_sanitizers(ptxsim_arith)

# Host floating-point tolerance helpers are diagnostic-only. Keeping them in
# a distinct opt-in target prevents production users of ptxsim::arith from
# linking that code accidentally.
add_library(ptxsim_arith_validation src/validation.cpp)
add_library(ptxsim::arith_validation ALIAS ptxsim_arith_validation)
set_target_properties(ptxsim_arith_validation PROPERTIES EXPORT_NAME arith_validation)
target_link_libraries(ptxsim_arith_validation PUBLIC ptxsim_arith)
ptxsim_enable_sanitizers(ptxsim_arith_validation)

if(BUILD_TESTING)
    file(GLOB ptxsim_arith_test_srcs CONFIGURE_DEPENDS test/*.cpp)
    foreach(ptxsim_arith_test_source IN LISTS ptxsim_arith_test_srcs)
        file(READ "${ptxsim_arith_test_source}" ptxsim_arith_test_contents)
        string(FIND "${ptxsim_arith_test_contents}" "detail::"
            ptxsim_arith_detail_test_use)
        if(NOT ptxsim_arith_detail_test_use EQUAL -1)
            message(FATAL_ERROR
                "Public arithmetic tests must use the supported API, not detail:: (${ptxsim_arith_test_source})")
        endif()
    endforeach()
    add_executable(test_ptxsim_arith ${ptxsim_arith_test_srcs})
    find_package(GTest CONFIG REQUIRED)
    target_link_libraries(
        test_ptxsim_arith
        PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::arith)
    ptxsim_enable_sanitizers(test_ptxsim_arith)
    gtest_discover_tests(test_ptxsim_arith)

    add_executable(test_ptxsim_arith_internal
        test/internal/test_softfloat_context.cpp)
    target_include_directories(test_ptxsim_arith_internal PRIVATE src)
    target_link_libraries(test_ptxsim_arith_internal PRIVATE
        GTest::gtest
        GTest::gtest_main
        ptxsim::arith
        softfloat::softfloat)
    ptxsim_enable_sanitizers(test_ptxsim_arith_internal)
    gtest_discover_tests(test_ptxsim_arith_internal)

    if(PTXSIM_BUILD_CONSUMER_TESTS)
        foreach(ptxsim_arith_consumer_mode IN ITEMS build-tree installed)
            add_test(
                NAME ptxsim_arith_${ptxsim_arith_consumer_mode}_consumer
                COMMAND "${CMAKE_COMMAND}"
                    "-DPTXSIM_CONSUMER_MODE=${ptxsim_arith_consumer_mode}"
                    "-DPTXSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                    "-DPTXSIM_BUILD_DIR=${PROJECT_BINARY_DIR}"
                    "-DPTXSIM_PACKAGE_BUILD_DIR=${PROJECT_BINARY_DIR}"
                    "-DPTXSIM_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
                    "-DPTXSIM_CONSUMER_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-${ptxsim_arith_consumer_mode}"
                    "-DPTXSIM_GENERATOR=${CMAKE_GENERATOR}"
                    "-DPTXSIM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                    "-DPTXSIM_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                    "-DPTXSIM_SOFTFLOAT_DIR=${softfloat_DIR}"
                    "-DPTXSIM_DYNAMIC_BITSET_DIR=${sul-dynamic_bitset_DIR}"
                    "-DPTXSIM_CONSUMER_ENABLE_SANITIZERS=${PTXSIM_SANITIZER_FLAGS}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/test/run_consumer.cmake")
        endforeach()
    endif()
endif()
