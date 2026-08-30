vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO endingly/ptx_frontend
    REF 1c4547f65c888ee92b1933a20f9a74b380b96953
    SHA512 9a90e6bd5f7b52d84fffda7340215eabfeec5103c04da2253e3d22142728301b70c1f7ae713c59f851500f8e37f6f0359209467cce8ce9c4df57dbc23ef2b2d9
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

# This revision asks CMake for the generated topology before adding resolved_ir
# sources.  The overlay deliberately supplies the checked-in snapshot instead
# of running Python generation, so return that fixed topology from the copied
# payloads.
function(ptx_resolved_ir_list_generated_outputs output_variable spec_dir output_dir)
    set(_ptx_resolved_ir_generated_outputs
        "${PTX_RESOLVED_IR_GENERATED_DIR}/public/resolved_ir.gen.hpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/resolved_value_domains.gen.hpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/resolved_ir_dispatch.gen.cpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/resolved_ir_arithmetic.gen.cpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/resolved_ir_control_flow.gen.cpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/resolved_ir_data_movement.gen.cpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/resolved_ir_matrix.gen.cpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/resolved_ir_parallel_synchronization_and_communication.gen.cpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/syntax_descriptor.gen.cpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/resolved_descriptor.gen.cpp"
        "${PTX_RESOLVED_IR_GENERATED_DIR}/private/resolved_ir_checker_descriptor.gen.cpp")
    set(${output_variable} "${_ptx_resolved_ir_generated_outputs}" PARENT_SCOPE)
endfunction()

ptx_resolved_ir_list_generated_outputs(
    PTX_RESOLVED_IR_GENERATED_FILES "" "")
set(PTX_RESOLVED_IR_GENERATED_SRCS ${PTX_RESOLVED_IR_GENERATED_FILES})
list(FILTER PTX_RESOLVED_IR_GENERATED_SRCS INCLUDE REGEX "\\.gen\\.cpp$")
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
