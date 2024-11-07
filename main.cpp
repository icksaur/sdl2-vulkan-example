// g++ -g -lSDL2 -lSDL2main -lvulkan main.cpp

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <istream>
#include <fstream>
#include <vulkan/vulkan_core.h>
#include <vector>
#include <set>
#include <assert.h>

#include "tga.h"

// Global Settings
const char                      gAppName[] = "VulkanTest";
const char                      gEngineName[] = "VulkanTestEngine";
int                             gWindowWidth = 1280;
int                             gWindowHeight = 720;
VkPresentModeKHR                gPresentationMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
VkSurfaceTransformFlagBitsKHR   gTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
VkFormat                        gFormat = VK_FORMAT_B8G8R8A8_SRGB;
VkColorSpaceKHR                 gColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
VkImageUsageFlags               gImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

struct PipelineInfo {
    float w, h;
    VkExtent2D extent;
    VkFormat colorFormat;
} pipelineInfo;

std::vector<char> readFileBytes(std::istream & file) {
    return std::vector<char>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

/**
 * This demo attempts to create a window and vulkan compatible surface using SDL
 * Verified and tested using multiple CPUs under windows.
 * Should work on every other SDL / Vulkan supported operating system (OSX, Linux, Android)
 * main() clearly outlines all the specific steps taken to create a vulkan instance,
 * select a device, create a vulkan compatible surface (opaque) associated with a window.
 */

//////////////////////////////////////////////////////////////////////////
// Global Settings
//////////////////////////////////////////////////////////////////////////

const std::set<std::string>& getRequestedLayerNames() {
    static std::set<std::string> layers;
    if (layers.empty()) {
        layers.emplace("VK_LAYER_NV_optimus");
        layers.emplace("VK_LAYER_KHRONOS_validation");
    }
    return layers;
}

const std::set<std::string>& getRequestedDeviceExtensionNames() {
    static std::set<std::string> layers;
    if (layers.empty()) {
        layers.emplace(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    return layers;
}

const std::vector<VkImageUsageFlags> getRequestedImageUsages() {
    static std::vector<VkImageUsageFlags> usages;
    if (usages.empty()) {
        usages.emplace_back(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }
    return usages;
}

template<typename T>
T clamp(T value, T min, T max) {
    return (value < min) ? min : (value > max) ? max : value;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objType,
    uint64_t obj,
    size_t location,
    int32_t code,
    const char* layerPrefix,
    const char* msg,
    void* userData)
{
    std::cout << "validation layer: " << layerPrefix << ": " << msg << std::endl;
    return VK_FALSE;
}

VkResult createDebugReportCallbackEXT(
    VkInstance instance,
    const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugReportCallbackEXT* pCallback)
{
    auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pCallback);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

bool setupDebugCallback(VkInstance instance, VkDebugReportCallbackEXT& callback) {
    VkDebugReportCallbackCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    createInfo.pfnCallback = debugCallback;

    if (createDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback) != VK_SUCCESS) {
        std::cout << "unable to create debug report callback extension\n";
        return false;
    }
    return true;
}

void destroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
    if (func != nullptr) {
        func(instance, callback, pAllocator);
    }
}

bool getAvailableVulkanLayers(std::vector<std::string>& outLayers) {
    // Figure out the amount of available layers
    // Layers are used for debugging / validation etc / profiling..
    unsigned int instance_layer_count = 0;
    if (vkEnumerateInstanceLayerProperties(&instance_layer_count, NULL) != VK_SUCCESS) {
        std::cout << "unable to query vulkan instance layer property count\n";
        return false;
    }

    std::vector<VkLayerProperties> instance_layer_names(instance_layer_count);
    if (vkEnumerateInstanceLayerProperties(&instance_layer_count, instance_layer_names.data()) != VK_SUCCESS) {
        std::cout << "unable to retrieve vulkan instance layer names\n";
        return false;
    }

    // Display layer names and find the ones we specified above
    std::cout << "found " << instance_layer_count << " instance layers:\n";
    std::vector<const char*> valid_instance_layer_names;

    std::set<std::string> requestedLayers({"VK_LAYER_KHRONOS_validation"});

    int count = 0;
    outLayers.clear();
    for (const auto& name : instance_layer_names) {
        std::cout << count << ": " << name.layerName << ": " << name.description << "\n";
        auto it = requestedLayers.find(std::string(name.layerName));
        if (it != requestedLayers.end())
            outLayers.emplace_back(name.layerName);
        count++;
    }

    // Print the ones we're enabling
    std::cout << "\n";
    for (const auto& layer : outLayers)
        std::cout << "applying layer: " << layer.c_str() << "\n";
    return true;
}


bool getAvailableVulkanExtensions(SDL_Window* window, std::vector<std::string>& outExtensions) {
    // Figure out the amount of extensions vulkan needs to interface with the os windowing system
    // This is necessary because vulkan is a platform agnostic API and needs to know how to interface with the windowing system
    unsigned int ext_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &ext_count, nullptr))
    {
        std::cout << "Unable to query the number of Vulkan instance extensions\n";
        return false;
    }

    // Use the amount of extensions queried before to retrieve the names of the extensions
    std::vector<const char*> ext_names(ext_count);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &ext_count, ext_names.data()))
    {
        std::cout << "Unable to query the number of Vulkan instance extension names\n";
        return false;
    }

    // Display names
    std::cout << "found " << ext_count << " Vulkan instance extensions:\n";
    for (unsigned int i = 0; i < ext_count; i++)
    {
        std::cout << i << ": " << ext_names[i] << "\n";
        outExtensions.emplace_back(ext_names[i]);
    }

    // Add debug display extension, we need this to relay debug messages
    outExtensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    std::cout << "\n";
    return true;
}

