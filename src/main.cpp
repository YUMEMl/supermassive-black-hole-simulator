#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <windows.h>
#include <dwmapi.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr GLenum GlRgb = 0x1907;
constexpr GLenum GlRgba8 = 0x8058;
constexpr GLenum GlFramebuffer = 0x8D40;
constexpr GLenum GlColorAttachment0 = 0x8CE0;
constexpr GLenum GlFramebufferComplete = 0x8CD5;
constexpr double TargetFrameSeconds = 1.0 / 20.0;
constexpr float AutoOrbitRadiansPerSecond = 0.16f;

using Uniform1fProc = void(APIENTRY*)(GLint, GLfloat);
using Uniform2fProc = void(APIENTRY*)(GLint, GLfloat, GLfloat);
using Uniform3fProc = void(APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat);
using DrawArraysProc = void(APIENTRY*)(GLenum, GLint, GLsizei);
using GenFramebuffersProc = void(APIENTRY*)(GLsizei, GLuint*);
using BindFramebufferProc = void(APIENTRY*)(GLenum, GLuint);
using FramebufferTexture2DProc = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFramebufferStatusProc = GLenum(APIENTRY*)(GLenum);
using DeleteFramebuffersProc = void(APIENTRY*)(GLsizei, const GLuint*);

Uniform1fProc uniform1f = nullptr;
Uniform2fProc uniform2f = nullptr;
Uniform3fProc uniform3f = nullptr;
DrawArraysProc drawArrays = nullptr;
GenFramebuffersProc genFramebuffers = nullptr;
BindFramebufferProc bindFramebuffer = nullptr;
FramebufferTexture2DProc framebufferTexture2D = nullptr;
CheckFramebufferStatusProc checkFramebufferStatus = nullptr;
DeleteFramebuffersProc deleteFramebuffers = nullptr;

struct Camera {
    float radius = 23.0f;
    float yaw = 0.0f;
    float latitude = glm::radians(12.0f);
    float fov = 47.0f;
};

struct SimulationParameters {
    float massSolarMasses = 6.60e10f;
    float spin = 0.75f;
    float accretionRate = 0.92f;
    float timeScale = 1.0f;
    float diskInner = 3.158f;
    float diskOuter = 16.0f;
    float exposure = 1.12f;
    float minimumStep = 0.0035f;
    float maximumStep = 0.22f;
    int maximumSteps = 256;
};

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

void preciseSleep(double seconds) {
    if (seconds <= 0.0) return;

    // High-resolution waitable timers avoid the 10-16 ms oversleep that a
    // regular Windows sleep can add to a 20 FPS frame budget.
    static HANDLE timer = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        0x00000002, // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
        TIMER_ALL_ACCESS
    );
    if (timer != nullptr) {
        LARGE_INTEGER dueTime{};
        dueTime.QuadPart = -std::max<LONGLONG>(
            1,
            static_cast<LONGLONG>(seconds * 10'000'000.0)
        );
        if (SetWaitableTimer(timer, &dueTime, 0, nullptr, nullptr, FALSE) != FALSE) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }

    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open: " + path.string());
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

GLuint compileShader(GLenum type, const std::string& source, const char* label) {
    const GLuint shader = glCreateShader(type);
    const char* sourcePointer = source.c_str();
    glShaderSource(shader, 1, &sourcePointer, nullptr);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::string(label) + " shader compilation failed:\n" + log);
    }
    return shader;
}

GLuint createProgram(const std::string& vertexSource, const std::string& fragmentSource) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource, "Vertex");
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource, "Fragment");
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Shader link failed:\n" + log);
    }
    return program;
}

template <typename T>
T loadOpenGlProcedure(const char* name) {
    const auto procedure = reinterpret_cast<T>(glfwGetProcAddress(name));
    if (!procedure) throw std::runtime_error(std::string("Missing OpenGL procedure: ") + name);
    return procedure;
}

float progradeIsco(float spin) {
    const float z1 = 1.0f + std::cbrt(1.0f - spin * spin) *
        (std::cbrt(1.0f + spin) + std::cbrt(1.0f - spin));
    const float z2 = std::sqrt(3.0f * spin * spin + z1 * z1);
    return 3.0f + z2 - std::sqrt((3.0f - z1) * (3.0f + z1 + 2.0f * z2));
}

