add_executable(imforge-fmt "${CMAKE_CURRENT_SOURCE_DIR}/imforge_fmt.cpp")
target_link_libraries(imforge-fmt PRIVATE imforge::headers imforge::warnings)
set_target_properties(imforge-fmt PROPERTIES FOLDER "tools")

# fails when the committed include/imforge/imb_motion_presets.h no longer matches presets/animations
# (run it explicitly or in CI, it's not part of ALL)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(imforge_presets
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/gen_motion_presets.py" --check
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Checking imb_motion_presets.h against presets/animations"
        VERBATIM)
    set_target_properties(imforge_presets PROPERTIES FOLDER "tools")
else()
    message(STATUS "ImForge: Python3 not found; imforge_presets is not available")
endif()
