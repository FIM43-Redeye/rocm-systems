# toolchain-linux.cmake — Default toolchain for RCCL on Linux.
#
# Sets ROCM_PATH and AMD clang++/clang as the CXX and C compiler.
# Also sets CXX/C compiler flags for Release/Debug/RelWithDebInfo builds.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain-linux.cmake ..
#
# The toolchain is auto-loaded by CMakeLists.txt if no toolchain file is specified.

# Detect ROCm installation.
# Priority: -DROCM_PATH > $ROCM_PATH env > PATH (via amdclang++/hipcc) > /opt/rocm.
# NOTE: ROCM_PATH is written to the CMake cache on first configure. If you change the
# ROCm installation, pass -DROCM_PATH=<new_path> or wipe the build directory.

# 1. -DROCM_PATH or $ROCM_PATH env var.
if(NOT ROCM_PATH)
    if(DEFINED ENV{ROCM_PATH} AND NOT "$ENV{ROCM_PATH}" STREQUAL "")
        set(ROCM_PATH "$ENV{ROCM_PATH}" CACHE PATH "Path to ROCm installation.")
    endif()
endif()

# 2. Derive from PATH: find amdclang++ or clang++ and walk up to the ROCm root.
#    Handles both ${ROCM_PATH}/bin/ and ${ROCM_PATH}/llvm/bin/ layouts.
if(NOT ROCM_PATH)
    find_program(_rocm_bin_hint NAMES amdclang++ clang++)
    if(_rocm_bin_hint)
        get_filename_component(_bin_dir "${_rocm_bin_hint}" DIRECTORY)
        get_filename_component(_parent  "${_bin_dir}"       DIRECTORY)
        if(EXISTS "${_parent}/lib/libamdhip64.so")
            set(ROCM_PATH "${_parent}" CACHE PATH "Path to ROCm installation (auto-detected from PATH).")
            message(STATUS "ROCM_PATH auto-detected from PATH: ${ROCM_PATH}")
        else()
            # llvm/bin layout: go one level higher
            get_filename_component(_grandparent "${_parent}" DIRECTORY)
            if(EXISTS "${_grandparent}/lib/libamdhip64.so")
                set(ROCM_PATH "${_grandparent}" CACHE PATH "Path to ROCm installation (auto-detected from PATH).")
                message(STATUS "ROCM_PATH auto-detected from PATH: ${ROCM_PATH}")
            endif()
        endif()
    endif()
    unset(_rocm_bin_hint CACHE)
    unset(_bin_dir)
    unset(_parent)
    unset(_grandparent)
endif()

# 3. Fall back to /opt/rocm.
if(NOT ROCM_PATH)
    set(ROCM_PATH "/opt/rocm" CACHE PATH "Path to ROCm installation.")
endif()

if(NOT EXISTS "${ROCM_PATH}")
    message(FATAL_ERROR "ROCM_PATH=${ROCM_PATH} does not exist")
endif()

# Detect CXX compiler. Priority: -DCMAKE_CXX_COMPILER > $CXX env > auto-detect from ROCm.
# NOTE: Once written to cache, CMAKE_CXX_COMPILER is not re-detected from $CXX on re-runs.
# To change compilers, pass -DCMAKE_CXX_COMPILER=<path> or wipe the build directory.
if(NOT CMAKE_CXX_COMPILER)
    if(DEFINED ENV{CXX} AND NOT "$ENV{CXX}" STREQUAL "")
        set(CMAKE_CXX_COMPILER "$ENV{CXX}" CACHE PATH "Path to C++ compiler")
    elseif(EXISTS "${ROCM_PATH}/bin/amdclang++")
        set(CMAKE_CXX_COMPILER "${ROCM_PATH}/bin/amdclang++" CACHE PATH "Path to C++ compiler")
    elseif(EXISTS "${ROCM_PATH}/llvm/bin/amdclang++")
        set(CMAKE_CXX_COMPILER "${ROCM_PATH}/llvm/bin/amdclang++" CACHE PATH "Path to C++ compiler")
    elseif(EXISTS "${ROCM_PATH}/llvm/bin/clang++")
        set(CMAKE_CXX_COMPILER "${ROCM_PATH}/llvm/bin/clang++" CACHE PATH "Path to C++ compiler")
    else()
        message(FATAL_ERROR "Cannot find amdclang++/clang++ under ${ROCM_PATH}/bin or ${ROCM_PATH}/llvm/bin.")
    endif()