bool createVulkanInstance(const std::vector<std::string>& layerNames, const std::vector<std::string>& extensionNames, VkInstance& outInstance) {
    // Copy layers
    std::vector<const char*> layer_names;
    for (const auto& layer : layerNames)
        layer_names.emplace_back(layer.c_str());

    // Copy extensions
    std::vector<const char*> ext_names;
    for (const auto& ext : extensionNames)
        ext_names.emplace_back(ext.c_str());

    // Get the suppoerted vulkan instance version
    unsigned int api_version;
    vkEnumerateInstanceVersion(&api_version);

    // initialize the VkApplicationInfo structure
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pNext = NULL;
    app_info.pApplicationName = gAppName;
    app_info.applicationVersion = 1;
    app_info.pEngineName = gEngineName;
    app_info.engineVersion = 1;
    app_info.apiVersion = VK_API_VERSION_1_0;

    // initialize the VkInstanceCreateInfo structure
    VkInstanceCreateInfo inst_info = {};
    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pNext = NULL;
    inst_info.flags = 0;
    inst_info.pApplicationInfo = &app_info;
    inst_info.enabledExtensionCount = static_cast<uint32_t>(ext_names.size());
    inst_info.ppEnabledExtensionNames = ext_names.data();
    inst_info.enabledLayerCount = static_cast<uint32_t>(layer_names.size());
    inst_info.ppEnabledLayerNames = layer_names.data();

    // Create vulkan runtime instance
    std::cout << "initializing Vulkan instance\n\n";
    VkResult res = vkCreateInstance(&inst_info, NULL, &outInstance);
    switch (res)
    {
    case VK_SUCCESS:
        break;
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        std::cout << "unable to create vulkan instance, cannot find a compatible Vulkan ICD\n";
        return false;
    default:
        std::cout << "unable to create Vulkan instance: unknown error\n";
        return false;
    }
    return true;
}

bool selectGPU(VkInstance instance, VkPhysicalDevice& outDevice, unsigned int& outQueueFamilyIndex) {
    // Get number of available physical devices, needs to be at least 1
    unsigned int physical_device_count = 0;
    vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr);
    if (physical_device_count == 0) {
        std::cout << "No physical devices found\n";
        return false;
    }

    // Now get the devices
    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data());

    // Show device information
    std::cout << "found " << physical_device_count << " GPU(s):\n";
    int count = 0;
    std::vector<VkPhysicalDeviceProperties> physical_device_properties(physical_devices.size());
    for (auto& physical_device : physical_devices) {
        vkGetPhysicalDeviceProperties(physical_device, &(physical_device_properties[count]));
        std::cout << count << ": " << physical_device_properties[count].deviceName << "\n";
        count++;
    }

    // Select one if more than 1 is available
    unsigned int selection_id = 0;
    if (physical_device_count > 1)  {
        while (true) {
            std::cout << "select device: ";
            std::cin  >> selection_id;
            if (selection_id >= physical_device_count) {
                std::cout << "invalid selection, expected a value between 0 and " << physical_device_count - 1 << "\n";
                continue;
            }
            break;
        }
    }

    std::cout << "selected: " << physical_device_properties[selection_id].deviceName << "\n";
    VkPhysicalDevice selected_device = physical_devices[selection_id];

    // Find the number queues this device supports, we want to make sure that we have a queue that supports graphics commands
    unsigned int family_queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(selected_device, &family_queue_count, nullptr);
    if (family_queue_count == 0) {
        std::cout << "device has no family of queues associated with it\n";
        return false;
    }

    // Extract the properties of all the queue families
    std::vector<VkQueueFamilyProperties> queue_properties(family_queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(selected_device, &family_queue_count, queue_properties.data());

    // Make sure the family of commands contains an option to issue graphical commands.
    int queue_node_index = -1;
    for (unsigned int i = 0; i < family_queue_count; i++) {
        if (queue_properties[i].queueCount > 0 && queue_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_node_index = i;
            break;
        }
    }

    if (queue_node_index == -1) {
        std::cout << "Unable to find a queue command family that accepts graphics commands\n";
        return false;
    }

    // Set the output variables
    outDevice = selected_device;
    outQueueFamilyIndex = queue_node_index;
    return true;
}

