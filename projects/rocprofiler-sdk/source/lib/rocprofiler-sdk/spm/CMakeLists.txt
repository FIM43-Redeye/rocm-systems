set(ROCPROFILER_LIB_SPM_SOURCES dlsym.cpp)
set(ROCPROFILER_LIB_SPM_HEADERS dlsym.hpp)
target_sources(rocprofiler-sdk-object-library PRIVATE ${ROCPROFILER_LIB_SPM_SOURCES}
                                                      ${ROCPROFILER_LIB_SPM_HEADERS})

