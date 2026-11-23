#include "editor/EditorGuiApp.h"
#include "core/Version.h"
#include "framework/CombatAndWorldEvents.h"
#include "framework/DialogueNarrative.h"
#include "framework/InventoryProgression.h"
#include "framework/QuestSystem.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace sidera::editor {

namespace {

struct EditorSceneEntity {
    EntityID id = 1;
    EntityID parentId = kInvalidEntityID;
    std::string name;
    glm::vec3 position{0.0f};
    glm::vec3 rotationDeg{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 albedo{0.35f, 0.65f, 0.92f};
    float metallic = 0.2f;
    float roughness = 0.45f;
    std::string meshAsset = "CubeMesh";
    std::vector<std::string> components{"Transform", "MeshRenderer"};
};

struct EditorLightingState {
    float sunYawDeg = 42.0f;
    float sunPitchDeg = -48.0f;
    float sunIntensity = 2.6f;
    float sunColor[3] = {1.0f, 0.96f, 0.88f};
    float ambientIntensity = 0.30f;
    float exposure = 1.0f;
};

struct EditorTransformCommand final : IEditorCommand {
    std::vector<EditorSceneEntity>* entities = nullptr;
    EntityID targetId = kInvalidEntityID;
    glm::vec3 oldPos{}, newPos{};
    glm::vec3 oldRot{}, newRot{};
    glm::vec3 oldScale{}, newScale{};
    std::string label;

    bool execute() override {
        if (!entities) return false;
        for (auto& e : *entities) {
            if (e.id == targetId) {
                e.position = newPos;
                e.rotationDeg = newRot;
                e.scale = newScale;
                return true;
            }
        }
        return false;
    }
    bool undo() override {
        if (!entities) return false;
        for (auto& e : *entities) {
            if (e.id == targetId) {
                e.position = oldPos;
                e.rotationDeg = oldRot;
                e.scale = oldScale;
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] std::string getDescription() const override { return label; }
};

// Minimal, rock-solid Vulkan + ImGui_ImplVulkanH wrapper for the Editor GUI window.
struct VulkanGuiContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window mainWindowData{};
    uint32_t minImageCount = 2;
    bool swapChainRebuild = false;

    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
        }
        return 0;
    }

    bool init(GLFWwindow* window) {
        uint32_t extCount = 0;
        const char** exts = glfwGetRequiredInstanceExtensions(&extCount);
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "SIDERA Editor 1.0.0";
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledExtensionCount = extCount;
        instInfo.ppEnabledExtensionNames = exts;
        if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS) return false;

