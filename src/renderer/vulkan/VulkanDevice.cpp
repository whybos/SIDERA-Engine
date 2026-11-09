#include "renderer/vulkan/VulkanDevice.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"

#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sidera {

const std::vector<const char*> VulkanDevice::s_requiredDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

namespace {
constexpr std::string_view deviceTypeToString(VkPhysicalDeviceType type) noexcept {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU / Software";
        default:                                     return "Other";
    }
}
} // namespace

VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface) {
    pickPhysicalDevice(instance, surface);
    createLogicalDevice();
}

VulkanDevice::~VulkanDevice() {
    if (m_device != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Vulkan", "Destroying Vulkan logical device...");
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
}

void VulkanDevice::waitIdle() const {
    if (m_device != VK_NULL_HANDLE) {
        VK_CHECK(vkDeviceWaitIdle(m_device), "Failed waiting for Vulkan logical device idle");
    }
}

void VulkanDevice::pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr),
             "Failed to enumerate physical device count");

    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find any GPUs with Vulkan support.");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()),
             "Failed to enumerate physical devices");

    SIDERA_LOG_INFO("Vulkan", "Discovered {} Vulkan physical device(s):", deviceCount);

    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[i], &props);

        const int score = scorePhysicalDevice(devices[i], surface);
        SIDERA_LOG_INFO(
            "Vulkan",
            "  [GPU {}] {} ({}, Vulkan {}.{}.{}) -> Score: {}",
            i,
            props.deviceName,
            deviceTypeToString(props.deviceType),
            VK_API_VERSION_MAJOR(props.apiVersion),
            VK_API_VERSION_MINOR(props.apiVersion),
            VK_API_VERSION_PATCH(props.apiVersion),
            score
        );

        if (score > bestScore) {
            bestScore = score;
            bestDevice = devices[i];
        }
    }

    if (bestDevice == VK_NULL_HANDLE || bestScore <= 0) {
        throw std::runtime_error(
            "Failed to find a suitable GPU supporting Vulkan 1.3 Dynamic Rendering, Swapchain, and Presentation."
        );
    }

    m_physicalDevice = bestDevice;
    m_queueFamilies = findQueueFamilies(m_physicalDevice, surface);
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);

    SIDERA_LOG_INFO(
        "Vulkan",
        "Selected Physical GPU: '{}' ({}, API {}.{}.{}, Graphics Queue Family: {}, Present Queue Family: {})",
        m_properties.deviceName,
        deviceTypeToString(m_properties.deviceType),
        VK_API_VERSION_MAJOR(m_properties.apiVersion),
        VK_API_VERSION_MINOR(m_properties.apiVersion),
        VK_API_VERSION_PATCH(m_properties.apiVersion),
        m_queueFamilies.graphicsFamily.value(),
        m_queueFamilies.presentFamily.value()
    );
}

int VulkanDevice::scorePhysicalDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const {
    const QueueFamilyIndices indices = findQueueFamilies(device, surface);
    if (!indices.isComplete()) {
        return 0;
    }

    if (!checkDeviceExtensionSupport(device)) {
        return 0;
    }

    if (!checkVulkan13FeatureSupport(device)) {
        return 0;
    }

    VkPhysicalDeviceDriverProperties driverProps{};
    driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &driverProps;
    vkGetPhysicalDeviceProperties2(device, &props2);

    const VkPhysicalDeviceProperties& props = props2.properties;

    int score = 0;

    // Strongly favor discrete GPUs for large-scale open-world rendering
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 10000;
    } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 1000;
    } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) {
        score += 500;
    } else {
        score += 100;
    }

    // Prefer native hardware Vulkan drivers over translation layers (e.g., Mesa Dozen D3D12 wrapper)
    if (driverProps.driverID != VK_DRIVER_ID_MESA_DOZEN) {
        score += 5000;
    }

    // Favor unified graphics + presentation queue family for reduced synchronization overhead
    if (indices.graphicsFamily == indices.presentFamily) {
        score += 500;
    }

    // Favor higher supported Vulkan API minor version and texture limits
    score += static_cast<int>(VK_API_VERSION_MINOR(props.apiVersion)) * 100;
    score += static_cast<int>(props.limits.maxImageDimension2D / 1024);

    return score;
}

void VulkanDevice::createLogicalDevice() {
    std::set<uint32_t> uniqueQueueFamilies = {
        m_queueFamilies.graphicsFamily.value(),
        m_queueFamilies.presentFamily.value()
    };

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueQueueFamilies.size());

    const float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supportedFeatures);
    m_samplerAnisotropyEnabled = (supportedFeatures.samplerAnisotropy == VK_TRUE);

    // Enable Vulkan 1.3 Dynamic Rendering and Synchronization2
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &features13;
    deviceFeatures2.features.samplerAnisotropy = m_samplerAnisotropyEnabled ? VK_TRUE : VK_FALSE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &deviceFeatures2;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = nullptr; // Using VkPhysicalDeviceFeatures2 in pNext
    createInfo.enabledExtensionCount = static_cast<uint32_t>(s_requiredDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = s_requiredDeviceExtensions.data();
    createInfo.enabledLayerCount = 0;

    VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device),
             "Failed to create Vulkan logical device");

    m_dynamicRenderingEnabled = true;

    vkGetDeviceQueue(m_device, m_queueFamilies.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilies.presentFamily.value(), 0, &m_presentQueue);

    SIDERA_LOG_INFO(
        "Vulkan",
        "Logical device created (Vulkan 1.3 Dynamic Rendering: ENABLED, Synchronization2: ENABLED, SamplerAnisotropy: {}).",
        m_samplerAnisotropyEnabled ? "ENABLED" : "DISABLED"
    );
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        VK_CHECK(
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport),
            "Failed to query physical device surface support"
        );

        if (presentSupport == VK_TRUE) {
            indices.presentFamily = i;
        }

        if (indices.graphicsFamily == i && indices.presentFamily == i) {
            break;
        }
    }

    return indices;
}

bool VulkanDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount = 0;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr),
             "Failed to enumerate device extension count");

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    VK_CHECK(
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data()),
        "Failed to enumerate device extensions"
    );

    std::set<std::string> requiredExtensions(
        s_requiredDeviceExtensions.begin(),
        s_requiredDeviceExtensions.end()
    );

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

bool VulkanDevice::checkVulkan13FeatureSupport(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);
    if (props.apiVersion < VK_API_VERSION_1_3) {
        return false;
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;

    vkGetPhysicalDeviceFeatures2(device, &features2);

    return (features13.dynamicRendering == VK_TRUE) && (features13.synchronization2 == VK_TRUE);
}

} // namespace sidera
