vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO endingly/ptx_frontend
    REF 992fc36527e1ffe2d1b3dd2a07de2b6d721e7898
    SHA512 f87122efa035d3540ff0d5a56daa4b4e73f80a72c055826148b7f9b612c4bd26b56de25f60418d72b28ea4b7c76b8c38834623a91a4c3b79dfc76c6f90e57bba
    HEAD_REF dev
)

vcpkg_find_acquire_program(FLEX)

#
# Configure / build / install
#
vcpkg_cmake_configure(
    SOURCE_PATH
    "${SOURCE_PATH}"

    OPTIONS
    -DBUILD_TESTING=OFF
    -DPTX_USE_CCACHE=OFF

    "-DFLEX_EXECUTABLE=${FLEX}"
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME
    ptx_frontend

    CONFIG_PATH
    lib/cmake/ptx_frontend
)

file(
    REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(
    FILE_LIST "${SOURCE_PATH}/LICENSE"
)
