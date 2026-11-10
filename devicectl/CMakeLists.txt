# openeq-devicectl.exe: elevated helper that installs/uninstalls the APO on an endpoint.
add_executable(openeq-devicectl main.cpp Installer.cpp Installer.h devicectl.manifest)
target_link_libraries(openeq-devicectl PRIVATE openeq_common ole32 oleaut32 propsys advapi32 shell32 uuid)
set_target_properties(openeq-devicectl PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Runs as invoker; commands that change the system check for elevation and the
# Qt app launches us with the "runas" verb for those.
target_link_options(openeq-devicectl PRIVATE /MANIFESTUAC:NO)

install(TARGETS openeq-devicectl RUNTIME DESTINATION .)
