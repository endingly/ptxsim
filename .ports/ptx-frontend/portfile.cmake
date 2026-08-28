vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO endingly/ptx_frontend
    REF bf3538f6243dcef72e6e7d2db3e209a93114f35c
    SHA512 df510bf3701cf40d133e5ee20c63ee1ba899667aa00ae2e76a7cc56b6b18c2f2f1d02a4b63388300ebd7e7efa11fc7090da87b2d4643439a3f29913e28430fc8
    HEAD_REF main
)

vcpkg_find_acquire_program(PYTHON3)
vcpkg_find_acquire_program(FLEX)

#
# Isolated Python build environment
#
set(
    PTX_PYTHON_VENV
    "${CURRENT_BUILDTREES_DIR}/python-venv"
)

file(REMOVE_RECURSE "${PTX_PYTHON_VENV}")

vcpkg_execute_required_process(
    COMMAND
    "${PYTHON3}"
    -m venv
    "${PTX_PYTHON_VENV}"

    WORKING_DIRECTORY
    "${CURRENT_BUILDTREES_DIR}"

    LOGNAME
    python-venv
)

if(VCPKG_HOST_IS_WINDOWS)
    set(
        PTX_PYTHON
        "${PTX_PYTHON_VENV}/Scripts/python.exe"
    )
else()
    set(
        PTX_PYTHON
        "${PTX_PYTHON_VENV}/bin/python"
    )
endif()

#
# Install all Python dependencies declared by ptx_frontend.
#
vcpkg_execute_required_process(
    COMMAND
    "${PTX_PYTHON}"
    -m pip
    install
    -r "${SOURCE_PATH}/requirements.txt"

    WORKING_DIRECTORY
    "${SOURCE_PATH}"

    LOGNAME
    python-dependencies
)

#
# Configure / build / install
#
vcpkg_cmake_configure(
    SOURCE_PATH
    "${SOURCE_PATH}"

    OPTIONS
    -DBUILD_TESTING=OFF
    -DPTX_USE_CCACHE=OFF

    "-DPython3_EXECUTABLE=${PTX_PYTHON}"
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