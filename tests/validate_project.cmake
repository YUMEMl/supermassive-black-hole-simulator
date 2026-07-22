if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

set(required_files
    "${PROJECT_ROOT}/src/main.cpp"
    "${PROJECT_ROOT}/shaders/fullscreen.vert"
    "${PROJECT_ROOT}/shaders/kerr.frag"
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
file(READ "${PROJECT_ROOT}/shaders/kerr.frag" shader_source)
file(READ "${PROJECT_ROOT}/shaders/blit.frag" blit_source)

set(required_main_markers
    "constexpr double TargetFrameSeconds = 1.0 / 20.0"
    "glfwWaitEventsTimeout(TargetFrameSeconds)"
    "SliderFloat(\"Viewing angle\", &viewingAngle, 8.0f, 90.0f"
    "glm::radians(-82.0f)"
    "massVisualScale"
    "checkFramebufferStatus(GlFramebuffer)"
)
foreach(marker IN LISTS required_main_markers)
    string(FIND "${main_source}" "${marker}" marker_position)
    if(marker_position EQUAL -1)
        message(FATAL_ERROR "Runtime safeguard is missing: ${marker}")
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

string(FIND "${blit_source}" "float halfRepairWidth = uAxisRepairWidth" blit_marker_position)
if(blit_marker_position EQUAL -1)
    message(FATAL_ERROR "Final seam repair pass is missing")
endif()
string(FIND "${blit_source}" "vec2 rotatedPixels" polar_marker_position)
if(polar_marker_position EQUAL -1)
    message(FATAL_ERROR "Polar-view repair is missing")
endif()

message(STATUS "Project validation passed")
