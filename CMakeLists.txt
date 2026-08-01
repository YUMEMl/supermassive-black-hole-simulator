cmake_minimum_required(VERSION 3.20)
project(TON618Native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")

set(GLFW_ROOT "${CMAKE_SOURCE_DIR}/third_party/glfw")
set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/third_party/imgui")

if(WIN32)
    add_library(glfw STATIC
        ${GLFW_ROOT}/src/context.c
        ${GLFW_ROOT}/src/init.c
        ${GLFW_ROOT}/src/input.c
        ${GLFW_ROOT}/src/monitor.c
        ${GLFW_ROOT}/src/platform.c
        ${GLFW_ROOT}/src/vulkan.c
        ${GLFW_ROOT}/src/window.c
        ${GLFW_ROOT}/src/egl_context.c
        ${GLFW_ROOT}/src/osmesa_context.c
        ${GLFW_ROOT}/src/null_init.c
        ${GLFW_ROOT}/src/null_monitor.c
        ${GLFW_ROOT}/src/null_window.c
        ${GLFW_ROOT}/src/null_joystick.c
        ${GLFW_ROOT}/src/win32_init.c
        ${GLFW_ROOT}/src/win32_joystick.c
        ${GLFW_ROOT}/src/win32_monitor.c
        ${GLFW_ROOT}/src/win32_window.c
        ${GLFW_ROOT}/src/wgl_context.c
        ${GLFW_ROOT}/src/win32_module.c
        ${GLFW_ROOT}/src/win32_time.c
        ${GLFW_ROOT}/src/win32_thread.c
    )
    target_compile_definitions(glfw PRIVATE _GLFW_WIN32 _CRT_SECURE_NO_WARNINGS)
    target_include_directories(glfw PUBLIC ${GLFW_ROOT}/include PRIVATE ${GLFW_ROOT}/src)
    target_link_libraries(glfw PRIVATE opengl32 gdi32 user32 shell32)

    add_library(imgui STATIC
        ${IMGUI_ROOT}/imgui.cpp
        ${IMGUI_ROOT}/imgui_draw.cpp
        ${IMGUI_ROOT}/imgui_tables.cpp
        ${IMGUI_ROOT}/imgui_widgets.cpp
        ${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp
        ${IMGUI_ROOT}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_ROOT} ${IMGUI_ROOT}/backends)
    target_link_libraries(imgui PUBLIC glfw opengl32)

    add_executable(ton618 WIN32 src/main.cpp)
    target_compile_definitions(ton618 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(ton618 PRIVATE imgui glfw opengl32 gdi32 user32 shell32 dwmapi)

    if(MINGW)
        target_compile_options(ton618 PRIVATE -O3 -ffast-math -Wall -Wextra)
        target_link_options(ton618 PRIVATE -static-libgcc -static-libstdc++)
    endif()

    add_custom_command(TARGET ton618 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ton618>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/shaders/fullscreen.vert"
                "$<TARGET_FILE_DIR:ton618>/shaders/fullscreen.vert"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/shaders/blit.frag"
                "$<TARGET_FILE_DIR:ton618>/shaders/blit.frag"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/shaders/kerr.frag"
                "$<TARGET_FILE_DIR:ton618>/shaders/kerr.frag"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
                "$<TARGET_FILE_DIR:ton618>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/README.md"
                "$<TARGET_FILE_DIR:ton618>/README.md"
    )
else()
    find_package(Vulkan REQUIRED)
    find_program(GLSLC_EXECUTABLE NAMES glslc REQUIRED)

    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    add_subdirectory(${GLFW_ROOT} third_party/glfw EXCLUDE_FROM_ALL)

    add_library(imgui STATIC
        ${IMGUI_ROOT}/imgui.cpp
        ${IMGUI_ROOT}/imgui_draw.cpp
        ${IMGUI_ROOT}/imgui_tables.cpp
        ${IMGUI_ROOT}/imgui_widgets.cpp
        ${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp
        ${IMGUI_ROOT}/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_ROOT} ${IMGUI_ROOT}/backends)
    target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

    add_executable(ton618 src/main_vulkan.cpp)
    target_link_libraries(ton618 PRIVATE imgui glfw Vulkan::Vulkan)
    target_compile_options(ton618 PRIVATE -O3 -ffast-math -Wall -Wextra -Wpedantic)

    set(VULKAN_SHADER_OUTPUTS)
    foreach(shader_spec
        "fullscreen.vert;vert"
        "kerr.frag;frag"
        "blit.frag;frag"
    )
        list(GET shader_spec 0 shader_file)
        list(GET shader_spec 1 shader_stage)
        set(shader_source "${CMAKE_SOURCE_DIR}/shaders/${shader_file}")
        set(shader_output "${CMAKE_SOURCE_DIR}/dist/shaders/${shader_file}.spv")
        add_custom_command(
            OUTPUT "${shader_output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist/shaders"
            COMMAND ${GLSLC_EXECUTABLE}
                    -std=450core
                    -DTON618_VULKAN=1
                    -fshader-stage=${shader_stage}
                    "${shader_source}"
                    -o "${shader_output}"
            DEPENDS "${shader_source}"
            VERBATIM
        )
        list(APPEND VULKAN_SHADER_OUTPUTS "${shader_output}")
    endforeach()

    add_custom_target(ton618_vulkan_shaders ALL DEPENDS ${VULKAN_SHADER_OUTPUTS})
    add_dependencies(ton618 ton618_vulkan_shaders)

    add_custom_command(TARGET ton618 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/README.md"
                "$<TARGET_FILE_DIR:ton618>/README.md"
    )
endif()

set_target_properties(ton618 PROPERTIES OUTPUT_NAME "Supermassive Black Hole-simulator")
target_include_directories(ton618 PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/glm
    ${CMAKE_SOURCE_DIR}/third_party/stb
    ${IMGUI_ROOT}
    ${IMGUI_ROOT}/backends
)

enable_testing()
add_test(
    NAME project_validation
    COMMAND ${CMAKE_COMMAND}
        "-DPROJECT_ROOT=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/tests/validate_project.cmake"
)