bool createLogicalDevice(VkPhysicalDevice& physicalDevice,
    unsigned int queueFamilyIndex,
    const std::vector<std::string>& layerNames,
    VkDevice& outDevice)
{
    // Copy layer names
    std::vector<const char*> layer_names;
    for (const auto& layer : layerNames) {
        layer_names.emplace_back(layer.c_str());
    }
    
    // Get the number of available extensions for our graphics card
    uint32_t device_property_count(0);
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &device_property_count, NULL) != VK_SUCCESS) {
        std::cout << "Unable to acquire device extension property count\n";
        return false;
    }
    std::cout << "\nfound " << device_property_count << " device extensions\n";

    // Acquire their actual names
    std::vector<VkExtensionProperties> device_properties(device_property_count);
    if (vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &device_property_count, device_properties.data()) != VK_SUCCESS) {
        std::cout << "Unable to acquire device extension property names\n";
        return false;
    }

    // Match names against requested extension
    std::vector<const char*> device_property_names;
    const std::set<std::string>& required_extension_names = getRequestedDeviceExtensionNames();
    int count = 0;
    for (const auto& ext_property : device_properties) {
        std::cout << count << ": " << ext_property.extensionName << "\n";
        auto it = required_extension_names.find(std::string(ext_property.extensionName));
        if (it != required_extension_names.end()) {
            device_property_names.emplace_back(ext_property.extensionName);
        }
        count++;
    }

    // Warn if not all required extensions were found
    if (required_extension_names.size() != device_property_names.size()) {
        std::cout << "not all required device extensions are supported!\n";
        return false;
    }

    std::cout << "\n";
    for (const auto& name : device_property_names) {
        std::cout << "applying device extension: " << name << "\n";
    }

    // Create queue information structure used by device based on the previously fetched queue information from the physical device
    // We create one command processing queue for graphics
    VkDeviceQueueCreateInfo queue_create_info;
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queueFamilyIndex;
    queue_create_info.queueCount = 1;
    std::vector<float> queue_prio = { 1.0f };
    queue_create_info.pQueuePriorities = queue_prio.data();
    queue_create_info.pNext = NULL;
    queue_create_info.flags = 0;

    // Device creation information
    VkDeviceCreateInfo create_info;
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_create_info;
    create_info.ppEnabledLayerNames = layer_names.data();
    create_info.enabledLayerCount = static_cast<uint32_t>(layer_names.size());
    create_info.ppEnabledExtensionNames = device_property_names.data();
    create_info.enabledExtensionCount = static_cast<uint32_t>(device_property_names.size());
    create_info.pNext = NULL;
    create_info.pEnabledFeatures = NULL;
    create_info.flags = 0;

    // Finally we're ready to create a new device
    if (vkCreateDevice(physicalDevice, &create_info, nullptr, &outDevice) != VK_SUCCESS) {
        std::cout << "failed to create logical device!\n";
        return false;
    }
    return true;
}

void getDeviceQueue(VkDevice device, int familyQueueIndex, VkQueue& outGraphicsQueue) {
    vkGetDeviceQueue(device, familyQueueIndex, 0, &outGraphicsQueue);
}

bool createSurface(SDL_Window* window, VkInstance instance, VkPhysicalDevice gpu, uint32_t graphicsFamilyQueueIndex, VkSurfaceKHR& outSurface) {
    if (!SDL_Vulkan_CreateSurface(window, instance, &outSurface)) {
        std::cout << "Unable to create Vulkan compatible surface using SDL\n";
        return false;
    }

    // Make sure the surface is compatible with the queue family and gpu
    VkBool32 supported = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu, graphicsFamilyQueueIndex, outSurface, &supported);
    if (!supported) {
        std::cout << "Surface is not supported by physical device!\n";
        return false;
    }

    return true;
}

bool getPresentationMode(VkSurfaceKHR surface, VkPhysicalDevice device, VkPresentModeKHR& ioMode) {
    uint32_t mode_count(0);
    if(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, NULL) != VK_SUCCESS) {
        std::cout << "unable to query present mode count for physical device\n";
        return false;
    }

    std::vector<VkPresentModeKHR> available_modes(mode_count);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, available_modes.data()) != VK_SUCCESS) {
        std::cout << "unable to query the various present modes for physical device\n";
        return false;
    }

    for (auto& mode : available_modes) {
        if (mode == ioMode)
            return true;
    }
    std::cout << "unable to obtain preferred display mode, fallback to FIFO\n";

    std::cout << "available present modes: " << std::endl;
    for (auto & mode : available_modes) {
        std::cout << "    "  << mode << std::endl;
    }

    ioMode = VK_PRESENT_MODE_FIFO_KHR;
    return true;
}

bool getSurfaceProperties(VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR& capabilities) {
    if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities) != VK_SUCCESS) {
        std::cout << "unable to acquire surface capabilities\n";
        return false;
    }
    return true;
}

unsigned int getNumberOfSwapImages(const VkSurfaceCapabilitiesKHR& capabilities) {
    unsigned int number = capabilities.minImageCount + 1;
    return number > capabilities.maxImageCount ? capabilities.minImageCount : number;
}

VkExtent2D getSwapImageSize(const VkSurfaceCapabilitiesKHR& capabilities) {
    // Default size = window size
    VkExtent2D size = { (unsigned int)gWindowWidth, (unsigned int)gWindowHeight };

    // This happens when the window scales based on the size of an image
    if (capabilities.currentExtent.width == 0xFFFFFFF) {
        size.width  = clamp(size.width,  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width);
        size.height = clamp(size.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    } else {
        size = capabilities.currentExtent;
    }
    return size;
}

bool getImageUsage(const VkSurfaceCapabilitiesKHR& capabilities, VkImageUsageFlags& outUsage) {
    const std::vector<VkImageUsageFlags>& desir_usages = getRequestedImageUsages();
    assert(desir_usages.size() > 0);

    // Needs to be always present
    outUsage = desir_usages[0];

    for (const auto& desired_usage : desir_usages)
    {
        VkImageUsageFlags image_usage = desired_usage & capabilities.supportedUsageFlags;
        if (image_usage != desired_usage) {
            std::cout << "unsupported image usage flag: " << desired_usage << "\n";
            return false;
        }

        // Add bit if found as supported color
        outUsage = (outUsage | desired_usage);
    }

    return true;
}

VkSurfaceTransformFlagBitsKHR getSurfaceTransform(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.supportedTransforms & gTransform) {
        return gTransform;
    }
    std::cout << "unsupported surface transform: " << gTransform;
    return capabilities.currentTransform;
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        // Check if this memory type is included in memoryTypeBits (bitwise AND)
        if ((memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

bool getSurfaceFormat(VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR& outFormat) {
    unsigned int count(0);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr) != VK_SUCCESS) {
        std::cout << "unable to query number of supported surface formats";
        return false;
    }

    std::vector<VkSurfaceFormatKHR> found_formats(count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, found_formats.data()) != VK_SUCCESS) {
        std::cout << "unable to query all supported surface formats\n";
        return false;
    }

    // This means there are no restrictions on the supported format.
    // Preference would work
    if (found_formats.size() == 1 && found_formats[0].format == VK_FORMAT_UNDEFINED) {
        outFormat.format = gFormat;
        outFormat.colorSpace = gColorSpace;
        return true;
    }

    // Otherwise check if both are supported
    for (const auto& found_format_outer : found_formats) {
        // Format found
        if (found_format_outer.format == gFormat) {
            outFormat.format = found_format_outer.format;
            for (const auto& found_format_inner : found_formats) {
                // Color space found
                if (found_format_inner.colorSpace == gColorSpace) {
                    outFormat.colorSpace = found_format_inner.colorSpace;
                    return true;
                }
            }

            // No matching color space, pick first one
            std::cout << "warning: no matching color space found, picking first available one\n!";
            outFormat.colorSpace = found_formats[0].colorSpace;
            return true;
        }
    }

    // No matching formats found
    std::cout << "warning: no matching color format found, picking first available one\n";
    outFormat = found_formats[0];
    return true;
}

