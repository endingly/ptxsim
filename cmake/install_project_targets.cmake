include_guard(GLOBAL)

# install_project_targets.cmake
#
# Usage:
# install_project_targets(
# PROJECT <proj_name>
# VERSION <major.minor.patch>
# NAMESPACE <namespace>            # e.g. vpdserialize (will create targets vpdserialize::...)
# TARGETS <tgt1> [<tgt2> ...]
# INCLUDE_DIRS <dir1> [<dir2> ...] # relative or absolute header roots
# INCLUDE_DESTINATIONS <dir1> [<dir2> ...] # relative paths below ${CMAKE_INSTALL_INCLUDEDIR}; maps one-to-one with INCLUDE_DIRS
# DEPENDENCIES <dep1> [<dep2> ...] # optional list of find_dependency(...) entries for config
# )
#
# This function:
# - installs given targets and headers
# - exports targets to a .cmake file and installs Config files
# - installs the contents of each include root below an explicitly mapped
#   include destination, matching the include paths exposed by the build-tree
#   target
#
function(install_project_targets)
    include(GNUInstallDirs)

    cmake_parse_arguments(
        INSTALL_PROJECT
        ""
        "PROJECT;VERSION;NAMESPACE"
        "TARGETS;INCLUDE_DIRS;INCLUDE_DESTINATIONS;DEPENDENCIES"
        ${ARGN}
    )

    if(NOT INSTALL_PROJECT_PROJECT)
        message(FATAL_ERROR "install_project_targets: PROJECT argument is required")
    endif()

    if(NOT INSTALL_PROJECT_VERSION)
        message(FATAL_ERROR "install_project_targets: VERSION argument is required")
    endif()

    if(NOT INSTALL_PROJECT_NAMESPACE)
        set(INSTALL_PROJECT_NAMESPACE ${INSTALL_PROJECT_PROJECT})
    else()
        set(INSTALL_PROJECT_NAMESPACE ${INSTALL_PROJECT_NAMESPACE})
    endif()

    if(NOT INSTALL_PROJECT_TARGETS)
        message(FATAL_ERROR "install_project_targets: TARGETS argument is required")
    endif()

    if(NOT INSTALL_PROJECT_INCLUDE_DIRS)
        message(FATAL_ERROR "install_project_targets: INCLUDE_DIRS argument is required")
    endif()

    list(LENGTH INSTALL_PROJECT_INCLUDE_DIRS _include_dir_count)
    list(LENGTH INSTALL_PROJECT_INCLUDE_DESTINATIONS _include_destination_count)
    if(_include_destination_count GREATER 0 AND
       NOT _include_dir_count EQUAL _include_destination_count)
        message(FATAL_ERROR
            "install_project_targets: INCLUDE_DESTINATIONS must map one-to-one to INCLUDE_DIRS")
    endif()
    math(EXPR _last_include_index "${_include_dir_count} - 1")

    set(_install_cmake_dir
        "${CMAKE_INSTALL_LIBDIR}/cmake/${INSTALL_PROJECT_PROJECT}")

    # 1) install targets and export
    install(TARGETS ${INSTALL_PROJECT_TARGETS}
        EXPORT ${INSTALL_PROJECT_PROJECT}Targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    # 2) install headers
    foreach(_include_index RANGE 0 ${_last_include_index})
        list(GET INSTALL_PROJECT_INCLUDE_DIRS ${_include_index} _include_dir)
        if(NOT IS_DIRECTORY "${_include_dir}")
            message(FATAL_ERROR
                "install_project_targets: INCLUDE_DIRS entry is not a directory: ${_include_dir}")
        endif()

        if(_include_destination_count GREATER 0)
            list(GET INSTALL_PROJECT_INCLUDE_DESTINATIONS ${_include_index}
                 _include_destination)
        else()
            set(_include_destination "")
        endif()
        if(IS_ABSOLUTE "${_include_destination}" OR
           _include_destination MATCHES "(^|/)\\.\\.(/|$)")
            message(FATAL_ERROR
                "install_project_targets: INCLUDE_DESTINATIONS entries must be relative paths below the install include directory")
        endif()

        install(DIRECTORY "${_include_dir}/"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_include_destination}"
            FILES_MATCHING
                PATTERN "*.h"
                PATTERN "*.hpp"
                PATTERN "*.inl"
                PATTERN "*.ipp"
                PATTERN "*.def")
    endforeach()

    export(EXPORT ${INSTALL_PROJECT_PROJECT}Targets
        FILE "${CMAKE_CURRENT_BINARY_DIR}/${INSTALL_PROJECT_PROJECT}Targets.cmake"
        NAMESPACE ${INSTALL_PROJECT_NAMESPACE}::
    )

    # 4) configure and write package config files
    include(CMakePackageConfigHelpers)

    set(_config_out "${CMAKE_CURRENT_BINARY_DIR}/${INSTALL_PROJECT_PROJECT}Config.cmake")
    set(_version_out "${CMAKE_CURRENT_BINARY_DIR}/${INSTALL_PROJECT_PROJECT}ConfigVersion.cmake")

    set(PACKAGE_DEPENDENCY_CALLS "")
    foreach(_dependency IN LISTS INSTALL_PROJECT_DEPENDENCIES)
        string(APPEND PACKAGE_DEPENDENCY_CALLS "find_dependency(${_dependency})\n")
    endforeach()

    # configure_package_config_file expects a template; use a project-specific
    # one when present, otherwise create a relocatable generic template.
    set(_template "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${INSTALL_PROJECT_PROJECT}Config.cmake.in")

    if(NOT EXISTS "${_template}")
        file(WRITE "${CMAKE_BINARY_DIR}/${INSTALL_PROJECT_PROJECT}Config.cmake.in" [=[
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
@PACKAGE_DEPENDENCY_CALLS@
include("${CMAKE_CURRENT_LIST_DIR}/@PACKAGE_NAME@Targets.cmake")
set(@PACKAGE_NAME@_VERSION "@PROJECT_VERSION@")
]=])
        set(_template "${CMAKE_BINARY_DIR}/${INSTALL_PROJECT_PROJECT}Config.cmake.in")
    endif()

    # Provide variables for template
    set(PROJECT_VERSION ${INSTALL_PROJECT_VERSION})
    set(PACKAGE_NAME ${INSTALL_PROJECT_PROJECT})
    configure_package_config_file(${_template}
        ${_config_out}
        INSTALL_DESTINATION ${_install_cmake_dir}
        NO_SET_AND_CHECK_MACRO
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

    write_basic_package_version_file(
        "${_version_out}"
        VERSION "${INSTALL_PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    # 5) install the generated config and the exported targets
    install(FILES "${_config_out}" "${_version_out}"
        DESTINATION "${_install_cmake_dir}"
    )

    install(EXPORT ${INSTALL_PROJECT_PROJECT}Targets
        FILE "${INSTALL_PROJECT_PROJECT}Targets.cmake"
        NAMESPACE ${INSTALL_PROJECT_NAMESPACE}::
        DESTINATION "${_install_cmake_dir}"
    )
endfunction()
