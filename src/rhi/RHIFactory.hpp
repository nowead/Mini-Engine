#pragma once

#include "RHIDevice.hpp"
#include "RHITypes.hpp"
#include <memory>
#include <vector>
#include <string>

// Forward declarations for platform types
struct GLFWwindow;

namespace rhi {

/**
 * @brief Device creation information
 *
 * Contains all parameters needed to create an RHI device.
 */
struct DeviceCreateInfo {
    /// Backend type to create (Vulkan, WebGPU, etc.)
    RHIBackendType backend = RHIBackendType::Vulkan;

    /// Enable validation/debug layers
    bool enableValidation = true;

    /// Prefer discrete GPU over integrated
    bool preferDiscreteGPU = true;

    /// Platform window handle for surface creation
    /// For GLFW: pass GLFWwindow*
    void* windowHandle = nullptr;

    /// Application name (used by some backends)
    std::string applicationName = "Mini-Engine";

    /// Application version
    uint32_t applicationVersion = 1;

    DeviceCreateInfo() = default;

    /// Builder pattern for fluent API
    DeviceCreateInfo& setBackend(RHIBackendType type) {
        backend = type;
        return *this;
    }

    DeviceCreateInfo& setValidation(bool enable) {
        enableValidation = enable;
        return *this;
    }

    DeviceCreateInfo& setWindow(void* window) {
        windowHandle = window;
        return *this;
    }

    DeviceCreateInfo& setAppName(const std::string& name) {
        applicationName = name;
        return *this;
    }
};

/**
 * @brief Backend information
 */
struct BackendInfo {
    RHIBackendType type;
    std::string name;
    bool available;
    std::string unavailableReason;  // Empty if available
};

/**
 * @brief RHI Factory for creating devices
 *
 * Static factory class that creates RHI devices for different backends.
 * Use this as the entry point to the RHI system.
 *
 * @code
 * // Example usage:
 * auto info = rhi::DeviceCreateInfo{}
 *     .setBackend(rhi::RHIBackendType::Vulkan)
 *     .setValidation(true)
 *     .setWindow(glfwWindow);
 *
 * auto device = rhi::RHIFactory::createDevice(info);
 * @endcode
 */
class RHIFactory {
public:
    RHIFactory() = delete;  // Static class, no instantiation

    /**
     * @brief Create an RHI device
     * @param info Device creation parameters
     * @return Unique pointer to the created device, or nullptr on failure
     * @throws std::runtime_error if device creation fails
     */
    static std::unique_ptr<RHIDevice> createDevice(const DeviceCreateInfo& info);

    /**
     * @brief Get list of available backends
     * @return Vector of available backend information
     */
    static std::vector<BackendInfo> getAvailableBackends();

    /**
     * @brief Get the default/recommended backend for the current platform
     * @return Default backend type
     */
    static RHIBackendType getDefaultBackend();

    /**
     * @brief Check if a specific backend is available
     * @param backend Backend type to check
     * @return true if the backend is available
     */
    static bool isBackendAvailable(RHIBackendType backend);

    /**
     * @brief Get the name string for a backend type
     * @param backend Backend type
     * @return Human-readable backend name
     */
    static const char* getBackendName(RHIBackendType backend);
};

} // namespace rhi