std::tuple<VkBuffer, VkDeviceMemory> createBuffer(VkPhysicalDevice gpu, VkDevice device, VkBufferUsageFlagBits usageFlags, size_t byteCount) {
    VkBuffer buffer;
    VkDeviceMemory memory;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteCount;
    bufferInfo.usage = usageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // Not shared across multiple queue families

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(gpu, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }

    vkBindBufferMemory(device, buffer, memory, 0);

    return std::make_tuple(buffer, memory);
}

// a helper to start and end a command buffer which can be submitted and waited
struct ScopedCommandBuffer {
    VkDevice device;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;
    VkCommandBuffer commandBuffer;
    ScopedCommandBuffer(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue) : device(device), commandPool(commandPool), graphicsQueue(graphicsQueue) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer");
        }
    }
    void submitAndWait() {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
    }
    ~ScopedCommandBuffer() {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }
};

void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    ScopedCommandBuffer scopedCommandBuffer(device, commandPool, graphicsQueue);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        scopedCommandBuffer.commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    scopedCommandBuffer.submitAndWait();
}

void copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    ScopedCommandBuffer scopedCommandBuffer(device, commandPool, graphicsQueue);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(scopedCommandBuffer.commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    scopedCommandBuffer.submitAndWait();
}

std::tuple<VkImage, VkDeviceMemory, VkFormat> createImageFromTGAFile(const char * filename, VkPhysicalDevice gpu, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue) {
    VkImage image;
    VkDeviceMemory memory;

    std::ifstream file(filename);
    std::vector<char> fileBytes = readFileBytes(file);
    file.close();
    unsigned width, height;
    int bpp;
    void* tgaBytes = read_tga(fileBytes, width, height, bpp);
    if (tgaBytes == nullptr) {
        throw std::runtime_error("failed to read file as TGA");
    }

    unsigned tgaByteCount = width*height*(bpp/8);

    // TGA is BGR, not RGB
    VkFormat format = (bpp == 32) ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8_SRGB;

    // put the image bytes into a buffer for transitioning
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    std::tie(stagingBuffer, stagingMemory) = createBuffer(gpu, device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tgaByteCount);

    void * stagingBytes;
    vkMapMemory(device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &stagingBytes);
    memcpy(stagingBytes, tgaBytes, (size_t)tgaByteCount);
    vkUnmapMemory(device, stagingMemory);
    free(tgaBytes);

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // we must "transition" this image to a device-optimal format
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; // copying to this image, and sampling from it
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan image");
    }

    VkMemoryRequirements memoryRequirements = {};
    vkGetImageMemoryRequirements(device, image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(gpu, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory");
    }
    vkBindImageMemory(device, image, memory, 0);

    transitionImageLayout(device, commandPool, graphicsQueue, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copyBufferToImage(device, commandPool, graphicsQueue, stagingBuffer, image, width, height);

    transitionImageLayout(device, commandPool, graphicsQueue, image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkFreeMemory(device, stagingMemory, nullptr);
    vkDestroyBuffer(device, stagingBuffer, nullptr);

    return std::make_tuple(image, memory, format);
}

bool createSwapChain(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice device, VkSwapchainKHR& outSwapChain) {
    vkDeviceWaitIdle(device);

    // Get properties of surface, necessary for creation of swap-chain
    VkSurfaceCapabilitiesKHR surface_properties;
    if (!getSurfaceProperties(physicalDevice, surface, surface_properties)) {
        return false;
    }

    // Get the image presentation mode (synced, immediate etc.)
    VkPresentModeKHR presentation_mode = gPresentationMode;
    if (!getPresentationMode(surface, physicalDevice, presentation_mode)) {
        return false;
    }

    // Get other swap chain related features
    unsigned int swapImageCount = getNumberOfSwapImages(surface_properties);
    std::cout << "swap chain image count: " << swapImageCount << std::endl;

    // Size of the images
    VkExtent2D swap_image_extent = getSwapImageSize(surface_properties);

    pipelineInfo.w = (float)swap_image_extent.width;
    pipelineInfo.h = (float)swap_image_extent.height;
    pipelineInfo.extent.height = pipelineInfo.h;
    pipelineInfo.extent.width = pipelineInfo.w;

    // Get image usage (color etc.)
    VkImageUsageFlags usage_flags;
    if (!getImageUsage(surface_properties, usage_flags)) {
        return false;
    }

    // Get the transform, falls back on current transform when transform is not supported
    VkSurfaceTransformFlagBitsKHR transform = getSurfaceTransform(surface_properties);

    // Get swapchain image format
    VkSurfaceFormatKHR imageFormat;
    if (!getSurfaceFormat(physicalDevice, surface, imageFormat)) {
        return false;
    }

    pipelineInfo.colorFormat = imageFormat.format;

    // Old swap chain
    VkSwapchainKHR old_swap_chain = outSwapChain;

    // Populate swapchain creation info
    VkSwapchainCreateInfoKHR swap_info;
    swap_info.pNext = nullptr;
    swap_info.flags = 0;
    swap_info.surface = surface;
    swap_info.minImageCount = swapImageCount;
    swap_info.imageFormat = imageFormat.format;
    swap_info.imageColorSpace = imageFormat.colorSpace;
    swap_info.imageExtent = swap_image_extent;
    swap_info.imageArrayLayers = 1;
    swap_info.imageUsage = usage_flags;
    swap_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swap_info.queueFamilyIndexCount = 0;
    swap_info.pQueueFamilyIndices = nullptr;
    swap_info.preTransform = transform;
    swap_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swap_info.presentMode = presentation_mode;
    swap_info.clipped = true;
    swap_info.oldSwapchain = NULL;
    swap_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;

    // Destroy old swap chain
    if (old_swap_chain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, old_swap_chain, nullptr);
        old_swap_chain = VK_NULL_HANDLE;
    }

    // Create new one
    if (vkCreateSwapchainKHR(device, &swap_info, nullptr, &old_swap_chain) != VK_SUCCESS) {
        std::cout << "unable to create swap chain\n";
        return false;
    }

    // Store handle
    outSwapChain = old_swap_chain;
    return true;
}

bool getSwapChainImageHandles(VkDevice device, VkSwapchainKHR chain, std::vector<VkImage>& outImageHandles) {
    unsigned int imageCount = 0;
    VkResult res = vkGetSwapchainImagesKHR(device, chain, &imageCount, nullptr);
    if (res != VK_SUCCESS) {
        std::cout << "unable to get number of images in swap chain\n";
        return false;
    }

    outImageHandles.clear();
    outImageHandles.resize(imageCount);
    if (vkGetSwapchainImagesKHR(device, chain, &imageCount, outImageHandles.data()) != VK_SUCCESS) {
        std::cout << "unable to get image handles from swap chain\n";
        return false;
    }
    return true;
}

void makeChainImageViews(VkDevice device, VkSwapchainKHR swapChain, std::vector<VkImage> & images, std::vector<VkImageView> & imageViews) {
    imageViews.resize(images.size());
    for (size_t i=0; i < images.size(); i++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images[i];  // The image from the swap chain
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pipelineInfo.colorFormat;  // Format of the swap chain images

        // Subresource range describes which parts of the image are accessible
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;  // Color attachment
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views!");
        }
    }
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format) {
    VkImageView textureImageView;
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image views");
    }

    return textureImageView;
}

