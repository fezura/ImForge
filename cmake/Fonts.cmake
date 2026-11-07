# builder fonts, downloaded at configure time. -DIMFORGE_FONTS_DIR=<dir> with IMFORGE_FONT_FILES skips that
set(IMFORGE_FONT_FILES Inter-Regular.ttf Inter-Bold.ttf JetBrainsMono-Regular.ttf fa-solid-900.ttf IconsFontAwesome6.h)
set(IMFORGE_FONTS_BUILD_DIR "${CMAKE_BINARY_DIR}/fonts")
set(IMFORGE_DOWNLOAD_CACHE_DIR "${CMAKE_BINARY_DIR}/_downloads" CACHE PATH "Cache for configure-time downloads (fonts)")

set(IMFORGE_INTER_VERSION 4.1)
set(IMFORGE_INTER_URL "https://github.com/rsms/inter/releases/download/v${IMFORGE_INTER_VERSION}/Inter-${IMFORGE_INTER_VERSION}.zip")
set(IMFORGE_JETBRAINS_MONO_TAG v2.304)
set(IMFORGE_JETBRAINS_MONO_URL "https://raw.githubusercontent.com/JetBrains/JetBrainsMono/${IMFORGE_JETBRAINS_MONO_TAG}/fonts/ttf/JetBrainsMono-Regular.ttf")
set(IMFORGE_JETBRAINS_MONO_LICENSE_URL "https://raw.githubusercontent.com/JetBrains/JetBrainsMono/${IMFORGE_JETBRAINS_MONO_TAG}/OFL.txt")
set(IMFORGE_FONT_AWESOME_TAG 6.7.2)
set(IMFORGE_FONT_AWESOME_URL "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/${IMFORGE_FONT_AWESOME_TAG}/webfonts/fa-solid-900.ttf")
set(IMFORGE_FONT_AWESOME_LICENSE_URL "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/${IMFORGE_FONT_AWESOME_TAG}/LICENSE.txt")
set(IMFORGE_ICON_HEADERS_COMMIT 8a381189ecfe58e732466cc52e79ca887dd6a297)
set(IMFORGE_ICON_HEADERS_URL "https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/${IMFORGE_ICON_HEADERS_COMMIT}/IconsFontAwesome6.h")
set(IMFORGE_ICON_HEADERS_LICENSE_URL "https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/${IMFORGE_ICON_HEADERS_COMMIT}/licence.txt")

function(imforge_download url dest what)
    if(EXISTS "${dest}")
        return()
    endif()
    get_filename_component(_dir "${dest}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    message(STATUS "ImForge: downloading ${what}\n   ${url}")
    file(DOWNLOAD "${url}" "${dest}.part" STATUS _status TLS_VERIFY ON TIMEOUT 600 INACTIVITY_TIMEOUT 120)
    list(GET _status 0 _code)
    list(GET _status 1 _msg)
    if(NOT _code EQUAL 0)
        file(REMOVE "${dest}.part")
        message(FATAL_ERROR
            "ImForge: failed to download ${what}\n"
            "   url:   ${url}\n"
            "   error: ${_msg}\n"
            "If you are offline, put ${IMFORGE_FONT_FILES} into a directory and configure with\n"
            "   -DIMFORGE_FONTS_DIR=<that directory>\n"
            "or disable the editor with -DIMFORGE_BUILD_BUILDER=OFF.")
    endif()
    file(SIZE "${dest}.part" _size)
    if(_size EQUAL 0)
        file(REMOVE "${dest}.part")
        message(FATAL_ERROR "ImForge: downloaded ${what} from ${url} is empty")
    endif()
    file(RENAME "${dest}.part" "${dest}")
endfunction()

if(IMFORGE_FONTS_DIR)
    foreach(_f IN LISTS IMFORGE_FONT_FILES)
        if(NOT EXISTS "${IMFORGE_FONTS_DIR}/${_f}")
            message(FATAL_ERROR "IMFORGE_FONTS_DIR=${IMFORGE_FONTS_DIR} is missing ${_f}")
        endif()
    endforeach()
    set(IMFORGE_FONTS_RUNTIME_DIR "${IMFORGE_FONTS_DIR}")
    message(STATUS "ImForge: using pre-downloaded fonts from ${IMFORGE_FONTS_DIR}")
else()
    set(_fonts "${IMFORGE_FONTS_BUILD_DIR}")
    set(_lic "${IMFORGE_FONTS_BUILD_DIR}/licenses")
    set(_cache "${IMFORGE_DOWNLOAD_CACHE_DIR}")
    file(MAKE_DIRECTORY "${_fonts}" "${_lic}" "${_cache}")

    if(NOT EXISTS "${_fonts}/Inter-Regular.ttf" OR NOT EXISTS "${_fonts}/Inter-Bold.ttf" OR NOT EXISTS "${_lic}/Inter-LICENSE.txt")
        set(_zip "${_cache}/Inter-${IMFORGE_INTER_VERSION}.zip")
        set(_extract "${_cache}/Inter-${IMFORGE_INTER_VERSION}")
        imforge_download("${IMFORGE_INTER_URL}" "${_zip}" "Inter ${IMFORGE_INTER_VERSION} (zip, ~33 MB)")
        file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${_extract}"
             PATTERNS extras/ttf/Inter-Regular.ttf extras/ttf/Inter-Bold.ttf LICENSE.txt)
        foreach(_f Inter-Regular.ttf Inter-Bold.ttf)
            if(NOT EXISTS "${_extract}/extras/ttf/${_f}")
                message(FATAL_ERROR "ImForge: ${_f} not found inside ${_zip} (expected extras/ttf/${_f})")
            endif()
            file(COPY_FILE "${_extract}/extras/ttf/${_f}" "${_fonts}/${_f}" ONLY_IF_DIFFERENT)
        endforeach()
        if(EXISTS "${_extract}/LICENSE.txt")
            file(COPY_FILE "${_extract}/LICENSE.txt" "${_lic}/Inter-LICENSE.txt" ONLY_IF_DIFFERENT)
        endif()
    endif()

    imforge_download("${IMFORGE_JETBRAINS_MONO_URL}" "${_fonts}/JetBrainsMono-Regular.ttf" "JetBrains Mono ${IMFORGE_JETBRAINS_MONO_TAG}")
    imforge_download("${IMFORGE_JETBRAINS_MONO_LICENSE_URL}" "${_lic}/JetBrainsMono-OFL.txt" "JetBrains Mono license")
    imforge_download("${IMFORGE_FONT_AWESOME_URL}" "${_fonts}/fa-solid-900.ttf" "Font Awesome ${IMFORGE_FONT_AWESOME_TAG} Free Solid")
    imforge_download("${IMFORGE_FONT_AWESOME_LICENSE_URL}" "${_lic}/FontAwesome-LICENSE.txt" "Font Awesome license")
    imforge_download("${IMFORGE_ICON_HEADERS_URL}" "${_fonts}/IconsFontAwesome6.h" "IconFontCppHeaders IconsFontAwesome6.h")
    imforge_download("${IMFORGE_ICON_HEADERS_LICENSE_URL}" "${_lic}/IconFontCppHeaders-licence.txt" "IconFontCppHeaders license")

    set(IMFORGE_FONTS_RUNTIME_DIR "${IMFORGE_FONTS_BUILD_DIR}")
