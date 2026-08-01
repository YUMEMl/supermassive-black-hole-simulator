if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

set(required_files
    "${PROJECT_ROOT}/src/main.cpp"
    "${PROJECT_ROOT}/src/main_vulkan.cpp"
    "${PROJECT_ROOT}/shaders/fullscreen.vert"
    "${PROJECT_ROOT}/shaders/kerr.frag"
    "${PROJECT_ROOT}/shaders/blit.frag"
    "${PROJECT_ROOT}/README.md"
)

foreach(required_file IN LISTS required_files)
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Required project file is missing: ${required_file}")
    endif()
endforeach()

if(EXISTS "${PROJECT_ROOT}/assets/accretion_reference.jpg")
    message(FATAL_ERROR "Removed reference image is still present")
endif()

file(READ "${PROJECT_ROOT}/src/main.cpp" main_source)
file(READ "${PROJECT_ROOT}/src/main_vulkan.cpp" vulkan_source)
file(READ "${PROJECT_ROOT}/shaders/kerr.frag" shader_source)
file(READ "${PROJECT_ROOT}/shaders/blit.frag" blit_source)
file(READ "${PROJECT_ROOT}/shaders/fullscreen.vert" vertex_source)
file(READ "${PROJECT_ROOT}/CMakeLists.txt" cmake_source)

set(required_main_markers
    "constexpr double TargetFrameSeconds = 1.0 / 20.0"
    "glfwWaitEventsTimeout(TargetFrameSeconds)"
    "SliderFloat(\"Viewing angle\", &viewingAngle, 8.0f, 90.0f"
    "glm::radians(-82.0f)"
    "massVisualScale"
    "checkFramebufferStatus(GlFramebuffer)"
    "GLFW_KEY_P"
    "GLFW_KEY_SPACE"
    "AutoOrbitRadiansPerSecond"
    "TON618_DIAGNOSTICS_AUTO_PLAY"
)
foreach(marker IN LISTS required_main_markers)
    string(FIND "${main_source}" "${marker}" marker_position)
    if(marker_position EQUAL -1)
        message(FATAL_ERROR "Runtime safeguard is missing: ${marker}")
    endif()
endforeach()

set(required_vulkan_markers
    "glfwVulkanSupported()"
    "VK_KHR_SWAPCHAIN_EXTENSION_NAME"
    "ImGui_ImplVulkan_Init"
    "recordScenePass"
    "recordWindowPass"
    "recordSwapchainCapture"
    "VK_PRESENT_MODE_FIFO_KHR"
    "TargetFrameSeconds = 1.0 / 20.0"
)
foreach(marker IN LISTS required_vulkan_markers)
    string(FIND "${vulkan_source}" "${marker}" marker_position)
    if(marker_position EQUAL -1)
        message(FATAL_ERROR "Vulkan runtime safeguard is missing: ${marker}")
    endif()
endforeach()

set(required_cmake_markers
    "find_package(Vulkan REQUIRED)"
    "find_program(GLSLC_EXECUTABLE"
    "src/main_vulkan.cpp"
    "imgui_impl_vulkan.cpp"
    "TON618_VULKAN=1"
)
foreach(marker IN LISTS required_cmake_markers)
    string(FIND "${cmake_source}" "${marker}" marker_position)
    if(marker_position EQUAL -1)
        message(FATAL_ERROR "Linux Vulkan build wiring is missing: ${marker}")
    endif()
endforeach()

set(required_shader_markers
    "integrateRk4"
    "diskTurbulence"
    "if (!captured && !escaped)"
    "float captureFallbackRadius = max(horizon * 2.65, 4.25)"
    "state.phi += PI"
)
foreach(marker IN LISTS required_shader_markers)
    string(FIND "${shader_source}" "${marker}" marker_position)
    if(marker_position EQUAL -1)
        message(FATAL_ERROR "Shader safeguard is missing: ${marker}")
    endif()
endforeach()

string(FIND "${shader_source}" "#ifdef TON618_VULKAN" vulkan_shader_marker)
if(vulkan_shader_marker EQUAL -1)
    message(FATAL_ERROR "Kerr shader does not expose the Vulkan path")
endif()

string(FIND "${vertex_source}" "gl_VertexIndex" vulkan_vertex_marker)
if(vulkan_vertex_marker EQUAL -1)
    message(FATAL_ERROR "Fullscreen shader does not use the Vulkan vertex index")
endif()

string(FIND "${blit_source}" "float halfRepairWidth = uAxisRepairWidth" blit_marker_position)
if(blit_marker_position EQUAL -1)
    message(FATAL_ERROR "Final seam repair pass is missing")
endif()
string(FIND "${blit_source}" "vec2 rotatedPixels" polar_marker_position)
if(polar_marker_position EQUAL -1)
    message(FATAL_ERROR "Polar-view repair is missing")
endif()

message(STATUS "Project validation passed")
