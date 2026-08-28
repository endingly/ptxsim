if(NOT DEFINED PTXSIM_CONSUMER_MODE OR
   NOT PTXSIM_CONSUMER_MODE MATCHES "^(build-tree|installed)$")
    message(FATAL_ERROR "PTXSIM_CONSUMER_MODE must be build-tree or installed")
endif()

set(ptxsim_consumer_source "${PTXSIM_SOURCE_DIR}/submod/arith/test/consumer")
set(ptxsim_consumer_build "${PTXSIM_CONSUMER_BINARY_DIR}")
file(REMOVE_RECURSE "${ptxsim_consumer_build}")

if(PTXSIM_CONSUMER_MODE STREQUAL "installed")
    set(ptxsim_install_prefix "${ptxsim_consumer_build}/prefix")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${PTXSIM_BUILD_DIR}"
                --prefix "${ptxsim_install_prefix}"
        RESULT_VARIABLE ptxsim_install_result
        OUTPUT_VARIABLE ptxsim_install_output
        ERROR_VARIABLE ptxsim_install_error)
    if(NOT ptxsim_install_result EQUAL 0)
        message(FATAL_ERROR "ptxsim install failed:\n${ptxsim_install_output}\n${ptxsim_install_error}")
    endif()
    set(ptxsim_package_dir
        "${ptxsim_install_prefix}/${PTXSIM_INSTALL_LIBDIR}/cmake/ptxsim")
else()
    set(ptxsim_package_dir "${PTXSIM_PACKAGE_BUILD_DIR}")
endif()

set(ptxsim_consumer_args
    -S "${ptxsim_consumer_source}"
    -B "${ptxsim_consumer_build}/build"
    -G "${PTXSIM_GENERATOR}"
    "-Dptxsim_DIR=${ptxsim_package_dir}"
    "-Dsoftfloat_DIR=${PTXSIM_SOFTFLOAT_DIR}"
    "-DCMAKE_CXX_COMPILER=${PTXSIM_CXX_COMPILER}")
if(PTXSIM_MAKE_PROGRAM)
    list(APPEND ptxsim_consumer_args
        "-DCMAKE_MAKE_PROGRAM=${PTXSIM_MAKE_PROGRAM}")
endif()
if(PTXSIM_CONSUMER_ENABLE_SANITIZERS)
    list(APPEND ptxsim_consumer_args -DPTXSIM_CONSUMER_ENABLE_SANITIZERS=ON)
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${ptxsim_consumer_args}
    RESULT_VARIABLE ptxsim_configure_result
    OUTPUT_VARIABLE ptxsim_configure_output
    ERROR_VARIABLE ptxsim_configure_error)
if(NOT ptxsim_configure_result EQUAL 0)
    message(FATAL_ERROR "external consumer configure failed:\n${ptxsim_configure_output}\n${ptxsim_configure_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${ptxsim_consumer_build}/build"
    RESULT_VARIABLE ptxsim_build_result
    OUTPUT_VARIABLE ptxsim_build_output
    ERROR_VARIABLE ptxsim_build_error)
if(NOT ptxsim_build_result EQUAL 0)
    message(FATAL_ERROR "external consumer build failed:\n${ptxsim_build_output}\n${ptxsim_build_error}")
endif()

execute_process(
    COMMAND "${ptxsim_consumer_build}/build/ptxsim_arith_external_consumer"
    RESULT_VARIABLE ptxsim_run_result
    OUTPUT_VARIABLE ptxsim_run_output
    ERROR_VARIABLE ptxsim_run_error)
if(NOT ptxsim_run_result EQUAL 0)
    message(FATAL_ERROR "external consumer run failed:\n${ptxsim_run_output}\n${ptxsim_run_error}")
endif()
