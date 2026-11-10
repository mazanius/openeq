# DSP core: no Qt and no Windows dependencies.
# Built twice: once against the DLL CRT (/MD, for the Qt app and tests) and once
# against the static CRT (/MT, for the APO, which must not depend on the VC++ redist
# being present inside audiodg.exe).
set(OPENEQ_CORE_SOURCES
    Biquad.cpp
    FilterChain.cpp
    EqConfig.cpp
    Response.cpp
)

add_library(openeq_core STATIC ${OPENEQ_CORE_SOURCES})
target_include_directories(openeq_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(openeq_core PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

add_library(openeq_core_mt STATIC ${OPENEQ_CORE_SOURCES})
target_include_directories(openeq_core_mt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(openeq_core_mt PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
