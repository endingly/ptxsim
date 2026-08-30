find_package(ptx_frontend CONFIG QUIET)

if(ptx_frontend_FOUND)
    add_library(ptxsim_lowering STATIC
        src/context.cpp
        src/diagnostic.cpp
        src/lowering.cpp)
    add_library(ptxsim::lowering ALIAS ptxsim_lowering)
    set_target_properties(ptxsim_lowering PROPERTIES EXPORT_NAME lowering)
    target_compile_features(ptxsim_lowering PUBLIC cxx_std_23)
    target_link_libraries(ptxsim_lowering
        PUBLIC ptxsim::common
               ptxsim::program
               ptx_frontend::resolved_ir)
    register_headers(ptxsim_lowering lowering PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    ptxsim_enable_sanitizers(ptxsim_lowering)

    file(GLOB ptxsim_lowering_public_headers CONFIGURE_DEPENDS include/*.hpp)
    set(ptxsim_lowering_header_check_sources)
    foreach(ptxsim_lowering_header IN LISTS ptxsim_lowering_public_headers)
        get_filename_component(ptxsim_lowering_header_name
            "${ptxsim_lowering_header}" NAME)
        string(MAKE_C_IDENTIFIER "${ptxsim_lowering_header_name}"
            ptxsim_lowering_header_id)
        set(ptxsim_lowering_header_check
            "${CMAKE_CURRENT_BINARY_DIR}/header-check/${ptxsim_lowering_header_id}.cpp")
        file(GENERATE OUTPUT "${ptxsim_lowering_header_check}"
             CONTENT "#include <ptxsim/lowering/${ptxsim_lowering_header_name}>\n")
        list(APPEND ptxsim_lowering_header_check_sources
            "${ptxsim_lowering_header_check}")
    endforeach()
    add_library(ptxsim_lowering_public_headers OBJECT
        ${ptxsim_lowering_header_check_sources})
    target_link_libraries(ptxsim_lowering_public_headers PRIVATE ptxsim::lowering)
    ptxsim_enable_sanitizers(ptxsim_lowering_public_headers)
    add_custom_target(ptxsim_lowering_public_header_check ALL
        DEPENDS ptxsim_lowering_public_headers)

    if(BUILD_TESTING)
        add_executable(test_ptxsim_lowering
            test/test_context.cpp
            test/test_diagnostic.cpp
            test/test_lowering.cpp)
        find_package(GTest CONFIG REQUIRED)
        target_link_libraries(test_ptxsim_lowering PRIVATE
            GTest::gtest
            GTest::gtest_main
            ptxsim::lowering
            ptxsim::state)
        ptxsim_enable_sanitizers(test_ptxsim_lowering)
        gtest_discover_tests(test_ptxsim_lowering)

        add_executable(test_ptxsim_frontend_lowering_smoke
            test/frontend_lowering_smoke.cpp)
        target_link_libraries(test_ptxsim_frontend_lowering_smoke
            PRIVATE ptxsim::lowering)
        ptxsim_enable_sanitizers(test_ptxsim_frontend_lowering_smoke)
        add_test(NAME ptxsim_frontend_lowering_smoke
            COMMAND test_ptxsim_frontend_lowering_smoke)

        if(PTXSIM_BUILD_CONSUMER_TESTS)
            foreach(ptxsim_lowering_consumer_mode IN ITEMS build-tree installed)
                add_test(
                    NAME ptxsim_lowering_${ptxsim_lowering_consumer_mode}_consumer
                    COMMAND "${CMAKE_COMMAND}"
                        "-DPTXSIM_CONSUMER_MODE=${ptxsim_lowering_consumer_mode}"
                        "-DPTXSIM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                        "-DPTXSIM_BUILD_DIR=${PROJECT_BINARY_DIR}"
                        "-DPTXSIM_PACKAGE_BUILD_DIR=${PROJECT_BINARY_DIR}"
                        "-DPTXSIM_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
                        "-DPTXSIM_CONSUMER_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-${ptxsim_lowering_consumer_mode}"
                        "-DPTXSIM_GENERATOR=${CMAKE_GENERATOR}"
                        "-DPTXSIM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                        "-DPTXSIM_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                        "-DPTXSIM_PTX_FRONTEND_DIR=${ptx_frontend_DIR}"
                        "-DPTXSIM_VCPKG_PREFIX=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/test/run_consumer.cmake")
            endforeach()
        endif()
    endif()
endif()
