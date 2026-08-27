cmake_minimum_required(VERSION 3.20)

project(
    softfloat
    VERSION 3.0.0
    LANGUAGES C
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(TestBigEndian)

set(
    SOFTFLOAT_SPECIALIZATION
    "8086-SSE"
    CACHE STRING
    "Berkeley SoftFloat specialization"
)

set(
    SOFTFLOAT_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/source"
)

set(
    SOFTFLOAT_SPECIALIZATION_DIR
    "${SOFTFLOAT_SOURCE_DIR}/${SOFTFLOAT_SPECIALIZATION}"
)

if(NOT EXISTS "${SOFTFLOAT_SPECIALIZATION_DIR}/specialize.h")
    message(
        FATAL_ERROR
        "Unknown SoftFloat specialization: "
        "${SOFTFLOAT_SPECIALIZATION}"
    )
endif()

#
# Host properties used to produce platform.h
#
test_big_endian(SOFTFLOAT_BIG_ENDIAN)

if(SOFTFLOAT_BIG_ENDIAN)
    set(SOFTFLOAT_LITTLE_ENDIAN 0)
else()
    set(SOFTFLOAT_LITTLE_ENDIAN 1)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/platform.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/platform.h"
    @ONLY
)

#
# SoftFloat source files.
#
# source/*.c contains the common implementation.
# source/<specialization>/*.c contains the specialization-specific
# NaN/exception/conversion behaviour.
#
file(
    GLOB
    SOFTFLOAT_COMMON_SOURCES
    CONFIGURE_DEPENDS
    "${SOFTFLOAT_SOURCE_DIR}/*.c"
)

# These internal multiword helpers are not part of SoftFloat's 64-bit build.
list(
    REMOVE_ITEM
    SOFTFLOAT_COMMON_SOURCES
    "${SOFTFLOAT_SOURCE_DIR}/s_addCarryM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_addComplCarryM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_addExtF80M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_addF128M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_addM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_compare128M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_compare96M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_compareNonnormExtF80M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_invalidExtF80M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_invalidF128M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_isNaNF128M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_mul128MTo256M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_mul64To128M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_mulAddF128M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_negXM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_normExtF80SigM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_normRoundPackMToExtF80M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_normRoundPackMToF128M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_normSubnormalF128SigM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_remStepMBy32.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_roundMToI64.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_roundMToUI64.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_roundPackMToExtF80M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_roundPackMToF128M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_shiftLeftM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_shiftNormSigF128M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_shiftRightJamM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_shiftRightM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_shortShiftLeft64To96M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_shortShiftLeftM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_shortShiftRightExtendM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_shortShiftRightJamM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_shortShiftRightM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_sub1XM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_subM.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_tryPropagateNaNExtF80M.c"
    "${SOFTFLOAT_SOURCE_DIR}/s_tryPropagateNaNF128M.c"
)

file(
    GLOB
    SOFTFLOAT_SPECIALIZATION_SOURCES
    CONFIGURE_DEPENDS
    "${SOFTFLOAT_SPECIALIZATION_DIR}/*.c"
)

list(
    REMOVE_ITEM
    SOFTFLOAT_SPECIALIZATION_SOURCES
    "${SOFTFLOAT_SPECIALIZATION_DIR}/s_commonNaNToExtF80M.c"
    "${SOFTFLOAT_SPECIALIZATION_DIR}/s_commonNaNToF128M.c"
    "${SOFTFLOAT_SPECIALIZATION_DIR}/s_extF80MToCommonNaN.c"
    "${SOFTFLOAT_SPECIALIZATION_DIR}/s_f128MToCommonNaN.c"
    "${SOFTFLOAT_SPECIALIZATION_DIR}/s_propagateNaNExtF80M.c"
    "${SOFTFLOAT_SPECIALIZATION_DIR}/s_propagateNaNF128M.c"
)

add_library(
    softfloat
    STATIC
    ${SOFTFLOAT_COMMON_SOURCES}
    ${SOFTFLOAT_SPECIALIZATION_SOURCES}
)

add_library(
    softfloat::softfloat
    ALIAS
    softfloat
)

set_target_properties(
    softfloat
    PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
    POSITION_INDEPENDENT_CODE ON
)

#
# Public interface:
#
# #include <softfloat/softfloat.h>
#
target_include_directories(
    softfloat

    PUBLIC
    $<BUILD_INTERFACE:${SOFTFLOAT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>

    PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${SOFTFLOAT_SPECIALIZATION_DIR}"
)

#
# These correspond to the options used by the upstream 64-bit
# reference builds.
#
target_compile_definitions(
    softfloat

    PRIVATE
    SOFTFLOAT_ROUND_ODD
    INLINE_LEVEL=5
    SOFTFLOAT_FAST_DIV32TO16
    SOFTFLOAT_FAST_DIV64TO32

    INTERFACE
    "$<$<COMPILE_LANGUAGE:C>:THREAD_LOCAL=_Thread_local>"
    "$<$<COMPILE_LANGUAGE:CXX>:THREAD_LOCAL=thread_local>"
)

if(SOFTFLOAT_LITTLE_ENDIAN)
    target_compile_definitions(
        softfloat
        INTERFACE
        LITTLEENDIAN
    )
endif()

#
# SoftFloat documents SOFTFLOAT_FAST_INT64 as appropriate for
# modern machines with efficient 64-bit integer arithmetic.
#
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(
        softfloat
        PUBLIC
        SOFTFLOAT_FAST_INT64
    )
endif()

#
# Installation
#
install(
    TARGETS
    softfloat

    EXPORT
    softfloatTargets

    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
    DIRECTORY
    "${SOFTFLOAT_SOURCE_DIR}/include/"

    DESTINATION
    "${CMAKE_INSTALL_INCLUDEDIR}/softfloat"

    FILES_MATCHING
    PATTERN "*.h"
)

install(
    EXPORT
    softfloatTargets

    FILE
    softfloatTargets.cmake

    NAMESPACE
    softfloat::

    DESTINATION
    share/softfloat
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/softfloatConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/softfloatConfig.cmake"

    INSTALL_DESTINATION
    share/softfloat
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/softfloatConfigVersion.cmake"

    VERSION
    ${PROJECT_VERSION}

    COMPATIBILITY
    SameMajorVersion
)

install(
    FILES
    "${CMAKE_CURRENT_BINARY_DIR}/softfloatConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/softfloatConfigVersion.cmake"

    DESTINATION
    share/softfloat
)
