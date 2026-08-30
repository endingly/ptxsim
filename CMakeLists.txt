cmake_minimum_required(VERSION 3.28.0)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
project(ptxsim VERSION 0.0.1 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# ptxsim::arith currently relies on the GCC/Clang __int128 extension for
# integer widening. Keep extensions enabled until that backend has a portable
# replacement; submod/arith verifies the capability explicitly.
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(PTX_USE_CCACHE "Use ccache to speed up compilation" ON)
option(BUILD_TESTING "enable test program building" OFF)
option(PTXSIM_BUILD_CONSUMER_TESTS
       "Build CTest gates for build-tree and installed-package consumers"
       ${BUILD_TESTING})
option(PTXSIM_ENABLE_ASAN "Enable AddressSanitizer for ptxsim targets" OFF)
option(PTXSIM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for ptxsim targets" OFF)

if((PTXSIM_ENABLE_ASAN OR PTXSIM_ENABLE_UBSAN) AND
   NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR
        "PTXSIM_ENABLE_ASAN/PTXSIM_ENABLE_UBSAN currently require GCC or Clang")
endif()

set(PTXSIM_SANITIZER_FLAGS)
if(PTXSIM_ENABLE_ASAN)
    list(APPEND PTXSIM_SANITIZER_FLAGS -fsanitize=address)
endif()
if(PTXSIM_ENABLE_UBSAN)
    list(APPEND PTXSIM_SANITIZER_FLAGS -fsanitize=undefined)
endif()

function(ptxsim_enable_sanitizers target)
    if(PTXSIM_SANITIZER_FLAGS)
        target_compile_options(${target} PRIVATE
            ${PTXSIM_SANITIZER_FLAGS}
            -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE ${PTXSIM_SANITIZER_FLAGS})
    endif()
endfunction()

if(PTX_USE_CCACHE)
    find_program(CCACHE_EXECUTABLE ccache)
    if(CCACHE_EXECUTABLE)
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE})
    else()
        message(WARNING "PTX_USE_CCACHE is ON, but ccache was not found")
    endif()
endif()

if(BUILD_TESTING)
    include(CTest)
    enable_testing()
    include(GoogleTest)
    add_test(
        NAME ptxsim_ptx_frontend_snapshot_integrity
        COMMAND "${CMAKE_COMMAND}"
            "-DPTXSIM_SNAPSHOT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.ports/ptx-frontend/generated"
            "-DPTXSIM_PTX_FRONTEND_PORTFILE=${CMAKE_CURRENT_SOURCE_DIR}/.ports/ptx-frontend/portfile.cmake"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_ptx_frontend_snapshot.cmake")
endif()

add_subdirectory(submod)

# Package ownership belongs to the project root: individual modules define
# targets, while this project-level surface selects the production and
# diagnostic libraries that form the public ptxsim package.
include(install_project_targets)
install_project_targets(
    PROJECT "${PROJECT_NAME}"
    VERSION "${PROJECT_VERSION}"
    NAMESPACE ptxsim
    TARGETS
        ptxsim_common
        ptxsim_exec_ir
        ptxsim_program
        ptxsim_state
        ptxsim_arith
        ptxsim_arith_validation
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/common/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/exec_ir/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/program/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/state/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/submod/arith/include"
    INCLUDE_DESTINATIONS
        "${PROJECT_NAME}/common"
        "${PROJECT_NAME}/exec_ir"
        "${PROJECT_NAME}/program"
        "${PROJECT_NAME}/state"
        "${PROJECT_NAME}/arith"
    DEPENDENCIES "softfloat CONFIG REQUIRED")

# Lowering is intentionally a separate export: loading the default core package
# must not require the optional frontend dependency.
if(TARGET ptxsim_lowering)
    install(TARGETS ptxsim_lowering
        EXPORT ptxsimLoweringTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/submod/lowering/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ptxsim/lowering"
        FILES_MATCHING PATTERN "*.hpp")
    export(EXPORT ptxsimLoweringTargets
        FILE "${CMAKE_CURRENT_BINARY_DIR}/ptxsimLoweringTargets.cmake"
        NAMESPACE ptxsim::)
    install(EXPORT ptxsimLoweringTargets
        FILE ptxsimLoweringTargets.cmake
        NAMESPACE ptxsim::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ptxsim")
endif()