void applyWindowsAppearance(GLFWwindow* window) {
    HWND nativeWindow = glfwGetWin32Window(window);
    const BOOL useDarkMode = FALSE;
    constexpr DWORD immersiveDarkMode = 20;
    DwmSetWindowAttribute(nativeWindow, immersiveDarkMode, &useDarkMode, sizeof(useDarkMode));
    SendMessageW(nativeWindow, WM_SETICON, ICON_BIG, 0);
    SendMessageW(nativeWindow, WM_SETICON, ICON_SMALL, 0);
}

void setUniform1f(GLuint program, const char* name, float value) {
    uniform1f(glGetUniformLocation(program, name), value);
}

void setUniform2f(GLuint program, const char* name, float x, float y) {
    uniform2f(glGetUniformLocation(program, name), x, y);
}

void setUniform3f(GLuint program, const char* name, const glm::vec3& value) {
    uniform3f(glGetUniformLocation(program, name), value.x, value.y, value.z);
}

void renderTelemetry(const SimulationParameters& parameters, const Camera& camera) {
    constexpr double gravitationalConstant = 6.674e-11;
    constexpr double speedOfLight = 3.0e8;
    constexpr double solarMass = 1.98847e30;
    const double horizonInRg = 1.0 + std::sqrt(1.0 - parameters.spin * parameters.spin);
    const double gravitationalRadius = gravitationalConstant * parameters.massSolarMasses * solarMass /
        (speedOfLight * speedOfLight);
    const double horizonMetres = gravitationalRadius * horizonInRg;
    const float viewingAngle = 90.0f - std::abs(glm::degrees(camera.latitude));

    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("Telemetry", nullptr, flags)) {
        ImGui::Text("m=%.3e M_sun", parameters.massSolarMasses);
        ImGui::Text("Ds=%.3f", parameters.spin);
        ImGui::Text("Ar=%.3f L/L_Edd", parameters.accretionRate);
        ImGui::Text("Va=%.1f deg", viewingAngle);
        ImGui::Text("Ts=%.2fx", parameters.timeScale);
        ImGui::Text("G=6.674e-11 m^3 kg^-1 s^-2");
        ImGui::Text("c=3.0e8 m/s");
        ImGui::Text("Eh=%.2e m", horizonMetres);
    }
    ImGui::End();
}

