#include "renderer/vulkan/VulkanInstance.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace sidera {

namespace {
constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/) {

    const char* msg = (callbackData && callbackData->pMessage) ? callbackData->pMessage : "Unknown Vulkan debug message";

    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        SIDERA_LOG_ERROR("VulkanValidation", "{}", msg);
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        SIDERA_LOG_WARN("VulkanValidation", "{}", msg);
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        SIDERA_LOG_INFO("VulkanValidation", "{}", msg);
    } else {
        SIDERA_LOG_DEBUG("VulkanValidation", "{}", msg);
    }

    return VK_FALSE;
}

VkResult createDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator,
    VkDebugUtilsMessengerEXT* debugMessenger) {
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")
    );
    if (func != nullptr) {
        return func(instance, createInfo, allocator, debugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* allocator) {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")
    );
    if (func != nullptr) {
        func(instance, debugMessenger, allocator);
    }
}
} // namespace

VulkanInstance::VulkanInstance(std::string_view appName,
                               const std::vector<const char*>& requiredExtensions,
                               bool requestValidation) {
    const std::string appNameStr(appName);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appNameStr.c_str();
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 17, 0);
    appInfo.pEngineName = "SIDERA Engine";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 17, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Verify all required platform/window extensions are supported by the Vulkan loader
    std::vector<const char*> extensions = requiredExtensions;
    for (const char* ext : extensions) {
        if (!checkExtensionSupport(ext)) {
            throw std::runtime_error(std::string("Required Vulkan instance extension not supported: ") + ext);
        }
    }

    std::vector<const char*> enabledLayers;
    bool enableDebugUtils = false;

    if (requestValidation) {
        if (checkLayerSupport(kValidationLayerName)) {
            enabledLayers.push_back(kValidationLayerName);
            m_validationLayerActive = true;
            SIDERA_LOG_INFO("Vulkan", "Validation layer status: ENABLED ('{}' detected and active).",
                            kValidationLayerName);
        } else {
            m_validationLayerActive = false;
            SIDERA_LOG_WARN(
                "Vulkan",
                "Validation layer status: UNAVAILABLE ('{}' requested in development build, "
                "but not found in system Vulkan layers).",
                kValidationLayerName
            );
        }

        if (checkExtensionSupport(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            enableDebugUtils = true;
        } else {
            SIDERA_LOG_WARN("Vulkan", "Extension '{}' not available; debug messenger disabled.",
                            VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    } else {
        SIDERA_LOG_INFO("Vulkan", "Validation layer status: DISABLED (Release build).");
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    createInfo.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableDebugUtils) {
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    }

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance), "Failed to create Vulkan instance");
    SIDERA_LOG_INFO(
        "Vulkan",
        "Vulkan instance created (API target: 1.3, active extensions: {}, active layers: {}).",
        extensions.size(),
        enabledLayers.size()
    );

    if (enableDebugUtils) {
        setupDebugMessenger();
    }
}

VulkanInstance::~VulkanInstance() {
    if (m_debugMessenger != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Vulkan", "Destroying Vulkan debug messenger...");
        destroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Vulkan", "Destroying Vulkan instance...");
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

bool VulkanInstance::checkLayerSupport(const char* layerName) {
    uint32_t layerCount = 0;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, nullptr),
             "Failed to enumerate Vulkan instance layer count");

    std::vector<VkLayerProperties> availableLayers(layerCount);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()),
             "Failed to enumerate Vulkan instance layers");

    for (const auto& layerProperties : availableLayers) {
        if (std::strcmp(layerName, layerProperties.layerName) == 0) {
            return true;
        }
    }
    return false;
}

bool VulkanInstance::checkExtensionSupport(const char* extensionName) {
    uint32_t extensionCount = 0;
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr),
             "Failed to enumerate Vulkan instance extension count");

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data()),
             "Failed to enumerate Vulkan instance extensions");

    for (const auto& ext : availableExtensions) {
        if (std::strcmp(extensionName, ext.extensionName) == 0) {
            return true;
        }
    }
    return false;
}

void VulkanInstance::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) noexcept {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = vulkanDebugCallback;
    createInfo.pUserData = nullptr;
}

void VulkanInstance::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);

    VK_CHECK(
        createDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger),
        "Failed to create Vulkan debug utils messenger"
    );
    SIDERA_LOG_INFO("Vulkan", "Debug utils messenger attached (VK_EXT_debug_utils).");
}

} // namespace sidera
