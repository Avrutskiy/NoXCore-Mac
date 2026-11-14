# The integration harness reuses the production compile/link commands and SDL's
# private dummy-driver seam. Keep it opt-in and tied to the pinned dependencies.
if(NOT CMAKE_GENERATOR STREQUAL "Ninja" OR NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    message(FATAL_ERROR "Notch tests need Ninja and CMAKE_EXPORT_COMPILE_COMMANDS=ON")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()
add_executable(nox_notch_policy_tests "${mac}/tests/notch_policy_tests.cpp"
    "${mac}/backend.cpp" "${mac}/pointer.cpp")
target_compile_features(nox_notch_policy_tests PRIVATE cxx_std_20)
target_include_directories(nox_notch_policy_tests PRIVATE "${mac}" "${client}/src"
    "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
add_test(NAME mac_notch_policy COMMAND nox_notch_policy_tests)
add_test(NAME mac_notch_integration COMMAND "${Python3_EXECUTABLE}"
    "${mac}/tests/check_notch.py" "${PROJECT_BINARY_DIR}")
add_executable(nox_notch_display_smoke "${mac}/tests/notch_display_smoke.cpp" "${mac}/backend.cpp")
target_compile_features(nox_notch_display_smoke PRIVATE cxx_std_20)
target_include_directories(nox_notch_display_smoke PRIVATE "${client}/src")
target_link_libraries(nox_notch_display_smoke PRIVATE SDL3::SDL3 nox_warnings)
