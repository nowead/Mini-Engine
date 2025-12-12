#include "VulkanDevice.hpp"
#include "PlatformConfig.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <ranges>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

VulkanDevice::VulkanDevice(const std::vector<const char*>& validationLayers, bool enableValidation)
	: enableValidationLayers(enableValidation)
	, validationLayers(validationLayers)
	, requiredDeviceExtensions(Platform::getRequiredDeviceExtensions())
{
	createInstance();
	setupDebugMessenger();
	pickPhysicalDevice();
	// Note: createLogicalDevice() is called after surface creation in createSurface()
}

void VulkanDevice::createSurface(GLFWwindow* window) {
	VkSurfaceKHR _surface;
	if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != VK_SUCCESS) {
		throw std::runtime_error("failed to create window surface!");
	}
	surface = vk::raii::SurfaceKHR(instance, _surface);
}

void VulkanDevice::createInstance() {
	constexpr vk::ApplicationInfo appInfo{
		.pApplicationName = "Vulkan FDF",
		.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
		.pEngineName = "No Engine",
		.engineVersion = VK_MAKE_VERSION(1, 0, 0),
		.apiVersion = VK_API_VERSION_1_3
		//.apiVersion = vk::ApiVersion14
	};

	// Get the required layers
	std::vector<char const*> requiredLayers;
	if (enableValidationLayers) {
		requiredLayers.assign(validationLayers.begin(), validationLayers.end());
	}

	// Check if the required layers are supported by the Vulkan implementation
	auto layerProperties = context.enumerateInstanceLayerProperties();
	for (auto const& requiredLayer : requiredLayers) {
		if (std::ranges::none_of(layerProperties,
								 [requiredLayer](auto const& layerProperty)
								 { return strcmp(layerProperty.layerName, requiredLayer) == 0; })) {
			throw std::runtime_error("Required layer not supported: " + std::string(requiredLayer));
		}
	}

	auto requiredExtensions = getRequiredExtensions();

	// Check if the required GLFW extensions are supported by the Vulkan implementation
	auto extensionProperties = context.enumerateInstanceExtensionProperties();
	for (const auto &requiredExtension : requiredExtensions) {
		if (std::ranges::none_of(extensionProperties,
			[requiredExtension](auto const& extensionProperty)
			{ return strcmp(extensionProperty.extensionName, requiredExtension) == 0; })) {
			throw std::runtime_error("Required GLFW extension not supported: " + std::string(requiredExtension));
		}
	}

	vk::InstanceCreateInfo createInfo{
		.flags                      = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
		.pApplicationInfo           = &appInfo,
		.enabledLayerCount          = static_cast<uint32_t>(requiredLayers.size()),
		.ppEnabledLayerNames        = requiredLayers.data(),
		.enabledExtensionCount      = static_cast<uint32_t>(requiredExtensions.size()),
		.ppEnabledExtensionNames    = requiredExtensions.data()
	};
	instance = vk::raii::Instance(context, createInfo);
}

void VulkanDevice::setupDebugMessenger() {
	if (!enableValidationLayers) return;

	vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
	);
	vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
		vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
		vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
		vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
	);
	vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
		.messageSeverity = severityFlags,
		.messageType = messageTypeFlags,
		.pfnUserCallback = &debugCallback
	};
	debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
}

void VulkanDevice::pickPhysicalDevice() {
	std::vector<vk::raii::PhysicalDevice> devices = instance.enumeratePhysicalDevices();
	const auto devIter = std::ranges::find_if(
		devices,
		[&](auto const & device) {
			// Check if the device supports the Vulkan 1.1 API version (relaxed requirement)
			bool supportsVulkan1_1 = device.getProperties().apiVersion >= VK_API_VERSION_1_1;

			// Check if any of the queue families support graphics operations
			auto queueFamilies = device.getQueueFamilyProperties();
			bool supportsGraphics = std::ranges::any_of(
				queueFamilies,
				[](auto const & qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); }
			);

			// Check if all required device extensions are available
			auto availableDeviceExtensions = device.enumerateDeviceExtensionProperties();
			bool supportsAllRequiredExtensions = std::ranges::all_of(
				requiredDeviceExtensions,
				[&availableDeviceExtensions](auto const & requiredDeviceExtension) {
					return std::ranges::any_of(
						availableDeviceExtensions,
						[requiredDeviceExtension](auto const & availableDeviceExtension) {
							return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
						}
					);
				}
			);

			// Check required features
			bool supportsRequiredFeatures = Platform::checkDeviceFeatureSupport(device);

			return supportsVulkan1_1 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
		}
	);

	if (devIter != devices.end()) {
		physicalDevice = *devIter;
	} else {
		throw std::runtime_error("failed to find a suitable GPU!");
	}
}

