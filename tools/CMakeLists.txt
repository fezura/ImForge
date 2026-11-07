add_executable(imforge-fmt "${CMAKE_CURRENT_SOURCE_DIR}/imforge_fmt.cpp")
target_link_libraries(imforge-fmt PRIVATE imforge::headers imforge::warnings)
set_target_properties(imforge-fmt PROPERTIES FOLDER "tools")
