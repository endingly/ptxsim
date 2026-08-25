include_guard(GLOBAL)

# install_project_targets.cmake
#
# Usage:
# install_project_targets(
# PROJECT <proj_name>
# VERSION <major.minor.patch>
# NAMESPACE <namespace>            # e.g. vpdserialize (will create targets vpdserialize::...)
# TARGETS <tgt1> [<tgt2> ...]
# INCLUDE_DIRS <dir1> [<dir2> ...] # relative or absolute paths to headers; defaults to ${CMAKE_CURRENT_SOURCE_DIR}/include
# DEPENDENCIES <dep1> [<dep2> ...] # optional list of find_dependency(...) entries for config
# )
#
# This function:
# - installs given targets and headers
# - exports targets to a .cmake file and installs Config files
# - installs the contents of each include root under ${prefix}/include, matching
#   the include paths exposed by the build-tree target
#
function(install_project_targets)
    cmake_parse_arguments(
        INSTALL_PROJECT
        ""
        "PROJECT;VERSION;NAMESPACE"
        "TARGETS;INCLUDE_DIRS;DEPENDENCIES"
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

    set(_install_cmake_dir "lib/cmake/${INSTALL_PROJECT_PROJECT}")

    # 1) install targets and export
    install(TARGETS ${INSTALL_PROJECT_TARGETS}
        EXPORT ${INSTALL_PROJECT_PROJECT}Targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )

    # 2) install headers
    # INCLUDE_DIRS are include roots, not directories to preserve as another
    # path component.  Installing their contents directly keeps build-tree and
    # install-tree includes identical (for example <ptx_frontend/base/base.hpp>).
    foreach(_inc ${INSTALL_PROJECT_INCLUDE_DIRS})
        install(DIRECTORY ${_inc}/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl" PATTERN "*.ipp" PATTERN "*.def")
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

    if(NOT EXISTS ${_template})
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
        ${_version_out}
        VERSION ${INSTALL_PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    # 5) install the generated config and the exported targets
    install(FILES ${_config_out} ${_version_out}
        DESTINATION ${_install_cmake_dir}
    )

    install(EXPORT ${INSTALL_PROJECT_PROJECT}Targets
        FILE ${INSTALL_PROJECT_PROJECT}Targets.cmake
        NAMESPACE ${INSTALL_PROJECT_NAMESPACE}::
        DESTINATION ${_install_cmake_dir}
    )
endfunction()