        uint32_t gpuCount = 0;
        vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
        if (gpuCount == 0) return false;
        std::vector<VkPhysicalDevice> gpus(gpuCount);
        vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());
        physicalDevice = gpus[0];
        for (auto gpu : gpus) {
            VkPhysicalDeviceProperties p{};
            vkGetPhysicalDeviceProperties(gpu, &p);
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                physicalDevice = gpu;
                break;
            }
        }

        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, qfs.data());
        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { queueFamily = i; break; }
        }

        float prio = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily;
        qInfo.queueCount = 1;
        qInfo.pQueuePriorities = &prio;

        const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo devInfo{};
        devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        devInfo.queueCreateInfoCount = 1;
        devInfo.pQueueCreateInfos = &qInfo;
        devInfo.enabledExtensionCount = 1;
        devInfo.ppEnabledExtensionNames = devExts;
        if (vkCreateDevice(physicalDevice, &devInfo, nullptr, &device) != VK_SUCCESS) return false;
        vkGetDeviceQueue(device, queueFamily, 0, &queue);

        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64}
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 64;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) return false;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) return false;

        int w = 1600, h = 960;
        glfwGetFramebufferSize(window, &w, &h);
        mainWindowData.Surface = surface;
        const VkFormat formats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
        mainWindowData.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            physicalDevice, surface, formats, 2, VK_COLORSPACE_SRGB_NONLINEAR_KHR);
        VkPresentModeKHR presentModes[] = {VK_PRESENT_MODE_FIFO_KHR};
        mainWindowData.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
            physicalDevice, surface, presentModes, 1);

        ImGui_ImplVulkanH_CreateOrResizeWindow(
            instance, physicalDevice, device, &mainWindowData, queueFamily, nullptr, w, h, minImageCount, 0);
        return true;
    }

    bool saveSwapchainScreenshotBmp(const std::string& path) {
        if (path.empty() || mainWindowData.Width <= 0 || mainWindowData.Height <= 0) return false;
        vkDeviceWaitIdle(device);

        const uint32_t w = static_cast<uint32_t>(mainWindowData.Width);
        const uint32_t h = static_cast<uint32_t>(mainWindowData.Height);
        const VkImage srcImg = mainWindowData.Frames[mainWindowData.FrameIndex].Backbuffer;
        if (srcImg == VK_NULL_HANDLE) return false;

        const VkDeviceSize bufSize = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer dstBuf = VK_NULL_HANDLE;
        VkDeviceMemory dstMem = VK_NULL_HANDLE;
        VkBufferCreateInfo bInfo{};
        bInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bInfo.size = bufSize;
        bInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bInfo, nullptr, &dstBuf) != VK_SUCCESS) return false;

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, dstBuf, &req);
        VkMemoryAllocateInfo aInfo{};
        aInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        aInfo.allocationSize = req.size;
        aInfo.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device, &aInfo, nullptr, &dstMem);
        vkBindBufferMemory(device, dstBuf, dstMem, 0);

        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo pInfo{};
        pInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pInfo.queueFamilyIndex = queueFamily;
        pInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkCreateCommandPool(device, &pInfo, nullptr, &pool);

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        vkAllocateCommandBuffers(device, &cAlloc, &cmd);
        VkCommandBufferBeginInfo bBegin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
        vkBeginCommandBuffer(cmd, &bBegin);

        VkImageMemoryBarrier toSrc{};
        toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image = srcImg;
        toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toSrc.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toSrc);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(cmd, srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuf, 1, &region);

        VkImageMemoryBarrier toPres = toSrc;
        toPres.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toPres.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPres.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toPres.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toPres);

        vkEndCommandBuffer(cmd);
        VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr};
        vkQueueSubmit(queue, 1, &sub, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        void* mapped = nullptr;
        vkMapMemory(device, dstMem, 0, bufSize, 0, &mapped);
        const auto* px = static_cast<const uint8_t*>(mapped);
        const bool isBgra = (mainWindowData.SurfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM ||
                             mainWindowData.SurfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB);

        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (f.is_open()) {
            const uint32_t rowBytes = w * 3;
            const uint32_t pad = (4 - (rowBytes % 4)) % 4;
            const uint32_t imgSize = (rowBytes + pad) * h;
            const uint32_t fileSize = 54 + imgSize;
            uint8_t hdr[54] = {0};
            hdr[0] = 'B'; hdr[1] = 'M';
            std::memcpy(hdr + 2, &fileSize, 4);
            uint32_t off = 54; std::memcpy(hdr + 10, &off, 4);
            uint32_t biSize = 40; std::memcpy(hdr + 14, &biSize, 4);
            std::memcpy(hdr + 18, &w, 4);
            std::memcpy(hdr + 22, &h, 4);
            uint16_t planes = 1; std::memcpy(hdr + 26, &planes, 2);
            uint16_t bpp = 24; std::memcpy(hdr + 28, &bpp, 2);
            std::memcpy(hdr + 34, &imgSize, 4);
            f.write(reinterpret_cast<const char*>(hdr), 54);
            std::vector<uint8_t> row(rowBytes + pad, 0);
            for (int y = static_cast<int>(h) - 1; y >= 0; --y) {
                for (uint32_t x = 0; x < w; ++x) {
                    const size_t s = (static_cast<size_t>(y) * w + x) * 4;
                    row[x * 3 + 0] = isBgra ? px[s + 0] : px[s + 2];
                    row[x * 3 + 1] = px[s + 1];
                    row[x * 3 + 2] = isBgra ? px[s + 2] : px[s + 0];
                }
                f.write(reinterpret_cast<const char*>(row.data()), row.size());
            }
        }
        vkUnmapMemory(device, dstMem);
        vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyBuffer(device, dstBuf, nullptr);
        vkFreeMemory(device, dstMem, nullptr);
        return true;
    }

    void shutdown() {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            ImGui_ImplVulkanH_DestroyWindow(instance, device, &mainWindowData, nullptr);
            if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    }
};