void VulkanDevice::createLogicalDevice() {
	std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

	// Get the first index into queueFamilyProperties which supports both graphics and present
	for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++) {
		if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
			physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface)) {
			// Found a queue family that supports both graphics and present
			graphicsQueueFamily = qfpIndex;
			break;
		}
	}
	if (graphicsQueueFamily == ~0) {
		throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
	}

	float queuePriority = 0.0f;
	vk::DeviceQueueCreateInfo deviceQueueCreateInfo = Platform::createDeviceQueueCreateInfo(graphicsQueueFamily, &queuePriority);

	// Build feature chain based on platform requirements
	if constexpr (!Platform::USE_VULKAN_1_3_FEATURES) {
		// Linux: Use minimal Vulkan 1.0 features for llvmpipe compatibility
		auto availableFeatures = physicalDevice.getFeatures();

		vk::PhysicalDeviceFeatures2 featureChain = {
			.features = availableFeatures  // Enable all available features
		};

		vk::DeviceCreateInfo deviceCreateInfo{
			.pNext = &featureChain,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &deviceQueueCreateInfo,
			.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
			.ppEnabledExtensionNames = requiredDeviceExtensions.data()
		};

		device = vk::raii::Device(physicalDevice, deviceCreateInfo);
	} else {
		// macOS/Windows: Enable full Vulkan 1.3 features
		vk::StructureChain<
			vk::PhysicalDeviceFeatures2,
			vk::PhysicalDeviceVulkan11Features,
			vk::PhysicalDeviceVulkan13Features,
			vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
		> featureChain = {
			{.features = {.samplerAnisotropy = true }},             // vk::PhysicalDeviceFeatures2
			{.shaderDrawParameters = true },                        // vk::PhysicalDeviceVulkan11Features
			{.synchronization2 = true, .dynamicRendering = true },  // vk::PhysicalDeviceVulkan13Features
			{.extendedDynamicState = true }                         // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
		};

		vk::DeviceCreateInfo deviceCreateInfo{
			.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &deviceQueueCreateInfo,
			.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
			.ppEnabledExtensionNames = requiredDeviceExtensions.data()
		};

		device = vk::raii::Device(physicalDevice, deviceCreateInfo);
	}
	graphicsQueue = vk::raii::Queue(device, graphicsQueueFamily, 0);
}

std::vector<const char*> VulkanDevice::getRequiredExtensions() const {
	uint32_t glfwExtensionCount = 0;
	auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
	if (enableValidationLayers) {
		extensions.push_back(vk::EXTDebugUtilsExtensionName);
	}
	extensions.emplace_back(vk::KHRPortabilityEnumerationExtensionName);
	return extensions;
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
	vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type!");
}

vk::Format VulkanDevice::findSupportedFormat(
	const std::vector<vk::Format>& candidates,
	vk::ImageTiling tiling,
	vk::FormatFeatureFlags features) const
{
	auto formatIt = std::ranges::find_if(candidates, [&](auto const format) {
		vk::FormatProperties props = physicalDevice.getFormatProperties(format);
		return (((tiling == vk::ImageTiling::eLinear) && ((props.linearTilingFeatures & features) == features)) ||
				((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features)));
	});
	if (formatIt == candidates.end()) {
		throw std::runtime_error("failed to find supported format!");
	}
	return *formatIt;
}

// Use C++ Vulkan-Hpp types for all platforms
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::debugCallback(
	vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
	vk::DebugUtilsMessageTypeFlagsEXT type,
	const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void*)
{
	if (static_cast<uint32_t>(severity) & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) {
		std::cerr << "validation layer: type 0x" << std::hex << static_cast<uint32_t>(type) << std::dec << " msg: " << pCallbackData->pMessage << std::endl;
	}
	return VK_FALSE;
}
