# each examples/client/<name>/main.cpp and examples/fx/<name>/main.cpp becomes an executable if the
# backends in <name> (sdl3, gl3, dx9/dx11/dx12, win32, vulkan) exist on this platform
file(GLOB _example_mains CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/client/*/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/fx/*/main.cpp")

foreach(_main IN LISTS _example_mains)
    get_filename_component(_dir "${_main}" DIRECTORY)
    get_filename_component(_name "${_dir}" NAME)
    get_filename_component(_cat_dir "${_dir}" DIRECTORY)
    get_filename_component(_cat "${_cat_dir}" NAME)
    if(_cat STREQUAL "client")
        set(_target "imforge_example_${_name}")
    else()
        set(_target "imforge_${_cat}_${_name}")
    endif()

    set(_req)
    set(_libs)
    if(_name MATCHES "sdl3" OR (_name MATCHES "gl3" AND NOT _name MATCHES "win32"))
        list(APPEND _req imgui_impl_sdl3)
        list(APPEND _libs SDL3::SDL3)
    endif()
    if(_name MATCHES "gl3")
        list(APPEND _req imgui_impl_opengl3)
    endif()
    if(_name MATCHES "dx(9|11|12)")
        if(NOT WIN32)
            continue()
        endif()
        list(APPEND _req imgui_impl_win32 imgui_impl_dx${CMAKE_MATCH_1})
        list(APPEND _libs d3dcompiler dxgi)
    elseif(_name MATCHES "win32" AND NOT WIN32)
        continue()
    endif()
    if(_name MATCHES "vulkan")
        list(APPEND _req imgui_impl_vulkan)
    endif()

    set(_missing)
    foreach(_t IN LISTS _req)
        if(NOT TARGET ${_t})
            list(APPEND _missing ${_t})
        endif()
    endforeach()
    if(_missing)
        message(STATUS "ImForge: example ${_cat}/${_name} skipped (missing: ${_missing})")
        continue()
    endif()

    file(GLOB _srcs CONFIGURE_DEPENDS "${_dir}/*.cpp")
    add_executable(${_target} ${_srcs})
    target_include_directories(${_target} PRIVATE "${_dir}" "${CMAKE_CURRENT_SOURCE_DIR}")
    # pack folders, so imforge::live::RegisterBundledPacks() finds every imforge_adapters_<pack>.h
    file(GLOB _pack_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/packs/*")
    foreach(_pd IN LISTS _pack_dirs)
        if(IS_DIRECTORY "${_pd}")
            target_include_directories(${_target} PRIVATE "${_pd}")
        endif()
    endforeach()
    target_link_libraries(${_target} PRIVATE
        imforge::headers
        imgui
        ${_req}
        ${_libs}
        imforge_essentials
        Threads::Threads
        imforge::warnings)
    if(WIN32)
        target_link_libraries(${_target} PRIVATE ws2_32)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(${_target} PRIVATE rt)
    endif()
    set_target_properties(${_target} PROPERTIES FOLDER "examples")
    message(STATUS "ImForge: example ${_cat}/${_name} -> ${_target}")
endforeach()

# fake live server that serves examples/projects/CheatMenu to a client example without the editor.
# console app, headers only, no Dear ImGui
add_executable(imforge_fake_builder "${CMAKE_CURRENT_SOURCE_DIR}/../tools/imforge_fake_builder.cpp")
target_compile_definitions(imforge_fake_builder PRIVATE "IMFORGE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
target_link_libraries(imforge_fake_builder PRIVATE imforge::headers Threads::Threads imforge::warnings)
if(WIN32)
    target_link_libraries(imforge_fake_builder PRIVATE ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(imforge_fake_builder PRIVATE rt)
endif()
set_target_properties(imforge_fake_builder PROPERTIES FOLDER "examples")
message(STATUS "ImForge: tool -> imforge_fake_builder")
