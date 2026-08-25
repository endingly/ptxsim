include_guard(GLOBAL)

function(register_headers)
    cmake_parse_arguments(
        REGISTER_HEADERS
        ""
        "TARGET;MODULE_NAME"
        "PRIVATE;PUBLIC;INTERFACE"
        ${ARGN}
    )

    set(POSITIONAL_ARGS ${REGISTER_HEADERS_UNPARSED_ARGUMENTS})
    if(NOT REGISTER_HEADERS_TARGET)
        list(POP_FRONT POSITIONAL_ARGS REGISTER_HEADERS_TARGET)
    endif()
    if(NOT REGISTER_HEADERS_MODULE_NAME)
        list(POP_FRONT POSITIONAL_ARGS REGISTER_HEADERS_MODULE_NAME)
    endif()
    if(NOT REGISTER_HEADERS_TARGET OR NOT REGISTER_HEADERS_MODULE_NAME OR
       POSITIONAL_ARGS)
        message(FATAL_ERROR
            "register_headers requires TARGET, MODULE_NAME, and visibility/include-dir pairs")
    endif()

    set(HEADER_INDEX 0)
    foreach(VISIBILITY IN ITEMS PRIVATE PUBLIC INTERFACE)
        foreach(INCLUDE_DIR IN LISTS REGISTER_HEADERS_${VISIBILITY})
            set(MODULE_INCLUDE_ROOT
                "${CMAKE_BINARY_DIR}/include/${REGISTER_HEADERS_MODULE_NAME}/${HEADER_INDEX}")
            set(MODULE_LINK
                "${MODULE_INCLUDE_ROOT}/${PROJECT_NAME}/${REGISTER_HEADERS_MODULE_NAME}")

            file(MAKE_DIRECTORY
                "${MODULE_INCLUDE_ROOT}/${PROJECT_NAME}")
            file(CREATE_LINK
                "${INCLUDE_DIR}"
                "${MODULE_LINK}"
                SYMBOLIC)

            target_include_directories(${REGISTER_HEADERS_TARGET}
                ${VISIBILITY}
                $<BUILD_INTERFACE:${MODULE_INCLUDE_ROOT}>
                $<INSTALL_INTERFACE:include>)

            math(EXPR HEADER_INDEX "${HEADER_INDEX} + 1")
        endforeach()
    endforeach()
endfunction()
