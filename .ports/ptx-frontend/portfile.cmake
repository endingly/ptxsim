vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO endingly/ptx_frontend
    REF v0.8.0
    SHA512 0cb40c2301d47633abca8f6ef2de570e1097fcfde6e9f904ad4bd2f84fda61753e7276d19bfc7709c07c6a2ba29ca02407e74fc937241bbd38b49d3ba9e0f824
    HEAD_REF main
)

# Install only the matching source package's production/build dependencies.
# Upstream requirements.txt also requests test runners although tests are off.
x_vcpkg_get_python_packages(
    PYTHON_VERSION 3
    OUT_PYTHON_VAR PYTHON3
    PACKAGES "${SOURCE_PATH}/python"
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