VkSampler createSampler(VkDevice device) {
    VkSampler textureSampler;
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE; // fix to VK_TRUE when I get anisotropy feature enabled
    samplerInfo.maxAnisotropy = 16;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler");
    }

    return textureSampler;
}

void makeFramebuffers(VkDevice device, VkRenderPass renderPass, std::vector<VkImageView> & chainImageViews, std::vector<VkFramebuffer> & frameBuffers) {
    for (size_t i=0; i<chainImageViews.size(); i++) {
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;  // We are using only a color attachment
        framebufferInfo.pAttachments = &chainImageViews[i];  // Image view as color attachment
        framebufferInfo.width = pipelineInfo.extent.width;
        framebufferInfo.height = pipelineInfo.extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frameBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cout << "unable to open file: " << filename << "\n";
        return {};
     }
     std::vector<char> buffer(file.tellg());
     file.seekg(0);
     file.read(buffer.data(), buffer.size());
     file.close();
     return buffer;
}

bool createShaderModule(VkDevice device, const std::vector<char>& code, VkShaderModule& outShader) {
    VkShaderModuleCreateInfo module_info = {};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = code.size();
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    if (VK_SUCCESS == vkCreateShaderModule(device, &module_info, nullptr, &outShader)) {
        return true;
    }

    return false;
}

VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout) {
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    return pipelineLayout;
}

VkRenderPass createRenderPass(VkDevice device) {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = pipelineInfo.colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    VkRenderPass renderPass;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }

    return renderPass;
}

VkPipeline createGraphicsPipeline(VkDevice device, VkPipelineLayout pipelineLayout, VkRenderPass renderPass, VkShaderModule vertexShaderModule, VkShaderModule fragmentShaderModule) {
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertexShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragmentShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Binding description (one vec2 per vertex)
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 4;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Attribute description (vec2 -> location 0 in the shader)
    VkVertexInputAttributeDescription attributeDescriptions[2];
    attributeDescriptions[0] = {};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    // Attribute description (vec2 -> location 1 in the shader)
    VkVertexInputAttributeDescription attributeDescription = {};
    attributeDescriptions[1] = {};
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 2;

    // Pipeline vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = pipelineInfo.w;
    viewport.height = pipelineInfo.h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = pipelineInfo.extent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;  // Fill the triangles
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;    // Cull back faces
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // Counter-clockwise vertices are front
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipeline pipeline;
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stageCount = 2;
    pipelineCreateInfo.pStages = shaderStages;  // Vertex and fragment shaders
    pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
    pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pColorBlendState = &colorBlending;
    pipelineCreateInfo.layout = pipelineLayout;  // Pipeline layout created earlier
    pipelineCreateInfo.renderPass = renderPass;  // Render pass created earlier
    pipelineCreateInfo.subpass = 0;  // Index of the subpass where this pipeline will be used
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;  // Not deriving from another pipeline

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
    
    return pipeline;
}

