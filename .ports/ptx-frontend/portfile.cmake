vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO endingly/ptx_frontend
    REF bf3538f6243dcef72e6e7d2db3e209a93114f35c
    SHA512 df510bf3701cf40d133e5ee20c63ee1ba899667aa00ae2e76a7cc56b6b18c2f2f1d02a4b63388300ebd7e7efa11fc7090da87b2d4643439a3f29913e28430fc8
    HEAD_REF main
)

vcpkg_find_acquire_program(FLEX)

# The source revision above generates these files from YAML with Python,
# PyYAML and jsonschema.  Keep the generated output with this port instead:
# it is tied to that exact source revision and removes pip, resolver and PyPI
# availability from the build.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/generated/"
     DESTINATION "${SOURCE_PATH}/generated")
file(WRITE "${SOURCE_PATH}/cmake/generate_ptx_frontend.cmake" [=[
include_guard(DIRECTORY)

set(PTX_RESOLVED_IR_GENERATED_DIR "${PROJECT_SOURCE_DIR}/generated")
set(PTX_RESOLVED_IR_GENERATED_PUBLIC_INCLUDE_DIR
    "${PTX_RESOLVED_IR_GENERATED_DIR}/public")
set(PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR
    "${PTX_RESOLVED_IR_GENERATED_DIR}/private")
file(GLOB_RECURSE PTX_RESOLVED_IR_GENERATED_SRCS CONFIGURE_DEPENDS
    "${PTX_RESOLVED_IR_GENERATED_PRIVATE_INCLUDE_DIR}/*.gen.cpp")
add_custom_target(resolved_ir_codegen)
]=])

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
