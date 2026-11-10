find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

qt_add_executable(openeq WIN32
    main.cpp
    Theme.cpp Theme.h
    EqModel.cpp EqModel.h
    DeviceManager.cpp DeviceManager.h
    ConfigWriter.cpp ConfigWriter.h
    PresetManager.cpp PresetManager.h
    ResponseView.cpp ResponseView.h
    BandStrip.cpp BandStrip.h
    Knob.cpp Knob.h
    LedButton.cpp LedButton.h
    ValueBox.cpp ValueBox.h
    GraphicEqPanel.cpp GraphicEqPanel.h
    MainWindow.cpp MainWindow.h
)

qt_add_resources(openeq "ui" PREFIX "/ui" BASE resources FILES resources/arrow-down.svg)

target_link_libraries(openeq PRIVATE openeq_core openeq_common Qt6::Widgets Qt6::Network ole32 shell32 advapi32)

# The app hands install/uninstall to the helper and needs the APO next to it.
if(TARGET openeq-devicectl)
    add_dependencies(openeq openeq-devicectl)
endif()
if(TARGET openeq_apo)
    add_dependencies(openeq openeq_apo)
endif()

# Copy the Qt runtime next to the binaries so build/bin runs as-is.
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qtbin "${_qmake}" DIRECTORY)
find_program(WINDEPLOYQT windeployqt HINTS "${_qtbin}")
if(WINDEPLOYQT)
    add_custom_command(TARGET openeq POST_BUILD
        COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                --no-compiler-runtime --verbose 0 "$<TARGET_FILE:openeq>"
        COMMENT "Deploying Qt runtime")
endif()

# `cmake --install build --config Release --prefix dist` produces a folder that
# runs on a machine without Qt or Visual Studio. The VC++ runtime DLLs come from
# InstallRequiredSystemLibraries in the top-level CMakeLists.txt.
install(TARGETS openeq RUNTIME DESTINATION .)
if(WINDEPLOYQT)
    install(CODE "execute_process(COMMAND \"${WINDEPLOYQT}\" --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime --verbose 0 \"\${CMAKE_INSTALL_PREFIX}/openeq.exe\")")
endif()