VkShaderModule loadShaderModule(VkDevice device, const std::string& filename) {
    std::vector<char> code = readFile(filename);
    VkShaderModule shader_module = VK_NULL_HANDLE;
    createShaderModule(device, code, shader_module);
    return shader_module;
}

std::tuple<VkBuffer, VkDeviceMemory> createUniformbuffer(VkPhysicalDevice gpu, VkDevice device) {
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;

    float identityMatrix[]{ 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1 };

    size_t byteCount = sizeof(float)*16; // 4x4 matrix
    std::tie(uniformBuffer, uniformBufferMemory) = createBuffer(gpu, device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, byteCount);

    void* data;
    vkMapMemory(device, uniformBufferMemory, 0, byteCount, 0, &data);  // Map memory to CPU-accessible address
    memcpy(data, identityMatrix, (size_t)byteCount);                // Copy vertex data
    vkUnmapMemory(device, uniformBufferMemory);                              // Unmap memory after copying

    return std::make_tuple(uniformBuffer, uniformBufferMemory);
}

std::tuple<VkBuffer, VkDeviceMemory> createVertexBuffer(VkPhysicalDevice gpu, VkDevice device) {
    // Vulkan clip space has -1,-1 as the upper-left corner of the display and Y increases as you go down.
    // This is similar to most window system conventions and file formats.
    float vertices[] {
        -0.5f, -0.5f, 0.0f, 0.0f,
        0.5f, -0.5f, 1.0f, 0.0f,
        -0.5f, 0.5f, 0.0f, 1.0f,
        -0.5f, 0.5f, 0.0f, 1.0f,
        0.5f, -0.5f, 1.0f, 0.0f,
        0.5f, 0.5f, 1.0f, 1.0f,
    };

    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;

    size_t byteCount = sizeof(float)*4*6;
    std::tie(vertexBuffer, vertexBufferMemory) = createBuffer(gpu, device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, byteCount);

    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, byteCount, 0, &data);  // Map memory to CPU-accessible address
    memcpy(data, vertices, (size_t)byteCount);                // Copy vertex data
    vkUnmapMemory(device, vertexBufferMemory);                              // Unmap memory after copying

    return std::make_tuple(vertexBuffer, vertexBufferMemory);
}

VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device) {
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;  // No sampler here

    VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // binds both VkImageView and VkSampler
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr; // Sampler here?

    VkDescriptorSetLayoutBinding bindings[] {uboLayoutBinding, samplerLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout descriptorSetLayout;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }

    return descriptorSetLayout;
}

std::tuple<VkDescriptorPool, VkDescriptorSet> createDescriptorSet(VkDevice device, VkDescriptorSetLayout descriptorSetLayout) {
    VkDescriptorPoolSize poolSizes[2];
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // binds both VkImageView and VkSampler
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.poolSizeCount = 2;
    descriptorPoolCreateInfo.pPoolSizes = poolSizes;
    descriptorPoolCreateInfo.maxSets = 2;

    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool);

    VkDescriptorSetAllocateInfo allocInfo  = {};
    allocInfo.sType  = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool  = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);

    return std::make_tuple(descriptorPool, descriptorSet);
}

VkWriteDescriptorSet createBufferToDescriptorSetBinding(VkDevice device, VkDescriptorSet descriptorSet, VkBuffer uniformBuffer, VkDescriptorBufferInfo & bufferInfo) {
    bufferInfo = {};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(float)*16;

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    return descriptorWrite;
}

VkWriteDescriptorSet createSamplerToDescriptorSetBinding(VkDevice device, VkDescriptorSet descriptorSet, VkSampler sampler, VkImageView imageView, VkDescriptorImageInfo & imageInfo) {
    imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 1;  // match binding point in shader
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    return descriptorWrite;
}

void updateDescriptorSet(VkDevice device, VkDescriptorSet descriptorSet, std::vector<VkWriteDescriptorSet> & writeDescrptorSets) {
    vkUpdateDescriptorSets(device, writeDescrptorSets.size(), writeDescrptorSets.data(), 0, nullptr);
}

VkCommandPool createCommandPool(VkDevice device, uint32_t queueFamilyIndex) {
    VkCommandPool commandPool;

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // can be 0, but validation warns about implicit command buffer resets

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }

    return commandPool;
}

VkCommandBuffer createCommandBuffer(VkDevice device, VkPipeline graphicsPipeline, VkCommandPool commandPool) {
    VkCommandBuffer commandBuffer;

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;  // The command pool you created earlier
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // primary can be submitted, secondary can be a sub-command of primaries
    allocInfo.commandBufferCount = 1;  // Number of command buffers to allocate

    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffer!");
    }

    return commandBuffer;
}

VkSemaphore createSemaphore(VkDevice device) {
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.flags = 0;
    createInfo.pNext = nullptr;

    VkSemaphore semaphore;
    
    if (vkCreateSemaphore(device, &createInfo, NULL, &semaphore) != VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphore");
    }

    return semaphore;
}

VkQueue getPresentationQueue(VkPhysicalDevice gpu, VkDevice logicalDevice, uint graphicsQueueIndex, VkSurfaceKHR presentation_surface) {
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu, graphicsQueueIndex, presentation_surface, &presentSupport);
    if (presentSupport == false) {
        throw std::runtime_error("presentation queue is not supported on graphics queue index");
    }

    VkQueue presentQueue;
    vkGetDeviceQueue(logicalDevice, graphicsQueueIndex, 0, &presentQueue);

    return presentQueue;
}

