#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double TargetFrameSeconds = 1.0 / 20.0;
constexpr float AutoOrbitRadiansPerSecond = 0.16f;
constexpr uint32_t MinImageCount = 2;
constexpr VkFormat SceneFormat = VK_FORMAT_R8G8B8A8_UNORM;

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

struct RayPushConstants {
    glm::vec4 resolutionTime{};
    glm::vec4 cameraPositionFov{};
    glm::vec4 cameraForwardSpin{};
    glm::vec4 cameraRightDiskInner{};
    glm::vec4 cameraUpDiskOuter{};
    glm::vec4 integration{};
    glm::ivec4 limits{};
};

struct BlitPushConstants {
    glm::vec4 resolutionRepair{};
};

static_assert(sizeof(RayPushConstants) == 112, "Unexpected Vulkan push constant layout");
static_assert(sizeof(BlitPushConstants) == 16, "Unexpected Vulkan push constant layout");

void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << '\n';
}

void checkVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with VkResult " + std::to_string(result)
        );
    }
}

void imguiCheckVkResult(VkResult result) {
    if (result < 0) {
        std::cerr << "ImGui Vulkan backend error: VkResult " << result << '\n';
    }
}

std::filesystem::path executableDirectory() {
    std::error_code error;
    const auto executable = std::filesystem::canonical("/proc/self/exe", error);
    if (!error) return executable.parent_path();
    return std::filesystem::current_path();
}

void preciseSleep(double seconds) {
    if (seconds <= 0.0) return;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

std::vector<uint32_t> readSpirv(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Unable to open SPIR-V shader: " + path.string());
    const std::streamsize size = input.tellg();
    if (size <= 0 || size % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V shader size: " + path.string());
    }
    input.seekg(0, std::ios::beg);
    std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
    if (!input.read(reinterpret_cast<char*>(code.data()), size)) {
        throw std::runtime_error("Unable to read SPIR-V shader: " + path.string());
    }
    return code;
}

float progradeIsco(float spin) {
    const float z1 = 1.0f + std::cbrt(1.0f - spin * spin) *
        (std::cbrt(1.0f + spin) + std::cbrt(1.0f - spin));
    const float z2 = std::sqrt(3.0f * spin * spin + z1 * z1);
    return 3.0f + z2 - std::sqrt((3.0f - z1) * (3.0f + z1 + 2.0f * z2));
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
        ImGui::Text("Renderer: Vulkan");
        ImGui::Text("Auto play: %s", autoPlay ? "ON" : "OFF");
        ImGui::TextUnformatted("WASD: move/orbit   Q/E: latitude");
        ImGui::TextUnformatted("P: auto play   Space: pause/resume");
        ImGui::TextUnformatted("Mouse drag: look   F1: hide controls");
    }
    ImGui::End();
}

class VulkanRenderer {
public:
    void initialize(GLFWwindow* window, const std::filesystem::path& appDirectory) {
        appDirectory_ = appDirectory;
        createInstance();
        checkVk(glfwCreateWindowSurface(instance_, window, nullptr, &surface_), "glfwCreateWindowSurface");
        selectPhysicalDevice();
        createDevice();
        createDescriptorPool();
        createSwapchain(window);
        createOffscreenRenderPass();
        createSampler();
        createDescriptorResources();
        createPipelines();
        createOffscreen(windowData_.Width, windowData_.Height);
    }

