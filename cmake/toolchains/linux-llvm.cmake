# Shared by vcpkg chainloading and standalone CMake consumer checks.
# vcpkg port configurations pass this root before loading their chainload file.
# Preserve the platform setup, PIC, and triplet flags from its Linux toolchain.
if(DEFINED _VCPKG_ROOT_DIR)
    include("${_VCPKG_ROOT_DIR}/scripts/toolchains/linux.cmake")
endif()
# This dedicated toolchain also replaces stale cached tool paths.
# Clang uses its integrated assembler; llvm-as accepts LLVM IR, not platform ASM.
set(CMAKE_C_COMPILER "/usr/bin/clang" CACHE FILEPATH "LLVM C compiler" FORCE)
set(CMAKE_CXX_COMPILER "/usr/bin/clang++" CACHE FILEPATH "LLVM C++ compiler" FORCE)
set(CMAKE_ASM_COMPILER "/usr/bin/clang" CACHE FILEPATH "LLVM assembler driver" FORCE)
foreach(tool IN ITEMS AR RANLIB NM STRIP OBJCOPY OBJDUMP READELF ADDR2LINE)
    string(TOLOWER "${tool}" executable)
    set(CMAKE_${tool} "/usr/bin/llvm-${executable}" CACHE FILEPATH "LLVM ${executable}" FORCE)
endforeach()
foreach(language IN ITEMS C CXX)
    set(CMAKE_${language}_COMPILER_AR "/usr/bin/llvm-ar" CACHE FILEPATH "LLVM IPO archiver" FORCE)
    set(CMAKE_${language}_COMPILER_RANLIB "/usr/bin/llvm-ranlib" CACHE FILEPATH "LLVM IPO archive indexer" FORCE)
endforeach()
set(CMAKE_LINKER "/usr/bin/ld.lld" CACHE FILEPATH "LLVM linker" FORCE)
# CMAKE_LINKER alone does not select the linker invoked by the Clang driver.
# Initial flags keep this compatible with the project's CMake 3.28 minimum.
foreach(kind IN ITEMS EXE SHARED MODULE)
    if(NOT "${CMAKE_${kind}_LINKER_FLAGS_INIT}" MATCHES "(^| )-fuse-ld=lld($| )")
        string(APPEND CMAKE_${kind}_LINKER_FLAGS_INIT " -fuse-ld=lld")
    endif()
    # Existing preset build trees already have cached flags, so update those too.
    if(DEFINED CACHE{CMAKE_${kind}_LINKER_FLAGS} AND
       NOT "${CMAKE_${kind}_LINKER_FLAGS}" MATCHES "(^| )-fuse-ld=lld($| )")
        set(CMAKE_${kind}_LINKER_FLAGS "${CMAKE_${kind}_LINKER_FLAGS} -fuse-ld=lld"
            CACHE STRING "Linker flags with LLVM linker selection" FORCE)
    endif()
endforeach()
