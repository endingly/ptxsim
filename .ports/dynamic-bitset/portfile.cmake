vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO pinam45/dynamic_bitset
    REF "v${VERSION}"
    SHA512 812bb87e0169f510e09ed9ae24a40b2fbc3a607360ec5feca142865f7cc4c77a0769887a372b3e20f694f8217a850474ebd2b0610fdb7b6c8ebee4f8e474e673
    HEAD_REF master
)

set(VCPKG_BUILD_TYPE release)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DDYNAMICBITSET_INSTALL=ON
        -DDYNAMICBITSET_BUILD_EXAMPLE=OFF
        -DDYNAMICBITSET_BUILD_TESTS=OFF
        -DDYNAMICBITSET_BUILD_DOCS=OFF
        -DDYNAMICBITSET_FORMAT_TARGET=OFF
        -DDYNAMICBITSET_HEADERS_TARGET_IDE=OFF
        -DDYNAMICBITSET_USE_LIBPOPCNT=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME sul-dynamic_bitset
    CONFIG_PATH lib/cmake/sul-dynamic_bitset
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
