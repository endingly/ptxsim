vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO endingly/ptx_frontend
    REF 1a32fac33be61c0f540caaecf2ce14373c281b45
    SHA512 416e8922f10daf750b02ca5f3a80bb55d158512c41a31f9fd097d1be02c65e9d77a34bdcc9937f8069d6ce98cc05354156a4fb169a6ad9e603c91c6cde2327e3
    HEAD_REF main
)

# The requirements file's editable path is relative to the source checkout;
# vcpkg installs it from its buildtree instead.
vcpkg_replace_string(
    "${SOURCE_PATH}/requirements.txt"
    "-e ./python"
    "-e ${SOURCE_PATH}/python"
)

x_vcpkg_get_python_packages(
    PYTHON_VERSION 3
    OUT_PYTHON_VAR PYTHON3
    REQUIREMENTS_FILE "${SOURCE_PATH}/requirements.txt"
)

vcpkg_find_acquire_program(FLEX)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTING=OFF
        -DPTX_USE_CCACHE=OFF
        "-DFLEX_EXECUTABLE=${FLEX}"
        "-DPython3_EXECUTABLE=${PYTHON3}"
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME ptx_frontend
    CONFIG_PATH lib/cmake/ptx_frontend
)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
