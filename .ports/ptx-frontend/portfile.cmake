vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO endingly/ptx_frontend
    REF fdb5ef575087b530c2cd6db6cb3631cf430a8ce0
    SHA512 039ddd891b0920c3a6b6dfe6703b94058fdf565eb72f02c301913859aa85cc4e7b57f6cfc61d2560dbc1385fd71e347007f3febfa62e57ff2aa81ec45e9644fc
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
