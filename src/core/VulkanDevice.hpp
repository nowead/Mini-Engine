#pragma once

#include "../utils/VulkanCommon.hpp"
#include <vector>
#include <string>

// Forward declaration
struct GLFWwindow;

class VulkanDevice {
public:
	// Constructor: creates instance, picks physical device, creates logical device
	VulkanDevice(const std::vector<const char*>& validationLayers, bool enableValidation);
	~VulkanDevice() = default;

	// Delete copy constructor and assignment operator (RAII, non-copyable)
	VulkanDevice(const VulkanDevice&) = delete;
	VulkanDevice& operator=(const VulkanDevice&) = delete;

	// Move constructor and assignment operator (optional, but good practice)
	VulkanDevice(VulkanDevice&&) = default;
	VulkanDevice& operator=(VulkanDevice&&) = default;

	// Surface creation (requires GLFW window)
	void createSurface(GLFWwindow* window);

	// Device initialization (must be called after createSurface)
	void createLogicalDevice();

	// Accessors
	vk::raii::Context& getContext() { return context; }
	vk::raii::Instance& getInstance() { return instance; }
	vk::raii::PhysicalDevice& getPhysicalDevice() { return physicalDevice; }
	vk::raii::Device& getDevice() { return device; }
	vk::raii::Queue& getGraphicsQueue() { return graphicsQueue; }
	vk::raii::SurfaceKHR& getSurface() { return surface; }
	uint32_t getGraphicsQueueFamily() const { return graphicsQueueFamily; }

	// Utility functions
	uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
	vk::Format findSupportedFormat(
		const std::vector<vk::Format>& candidates,
		vk::ImageTiling tiling,
		vk::FormatFeatureFlags features) const;

private:
	// Vulkan objects (in order of creation)
	vk::raii::Context context;
	vk::raii::Instance instance = nullptr;
	vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
	vk::raii::SurfaceKHR surface = nullptr;
	vk::raii::PhysicalDevice physicalDevice = nullptr;
	vk::raii::Device device = nullptr;
	vk::raii::Queue graphicsQueue = nullptr;
	uint32_t graphicsQueueFamily = ~0;

	// Configuration
	bool enableValidationLayers;
	std::vector<const char*> validationLayers;
	std::vector<const char*> requiredDeviceExtensions;

	// Initialization functions
	void createInstance();
	void setupDebugMessenger();
	void pickPhysicalDevice();

	// Helper functions
	std::vector<const char*> getRequiredExtensions() const;
	// Use C++ Vulkan-Hpp types for all platforms
	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
		vk::DebugUtilsMessageTypeFlagsEXT type,
		const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData);
};
