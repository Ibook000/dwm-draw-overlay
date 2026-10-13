cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(CaptureExcludedOverlay LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use MSVC and an x64 generator on Windows")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(DWM_OVERLAY_OUTPUT_NAME "overlay_python" CACHE STRING "Renderer executable name")
add_executable(overlay_demo
    src/overlay_demo.cpp src/CDwmMsg.cpp src/CIMGuiDraw.cpp
    src/MyImGui.cpp src/save_state.cpp
    vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_widgets.cpp vendor/imgui/imgui_impl_dx11.cpp)
target_include_directories(overlay_demo PRIVATE src vendor/imgui)
target_compile_definitions(overlay_demo PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(overlay_demo PRIVATE /W3 /utf-8)
target_link_libraries(overlay_demo PRIVATE d3d11 dxgi d3dcompiler dwmapi user32 gdi32)
set_target_properties(overlay_demo PROPERTIES
    OUTPUT_NAME "${DWM_OVERLAY_OUTPUT_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin")
add_executable(draw_example examples/draw_functions.cpp src/CDwmMsg.cpp)
target_include_directories(draw_example PRIVATE src vendor/imgui)
target_compile_definitions(draw_example PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(draw_example PRIVATE /W3 /utf-8)
set_target_properties(draw_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin")
add_library(dwm_scroll STATIC src/DwmScrollPanel.cpp)
target_include_directories(dwm_scroll PUBLIC src vendor/imgui)
target_compile_definitions(dwm_scroll PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(dwm_scroll PRIVATE /W3 /utf-8)
target_link_libraries(dwm_scroll PUBLIC user32)
add_library(dwm_draw SHARED src/python_api.cpp src/CDwmMsg.cpp)
target_link_libraries(dwm_draw PRIVATE dwm_scroll)
target_compile_options(dwm_draw PRIVATE /W3 /utf-8)
set_target_properties(dwm_draw PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin")
add_executable(scroll_example examples/scroll_panel.cpp src/CDwmMsg.cpp)
target_link_libraries(scroll_example PRIVATE dwm_scroll)
target_compile_options(scroll_example PRIVATE /W3 /utf-8)
set_target_properties(scroll_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin")
include(CTest)
if(BUILD_TESTING)
    add_executable(drawing_api_test tests/drawing_api.cpp src/CDwmMsg.cpp)
    target_include_directories(drawing_api_test PRIVATE src vendor/imgui)
    target_compile_definitions(drawing_api_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(drawing_api_test PRIVATE /W3 /utf-8)
    add_test(NAME drawing_api COMMAND drawing_api_test)
    add_executable(scroll_panel_test tests/scroll_panel.cpp src/CDwmMsg.cpp)
    target_link_libraries(scroll_panel_test PRIVATE dwm_scroll)
    target_compile_options(scroll_panel_test PRIVATE /W3 /utf-8)
    add_test(NAME scroll_panel COMMAND scroll_panel_test)
endif()
