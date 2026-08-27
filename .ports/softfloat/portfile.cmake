vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH

    REPO
    ucb-bar/berkeley-softfloat-3

    REF
    a0c6494cdc11865811dec815d5c0049fba9d82a8

    SHA512
    1891d74a4b958af5c8751532ace127164e0ca16ebddcf712fca170d11ac752f3cd3aa6f994fbda2f520368944ffa144fdc44310c4b254455f2542686ed4061e6

    HEAD_REF
    master
)

#
# Berkeley SoftFloat does not provide a CMake build system.
# Supply a small vcpkg-owned wrapper without modifying upstream sources.
#
file(
    COPY
    "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt"
    "${CMAKE_CURRENT_LIST_DIR}/platform.h.in"
    "${CMAKE_CURRENT_LIST_DIR}/softfloatConfig.cmake.in"
    DESTINATION
    "${SOURCE_PATH}"
)

vcpkg_cmake_configure(
    SOURCE_PATH
    "${SOURCE_PATH}"

    OPTIONS
    -DSOFTFLOAT_SPECIALIZATION=8086-SSE
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME
    softfloat

    CONFIG_PATH
    share/softfloat
)

file(
    REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

file(
    INSTALL
    "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION
    "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)

vcpkg_install_copyright(
    FILE_LIST
    "${SOURCE_PATH}/COPYING.txt"
)