endif()

set(_third_party "${CMAKE_BINARY_DIR}/THIRD_PARTY.txt")
file(WRITE "${_third_party}"
"ImForge ${PROJECT_VERSION} - third-party components bundled with the editor

Inter ${IMFORGE_INTER_VERSION} (Inter-Regular.ttf, Inter-Bold.ttf)
  Copyright 2016 The Inter Project Authors (https://github.com/rsms/inter)
  License: SIL Open Font License 1.1
  https://github.com/rsms/inter/blob/v${IMFORGE_INTER_VERSION}/LICENSE.txt
  Source: ${IMFORGE_INTER_URL}

JetBrains Mono ${IMFORGE_JETBRAINS_MONO_TAG} (JetBrainsMono-Regular.ttf)
  Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono)
  License: SIL Open Font License 1.1
  https://github.com/JetBrains/JetBrainsMono/blob/${IMFORGE_JETBRAINS_MONO_TAG}/OFL.txt
  Source: ${IMFORGE_JETBRAINS_MONO_URL}

Font Awesome Free ${IMFORGE_FONT_AWESOME_TAG} (fa-solid-900.ttf)
  Fonticons, Inc. (https://fontawesome.com)
  License: fonts SIL Open Font License 1.1, icons CC BY 4.0, code MIT
  https://github.com/FortAwesome/Font-Awesome/blob/${IMFORGE_FONT_AWESOME_TAG}/LICENSE.txt
  Source: ${IMFORGE_FONT_AWESOME_URL}

IconFontCppHeaders (IconsFontAwesome6.h, commit ${IMFORGE_ICON_HEADERS_COMMIT})
  Copyright (c) 2017 Juliette Foucaut and Doug Binks
  License: zlib
  https://github.com/juliettef/IconFontCppHeaders/blob/${IMFORGE_ICON_HEADERS_COMMIT}/licence.txt

Dear ImGui (${IMFORGE_IMGUI_TAG})
  Copyright (c) 2014-2025 Omar Cornut
  License: MIT - https://github.com/ocornut/imgui/blob/master/LICENSE.txt

miniz 3.0.2
  Copyright 2013-2014 RAD Game Tools and Valve Software, 2010-2014 Rich Geldreich and Tenacious Software LLC
  License: MIT - https://github.com/richgel999/miniz/blob/3.0.2/LICENSE

SDL 3.2 (builder/examples only)
  License: zlib - https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt

doctest 2.4.11 (tests only)
  License: MIT - https://github.com/doctest/doctest/blob/v2.4.11/LICENSE.txt
")
if(NOT IMFORGE_FONTS_DIR)
    file(COPY_FILE "${_third_party}" "${IMFORGE_FONTS_BUILD_DIR}/THIRD_PARTY.txt" ONLY_IF_DIFFERENT)
endif()

add_library(imforge_fonts INTERFACE)
add_library(imforge::fonts ALIAS imforge_fonts)
target_include_directories(imforge_fonts INTERFACE "${IMFORGE_FONTS_RUNTIME_DIR}")
target_compile_definitions(imforge_fonts INTERFACE IMFORGE_FONTS_SOURCE_DIR="${IMFORGE_FONTS_RUNTIME_DIR}")
