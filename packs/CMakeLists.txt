add_library(imforge_essentials INTERFACE)
add_library(imforge::essentials ALIAS imforge_essentials)
target_include_directories(imforge_essentials INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/essentials")
target_link_libraries(imforge_essentials INTERFACE imforge::headers)
