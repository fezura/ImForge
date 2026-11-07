# install tree (cmake --install <build> --prefix <dir>):
#   bin/imforge_builder(.exe), bin/fonts/*          (only with IMFORGE_BUILD_BUILDER)
#   bin/imforge-reflect(.exe) [+ libclang.dll]
#   include/imforge/**/*.h
#   packs/essentials/**
#   share/imforge/examples/projects/**, share/imforge/templates/**, share/imforge/schemas/**
#   share/imforge/dist/*.h                          (single headers from imforge_amalgamate)
#   README.md, LICENSE, CHANGELOG.md, THIRD_PARTY.md / THIRD_PARTY.txt if present
# packages are ImForge-<version>-<os>-<arch>, ZIP everywhere plus TGZ on Linux
if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

set(IMFORGE_INSTALL_BINDIR "bin")
set(IMFORGE_INSTALL_INCLUDEDIR "include")
set(IMFORGE_INSTALL_PACKSDIR "packs")
set(IMFORGE_INSTALL_DATADIR "share/imforge")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
        DESTINATION "${IMFORGE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING PATTERN "*.h")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/packs/essentials"
        DESTINATION "${IMFORGE_INSTALL_PACKSDIR}"
        PATTERN "__pycache__" EXCLUDE)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/examples/projects"
        DESTINATION "${IMFORGE_INSTALL_DATADIR}/examples"
        PATTERN "generated" EXCLUDE
        PATTERN "imgui.ini" EXCLUDE)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/templates" "${PROJECT_SOURCE_DIR}/schemas"
        DESTINATION "${IMFORGE_INSTALL_DATADIR}")

install(FILES
        "${PROJECT_SOURCE_DIR}/README.md"
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
        DESTINATION ".")
foreach(_third_party "${PROJECT_SOURCE_DIR}/THIRD_PARTY.md" "${CMAKE_BINARY_DIR}/THIRD_PARTY.txt")
    if(EXISTS "${_third_party}")
        install(FILES "${_third_party}" DESTINATION ".")
    endif()
endforeach()

if(TARGET imforge-reflect)
    install(TARGETS imforge-reflect RUNTIME DESTINATION "${IMFORGE_INSTALL_BINDIR}")
    if(IMFORGE_HAVE_LIBCLANG AND IMFORGE_LIBCLANG_DLL)
        install(FILES "${IMFORGE_LIBCLANG_DLL}" DESTINATION "${IMFORGE_INSTALL_BINDIR}")
    endif()
endif()

# single headers via tools/amalgamate.py, written to ${CMAKE_BINARY_DIR}/dist
find_package(Python3 COMPONENTS Interpreter QUIET)
set(IMFORGE_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
set(IMFORGE_DIST_HEADERS
    "${IMFORGE_DIST_DIR}/imforge_client_single.h"
    "${IMFORGE_DIST_DIR}/imb_runtime_single.h"
    "${IMFORGE_DIST_DIR}/imb_essentials_single.h")
if(Python3_Interpreter_FOUND)
    file(GLOB_RECURSE _imforge_amalgamate_inputs CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/include/*.h"
        "${PROJECT_SOURCE_DIR}/packs/essentials/*.h")
    add_custom_command(
        OUTPUT ${IMFORGE_DIST_HEADERS}
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/amalgamate.py" --out-dir "${IMFORGE_DIST_DIR}"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/amalgamate.py" ${_imforge_amalgamate_inputs}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Amalgamating single headers into ${IMFORGE_DIST_DIR}"
        VERBATIM)
    add_custom_target(imforge_amalgamate ALL DEPENDS ${IMFORGE_DIST_HEADERS})
    set_target_properties(imforge_amalgamate PROPERTIES FOLDER "tools")
    install(FILES ${IMFORGE_DIST_HEADERS} DESTINATION "${IMFORGE_INSTALL_DATADIR}/dist")
else()
    message(STATUS "ImForge: Python3 not found; imforge_amalgamate and ${IMFORGE_INSTALL_DATADIR}/dist/*.h are not available")
endif()

if(WIN32)
    set(_imforge_pkg_os "windows")
elseif(APPLE)
    set(_imforge_pkg_os "macos")
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _imforge_pkg_os)
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _imforge_pkg_arch)
if(_imforge_pkg_arch MATCHES "^(amd64|x86_64|x64)$")
    set(_imforge_pkg_arch "x64")
elseif(_imforge_pkg_arch MATCHES "^(arm64|aarch64)$")
    set(_imforge_pkg_arch "arm64")
elseif(_imforge_pkg_arch MATCHES "^(x86|i[3-6]86)$")
    set(_imforge_pkg_arch "x86")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND _imforge_pkg_arch STREQUAL "x64")
    set(_imforge_pkg_arch "x86")
endif()

set(CPACK_PACKAGE_NAME "ImForge")
set(CPACK_PACKAGE_VENDOR "ImForge")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ImForge: a visual UI builder for Dear ImGui with live attach and plain C++ export")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_FILE_NAME "ImForge-${PROJECT_VERSION}-${_imforge_pkg_os}-${_imforge_pkg_arch}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_GENERATOR "ZIP")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CPACK_GENERATOR "TGZ")
endif()
set(CPACK_BINARY_NSIS OFF)
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_VERBATIM_VARIABLES ON)
set(IMFORGE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}")
set(IMFORGE_PACKAGE_GENERATORS "${CPACK_GENERATOR}")
include(CPack)

message(STATUS "ImForge: packaging enabled, package ${IMFORGE_PACKAGE_FILE_NAME} (${IMFORGE_PACKAGE_GENERATORS}), single headers=${Python3_Interpreter_FOUND}")
