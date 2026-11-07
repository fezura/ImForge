file(GLOB_RECURSE IMFORGE_BUILDER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# dev switches: IMFORGE_BUILDER_EXCLUDE drops sources (relative to src/builder, e.g. panels/foo_panel.cpp),
# IMFORGE_BUILDER_WITHOUT_CODEGEN skips imforge_codegen/imforge_roundtrip and export/code view fall back
# to header-only mode
set(IMFORGE_BUILDER_EXCLUDE "" CACHE STRING "Builder sources to exclude from the build (semicolon list, relative to src/builder)")
option(IMFORGE_BUILDER_WITHOUT_CODEGEN "Build imforge_builder without linking imforge_codegen/imforge_roundtrip" OFF)
foreach(_ex IN LISTS IMFORGE_BUILDER_EXCLUDE)
    list(REMOVE_ITEM IMFORGE_BUILDER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_ex}")
    message(STATUS "ImForge: builder source excluded: ${_ex}")
endforeach()

if(NOT IMFORGE_BUILDER_SOURCES)
    message(STATUS "ImForge: no sources in src/builder yet, imforge_builder is skipped")
    return()
endif()

if(IMFORGE_BUILDER_BACKEND STREQUAL "SDL3_GL3")
    set(_backend_targets imgui_impl_sdl3 imgui_impl_opengl3)
    set(_backend_libs SDL3::SDL3)
else()
    set(_backend_targets imgui_impl_win32 imgui_impl_dx11)
    set(_backend_libs d3d11 dxgi d3dcompiler)
endif()
foreach(_t IN LISTS _backend_targets)
    if(NOT TARGET ${_t})
        message(WARNING "ImForge: backend target ${_t} is not available with Dear ImGui '${IMFORGE_IMGUI_TAG}', imforge_builder is skipped")
        return()
    endif()
endforeach()
if(NOT IMFORGE_IMGUI_HAS_DOCK)
    message(WARNING "ImForge: '${IMFORGE_IMGUI_TAG}' is not a docking build of Dear ImGui; the builder expects the docking branch")
endif()

if(IMFORGE_BUILDER_WITHOUT_CODEGEN)
    set(_codegen_libs)
    message(STATUS "ImForge: imforge_builder is built WITHOUT imforge_codegen/imforge_roundtrip (IMFORGE_BUILDER_WITHOUT_CODEGEN=ON)")
else()
    set(_codegen_libs imforge_codegen imforge_roundtrip)
endif()

add_executable(imforge_builder ${IMFORGE_BUILDER_SOURCES})
target_include_directories(imforge_builder PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(imforge_builder PRIVATE
    imforge::headers
    imgui
    ${_backend_targets}
    ${_backend_libs}
    ${_codegen_libs}
    imforge_binding
    imforge_essentials
    imforge::fonts
    miniz
    Threads::Threads
    imforge::warnings)
target_compile_definitions(imforge_builder PRIVATE
    IMFORGE_BUILDER=1
    IMFORGE_CATALOG_ALL_VERSIONS=1
    IMFORGE_BUILDER_BACKEND_${IMFORGE_BUILDER_BACKEND}=1
    IMFORGE_BUILDER_BACKEND_NAME="${IMFORGE_BUILDER_BACKEND}"
    IMFORGE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_target_properties(imforge_builder PROPERTIES
    FOLDER "apps"
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/builder"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:imforge_builder>")
set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT imforge_builder)

if(WIN32)
    if(MSVC)
        target_link_options(imforge_builder PRIVATE
            "$<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>"
            "$<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>"
            "$<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>")
    else()
        target_link_options(imforge_builder PRIVATE "$<$<NOT:$<CONFIG:Debug>>:-mwindows>")
    endif()
endif()

add_custom_command(TARGET imforge_builder POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${IMFORGE_FONTS_RUNTIME_DIR}" "$<TARGET_FILE_DIR:imforge_builder>/fonts"
    COMMENT "Copying fonts next to imforge_builder"
    VERBATIM)
if(IMFORGE_HAVE_LIBCLANG AND IMFORGE_LIBCLANG_DLL)
    add_custom_command(TARGET imforge_builder POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${IMFORGE_LIBCLANG_DLL}" "$<TARGET_FILE_DIR:imforge_builder>"
        VERBATIM)
endif()

if(IMFORGE_PACKAGING)
    install(TARGETS imforge_builder RUNTIME DESTINATION bin)
    install(DIRECTORY "${IMFORGE_FONTS_RUNTIME_DIR}/" DESTINATION bin/fonts)
    if(IMFORGE_HAVE_LIBCLANG AND IMFORGE_LIBCLANG_DLL)
        install(FILES "${IMFORGE_LIBCLANG_DLL}" DESTINATION bin)
    endif()
endif()