void saveEditorSceneFile(const std::filesystem::path& scenePath,
                         const std::vector<EditorSceneEntity>& entities,
                         const EditorLightingState& light) {
    std::error_code ec;
    std::filesystem::create_directories(scenePath.parent_path(), ec);
    std::ofstream f(scenePath, std::ios::trunc);
    if (!f.is_open()) return;
    f << "SIDERA_SCENE\nschema_version 1\nname EditorActiveScene\n";
    f << "lighting " << light.sunYawDeg << " " << light.sunPitchDeg << " "
      << light.sunIntensity << " " << light.ambientIntensity << " " << light.exposure << "\n";
    for (const auto& e : entities) {
        f << "entity " << e.id << " " << e.name << " "
          << e.position.x << " " << e.position.y << " " << e.position.z << " "
          << e.rotationDeg.x << " " << e.rotationDeg.y << " " << e.rotationDeg.z << " "
          << e.scale.x << " " << e.scale.y << " " << e.scale.z << " "
          << e.albedo.r << " " << e.albedo.g << " " << e.albedo.b << " "
          << e.metallic << " " << e.roughness << "\n";
    }
}

} // namespace

int runInteractiveEditorGui(const EditorGuiLaunchOptions& options) {
    if (!glfwInit()) {
        std::cerr << "[SIDERA Editor] Failed to initialize GLFW.\n";
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1600, 960, "SIDERA Engine 1.0.0 — Editor", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cerr << "[SIDERA Editor] Failed to create GLFW window.\n";
        return 1;
    }

    VulkanGuiContext vkCtx;
    if (!vkCtx.init(window)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr << "[SIDERA Editor] Failed to initialize Vulkan GUI context.\n";
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4.0f;
    ImGui::GetStyle().FrameRounding = 3.0f;

    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_2;
    initInfo.Instance = vkCtx.instance;
    initInfo.PhysicalDevice = vkCtx.physicalDevice;
    initInfo.Device = vkCtx.device;
    initInfo.QueueFamily = vkCtx.queueFamily;
    initInfo.Queue = vkCtx.queue;
    initInfo.DescriptorPool = vkCtx.descriptorPool;
    initInfo.MinImageCount = vkCtx.minImageCount;
    initInfo.ImageCount = vkCtx.mainWindowData.ImageCount;
    initInfo.PipelineInfoMain.RenderPass = vkCtx.mainWindowData.RenderPass;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&initInfo);

    // Initialize real EditorApplication backend
    EditorApplication editorApp;
    editorApp.init();
    if (!options.openProjectPath.empty()) {
        editorApp.openProject(options.openProjectPath);
    }

    // Scene state backed by EditorApplication Undo/Redo & Selection
    std::vector<EditorSceneEntity> sceneEntities = {
        {1, kInvalidEntityID, "PlayerStart", {0.0f, 0.5f, 0.0f}, {0, 0, 0}, {1, 1, 1}, {0.25f, 0.72f, 0.96f}, 0.2f, 0.35f, "CapsuleMesh", {"Transform", "CharacterController"}},
        {100, kInvalidEntityID, "GuideNPC", {3.0f, 0.5f, -1.5f}, {0, -30, 0}, {1, 1.2f, 1}, {0.92f, 0.72f, 0.28f}, 0.1f, 0.55f, "NPCMesh", {"Transform", "NPCController", "Interactable"}},
        {200, kInvalidEntityID, "BeaconRelic", {-2.5f, 0.8f, 2.0f}, {0, 45, 0}, {0.8f, 1.4f, 0.8f}, {0.35f, 0.95f, 0.58f}, 0.85f, 0.20f, "RelicMesh", {"Transform", "MeshRenderer", "PointLight"}}
    };
    EntityID nextEntityId = 201;
    EditorLightingState lighting{};

    // Interactive Viewport Camera & Gizmo State
    float camYaw = 35.0f, camPitch = 24.0f, camDist = 11.0f;
    glm::vec3 camTarget{0.0f, 0.5f, 0.0f};
    int gizmoMode = 0; // 0=Translate, 1=Rotate, 2=Scale
    bool localSpace = false;
    bool snapEnabled = true;
    float snapStep = 0.5f;

    // UI Panel visibility & input buffers
    bool showWelcome = (editorApp.getState() == EditorAppState::NoProject) && !options.guiSmoke;
    bool showHierarchy = true, showInspector = true, showViewport = true;
    bool showAssets = true, showMaterials = true, showTools = true, showConsole = true;
    char searchBuf[64] = "";
    char newProjName[64] = "MySideraGame";
    char newProjDir[256] = "C:\\Users\\Public\\SideraProjects\\MySideraGame";
    char openProjDir[256] = "examples/rpg_reference_project";
    char newCompName[64] = "AudioSource";
    float frameTimes[64] = {0};
    int frameIdx = 0;

    // M41 Framework live state preview
    sidera::framework::WorldTimeSystem worldTime;
    worldTime.setTime(1, 17.2f);
    float terrainScale = 1.0f;
    int terrainPreset = 0;
    float masterVolume = 0.85f;
    const char* animStates[] = {"Idle", "Walk", "Run", "Sprint", "Jump"};
    int activeAnimState = 0;

    auto makeSel = [](EntityID id) -> SelectionItem { return SelectionItem{SelectionKind::Entity, id, {}}; };
    editorApp.getSelection().select(makeSel(100)); // Pre-select GuideNPC for immediate Inspector & Gizmo display
    auto getPrimaryEntity = [&]() -> EntityID {
        for (const auto& it : editorApp.getSelection().getSelection()) {
            if (it.kind == SelectionKind::Entity) return it.entityId;
        }
        return kInvalidEntityID;
    };

    uint32_t frameCounter = 0;
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        frameTimes[frameIdx] = dt * 1000.0f;
        frameIdx = (frameIdx + 1) % 64;
        editorApp.tick(dt);

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW > 0 && fbH > 0 && (vkCtx.swapChainRebuild || vkCtx.mainWindowData.Width != fbW || vkCtx.mainWindowData.Height != fbH)) {
            ImGui_ImplVulkan_SetMinImageCount(vkCtx.minImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(vkCtx.instance, vkCtx.physicalDevice, vkCtx.device,
                &vkCtx.mainWindowData, vkCtx.queueFamily, nullptr, fbW, fbH, vkCtx.minImageCount, 0);
            vkCtx.mainWindowData.FrameIndex = 0;
            vkCtx.swapChainRebuild = false;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // DockSpace over full viewport
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // 1. Main Menu Bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Welcome / Project Browser")) showWelcome = true;
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                    const std::filesystem::path dir = editorApp.getActiveProject().isValid()
                        ? std::filesystem::path(editorApp.getActiveProject().directory) / "scenes" / "main.siderascene"
                        : std::filesystem::path("examples/rpg_reference_project/scenes/main.siderascene");
                    saveEditorSceneFile(dir, sceneEntities, lighting);
                    editorApp.setProjectDirty(false);
                    editorApp.pushNotification(EditorLogSeverity::Info, "Scene Saved", dir.string());
                }
                if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(window, GLFW_TRUE);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editorApp.getCommands().canUndo())) editorApp.getCommands().undo();
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, editorApp.getCommands().canRedo())) editorApp.getCommands().redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Scene Viewport", nullptr, &showViewport);
                ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
                ImGui::MenuItem("Inspector", nullptr, &showInspector);
                ImGui::MenuItem("Asset Browser", nullptr, &showAssets);
                ImGui::MenuItem("Material & Lighting", nullptr, &showMaterials);
                ImGui::MenuItem("World / Audio / RPG Tools", nullptr, &showTools);
                ImGui::MenuItem("Console & Profiler", nullptr, &showConsole);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::Text("Project: %s%s",
                        editorApp.getActiveProject().isValid() ? editorApp.getActiveProject().name.c_str() : "None",
                        editorApp.isProjectDirty() ? "*" : "");
            ImGui::EndMainMenuBar();
        }

        // 2. Welcome / Project Browser
        if (showWelcome) {
            ImGui::Begin("Welcome to SIDERA Engine 1.0.0", &showWelcome);
            ImGui::TextUnformatted("Create a new independent game project or open an existing project:");
            ImGui::Separator();
            ImGui::InputText("Project Name", newProjName, sizeof(newProjName));
            ImGui::InputText("Project Directory", newProjDir, sizeof(newProjDir));
            if (ImGui::Button("Create & Open Project")) {
                if (EditorApplication::createProject(newProjDir, newProjName) && editorApp.openProject(newProjDir)) {
                    showWelcome = false;
                }
            }
            ImGui::Separator();
            ImGui::InputText("Open Directory", openProjDir, sizeof(openProjDir));
            if (ImGui::Button("Open Project")) {
                if (editorApp.openProject(openProjDir)) showWelcome = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Open RPG Reference Project")) {
                if (editorApp.openProject("examples/rpg_reference_project")) showWelcome = false;
            }
            ImGui::End();
        }

        // 3. Hierarchy Panel
        if (showHierarchy) {
            ImGui::SetNextWindowPos(ImVec2(8, 28), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(250, 520), ImGuiCond_FirstUseEver);
            ImGui::Begin("Hierarchy", &showHierarchy);
            ImGui::InputTextWithHint("##search", "Search entities...", searchBuf, sizeof(searchBuf));
            if (ImGui::Button("+ Entity")) {
                EditorSceneEntity e{};
                e.id = nextEntityId++;
                e.name = "Entity_" + std::to_string(e.id);
                e.position = {0.0f, 0.5f, 0.0f};
                sceneEntities.push_back(e);
                editorApp.getSelection().select(makeSel(e.id));
                editorApp.setProjectDirty(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate") && getPrimaryEntity() != kInvalidEntityID) {
                const EntityID sel = getPrimaryEntity();
                for (const auto& e : sceneEntities) {
                    if (e.id == sel) {
                        EditorSceneEntity copy = e;
                        copy.id = nextEntityId++;
                        copy.name += "_Copy";
                        copy.position.x += 1.0f;
                        sceneEntities.push_back(copy);
                        editorApp.getSelection().select(makeSel(copy.id));
                        editorApp.setProjectDirty(true);
                        break;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && getPrimaryEntity() != kInvalidEntityID) {
                const EntityID sel = getPrimaryEntity();
                sceneEntities.erase(std::remove_if(sceneEntities.begin(), sceneEntities.end(),
                    [&](const EditorSceneEntity& e) { return e.id == sel; }), sceneEntities.end());
                editorApp.getSelection().clear();
                editorApp.setProjectDirty(true);
            }
            ImGui::Separator();
            for (const auto& e : sceneEntities) {
                if (searchBuf[0] != '\0' && e.name.find(searchBuf) == std::string::npos) continue;
                const bool selected = editorApp.getSelection().isSelected(makeSel(e.id));
                if (ImGui::Selectable((e.name + "##" + std::to_string(e.id)).c_str(), selected)) {
                    if (io.KeyCtrl) editorApp.getSelection().toggleSelection(makeSel(e.id));
                    else editorApp.getSelection().select(makeSel(e.id));
                }
            }
            ImGui::End();
        }

        // 4. Interactive 3D Scene Viewport (with Toolbar, 3D projection, camera orbit, gizmos & object picking)
        if (showViewport) {
            ImGui::SetNextWindowPos(ImVec2(266, 28), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(960, 640), ImGuiCond_FirstUseEver);
            ImGui::Begin("Scene Viewport", &showViewport);
            ImGui::RadioButton("Translate", &gizmoMode, 0); ImGui::SameLine();
            ImGui::RadioButton("Rotate", &gizmoMode, 1); ImGui::SameLine();
            ImGui::RadioButton("Scale", &gizmoMode, 2); ImGui::SameLine();
            ImGui::Checkbox(localSpace ? "Local" : "World", &localSpace); ImGui::SameLine();
            ImGui::Checkbox("Snap", &snapEnabled); ImGui::SameLine();
            ImGui::SetNextItemWidth(60.0f);
            ImGui::DragFloat("##snap", &snapStep, 0.05f, 0.05f, 5.0f, "%.2f");
            ImGui::Separator();

            const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
            const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
            const float cw = std::max(canvasSize.x, 120.0f);
            const float ch = std::max(canvasSize.y, 120.0f);
            ImGui::InvisibleButton("viewport_canvas", ImVec2(cw, ch), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                    camYaw += io.MouseDelta.x * 0.4f;
                    camPitch = std::clamp(camPitch + io.MouseDelta.y * 0.35f, -80.0f, 80.0f);
                }
                if (io.MouseWheel != 0.0f) camDist = std::clamp(camDist - io.MouseWheel * 0.8f, 2.5f, 40.0f);
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + cw, canvasPos.y + ch), IM_COL32(18, 24, 34, 255));

            const float yr = glm::radians(camYaw), pr = glm::radians(camPitch);
            const glm::vec3 camPos = camTarget + glm::vec3(std::sin(yr) * std::cos(pr), std::sin(pr), std::cos(yr) * std::cos(pr)) * camDist;
            const glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0, 1, 0));
            const glm::mat4 proj = glm::perspective(glm::radians(50.0f), cw / ch, 0.1f, 200.0f);
            const glm::mat4 vp = proj * view;

            auto project = [&](const glm::vec3& p, ImVec2& out) -> bool {
                const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
                if (clip.w <= 0.05f) return false;
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                out.x = canvasPos.x + (ndc.x * 0.5f + 0.5f) * cw;
                out.y = canvasPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * ch;
                return true;
            };

            // 3D Ground Grid
            for (int i = -6; i <= 6; ++i) {
                ImVec2 a, b;
                if (project({static_cast<float>(i), 0.0f, -6.0f}, a) && project({static_cast<float>(i), 0.0f, 6.0f}, b))
                    dl->AddLine(a, b, i == 0 ? IM_COL32(80, 120, 180, 170) : IM_COL32(55, 65, 80, 120));
                if (project({-6.0f, 0.0f, static_cast<float>(i)}, a) && project({6.0f, 0.0f, static_cast<float>(i)}, b))
                    dl->AddLine(a, b, i == 0 ? IM_COL32(180, 80, 80, 170) : IM_COL32(55, 65, 80, 120));
            }

            // Render 3D entities + picking + gizmo axes
            for (const auto& e : sceneEntities) {
                ImVec2 center;
                if (!project(e.position, center)) continue;
                const float rad = std::clamp(220.0f / std::max(glm::length(e.position - camPos), 1.5f), 10.0f, 44.0f) * e.scale.x;
                const float lit = std::clamp(lighting.ambientIntensity + 0.25f * lighting.sunIntensity, 0.2f, 1.2f);
                const ImU32 fill = IM_COL32(
                    static_cast<int>(std::clamp(e.albedo.r * lit * 255.0f, 0.0f, 255.0f)),
                    static_cast<int>(std::clamp(e.albedo.g * lit * 255.0f, 0.0f, 255.0f)),
                    static_cast<int>(std::clamp(e.albedo.b * lit * 255.0f, 0.0f, 255.0f)), 235);
                const bool sel = editorApp.getSelection().isSelected(makeSel(e.id));
                dl->AddRectFilled(ImVec2(center.x - rad, center.y - rad), ImVec2(center.x + rad, center.y + rad), fill, 4.0f);
                dl->AddRect(ImVec2(center.x - rad, center.y - rad), ImVec2(center.x + rad, center.y + rad),
                            sel ? IM_COL32(255, 210, 60, 255) : IM_COL32(220, 230, 245, 160), 4.0f, 0, sel ? 2.5f : 1.0f);
                dl->AddText(ImVec2(center.x - rad, center.y - rad - 16.0f), IM_COL32(235, 240, 250, 255), e.name.c_str());

                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    const ImVec2 mp = io.MousePos;
                    if (std::abs(mp.x - center.x) <= rad && std::abs(mp.y - center.y) <= rad) {
                        editorApp.getSelection().select(makeSel(e.id));
                    }
                }
                if (sel) {
                    ImVec2 px, py, pz;
                    if (project(e.position + glm::vec3(1.5f, 0, 0), px)) dl->AddLine(center, px, IM_COL32(240, 70, 70, 255), 2.5f);
                    if (project(e.position + glm::vec3(0, 1.5f, 0), py)) dl->AddLine(center, py, IM_COL32(70, 230, 90, 255), 2.5f);
                    if (project(e.position + glm::vec3(0, 0, 1.5f), pz)) dl->AddLine(center, pz, IM_COL32(70, 140, 250, 255), 2.5f);
                }
            }

            dl->AddText(ImVec2(canvasPos.x + 10, canvasPos.y + 8), IM_COL32(140, 210, 255, 255),
                        "SIDERA 3D Viewport | RMB Orbit | Wheel Zoom | LMB Select");
            ImGui::End();
        }

        // 5. Inspector Panel (with Undo/Redo transform commands)
        if (showInspector) {
            ImGui::SetNextWindowPos(ImVec2(1234, 28), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(358, 440), ImGuiCond_FirstUseEver);
            ImGui::Begin("Inspector", &showInspector);
            const EntityID sel = getPrimaryEntity();
            EditorSceneEntity* target = nullptr;
            for (auto& e : sceneEntities) if (e.id == sel) { target = &e; break; }
            if (target) {
                char nameBuf[64];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", target->name.c_str());
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                    target->name = nameBuf;
                    editorApp.setProjectDirty(true);
                }
                glm::vec3 pos = target->position, rot = target->rotationDeg, scl = target->scale;
                const bool changed = ImGui::DragFloat3("Position", &pos.x, snapEnabled ? snapStep : 0.1f) |
                                     ImGui::DragFloat3("Rotation", &rot.x, 1.0f) |
                                     ImGui::DragFloat3("Scale", &scl.x, 0.05f, 0.05f, 50.0f);
                if (changed) {
                    auto cmd = std::make_unique<EditorTransformCommand>();
                    cmd->entities = &sceneEntities;
                    cmd->targetId = target->id;
                    cmd->oldPos = target->position; cmd->newPos = pos;
                    cmd->oldRot = target->rotationDeg; cmd->newRot = rot;
                    cmd->oldScale = target->scale; cmd->newScale = scl;
                    cmd->label = "Edit Transform (" + target->name + ")";
                    editorApp.getCommands().execute(std::move(cmd));
                    editorApp.setProjectDirty(true);
                }
                ImGui::Separator();
                ImGui::TextUnformatted("Material (PBR)");
                ImGui::ColorEdit3("Albedo", &target->albedo.r);
                ImGui::SliderFloat("Metallic", &target->metallic, 0.0f, 1.0f);
                ImGui::SliderFloat("Roughness", &target->roughness, 0.04f, 1.0f);
                ImGui::Separator();
                ImGui::TextUnformatted("Components");
                for (size_t i = 0; i < target->components.size(); ++i) {
                    ImGui::BulletText("%s", target->components[i].c_str());
                }
                ImGui::InputText("##newcomp", newCompName, sizeof(newCompName));
                ImGui::SameLine();
                if (ImGui::Button("Add Component") && newCompName[0] != '\0') {
                    target->components.emplace_back(newCompName);
                    editorApp.setProjectDirty(true);
                }
            } else {
                ImGui::TextDisabled("Select an entity in Hierarchy or Scene Viewport.");
            }
            ImGui::End();
        }

        // 6. Asset Browser
        if (showAssets) {
            ImGui::SetNextWindowPos(ImVec2(8, 556), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(250, 396), ImGuiCond_FirstUseEver);
            ImGui::Begin("Asset Browser", &showAssets);
            const std::filesystem::path root = editorApp.getActiveProject().isValid()
                ? std::filesystem::path(editorApp.getActiveProject().directory)
                : std::filesystem::path("examples/rpg_reference_project");
            ImGui::Text("Root: %s", root.string().c_str());
            if (std::filesystem::exists(root)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
                    if (entry.is_regular_file()) {
                        ImGui::BulletText("%s", std::filesystem::relative(entry.path(), root).string().c_str());
                    }
                }
            }
            ImGui::End();
        }

        // 7. Material & Lighting Editor
        if (showMaterials) {
            ImGui::SetNextWindowPos(ImVec2(1234, 476), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(358, 230), ImGuiCond_FirstUseEver);
            ImGui::Begin("Material & Lighting", &showMaterials);
            ImGui::SliderFloat("Sun Yaw", &lighting.sunYawDeg, -180.0f, 180.0f);
            ImGui::SliderFloat("Sun Pitch", &lighting.sunPitchDeg, -89.0f, 0.0f);
            ImGui::SliderFloat("Sun Intensity", &lighting.sunIntensity, 0.0f, 8.0f);
            ImGui::ColorEdit3("Sun Color", lighting.sunColor);
            ImGui::SliderFloat("Ambient", &lighting.ambientIntensity, 0.0f, 2.0f);
            ImGui::SliderFloat("Exposure", &lighting.exposure, 0.2f, 3.0f);
            ImGui::End();
        }

        // 8. World / Terrain / Animation / Audio / RPG Framework Tools
        if (showTools) {
            ImGui::SetNextWindowPos(ImVec2(1234, 714), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(358, 238), ImGuiCond_FirstUseEver);
            ImGui::Begin("World, Audio & RPG Framework", &showTools);
            if (ImGui::BeginTabBar("EngineToolsTabs")) {
                if (ImGui::BeginTabItem("Terrain")) {
                    const char* presets[] = {"Balanced", "Continental", "Mountainous", "Archipelago", "Desert", "Alpine"};
                    ImGui::Combo("World Preset", &terrainPreset, presets, 6);
                    ImGui::SliderFloat("World Scale", &terrainScale, 0.25f, 4.0f);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Animation")) {
                    ImGui::Combo("Preview State", &activeAnimState, animStates, 5);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Audio")) {
                    ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("RPG Framework")) {
                    float hr = worldTime.getHour();
                    if (ImGui::SliderFloat("World Hour", &hr, 0.0f, 23.9f)) worldTime.setTime(worldTime.getDay(), hr);
                    ImGui::Text("Active Quest: q_beacon (Find Beacon Relic)");
                    ImGui::Text("Dialogue Graph: guide_intro (Localized)");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }

        // 9. Console & Profiler
        if (showConsole) {
            ImGui::SetNextWindowPos(ImVec2(266, 676), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(960, 276), ImGuiCond_FirstUseEver);
            ImGui::Begin("Console & Profiler", &showConsole);
            ImGui::Text("FPS: %.1f | Entities: %zu | Undo Depth: %zu",
                        io.Framerate, sceneEntities.size(), editorApp.getCommands().getUndoCount());
            ImGui::PlotLines("Frame ms", frameTimes, 64, frameIdx, nullptr, 0.0f, 33.3f, ImVec2(0, 48));
            ImGui::Separator();
            for (const auto& n : editorApp.getNotifications()) {
                ImGui::Text("[%s] %s", n.title.c_str(), n.detail.c_str());
            }
            ImGui::End();
        }

        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData->DisplaySize.x > 0.0f && drawData->DisplaySize.y > 0.0f) {
            auto* wd = &vkCtx.mainWindowData;
            VkSemaphore imgSem = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
            VkSemaphore renSem = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
            VkResult err = vkAcquireNextImageKHR(vkCtx.device, wd->Swapchain, UINT64_MAX, imgSem, VK_NULL_HANDLE, &wd->FrameIndex);
            if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
                vkCtx.swapChainRebuild = true;
            } else if (err == VK_SUCCESS) {
                auto* fd = &wd->Frames[wd->FrameIndex];
                vkWaitForFences(vkCtx.device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
                vkResetFences(vkCtx.device, 1, &fd->Fence);
                vkResetCommandPool(vkCtx.device, fd->CommandPool, 0);
                VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
                vkBeginCommandBuffer(fd->CommandBuffer, &bi);

                VkClearValue clearVal{};
                clearVal.color = {{0.07f, 0.09f, 0.12f, 1.0f}};
                VkRenderPassBeginInfo rpi{};
                rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rpi.renderPass = wd->RenderPass;
                rpi.framebuffer = fd->Framebuffer;
                rpi.renderArea.extent = {static_cast<uint32_t>(wd->Width), static_cast<uint32_t>(wd->Height)};
                rpi.clearValueCount = 1;
                rpi.pClearValues = &clearVal;
                vkCmdBeginRenderPass(fd->CommandBuffer, &rpi, VK_SUBPASS_CONTENTS_INLINE);
                ImGui_ImplVulkan_RenderDrawData(drawData, fd->CommandBuffer);
                vkCmdEndRenderPass(fd->CommandBuffer);
                vkEndCommandBuffer(fd->CommandBuffer);

                VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 1, &imgSem, &waitStage, 1, &fd->CommandBuffer, 1, &renSem};
                vkQueueSubmit(vkCtx.queue, 1, &si, fd->Fence);

                VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 1, &renSem, 1, &wd->Swapchain, &wd->FrameIndex, nullptr};
                err = vkQueuePresentKHR(vkCtx.queue, &pi);
                if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) vkCtx.swapChainRebuild = true;
                wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;

                ++frameCounter;
                if (options.guiSmoke && frameCounter >= options.smokeFrames) {
                    if (!options.screenshotPath.empty()) {
                        vkCtx.saveSwapchainScreenshotBmp(options.screenshotPath);
                    }
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
            }
        }
    }

    editorApp.shutdown();
    vkDeviceWaitIdle(vkCtx.device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkCtx.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "[" << sidera::kSideraEngineBanner << " Editor] Graphical session exited cleanly (" << frameCounter << " frames).\n";
    return 0;
}

} // namespace sidera::editor
