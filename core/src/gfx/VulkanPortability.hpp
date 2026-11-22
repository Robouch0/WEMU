#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <vulkan/vulkan.h>

// MoltenVK (macOS) is a Vulkan portability driver: the loader hides it unless the instance opts in,
// and a device that exposes VK_KHR_portability_subset must have it enabled. Elsewhere both are no-ops.
namespace Core::Gfx::Portability {

    inline constexpr const char *kEnumeration = "VK_KHR_portability_enumeration";
    inline constexpr const char *kSubset = "VK_KHR_portability_subset";
    inline constexpr const char *kProperties2 = "VK_KHR_get_physical_device_properties2";

    [[nodiscard]] inline bool hasExtension(const std::vector<VkExtensionProperties> &available, const char *name)
    {
        return std::ranges::any_of(available, [name](const VkExtensionProperties &e) { return std::strcmp(e.extensionName, name) == 0; });
    }

    inline void addOnce(std::vector<const char *> &extensions, const char *name)
    {
        if (std::ranges::none_of(extensions, [name](const char *e) { return std::strcmp(e, name) == 0; }))
            extensions.push_back(name);
    }

    // Adds the portability instance extensions the loader offers and sets the matching create flag.
    // `extensions` must outlive vkCreateInstance; it becomes the create info's extension list.
    inline void enableEnumeration(VkInstanceCreateInfo &info, std::vector<const char *> &extensions)
    {
#ifdef __APPLE__
        // MoltenVK compiles shaders with Metal fast math by default, which breaks the bit-exact float
        // results Latte emulation relies on (depth quantization, NaN-aware compares). Must be set
        // before the driver loads; an explicit user setting wins.
        setenv("MVK_CONFIG_FAST_MATH_ENABLED", "0", 0);
#endif
        std::uint32_t count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
        if (hasExtension(available, kEnumeration)) {
            addOnce(extensions, kEnumeration);
            info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            // VK_KHR_portability_subset requires it on a Vulkan 1.0 instance.
            if (hasExtension(available, kProperties2))
                addOnce(extensions, kProperties2);
        }
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
    }

    // Adds VK_KHR_portability_subset when the physical device exposes it.
    inline void enableSubset(VkPhysicalDevice device, std::vector<const char *> &extensions)
    {
        std::uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());
        if (hasExtension(available, kSubset))
            addOnce(extensions, kSubset);
    }

} // namespace Core::Gfx::Portability