VkFence createFence(VkDevice device) {
    VkFenceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    createInfo.pNext = nullptr;

    VkFence fence;
    if (vkCreateFence(device, &createInfo, NULL, &fence) != VK_SUCCESS) {
        throw std::runtime_error("failed to create fence");
    }

    return fence;
}

void recordRenderPass(
    VkPipeline graphicsPipeline,
    VkRenderPass renderPass,
    VkFramebuffer framebuffer,
    VkCommandBuffer commandBuffer,
    VkBuffer vertexBuffer,
    VkPipelineLayout pipelineLayout,
    VkDescriptorSet descriptorSet
) {
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;  // Can be resubmitted multiple times

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin command buffer");
    }

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass;  // Your created render pass
    renderPassBeginInfo.framebuffer = framebuffer;  // The framebuffer corresponding to the swap chain image

    // Define the render area (usually the size of the swap chain image)
    renderPassBeginInfo.renderArea.offset = { 0, 0 };  // Starting at (0, 0)
    renderPassBeginInfo.renderArea.extent = pipelineInfo.extent;  // Covers the whole framebuffer (usually the swap chain image size)

    // Set clear values for attachments (e.g., clearing the color buffer to black and depth buffer to 1.0f)
    VkClearValue clearValues[2];
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };  // Clear color: black
    clearValues[1].depthStencil = { 1.0f, 0 };               // Clear depth: 1.0, no stencil

    renderPassBeginInfo.clearValueCount = 2;                 // Two clear values (color and depth)
    renderPassBeginInfo.pClearValues = clearValues;

    // begin recording the render pass
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind the descriptor which contains the shader uniform buffer
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);  // Bind the vertex buffer

    vkCmdDraw(commandBuffer, 6, 1, 0, 0);  // Draw 6 vertices

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void submitCommandBuffer(VkQueue graphicsQueue, VkCommandBuffer commandBuffer, VkSemaphore imageAvailableSemaphore, VkSemaphore renderFinishedSemaphore) {
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkCommandBuffer commandBuffers[] = {commandBuffer};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandBuffers;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit command buffer!");
    }

    if (vkQueueWaitIdle(graphicsQueue) != VK_SUCCESS) {
        throw std::runtime_error("failed to wait for the graphics queue to be idle");
    }
}