    void initializeImGui(GLFWwindow* window) {
        ImGui_ImplGlfw_InitForVulkan(window, true);
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_1;
        initInfo.Instance = instance_;
        initInfo.PhysicalDevice = physicalDevice_;
        initInfo.Device = device_;
        initInfo.QueueFamily = queueFamily_;
        initInfo.Queue = queue_;
        initInfo.DescriptorPool = descriptorPool_;
        initInfo.MinImageCount = MinImageCount;
        initInfo.ImageCount = windowData_.ImageCount;
        initInfo.PipelineInfoMain.RenderPass = windowData_.RenderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.CheckVkResultFn = imguiCheckVkResult;
        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            throw std::runtime_error("ImGui Vulkan backend initialization failed");
        }
        imguiInitialized_ = true;
    }

    void resize(int width, int height) {
        if (width <= 0 || height <= 0) return;
        checkVk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle before resize");
        if (imguiInitialized_) ImGui_ImplVulkan_SetMinImageCount(MinImageCount);
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            instance_,
            physicalDevice_,
            device_,
            &windowData_,
            queueFamily_,
            nullptr,
            width,
            height,
            MinImageCount,
            swapchainImageUsage_
        );
        windowData_.FrameIndex = 0;
        createOffscreen(windowData_.Width, windowData_.Height);
        swapchainRebuild_ = false;
    }

    bool needsResize(int width, int height) const {
        return swapchainRebuild_ || windowData_.Width != width || windowData_.Height != height;
    }

    bool render(
        ImDrawData* drawData,
        const RayPushConstants& rayParameters,
        const BlitPushConstants& blitParameters,
        bool captureRequested,
        const std::filesystem::path& capturePath
    ) {
        VkSemaphore imageAcquired =
            windowData_.FrameSemaphores[windowData_.SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore renderComplete =
            windowData_.FrameSemaphores[windowData_.SemaphoreIndex].RenderCompleteSemaphore;

        VkResult result = vkAcquireNextImageKHR(
            device_,
            windowData_.Swapchain,
            std::numeric_limits<uint64_t>::max(),
            imageAcquired,
            VK_NULL_HANDLE,
            &windowData_.FrameIndex
        );
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainRebuild_ = true;
            return false;
        }
        if (result == VK_SUBOPTIMAL_KHR) {
            swapchainRebuild_ = true;
        } else {
            checkVk(result, "vkAcquireNextImageKHR");
        }

        ImGui_ImplVulkanH_Frame& frame = windowData_.Frames[windowData_.FrameIndex];
        checkVk(
            vkWaitForFences(device_, 1, &frame.Fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
            "vkWaitForFences"
        );
        checkVk(vkResetFences(device_, 1, &frame.Fence), "vkResetFences");
        checkVk(vkResetCommandPool(device_, frame.CommandPool, 0), "vkResetCommandPool");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checkVk(vkBeginCommandBuffer(frame.CommandBuffer, &beginInfo), "vkBeginCommandBuffer");

        recordScenePass(frame.CommandBuffer, rayParameters);
        recordWindowPass(frame.CommandBuffer, frame.Framebuffer, drawData, blitParameters);

        CaptureBuffer capture{};
        if (captureRequested) {
            capture = createCaptureBuffer();
            if (captureSwapchainSupported_) {
                recordSwapchainCapture(frame.CommandBuffer, frame.Backbuffer, capture);
            } else {
                recordSceneCapture(frame.CommandBuffer, capture);
            }
        }
        checkVk(vkEndCommandBuffer(frame.CommandBuffer), "vkEndCommandBuffer");

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAcquired;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &frame.CommandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderComplete;
        checkVk(vkQueueSubmit(queue_, 1, &submitInfo, frame.Fence), "vkQueueSubmit");

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderComplete;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &windowData_.Swapchain;
        presentInfo.pImageIndices = &windowData_.FrameIndex;
        result = vkQueuePresentKHR(queue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            swapchainRebuild_ = true;
        } else {
            checkVk(result, "vkQueuePresentKHR");
        }
        windowData_.SemaphoreIndex =
            (windowData_.SemaphoreIndex + 1) % windowData_.SemaphoreCount;

        bool captureWritten = false;
        if (captureRequested) {
            checkVk(
                vkWaitForFences(device_, 1, &frame.Fence, VK_TRUE, std::numeric_limits<uint64_t>::max()),
                "vkWaitForFences for diagnostics"
            );
            captureWritten = writeCapture(capture, capturePath);
            destroyCaptureBuffer(capture);
        }
        return captureWritten;
    }

    void waitIdle() {
        if (device_ != VK_NULL_HANDLE) checkVk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }

    const VkPhysicalDeviceProperties& physicalDeviceProperties() const {
        return physicalDeviceProperties_;
    }

    void shutdown() {
        if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
        destroyOffscreen();
        if (rayPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, rayPipeline_, nullptr);
        if (blitPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, blitPipeline_, nullptr);
        if (rayPipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, rayPipelineLayout_, nullptr);
        if (blitPipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, blitPipelineLayout_, nullptr);
        if (blitDescriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, blitDescriptorSetLayout_, nullptr);
        }
        if (sceneSampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sceneSampler_, nullptr);
        if (offscreenRenderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, offscreenRenderPass_, nullptr);
        if (windowData_.Swapchain != VK_NULL_HANDLE) {
            ImGui_ImplVulkanH_DestroyWindow(instance_, device_, &windowData_, nullptr);
        }
        if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
        if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);

        instance_ = VK_NULL_HANDLE;
        physicalDevice_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        surface_ = VK_NULL_HANDLE;
        imguiInitialized_ = false;
    }