void renderControls(
    SimulationParameters& parameters,
    Camera& camera,
    bool& visible,
    bool autoPlay
) {
    if (!visible) return;

    ImGui::SetNextWindowPos(ImVec2(18.0f, 210.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(
        "Parameters",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse
    )) {
        ImGui::SliderFloat(
            "Mass (M_sun)",
            &parameters.massSolarMasses,
            1.0e6f,
            1.0e11f,
            "%.3e",
            ImGuiSliderFlags_Logarithmic
        );

        const float previousSpin = parameters.spin;
        ImGui::SliderFloat("Dimensionless spin (a/M)", &parameters.spin, 0.0f, 0.998f, "%.3f");
        if (previousSpin != parameters.spin) {
            parameters.diskInner = progradeIsco(parameters.spin);
        }

        ImGui::SliderFloat("Accretion rate (L/L_Edd)", &parameters.accretionRate, 0.01f, 2.0f, "%.3f");

        float viewingAngle = 90.0f - std::abs(glm::degrees(camera.latitude));
        if (ImGui::SliderFloat("Viewing angle", &viewingAngle, 8.0f, 90.0f, "%.1f deg")) {
            const float hemisphere = camera.latitude < 0.0f ? -1.0f : 1.0f;
            camera.latitude = hemisphere * glm::radians(90.0f - viewingAngle);
        }

        ImGui::SliderFloat("Time scale", &parameters.timeScale, 0.0f, 5.0f, "%.2fx");
        ImGui::Separator();
        ImGui::Text("Auto play: %s", autoPlay ? "ON" : "OFF");
        ImGui::TextUnformatted("WASD: move/orbit   Q/E: latitude");
        ImGui::TextUnformatted("P: auto play   Space: pause/resume");
        ImGui::TextUnformatted("Mouse drag: look   F1: hide controls");
    }
    ImGui::End();
}

void writeDiagnostics(
    int width,
    int height,
    float framesPerSecond,
    bool autoPlay,
    float cameraYaw,
    double simulationTime
) {
    const auto previewPath = std::filesystem::temp_directory_path() / "ton618-native-preview.png";
    const auto reportPath = std::filesystem::temp_directory_path() / "ton618-native-performance.txt";
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GlRgb, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    stbi_write_png(previewPath.string().c_str(), width, height, 3, pixels.data(), width * 3);

    std::ofstream report(reportPath, std::ios::binary);
    report << "fps=" << framesPerSecond << '\n';
    report << "renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << '\n';
    report << "vendor=" << reinterpret_cast<const char*>(glGetString(GL_VENDOR)) << '\n';
    report << "opengl=" << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << '\n';
    report << "auto_play=" << (autoPlay ? 1 : 0) << '\n';
    report << "camera_yaw=" << cameraYaw << '\n';
    report << "simulation_time=" << simulationTime << '\n';
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    try {
        const auto appDirectory = executableDirectory();
        SetCurrentDirectoryW(appDirectory.c_str());

        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, 0);

        GLFWwindow* window = glfwCreateWindow(
            1100,
            700,
            "Supermassive Black Hole-simulator",
            nullptr,
            nullptr
        );
        if (!window) {
            glfwTerminate();
            throw std::runtime_error("OpenGL 3.3 context creation failed");
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(0);
        applyWindowsAppearance(window);

        if (imgl3wInit2(reinterpret_cast<GL3WGetProcAddressProc>(glfwGetProcAddress)) != 0) {
            throw std::runtime_error("OpenGL procedure loading failed");
        }
        uniform1f = loadOpenGlProcedure<Uniform1fProc>("glUniform1f");
        uniform2f = loadOpenGlProcedure<Uniform2fProc>("glUniform2f");
        uniform3f = loadOpenGlProcedure<Uniform3fProc>("glUniform3f");
        drawArrays = loadOpenGlProcedure<DrawArraysProc>("glDrawArrays");
        genFramebuffers = loadOpenGlProcedure<GenFramebuffersProc>("glGenFramebuffers");
        bindFramebuffer = loadOpenGlProcedure<BindFramebufferProc>("glBindFramebuffer");
        framebufferTexture2D = loadOpenGlProcedure<FramebufferTexture2DProc>("glFramebufferTexture2D");
        checkFramebufferStatus = loadOpenGlProcedure<CheckFramebufferStatusProc>("glCheckFramebufferStatus");
        deleteFramebuffers = loadOpenGlProcedure<DeleteFramebuffersProc>("glDeleteFramebuffers");

        const std::string vertexSource = readTextFile(appDirectory / "shaders" / "fullscreen.vert");
        const std::string raySource = readTextFile(appDirectory / "shaders" / "kerr.frag");
        const std::string blitSource = readTextFile(appDirectory / "shaders" / "blit.frag");
        const GLuint rayProgram = createProgram(vertexSource, raySource);
        const GLuint blitProgram = createProgram(vertexSource, blitSource);

        GLuint fullscreenVao = 0;
        glGenVertexArrays(1, &fullscreenVao);
        GLuint sceneFramebuffer = 0;
        GLuint sceneTexture = 0;
        genFramebuffers(1, &sceneFramebuffer);
        glGenTextures(1, &sceneTexture);
        glBindTexture(GL_TEXTURE_2D, sceneTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        int sceneWidth = 0;
        int sceneHeight = 0;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding = 6.0f;
        ImGui::GetStyle().FrameRounding = 4.0f;
        const char* segoeUi = "C:\\Windows\\Fonts\\segoeui.ttf";
        if (std::filesystem::exists(segoeUi)) io.Fonts->AddFontFromFileTTF(segoeUi, 15.0f);
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330 core");

        Camera camera;
        if (std::getenv("TON618_DIAGNOSTICS_ZOOMED_OUT") != nullptr) {
            camera.radius = 52.0f;
        }
        SimulationParameters parameters;
        if (std::getenv("TON618_DIAGNOSTICS_EDGE_ON") != nullptr) {
            camera.latitude = glm::radians(82.0f);
        }
        if (std::getenv("TON618_DIAGNOSTICS_LOW_MASS") != nullptr) {
            parameters.massSolarMasses = 1.0e6f;
        } else if (std::getenv("TON618_DIAGNOSTICS_HIGH_MASS") != nullptr) {
            parameters.massSolarMasses = 1.0e11f;
        }
        parameters.diskInner = progradeIsco(parameters.spin);
        bool showControls = true;
        bool autoPlay = std::getenv("TON618_DIAGNOSTICS_AUTO_PLAY") != nullptr;
        bool previousF1 = false;
        bool previousP = false;
        bool previousSpace = false;
        bool dragging = false;
        const bool diagnosticsEnabled = std::getenv("TON618_DIAGNOSTICS") != nullptr;
        bool diagnosticsWritten = false;
        double previousMouseX = 0.0;
        double previousMouseY = 0.0;
        double previousTime = glfwGetTime() - TargetFrameSeconds;
        double simulationTime = 0.0;
        float smoothedFps = 20.0f;
        float resumeTimeScale = std::max(parameters.timeScale, 0.01f);

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            const double now = glfwGetTime();
            const float deltaTime = static_cast<float>(std::min(now - previousTime, 0.125));
            previousTime = now;
            const float instantaneousFps = deltaTime > 0.00001f ? 1.0f / deltaTime : smoothedFps;
            smoothedFps += (instantaneousFps - smoothedFps) * 0.04f;
            simulationTime += static_cast<double>(deltaTime) * parameters.timeScale;

            const bool f1 = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
            if (f1 && !previousF1) showControls = !showControls;
            previousF1 = f1;
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);

            const bool p = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
            const bool space = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
            if (!io.WantCaptureKeyboard) {
                if (p && !previousP) {
                    autoPlay = !autoPlay;
                    if (autoPlay && parameters.timeScale <= 0.001f) {
                        parameters.timeScale = resumeTimeScale;
                    }
                }
                if (space && !previousSpace) {
                    if (parameters.timeScale > 0.001f) {
                        resumeTimeScale = parameters.timeScale;
                        parameters.timeScale = 0.0f;
                    } else {
                        parameters.timeScale = std::max(resumeTimeScale, 0.01f);
                    }
                }
                const float movement = 6.0f * deltaTime;
                if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.radius -= movement;
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.radius += movement;
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.yaw -= movement * 0.22f;
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.yaw += movement * 0.22f;
                if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera.latitude += movement * 0.12f;
                if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera.latitude -= movement * 0.12f;
            }
            previousP = p;
            previousSpace = space;
            if (autoPlay && parameters.timeScale > 0.001f) {
                camera.yaw += AutoOrbitRadiansPerSecond * deltaTime;
            }
            camera.radius = std::clamp(camera.radius, 7.0f, 52.0f);
            camera.latitude = std::clamp(camera.latitude, glm::radians(-82.0f), glm::radians(82.0f));

            double mouseX = 0.0;
            double mouseY = 0.0;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            const bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            if (mouseDown && !io.WantCaptureMouse) {
                if (dragging) {
                    camera.yaw -= static_cast<float>(mouseX - previousMouseX) * 0.0045f;
                    camera.latitude += static_cast<float>(mouseY - previousMouseY) * 0.0045f;
                    camera.latitude = std::clamp(camera.latitude, glm::radians(-82.0f), glm::radians(82.0f));
                }
                dragging = true;
            } else {
                dragging = false;
            }
            previousMouseX = mouseX;
            previousMouseY = mouseY;

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0) {
                glfwWaitEventsTimeout(TargetFrameSeconds);
                continue;
            }

            constexpr float ReferenceMassSolarMasses = 6.60e10f;
            const float massRatio = std::max(
                parameters.massSolarMasses / ReferenceMassSolarMasses,
                1.0e-6f
            );
            const float massVisualScale = std::clamp(std::pow(massRatio, 0.12f), 0.40f, 1.18f);
            const float renderedCameraRadius = std::clamp(camera.radius / massVisualScale, 7.0f, 52.0f);
            const glm::vec3 cameraPosition = renderedCameraRadius * glm::vec3(
                std::cos(camera.latitude) * std::sin(camera.yaw),
                std::sin(camera.latitude),
                std::cos(camera.latitude) * std::cos(camera.yaw)
            );
            const glm::vec3 forward = glm::normalize(-cameraPosition);
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            if (glm::length(right) < 0.01f) right = glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 up = glm::normalize(glm::cross(right, forward));

            if (framebufferWidth != sceneWidth || framebufferHeight != sceneHeight) {
                glBindTexture(GL_TEXTURE_2D, sceneTexture);
                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GlRgba8,
                    framebufferWidth,
                    framebufferHeight,
                    0,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    nullptr
                );
                bindFramebuffer(GlFramebuffer, sceneFramebuffer);
                framebufferTexture2D(
                    GlFramebuffer,
                    GlColorAttachment0,
                    GL_TEXTURE_2D,
                    sceneTexture,
                    0
                );
                if (checkFramebufferStatus(GlFramebuffer) != GlFramebufferComplete) {
                    throw std::runtime_error("Scene framebuffer creation failed");
                }
                sceneWidth = framebufferWidth;
                sceneHeight = framebufferHeight;
            }

            bindFramebuffer(GlFramebuffer, sceneFramebuffer);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glDisable(GL_BLEND);
            glClearColor(0.002f, 0.003f, 0.008f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(rayProgram);
            glBindVertexArray(fullscreenVao);
            setUniform2f(rayProgram, "uResolution", static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight));
            setUniform1f(rayProgram, "uTime", static_cast<float>(simulationTime));
            setUniform3f(rayProgram, "uCameraPosition", cameraPosition);
            setUniform3f(rayProgram, "uCameraForward", forward);
            setUniform3f(rayProgram, "uCameraRight", right);
            setUniform3f(rayProgram, "uCameraUp", up);
            setUniform1f(rayProgram, "uFov", camera.fov);
            setUniform1f(rayProgram, "uSpin", parameters.spin);
            setUniform1f(rayProgram, "uDiskInner", parameters.diskInner);
            setUniform1f(rayProgram, "uDiskOuter", parameters.diskOuter);
            setUniform1f(rayProgram, "uAccretionRate", parameters.accretionRate);
            setUniform1f(rayProgram, "uExposure", parameters.exposure);
            setUniform1f(rayProgram, "uMinStep", parameters.minimumStep);
            setUniform1f(rayProgram, "uMaxStep", parameters.maximumStep);
            glUniform1i(glGetUniformLocation(rayProgram, "uMaxSteps"), parameters.maximumSteps);
            drawArrays(GL_TRIANGLES, 0, 3);

            bindFramebuffer(GlFramebuffer, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(blitProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sceneTexture);
            glUniform1i(glGetUniformLocation(blitProgram, "uScene"), 0);
            setUniform2f(
                blitProgram,
                "uResolution",
                static_cast<float>(framebufferWidth),
                static_cast<float>(framebufferHeight)
            );
            const float polarViewAmount = glm::smoothstep(
                glm::radians(68.0f),
                glm::radians(82.0f),
                std::abs(camera.latitude)
            );
            setUniform1f(blitProgram, "uAxisRepairWidth", 12.0f + 24.0f * polarViewAmount);
            setUniform1f(blitProgram, "uPolarRepairAmount", polarViewAmount);
            drawArrays(GL_TRIANGLES, 0, 3);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            renderTelemetry(parameters, camera);
            renderControls(parameters, camera, showControls, autoPlay);
            if (parameters.timeScale > 0.001f) {
                resumeTimeScale = parameters.timeScale;
            }
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            if (diagnosticsEnabled && !diagnosticsWritten && now > 4.0) {
                writeDiagnostics(
                    framebufferWidth,
                    framebufferHeight,
                    smoothedFps,
                    autoPlay,
                    camera.yaw,
                    simulationTime
                );
                diagnosticsWritten = true;
            }
            glfwSwapBuffers(window);

            // The relativistic shader is intentionally capped at 20 FPS. It
            // keeps the requested visual fidelity while allowing the CPU and
            // GPU to idle for the remainder of each frame.
            const double frameElapsed = glfwGetTime() - now;
            const double idleSeconds = TargetFrameSeconds - frameElapsed;
            preciseSleep(idleSeconds);
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glDeleteTextures(1, &sceneTexture);
        deleteFramebuffers(1, &sceneFramebuffer);
        glDeleteVertexArrays(1, &fullscreenVao);
        glDeleteProgram(blitProgram);
        glDeleteProgram(rayProgram);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Supermassive Black Hole-simulator", MB_OK | MB_ICONERROR);
        return 1;
    }
}
