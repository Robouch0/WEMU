//
// Created by nicolas on 2/18/26.
//

#include "headers/wemuEngineVulkan.hpp"
#include <map>
#include <set>
#include <stdexcept>

void WemuEngineVulkan::pickPhysicalDevice() {
    m_physicalDevice = VK_NULL_HANDLE;
    uint32_t deviceCount = 0;

    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr); // first one to count the devices

    std::vector<VkPhysicalDevice> devices(deviceCount);

    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data()); // second one to fill the devices vector

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support");
    }

    std::multimap<int, VkPhysicalDevice> candidates;

    for (const auto &device: devices) {
        if (isDeviceSuitable(device)) {
            m_physicalDevice = device;
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

WemuEngineVulkan::QueueFamilyIndices WemuEngineVulkan::findQueueFamilies(const VkPhysicalDevice device) const {
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto &queueFamily: queueFamilies) {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = 1;
        }

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        if (indices.isComplete())
            break;
        i++;
    }

    return indices;
}

bool WemuEngineVulkan::checkDeviceExtensionSupport(const VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);


    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

bool WemuEngineVulkan::isDeviceSuitable(const VkPhysicalDevice device) const {
    const QueueFamilyIndices indices = findQueueFamilies(device);

    const bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        const SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    // VkPhysicalDeviceFeatures supportedFeatures;
    // vkGetPhysicalDeviceFeatures(m_logicalDevice, &supportedFeatures);

    return indices.isComplete() && extensionsSupported && swapChainAdequate /*&& supportedFeatures.samplerAnisotropy*/;
}

[[nodiscard]] uint32_t WemuEngineVulkan::findMemoryType(const uint32_t typeFilter, const VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

