# Constants shared by the APO, devicectl and the app (header-only).
add_library(openeq_common INTERFACE)
target_include_directories(openeq_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
