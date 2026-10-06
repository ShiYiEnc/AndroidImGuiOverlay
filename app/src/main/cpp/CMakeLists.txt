cmake_minimum_required(VERSION 3.22.1)
project(imgui_overlay LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui")
add_library(imgui_overlay SHARED
    native_bridge.cpp vulkan_renderer.cpp template_ui.cpp
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_vulkan.cpp)
target_include_directories(imgui_overlay PRIVATE ${IMGUI_DIR})
target_compile_definitions(imgui_overlay PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
target_compile_options(imgui_overlay PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
set_target_properties(imgui_overlay PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_link_options(imgui_overlay PRIVATE "-Wl,-z,max-page-size=16384")
target_link_libraries(imgui_overlay PRIVATE android log vulkan)