bool presentQueue(VkQueue presentQueue, VkSwapchainKHR & swapchain, VkSemaphore renderFinishedSemaphore, uint nextImage) {
    // Present the image to the screen, waiting for renderFinishedSemaphore
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore; // waits for this
    VkSwapchainKHR swapChains[] = {swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &nextImage;

    VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return false;
        } else {
            throw std::runtime_error("failed to present swap chain image!");
        }
    }

    return true;
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        return -1;
    }

    // Create vulkan compatible window
    SDL_Window* window = SDL_CreateWindow(gAppName, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, gWindowWidth, gWindowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        SDL_Quit();
        return -1;
    }

    // Get available vulkan extensions, necessary for interfacing with native window
    // SDL takes care of this call and returns, next to the default VK_KHR_surface a platform specific extension
    // When initializing the vulkan instance these extensions have to be enabled in order to create a valid
    // surface later on.
    std::vector<std::string> found_extensions;
    if (!getAvailableVulkanExtensions(window, found_extensions))
        return -1;

    // Get available vulkan layer extensions, notify when not all could be found
    std::vector<std::string> found_layers;
    if (!getAvailableVulkanLayers(found_layers))
        return -1;

    // Warn when not all requested layers could be found
    if (found_layers.size() != getRequestedLayerNames().size())
        std::cout << "warning! not all requested layers could be found!\n";

    // Create Vulkan Instance
    VkInstance instance;
    if (!createVulkanInstance(found_layers, found_extensions, instance))
        return -1;

    // Vulkan messaging callback
    VkDebugReportCallbackEXT callback;
    setupDebugCallback(instance, callback);

    // Select GPU after succsessful creation of a vulkan instance (jeeeej no global states anymore)
    VkPhysicalDevice gpu;
    unsigned int graphicsQueueIndex(-1);
    if (!selectGPU(instance, gpu, graphicsQueueIndex))
        return -1;

    // Create a logical device that interfaces with the physical device
    VkDevice device;
    if (!createLogicalDevice(gpu, graphicsQueueIndex, found_layers, device))
        return -1;

    // Create the surface we want to render to, associated with the window we created before
    // This call also checks if the created surface is compatible with the previously selected physical device and associated render queue
    VkSurfaceKHR presentation_surface;
    if (!createSurface(window, instance, gpu, graphicsQueueIndex, presentation_surface))
        return -1;

    VkQueue presentationQueue = getPresentationQueue(gpu, device, graphicsQueueIndex, presentation_surface);

    // Create swap chain
    VkSwapchainKHR swapchain = NULL;
    if (!createSwapChain(presentation_surface, gpu, device, swapchain))
        return -1;

    // Get image handles from swap chain
    std::vector<VkImage> chainImages;
    if (!getSwapChainImageHandles(device, swapchain, chainImages))
        return -1;

    std::vector<VkImageView> chainImageViews(chainImages.size());
    makeChainImageViews(device, swapchain, chainImages, chainImageViews);

    // Fetch the queue we want to submit the actual commands to
    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, graphicsQueueIndex, 0, &graphicsQueue);

    VkCommandPool commandPool = createCommandPool(device, graphicsQueueIndex);

    VkShaderModule vertShader = loadShaderModule(device, "tri.vert.spv");
    VkShaderModule fragShader = loadShaderModule(device, "tri.frag.spv");

    // uniform buffer for our view projection matrix
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    std::tie(uniformBuffer, uniformBufferMemory) = createUniformbuffer(gpu, device);

    // texture for sampling
    VkDeviceMemory textureImageMemory;
    VkImage textureImage;
    VkFormat textureImageFormat;
    std::tie(textureImage, textureImageMemory, textureImageFormat) = createImageFromTGAFile("vulkan.tga", gpu, device, commandPool, graphicsQueue);

    VkImageView textureImageView = createImageView(device, textureImage, textureImageFormat);
    VkSampler textureSampler = createSampler(device);

    // descriptor of uniforms, both buffer and sampler
    VkDescriptorSetLayout descriptorSetLayout = createDescriptorSetLayout(device);
    
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    std::tie(descriptorPool, descriptorSet) = createDescriptorSet(device, descriptorSetLayout);

    VkDescriptorBufferInfo bufferInfo;
    VkDescriptorImageInfo imageInfo;

    std::vector<VkWriteDescriptorSet> descriptorWriteSets;
    descriptorWriteSets.push_back(createBufferToDescriptorSetBinding(device, descriptorSet, uniformBuffer, bufferInfo));
    descriptorWriteSets.push_back(createSamplerToDescriptorSetBinding(device, descriptorSet, textureSampler, textureImageView, imageInfo));

    updateDescriptorSet(device, descriptorSet, descriptorWriteSets);

    // pipeline and render pass
    VkPipelineLayout pipelineLayout = createPipelineLayout(device, descriptorSetLayout);

    VkRenderPass renderPass = createRenderPass(device);

    // buffers to render to for presenting
    std::vector<VkFramebuffer> frameBuffers(chainImages.size());
    makeFramebuffers(device, renderPass, chainImageViews, frameBuffers);

    VkPipeline pipeline = createGraphicsPipeline(device, pipelineLayout, renderPass, vertShader, fragShader);

    // vertex buffer for our vertices
    VkBuffer vertexBuffer;
    VkDeviceMemory deviceMemory;
    std::tie(vertexBuffer, deviceMemory) = createVertexBuffer(gpu, device);

    // command buffers for drawing
    std::vector<VkCommandBuffer> commandBuffers(chainImages.size());
    for (auto & commandBuffer : commandBuffers) {
        commandBuffer = createCommandBuffer(device, pipeline, commandPool);
    }

    // sync primitives
    VkSemaphore imageAvailableSemaphore = createSemaphore(device);
    VkSemaphore renderFinishedSemaphore = createSemaphore(device);
    VkFence fence = createFence(device);
    
    uint nextImage = 0;

    SDL_Event event;
    bool done = false;
    while (!done) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = true;
            }
        }
        vkResetFences(device, 1, &fence);

        VkResult nextImageResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, fence, &nextImage);
        if (nextImageResult != VK_SUCCESS) {
            std::cout << nextImageResult << std::endl;
            throw std::runtime_error("vkAcquireNextImageKHR failed");
        }

        recordRenderPass(pipeline, renderPass, frameBuffers[nextImage], commandBuffers[nextImage], vertexBuffer, pipelineLayout, descriptorSet);
        submitCommandBuffer(graphicsQueue, commandBuffers[nextImage], imageAvailableSemaphore, renderFinishedSemaphore);
        if (!presentQueue(presentationQueue, swapchain, renderFinishedSemaphore, nextImage)) {
            std::cout << "swap chain out of date, trying to remake" << std::endl;
            // remake framebuffers, image views, and swap chain
            vkDeviceWaitIdle(device);
            for (VkFramebuffer framebuffer : frameBuffers) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
            for (VkImageView view : chainImageViews) {
                vkDestroyImageView(device, view, nullptr);
            }
            vkDestroySwapchainKHR(device, swapchain, nullptr);

            swapchain = VK_NULL_HANDLE;
            if (!createSwapChain(presentation_surface, gpu, device, swapchain)) {
                throw std::runtime_error("failed to recreate swap chain");
            }
            if (!getSwapChainImageHandles(device, swapchain, chainImages)) {
                throw std::runtime_error("failed to re-obtain swap chain images");
            }
            makeChainImageViews(device, swapchain, chainImages, chainImageViews);
            makeFramebuffers(device, renderPass, chainImageViews, frameBuffers);
        }
        SDL_Delay(100);
        
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetCommandBuffer(commandBuffers[nextImage], 0); // manually reset, otherwise implicit reset causes warnings
    }

    vkQueueWaitIdle(graphicsQueue); // wait until we're done or the render finished semaphore may be in use

    for (auto commandBuffer : commandBuffers) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, deviceMemory, nullptr);
    vkDestroyBuffer(device, uniformBuffer, nullptr);
    vkFreeMemory(device, uniformBufferMemory,  nullptr);

    // freeing each descriptor requires the pool have the "free" bit. Look online for individual free 
    vkResetDescriptorPool(device, descriptorPool, 0); // frees all the descriptors
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    vkDestroySampler(device, textureSampler, nullptr);
    vkDestroyImageView(device, textureImageView, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    vkFreeMemory(device, textureImageMemory,  nullptr);

    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    for (VkFramebuffer framebuffer : frameBuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    for (VkImageView view : chainImageViews) {
        vkDestroyImageView(device, view, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);

    destroyDebugReportCallbackEXT(instance, callback, nullptr);
    vkDestroySurfaceKHR(instance, presentation_surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_Quit();

    return 1;
}