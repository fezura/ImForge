# imforge_add_test(<name> [extra libs...]) builds tests/<name>_tests.cpp as <name>_tests
function(imforge_add_test name)
    set(_src "${PROJECT_SOURCE_DIR}/tests/${name}_tests.cpp")
    if(NOT EXISTS "${_src}")
        message(WARNING "imforge_add_test(${name}): ${_src} does not exist, skipping")
        return()
    endif()
    set(_target "${name}_tests")
    add_executable(${_target} "${_src}")
    target_include_directories(${_target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(${_target} PRIVATE
        imforge::headers
        imgui
        doctest::doctest
        Threads::Threads
        imforge::warnings
        ${ARGN})
    target_compile_definitions(${_target} PRIVATE
        IMFORGE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        IMFORGE_TEST_DIR="${PROJECT_SOURCE_DIR}/tests"
        IMFORGE_BINARY_DIR="${PROJECT_BINARY_DIR}")
    set_target_properties(${_target} PROPERTIES FOLDER "tests")
    add_test(NAME ${_target} COMMAND ${_target} WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(${_target} PROPERTIES TIMEOUT 600 LABELS "${name}")
endfunction()
