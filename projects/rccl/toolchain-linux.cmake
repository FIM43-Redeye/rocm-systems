# toolchain-linux.cmake — Default toolchain for RCCL on Linux.
#
# Sets ROCM_PATH and AMD clang++/clang as the CXX and C compiler.
# Also sets CXX/C compiler flags for Release/Debug/RelWithDebInfo builds.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain-linux.cmake ..
#
# The toolchain is auto-loaded by CMakeLists.txt if no toolchain file is specified.

# Detect ROCm installation. Priority: -DROCM_PATH > $ROCM_PATH env > /opt/rocm.
# NOTE: ROCM_PATH is written to the CMake cache on first configure. If you change the
# ROCm installation, pass -DROCM_PATH=<new_path> or wipe the build directory.
if(NOT ROCM_PATH)
    if(DEFINED ENV{ROCM_PATH} AND NOT "$ENV{ROCM_PATH}" STREQUAL "")
        set(ROCM_PATH "$ENV{ROCM_PATH}" CACHE PATH "Path to ROCm installation.")
    else()
        set(ROCM_PATH "/opt/rocm" CACHE PATH "Path to ROCm installation.")
    endif()
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

# Set default per-build-type CXX flags unless the user has overridden them via $CXXFLAGS.
if(NOT CMAKE_CXX_FLAGS)
    if(NOT (DEFINED ENV{CXXFLAGS} AND NOT "$ENV{CXXFLAGS}" STREQUAL ""))
        if("${CMAKE_BUILD_SUBTYPE}" STREQUAL "DebugFast")
            set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g")
        else()
            set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g -ggdb3")
        endif()
        set(CMAKE_CXX_FLAGS_RELEASE "-O3")
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

# Set default per-build-type C flags unless the user has overridden them via $CFLAGS.
if(NOT CMAKE_C_FLAGS)
    if(NOT (DEFINED ENV{CFLAGS} AND NOT "$ENV{CFLAGS}" STREQUAL ""))
        if("${CMAKE_BUILD_SUBTYPE}" STREQUAL "DebugFast")
            set(CMAKE_C_FLAGS_DEBUG "-O1 -g")
        else()
            set(CMAKE_C_FLAGS_DEBUG "-O1 -g -ggdb3")
        endif()
        set(CMAKE_C_FLAGS_RELEASE "-O3")
        set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g")
    endif()
endif()