endif()

# Set default per-build-type CXX flags unless the user has overridden them via $CXXFLAGS
# or by explicitly setting the per-type variable (e.g. -DCMAKE_CXX_FLAGS_DEBUG=...).
# Note: CMAKE_CXX_FLAGS (base flags for all types) is intentionally not checked here —
# it is orthogonal to per-type flags and should not suppress them.
if(NOT (DEFINED ENV{CXXFLAGS} AND NOT "$ENV{CXXFLAGS}" STREQUAL ""))
    if(NOT CMAKE_CXX_FLAGS_DEBUG)
        if(CMAKE_BUILD_SUBTYPE MATCHES "DebugFast")
            set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g")
        else()
            set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g -ggdb3")
        endif()
    endif()
    if(NOT CMAKE_CXX_FLAGS_RELEASE)
        set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    endif()
    if(NOT CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g")
    endif()
endif()

# Detect C compiler. Priority: -DCMAKE_C_COMPILER > $CC env > auto-detect from ROCm.
# NOTE: Once written to cache, CMAKE_C_COMPILER is not re-detected from $CC on re-runs.
if(NOT CMAKE_C_COMPILER)
    if(DEFINED ENV{CC} AND NOT "$ENV{CC}" STREQUAL "")
        set(CMAKE_C_COMPILER "$ENV{CC}" CACHE PATH "Path to C compiler")
    elseif(EXISTS "${ROCM_PATH}/bin/amdclang")
        set(CMAKE_C_COMPILER "${ROCM_PATH}/bin/amdclang" CACHE PATH "Path to C compiler")
    elseif(EXISTS "${ROCM_PATH}/llvm/bin/amdclang")
        set(CMAKE_C_COMPILER "${ROCM_PATH}/llvm/bin/amdclang" CACHE PATH "Path to C compiler")
    elseif(EXISTS "${ROCM_PATH}/llvm/bin/clang")
        set(CMAKE_C_COMPILER "${ROCM_PATH}/llvm/bin/clang" CACHE PATH "Path to C compiler")
    else()
        message(FATAL_ERROR "Cannot find amdclang/clang under ${ROCM_PATH}/bin or ${ROCM_PATH}/llvm/bin.")
    endif()
endif()

# Set default per-build-type C flags unless the user has overridden them via $CFLAGS
# or by explicitly setting the per-type variable (e.g. -DCMAKE_C_FLAGS_DEBUG=...).
# Note: CMAKE_C_FLAGS (base flags for all types) is intentionally not checked here —
# it is orthogonal to per-type flags and should not suppress them.
if(NOT (DEFINED ENV{CFLAGS} AND NOT "$ENV{CFLAGS}" STREQUAL ""))
    if(NOT CMAKE_C_FLAGS_DEBUG)
        if(CMAKE_BUILD_SUBTYPE MATCHES "DebugFast")
            set(CMAKE_C_FLAGS_DEBUG "-O1 -g")
        else()
            set(CMAKE_C_FLAGS_DEBUG "-O1 -g -ggdb3")
        endif()
    endif()
    if(NOT CMAKE_C_FLAGS_RELEASE)
        set(CMAKE_C_FLAGS_RELEASE "-O3")
    endif()
    if(NOT CMAKE_C_FLAGS_RELWITHDEBINFO)
        set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g")
    endif()
endif()
