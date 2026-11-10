# openeq_apo.dll: the system effect loaded by audiodg.exe.
add_library(openeq_apo SHARED
    dllmain.cpp
    OpenEqApo.cpp
    OpenEqApo.h
    ConfigWatcher.cpp
    ConfigWatcher.h
    Log.cpp
    Log.h
    openeq_apo.def
)

target_link_libraries(openeq_apo PRIVATE
    openeq_core_mt
    openeq_common
    AudioBaseProcessingObjectV140
    audiomediatypecrt
    ole32
    oleaut32
    propsys
    shell32
    advapi32
    uuid
)

# Static CRT: audiodg.exe must not depend on a VC++ redistributable being installed.
set_target_properties(openeq_apo PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME openeq_apo)
target_compile_options(openeq_apo PRIVATE /EHsc /guard:cf)
target_link_options(openeq_apo PRIVATE /guard:cf)

install(TARGETS openeq_apo RUNTIME DESTINATION .)