private:
    struct CaptureBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        int width = 0;
        int height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    void createInstance() {
        uint32_t extensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (glfwExtensions == nullptr || extensionCount == 0) {
            throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
        }

        VkApplicationInfo applicationInfo{};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "Supermassive Black Hole Simulator";
        applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 1, 0);
        applicationInfo.pEngineName = "TON618 Native";
        applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 1, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = glfwExtensions;
        checkVk(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
    }

    void selectPhysicalDevice() {
        uint32_t deviceCount = 0;
        checkVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
        if (deviceCount == 0) throw std::runtime_error("No Vulkan physical device was found");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        checkVk(
            vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
            "vkEnumeratePhysicalDevices"
        );

        int bestScore = -1;
        for (VkPhysicalDevice candidate : devices) {
            uint32_t queueCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());

            for (uint32_t index = 0; index < queueCount; ++index) {
                VkBool32 presentSupported = VK_FALSE;
                checkVk(
                    vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface_, &presentSupported),
                    "vkGetPhysicalDeviceSurfaceSupportKHR"
                );
                if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 || presentSupported != VK_TRUE) {
                    continue;
                }

                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(candidate, &properties);
                const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10;
                if (score > bestScore) {
                    bestScore = score;
                    physicalDevice_ = candidate;
                    queueFamily_ = index;
                    physicalDeviceProperties_ = properties;
                }
            }
        }

        if (physicalDevice_ == VK_NULL_HANDLE) {
            throw std::runtime_error("No Vulkan graphics queue with presentation support was found");
        }
    }

    void createDevice() {
        constexpr float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily_;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;

        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueInfo;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = extensions;
        checkVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");
        vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    }

    void createDescriptorPool() {
        const std::array<VkDescriptorPoolSize, 3> poolSizes{{
            {VK_DESCRIPTOR_TYPE_SAMPLER, 128},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128}
        }};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 384;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        checkVk(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_), "vkCreateDescriptorPool");
    }

    void createSwapchain(GLFWwindow* window) {
        const VkFormat requestedFormats[] = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM
        };
        windowData_.Surface = surface_;
        windowData_.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            physicalDevice_,
            surface_,
            requestedFormats,
            2,
            VK_COLORSPACE_SRGB_NONLINEAR_KHR
        );
        const VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        windowData_.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
            physicalDevice_,
            surface_,
            &presentMode,
            1
        );
        windowData_.ClearValue.color.float32[0] = 0.002f;
        windowData_.ClearValue.color.float32[1] = 0.003f;
        windowData_.ClearValue.color.float32[2] = 0.008f;
        windowData_.ClearValue.color.float32[3] = 1.0f;

        VkSurfaceCapabilitiesKHR surfaceCapabilities{};
        checkVk(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                physicalDevice_,
                surface_,
                &surfaceCapabilities
            ),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"
        );
        captureSwapchainSupported_ =
            (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        swapchainImageUsage_ = captureSwapchainSupported_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            instance_,
            physicalDevice_,
            device_,
            &windowData_,
            queueFamily_,
            nullptr,
            width,
            height,
            MinImageCount,
            swapchainImageUsage_
        );
    }

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
        for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
            if ((typeBits & (1u << index)) != 0 &&
                (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties) {
                return index;
            }
        }
        throw std::runtime_error("No compatible Vulkan memory type was found");
    }

    void createOffscreenRenderPass() {
        VkAttachmentDescription attachment{};
        attachment.format = SceneFormat;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &attachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        createInfo.pDependencies = dependencies.data();
        checkVk(vkCreateRenderPass(device_, &createInfo, nullptr, &offscreenRenderPass_), "vkCreateRenderPass");
    }

    void createSampler() {
        VkSamplerCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = VK_FILTER_LINEAR;
        createInfo.minFilter = VK_FILTER_LINEAR;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.maxLod = 1.0f;
        checkVk(vkCreateSampler(device_, &createInfo, nullptr, &sceneSampler_), "vkCreateSampler");
    }

    void createDescriptorResources() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        checkVk(
            vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &blitDescriptorSetLayout_),
            "vkCreateDescriptorSetLayout"
        );

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool_;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &blitDescriptorSetLayout_;
        checkVk(vkAllocateDescriptorSets(device_, &allocateInfo, &blitDescriptorSet_), "vkAllocateDescriptorSets");
    }

    VkShaderModule createShaderModule(const std::filesystem::path& path) const {
        const std::vector<uint32_t> code = readSpirv(path);
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        checkVk(vkCreateShaderModule(device_, &createInfo, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    VkPipeline createGraphicsPipeline(
        VkShaderModule vertex,
        VkShaderModule fragment,
        VkPipelineLayout layout,
        VkRenderPass renderPass
    ) const {
        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr}
        }};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorAttachment{};
        colorAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &colorAttachment;

        const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        createInfo.pStages = shaderStages.data();
        createInfo.pVertexInputState = &vertexInput;
        createInfo.pInputAssemblyState = &inputAssembly;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterization;
        createInfo.pMultisampleState = &multisample;
        createInfo.pColorBlendState = &colorBlend;
        createInfo.pDynamicState = &dynamicState;
        createInfo.layout = layout;
        createInfo.renderPass = renderPass;
        createInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        checkVk(
            vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline),
            "vkCreateGraphicsPipelines"
        );
        return pipeline;
    }

    void createPipelines() {
        VkPushConstantRange rayRange{};
        rayRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        rayRange.offset = 0;
        rayRange.size = sizeof(RayPushConstants);
        VkPipelineLayoutCreateInfo rayLayoutInfo{};
        rayLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        rayLayoutInfo.pushConstantRangeCount = 1;
        rayLayoutInfo.pPushConstantRanges = &rayRange;
        checkVk(
            vkCreatePipelineLayout(device_, &rayLayoutInfo, nullptr, &rayPipelineLayout_),
            "vkCreatePipelineLayout for ray pass"
        );

        VkPushConstantRange blitRange{};
        blitRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        blitRange.offset = 0;
        blitRange.size = sizeof(BlitPushConstants);
        VkPipelineLayoutCreateInfo blitLayoutInfo{};
        blitLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        blitLayoutInfo.setLayoutCount = 1;
        blitLayoutInfo.pSetLayouts = &blitDescriptorSetLayout_;
        blitLayoutInfo.pushConstantRangeCount = 1;
        blitLayoutInfo.pPushConstantRanges = &blitRange;
        checkVk(
            vkCreatePipelineLayout(device_, &blitLayoutInfo, nullptr, &blitPipelineLayout_),
            "vkCreatePipelineLayout for blit pass"
        );

        const auto shaderDirectory = appDirectory_ / "shaders";
        const VkShaderModule vertex = createShaderModule(shaderDirectory / "fullscreen.vert.spv");
        const VkShaderModule rayFragment = createShaderModule(shaderDirectory / "kerr.frag.spv");
        const VkShaderModule blitFragment = createShaderModule(shaderDirectory / "blit.frag.spv");
        try {
            rayPipeline_ = createGraphicsPipeline(vertex, rayFragment, rayPipelineLayout_, offscreenRenderPass_);
            blitPipeline_ = createGraphicsPipeline(vertex, blitFragment, blitPipelineLayout_, windowData_.RenderPass);
        } catch (...) {
            vkDestroyShaderModule(device_, blitFragment, nullptr);
            vkDestroyShaderModule(device_, rayFragment, nullptr);
            vkDestroyShaderModule(device_, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(device_, blitFragment, nullptr);
        vkDestroyShaderModule(device_, rayFragment, nullptr);
        vkDestroyShaderModule(device_, vertex, nullptr);
    }

    void createOffscreen(int width, int height) {
        destroyOffscreen();
        sceneWidth_ = width;
        sceneHeight_ = height;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = SceneFormat;
        imageInfo.extent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            1
        };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        checkVk(vkCreateImage(device_, &imageInfo, nullptr, &sceneImage_), "vkCreateImage");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, sceneImage_, &requirements);
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        checkVk(vkAllocateMemory(device_, &allocateInfo, nullptr, &sceneMemory_), "vkAllocateMemory for scene");
        checkVk(vkBindImageMemory(device_, sceneImage_, sceneMemory_, 0), "vkBindImageMemory");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = sceneImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = SceneFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        checkVk(vkCreateImageView(device_, &viewInfo, nullptr, &sceneImageView_), "vkCreateImageView");

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = offscreenRenderPass_;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &sceneImageView_;
        framebufferInfo.width = static_cast<uint32_t>(width);
        framebufferInfo.height = static_cast<uint32_t>(height);
        framebufferInfo.layers = 1;
        checkVk(
            vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &sceneFramebuffer_),
            "vkCreateFramebuffer for scene"
        );

        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.sampler = sceneSampler_;
        imageDescriptor.imageView = sceneImageView_;
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = blitDescriptorSet_;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    }

    void destroyOffscreen() {
        if (sceneFramebuffer_ != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, sceneFramebuffer_, nullptr);
        if (sceneImageView_ != VK_NULL_HANDLE) vkDestroyImageView(device_, sceneImageView_, nullptr);
        if (sceneImage_ != VK_NULL_HANDLE) vkDestroyImage(device_, sceneImage_, nullptr);
        if (sceneMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, sceneMemory_, nullptr);
        sceneFramebuffer_ = VK_NULL_HANDLE;
        sceneImageView_ = VK_NULL_HANDLE;
        sceneImage_ = VK_NULL_HANDLE;
        sceneMemory_ = VK_NULL_HANDLE;
    }

    void setViewportAndScissor(VkCommandBuffer commandBuffer, int width, int height) const {
        VkViewport viewport{};
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent.width = static_cast<uint32_t>(width);
        scissor.extent.height = static_cast<uint32_t>(height);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void recordScenePass(VkCommandBuffer commandBuffer, const RayPushConstants& parameters) const {
        VkClearValue clear{};
        clear.color.float32[0] = 0.002f;
        clear.color.float32[1] = 0.003f;
        clear.color.float32[2] = 0.008f;
        clear.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        passInfo.renderPass = offscreenRenderPass_;
        passInfo.framebuffer = sceneFramebuffer_;
        passInfo.renderArea.extent.width = static_cast<uint32_t>(sceneWidth_);
        passInfo.renderArea.extent.height = static_cast<uint32_t>(sceneHeight_);
        passInfo.clearValueCount = 1;
        passInfo.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
        setViewportAndScissor(commandBuffer, sceneWidth_, sceneHeight_);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayPipeline_);
        vkCmdPushConstants(
            commandBuffer,
            rayPipelineLayout_,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(parameters),
            &parameters
        );
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }

    void recordWindowPass(
        VkCommandBuffer commandBuffer,
        VkFramebuffer framebuffer,
        ImDrawData* drawData,
        const BlitPushConstants& parameters
    ) const {
        VkRenderPassBeginInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        passInfo.renderPass = windowData_.RenderPass;
        passInfo.framebuffer = framebuffer;
        passInfo.renderArea.extent.width = static_cast<uint32_t>(windowData_.Width);
        passInfo.renderArea.extent.height = static_cast<uint32_t>(windowData_.Height);
        passInfo.clearValueCount = 1;
        passInfo.pClearValues = &windowData_.ClearValue;
        vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
        setViewportAndScissor(commandBuffer, windowData_.Width, windowData_.Height);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline_);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            blitPipelineLayout_,
            0,
            1,
            &blitDescriptorSet_,
            0,
            nullptr
        );
        vkCmdPushConstants(
            commandBuffer,
            blitPipelineLayout_,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(parameters),
            &parameters
        );
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
        vkCmdEndRenderPass(commandBuffer);
    }

    CaptureBuffer createCaptureBuffer() const {
        CaptureBuffer capture{};
        capture.width = windowData_.Width;
        capture.height = windowData_.Height;
        capture.format = captureSwapchainSupported_ ? windowData_.SurfaceFormat.format : SceneFormat;
        capture.size = static_cast<VkDeviceSize>(capture.width) *
            static_cast<VkDeviceSize>(capture.height) * 4u;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = capture.size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        checkVk(vkCreateBuffer(device_, &bufferInfo, nullptr, &capture.buffer), "vkCreateBuffer for diagnostics");

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, capture.buffer, &requirements);
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        checkVk(
            vkAllocateMemory(device_, &allocateInfo, nullptr, &capture.memory),
            "vkAllocateMemory for diagnostics"
        );
        checkVk(vkBindBufferMemory(device_, capture.buffer, capture.memory, 0), "vkBindBufferMemory");
        return capture;
    }

    void recordSceneCapture(VkCommandBuffer commandBuffer, const CaptureBuffer& capture) const {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = sceneImage_;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toTransfer
        );

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent.width = static_cast<uint32_t>(sceneWidth_);
        copy.imageExtent.height = static_cast<uint32_t>(sceneHeight_);
        copy.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer(
            commandBuffer,
            sceneImage_,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            capture.buffer,
            1,
            &copy
        );

        VkImageMemoryBarrier toShaderRead = toTransfer;
        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toShaderRead
        );
    }

    void recordSwapchainCapture(
        VkCommandBuffer commandBuffer,
        VkImage swapchainImage,
        const CaptureBuffer& capture
    ) const {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = swapchainImage;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toTransfer
        );

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent.width = static_cast<uint32_t>(capture.width);
        copy.imageExtent.height = static_cast<uint32_t>(capture.height);
        copy.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer(
            commandBuffer,
            swapchainImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            capture.buffer,
            1,
            &copy
        );

        VkImageMemoryBarrier toPresent = toTransfer;
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toPresent.dstAccessMask = 0;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toPresent
        );
    }

    bool writeCapture(const CaptureBuffer& capture, const std::filesystem::path& path) const {
        void* mapped = nullptr;
        checkVk(vkMapMemory(device_, capture.memory, 0, capture.size, 0, &mapped), "vkMapMemory");
        const void* pixels = mapped;
        std::vector<uint8_t> rgbaPixels;
        if (capture.format == VK_FORMAT_B8G8R8A8_UNORM ||
            capture.format == VK_FORMAT_B8G8R8A8_SRGB) {
            const auto* source = static_cast<const uint8_t*>(mapped);
            rgbaPixels.assign(source, source + static_cast<size_t>(capture.size));
            for (size_t offset = 0; offset < rgbaPixels.size(); offset += 4) {
                std::swap(rgbaPixels[offset], rgbaPixels[offset + 2]);
            }
            pixels = rgbaPixels.data();
        }
        const int written = stbi_write_png(
            path.string().c_str(),
            capture.width,
            capture.height,
            4,
            pixels,
            capture.width * 4
        );
        vkUnmapMemory(device_, capture.memory);
        return written != 0;
    }

    void destroyCaptureBuffer(CaptureBuffer& capture) const {
        if (capture.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device_, capture.buffer, nullptr);
        if (capture.memory != VK_NULL_HANDLE) vkFreeMemory(device_, capture.memory, nullptr);
        capture = {};
    }

    std::filesystem::path appDirectory_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physicalDeviceProperties_{};
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window windowData_{};
    bool swapchainRebuild_ = false;
    bool imguiInitialized_ = false;
    bool captureSwapchainSupported_ = false;
    VkImageUsageFlags swapchainImageUsage_ = 0;

    VkRenderPass offscreenRenderPass_ = VK_NULL_HANDLE;
    VkImage sceneImage_ = VK_NULL_HANDLE;
    VkDeviceMemory sceneMemory_ = VK_NULL_HANDLE;
    VkImageView sceneImageView_ = VK_NULL_HANDLE;
    VkFramebuffer sceneFramebuffer_ = VK_NULL_HANDLE;
    VkSampler sceneSampler_ = VK_NULL_HANDLE;
    int sceneWidth_ = 0;
    int sceneHeight_ = 0;

    VkDescriptorSetLayout blitDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet blitDescriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout rayPipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout blitPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline rayPipeline_ = VK_NULL_HANDLE;
    VkPipeline blitPipeline_ = VK_NULL_HANDLE;
};

