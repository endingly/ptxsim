vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO endingly/ptx_frontend
    REF 233bec4d8e979d05003e83d2102eb3a35dafe6da
    SHA512 c879f1f6640c21d1db086077cd35a799a28b174c22d9888ae2d2f0ba09cecb1032d69f422509309072d2ad1b61f43e49b67fecbd9537ad1865d8df0669160135
    HEAD_REF dev
)

# The requirements file's editable path is relative to the source checkout;
# vcpkg installs it from its buildtree instead.
vcpkg_replace_string(
    "${SOURCE_PATH}/requirements.txt"
    "-e ./python"
    "-e ${SOURCE_PATH}/python"
)

# fmt 12 keeps fmt::format in format.h rather than core.h.
vcpkg_replace_string(
    "${SOURCE_PATH}/submod/resolved_ir/include/ptx_resolved_ir.hpp"
    "#include <fmt/core.h>"
    "#include <fmt/format.h>"
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
