add_library(imforge_essentials INTERFACE)
add_library(imforge::essentials ALIAS imforge_essentials)
target_include_directories(imforge_essentials INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/essentials")
target_link_libraries(imforge_essentials INTERFACE imforge::headers)

add_library(imforge_controls INTERFACE)
add_library(imforge::controls ALIAS imforge_controls)
target_include_directories(imforge_controls INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/controls")
target_link_libraries(imforge_controls INTERFACE imforge::headers)

add_library(imforge_display INTERFACE)
add_library(imforge::display ALIAS imforge_display)
target_include_directories(imforge_display INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/display")
target_link_libraries(imforge_display INTERFACE imforge::headers)

add_library(imforge_gaming INTERFACE)
add_library(imforge::gaming ALIAS imforge_gaming)
target_include_directories(imforge_gaming INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/gaming")
target_link_libraries(imforge_gaming INTERFACE imforge::headers imforge_essentials)