void writeDiagnosticsReport(
    const VkPhysicalDeviceProperties& properties,
    float framesPerSecond,
    bool autoPlay,
    float cameraYaw,
    double simulationTime,
    const ImDrawData* drawData
) {
    const auto reportPath = std::filesystem::temp_directory_path() / "ton618-vulkan-performance.txt";
    std::ofstream report(reportPath, std::ios::binary);
    report << "fps=" << framesPerSecond << '\n';
    report << "renderer=" << properties.deviceName << '\n';
    report << "vulkan="
           << VK_API_VERSION_MAJOR(properties.apiVersion) << '.'
           << VK_API_VERSION_MINOR(properties.apiVersion) << '.'
           << VK_API_VERSION_PATCH(properties.apiVersion) << '\n';
    report << "auto_play=" << (autoPlay ? 1 : 0) << '\n';
    report << "camera_yaw=" << cameraYaw << '\n';
    report << "simulation_time=" << simulationTime << '\n';
    report << "imgui_vertices=" << drawData->TotalVtxCount << '\n';
    report << "imgui_indices=" << drawData->TotalIdxCount << '\n';
    report << "imgui_display=" << drawData->DisplaySize.x << 'x' << drawData->DisplaySize.y << '\n';
}

} // namespace

int main() {
    GLFWwindow* window = nullptr;
    VulkanRenderer renderer;
    bool imguiContextCreated = false;
    bool imguiBackendsInitialized = false;

    try {
        const auto appDirectory = executableDirectory();
        std::filesystem::current_path(appDirectory);

        glfwSetErrorCallback(glfwErrorCallback);
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        if (!glfwVulkanSupported()) throw std::runtime_error("GLFW reports that Vulkan is unavailable");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(
            1100,
            700,
            "Supermassive Black Hole Simulator (Vulkan)",
            nullptr,
            nullptr
        );
        if (window == nullptr) throw std::runtime_error("Vulkan window creation failed");

        renderer.initialize(window, appDirectory);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imguiContextCreated = true;
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding = 6.0f;
        ImGui::GetStyle().FrameRounding = 4.0f;
        const std::filesystem::path fontPath = "/usr/share/fonts/TTF/DejaVuSans.ttf";
        if (std::filesystem::exists(fontPath)) {
            io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 15.0f);
        }
        renderer.initializeImGui(window);
        imguiBackendsInitialized = true;

        Camera camera;
        if (std::getenv("TON618_DIAGNOSTICS_ZOOMED_OUT") != nullptr) camera.radius = 52.0f;
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
        const bool diagnosticsExit = std::getenv("TON618_DIAGNOSTICS_EXIT") != nullptr;
        bool diagnosticsWritten = false;
        uint32_t renderedFrames = 0;
        double previousMouseX = 0.0;
        double previousMouseY = 0.0;
        double previousTime = glfwGetTime() - TargetFrameSeconds;
        double simulationTime = 0.0;
        float smoothedFps = 20.0f;
        float resumeTimeScale = std::max(parameters.timeScale, 0.01f);

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            const double frameStart = glfwGetTime();
            const float deltaTime = static_cast<float>(std::min(frameStart - previousTime, 0.125));
            previousTime = frameStart;
            const float instantaneousFps = deltaTime > 0.00001f ? 1.0f / deltaTime : smoothedFps;
            smoothedFps += (instantaneousFps - smoothedFps) * 0.04f;
            simulationTime += static_cast<double>(deltaTime) * parameters.timeScale;

            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            if (framebufferWidth <= 0 || framebufferHeight <= 0) {
                glfwWaitEventsTimeout(TargetFrameSeconds);
                continue;
            }
            if (renderer.needsResize(framebufferWidth, framebufferHeight)) {
                renderer.resize(framebufferWidth, framebufferHeight);
            }

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            const bool f1 = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
            if (f1 && !previousF1) showControls = !showControls;
            previousF1 = f1;
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }

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
                    camera.latitude = std::clamp(
                        camera.latitude,
                        glm::radians(-82.0f),
                        glm::radians(82.0f)
                    );
                }
                dragging = true;
            } else {
                dragging = false;
            }
            previousMouseX = mouseX;
            previousMouseY = mouseY;

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

            renderTelemetry(parameters, camera);
            renderControls(parameters, camera, showControls, autoPlay);
            if (parameters.timeScale > 0.001f) resumeTimeScale = parameters.timeScale;
            ImGui::Render();

            RayPushConstants rayParameters{};
            rayParameters.resolutionTime = glm::vec4(
                static_cast<float>(framebufferWidth),
                static_cast<float>(framebufferHeight),
                static_cast<float>(simulationTime),
                0.0f
            );
            rayParameters.cameraPositionFov = glm::vec4(cameraPosition, camera.fov);
            rayParameters.cameraForwardSpin = glm::vec4(forward, parameters.spin);
            rayParameters.cameraRightDiskInner = glm::vec4(right, parameters.diskInner);
            rayParameters.cameraUpDiskOuter = glm::vec4(up, parameters.diskOuter);
            rayParameters.integration = glm::vec4(
                parameters.accretionRate,
                parameters.exposure,
                parameters.minimumStep,
                parameters.maximumStep
            );
            rayParameters.limits = glm::ivec4(parameters.maximumSteps, 0, 0, 0);

            const float polarViewAmount = glm::smoothstep(
                glm::radians(68.0f),
                glm::radians(82.0f),
                std::abs(camera.latitude)
            );
            BlitPushConstants blitParameters{};
            blitParameters.resolutionRepair = glm::vec4(
                static_cast<float>(framebufferWidth),
                static_cast<float>(framebufferHeight),
                28.0f + 24.0f * polarViewAmount,
                polarViewAmount
            );

            const bool captureRequested = diagnosticsEnabled &&
                !diagnosticsWritten &&
                (diagnosticsExit ? renderedFrames >= 2 : frameStart > 4.0);
            const auto capturePath = std::filesystem::temp_directory_path() / "ton618-vulkan-preview.png";
            if (renderer.render(
                ImGui::GetDrawData(),
                rayParameters,
                blitParameters,
                captureRequested,
                capturePath
            )) {
                writeDiagnosticsReport(
                    renderer.physicalDeviceProperties(),
                    smoothedFps,
                    autoPlay,
                    camera.yaw,
                    simulationTime,
                    ImGui::GetDrawData()
                );
                diagnosticsWritten = true;
                if (diagnosticsExit) glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            ++renderedFrames;

            const double frameElapsed = glfwGetTime() - frameStart;
            preciseSleep(TargetFrameSeconds - frameElapsed);
        }

        renderer.waitIdle();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        imguiBackendsInitialized = false;
        ImGui::DestroyContext();
        imguiContextCreated = false;
        renderer.shutdown();
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Supermassive Black Hole Simulator: " << error.what() << '\n';
        try {
            renderer.waitIdle();
        } catch (...) {
        }
        if (imguiBackendsInitialized) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
        }
        if (imguiContextCreated) ImGui::DestroyContext();
        renderer.shutdown();
        if (window != nullptr) glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
