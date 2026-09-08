set(ptxsim_link_contract_source
    "${PTXSIM_SOURCE_DIR}/submod/inst_execute_engine/test/link_contract")
set(ptxsim_link_contract_build "${PTXSIM_CONSUMER_BINARY_DIR}/build")

foreach(ptxsim_link_contract_file IN ITEMS
        "${PTXSIM_GENERATED_HEADER}" "${PTXSIM_GENERATED_SOURCE}")
    if(NOT EXISTS "${ptxsim_link_contract_file}")
        message(FATAL_ERROR
            "generated execution preparation is unavailable: ${ptxsim_link_contract_file}")
    endif()
endforeach()

set(ptxsim_link_contract_args
    -S "${ptxsim_link_contract_source}"
    -B "${ptxsim_link_contract_build}"
    -G "${PTXSIM_GENERATOR}"
    "-Dptxsim_DIR=${PTXSIM_PACKAGE_BUILD_DIR}"
    "-Dsoftfloat_DIR=${PTXSIM_SOFTFLOAT_DIR}"
    "-Dsul-dynamic_bitset_DIR=${PTXSIM_DYNAMIC_BITSET_DIR}"
    "-Dfmt_DIR=${PTXSIM_FMT_DIR}"
    "-Dmagic_enum_DIR=${PTXSIM_MAGIC_ENUM_DIR}"
    "-Dptx_frontend_DIR=${PTXSIM_PTX_FRONTEND_DIR}"
    "-DCMAKE_CXX_COMPILER=${PTXSIM_CXX_COMPILER}"
    "-DPTXSIM_SOURCE_DIR=${PTXSIM_SOURCE_DIR}"
    "-DPTXSIM_GENERATED_HEADER=${PTXSIM_GENERATED_HEADER}"
    "-DPTXSIM_GENERATED_SOURCE=${PTXSIM_GENERATED_SOURCE}"
    "-DPTXSIM_ENABLE_ASAN=${PTXSIM_ENABLE_ASAN}"
    "-DPTXSIM_ENABLE_UBSAN=${PTXSIM_ENABLE_UBSAN}")
if(PTXSIM_MAKE_PROGRAM)
    list(APPEND ptxsim_link_contract_args
        "-DCMAKE_MAKE_PROGRAM=${PTXSIM_MAKE_PROGRAM}")
endif()
if(PTXSIM_BUILD_TYPE)
    list(APPEND ptxsim_link_contract_args
        "-DCMAKE_BUILD_TYPE=${PTXSIM_BUILD_TYPE}")
endif()
if(PTXSIM_TOOLCHAIN_FILE)
    list(APPEND ptxsim_link_contract_args
        "-DCMAKE_TOOLCHAIN_FILE=${PTXSIM_TOOLCHAIN_FILE}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${ptxsim_link_contract_args}
            "-DCMAKE_CXX_COMPILER_LAUNCHER=${PTXSIM_CXX_COMPILER_LAUNCHER}"
    RESULT_VARIABLE ptxsim_configure_result
    OUTPUT_VARIABLE ptxsim_configure_output
    ERROR_VARIABLE ptxsim_configure_error)
if(NOT ptxsim_configure_result EQUAL 0)
    message(FATAL_ERROR
        "link contract configure failed:\n${ptxsim_configure_output}\n${ptxsim_configure_error}")
endif()

set(ptxsim_positive_command "${CMAKE_COMMAND}" --build
    "${ptxsim_link_contract_build}" --target
    ptxsim_inst_execute_engine_link_contract_complete --parallel 2)
if(PTXSIM_BUILD_CONFIG)
    list(APPEND ptxsim_positive_command --config "${PTXSIM_BUILD_CONFIG}")
endif()
execute_process(
    COMMAND ${ptxsim_positive_command}
    RESULT_VARIABLE ptxsim_positive_result
    OUTPUT_VARIABLE ptxsim_positive_output
    ERROR_VARIABLE ptxsim_positive_error)
if(NOT ptxsim_positive_result EQUAL 0)
    message(FATAL_ERROR
        "link-contract positive control failed:\n${ptxsim_positive_output}\n${ptxsim_positive_error}")
endif()

set(ptxsim_negative_command "${CMAKE_COMMAND}" --build
    "${ptxsim_link_contract_build}" --target
    ptxsim_inst_execute_engine_link_contract_missing_add --parallel 2)
if(PTXSIM_BUILD_CONFIG)
    list(APPEND ptxsim_negative_command --config "${PTXSIM_BUILD_CONFIG}")
endif()
execute_process(
    COMMAND ${ptxsim_negative_command}
    RESULT_VARIABLE ptxsim_negative_result
    OUTPUT_VARIABLE ptxsim_negative_output
    ERROR_VARIABLE ptxsim_negative_error)
if(ptxsim_negative_result EQUAL 0)
    message(FATAL_ERROR "missing Add semantics unexpectedly linked")
endif()

set(ptxsim_negative_log
    "${ptxsim_negative_output}\n${ptxsim_negative_error}")
if(NOT ptxsim_negative_log MATCHES
       "undefined reference|unresolved external|undefined symbol")
    message(FATAL_ERROR
        "missing Add semantics failed before link:\n${ptxsim_negative_log}")
endif()
if(NOT ptxsim_negative_log MATCHES "semantics::add")
    message(FATAL_ERROR
        "link failure did not name generated semantics::add:\n${ptxsim_negative_log}")
endif()
