find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE openeq_core Qt6::Test)
set_target_properties(test_core PROPERTIES AUTOMOC ON)
add_test(NAME test_core COMMAND test_core)

# Makes Qt DLLs findable when ctest runs the Qt-based test.
set_tests_properties(test_core PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")

if(OPENEQ_BUILD_APO)
    # Loads openeq_apo.dll directly (no registration) and pushes audio through it.
    add_executable(apo_host apo_host.cpp)
    target_link_libraries(apo_host PRIVATE openeq_common ole32 uuid)
    add_dependencies(apo_host openeq_apo)
    add_test(NAME apo_host COMMAND apo_host $<TARGET_FILE:openeq_apo>)
endif()
