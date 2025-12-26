#include "RHIFactory.hpp"

// Backend implementations
#ifdef RHI_BACKEND_VULKAN
#include <rhi-vulkan/VulkanRHIDevice.hpp>
#endif

#ifdef RHI_BACKEND_WEBGPU
#include <rhi-webgpu/WebGPURHIDevice.hpp>
#endif

#include <stdexcept>

// Forward declaration for GLFW
struct GLFWwindow;

namespace rhi {

// ============================================================================
// Device Creation
// ============================================================================

std::unique_ptr<RHIDevice> RHIFactory::createDevice(const DeviceCreateInfo& info) {
    if (!info.windowHandle) {
        throw std::runtime_error("RHIFactory::createDevice: windowHandle is required");
    }

    switch (info.backend) {
#ifdef RHI_BACKEND_VULKAN
        case RHIBackendType::Vulkan: {
            auto* window = static_cast<GLFWwindow*>(info.windowHandle);
            return std::make_unique<RHI::Vulkan::VulkanRHIDevice>(
                window,
                info.enableValidation
            );
        }
#endif

#ifdef RHI_BACKEND_WEBGPU
        case RHIBackendType::WebGPU: {
            auto* window = static_cast<GLFWwindow*>(info.windowHandle);
            return std::make_unique<RHI::WebGPU::WebGPURHIDevice>(
                window,
                info.enableValidation
            );
        }
#endif

        case RHIBackendType::D3D12:
            throw std::runtime_error("D3D12 backend not yet implemented");

        case RHIBackendType::Metal:
            throw std::runtime_error("Metal backend not yet implemented");

        default:
            throw std::runtime_error("Unknown backend type");
    }

    // Fallback (should not reach here)
    return nullptr;
}

// ============================================================================
// Backend Enumeration
// ============================================================================

std::vector<BackendInfo> RHIFactory::getAvailableBackends() {
    std::vector<BackendInfo> backends;

#ifdef RHI_BACKEND_VULKAN
    backends.push_back({
        .type = RHIBackendType::Vulkan,
        .name = "Vulkan",
        .available = true,
        .unavailableReason = ""
    });
#else
    backends.push_back({
        .type = RHIBackendType::Vulkan,
        .name = "Vulkan",
        .available = false,
        .unavailableReason = "Not compiled with RHI_BACKEND_VULKAN"
    });
#endif

#ifdef RHI_BACKEND_WEBGPU
    backends.push_back({
        .type = RHIBackendType::WebGPU,
        .name = "WebGPU",
        .available = true,
        .unavailableReason = ""
    });
#else
    backends.push_back({
        .type = RHIBackendType::WebGPU,
        .name = "WebGPU",
        .available = false,
        .unavailableReason = "Not compiled with RHI_BACKEND_WEBGPU"
    });
#endif

    // D3D12 - Windows only
    backends.push_back({
        .type = RHIBackendType::D3D12,
        .name = "Direct3D 12",
        .available = false,
#ifdef _WIN32
        .unavailableReason = "Not yet implemented"
#else
        .unavailableReason = "Windows only"
#endif
    });

    // Metal - Apple only
    backends.push_back({
        .type = RHIBackendType::Metal,
        .name = "Metal",
        .available = false,
#ifdef __APPLE__
        .unavailableReason = "Not yet implemented"
#else
        .unavailableReason = "macOS/iOS only"
#endif
    });

    return backends;
}

RHIBackendType RHIFactory::getDefaultBackend() {
    // Priority: Vulkan > WebGPU > Metal > D3D12
#ifdef RHI_BACKEND_VULKAN
    return RHIBackendType::Vulkan;
#elif defined(RHI_BACKEND_WEBGPU)
    return RHIBackendType::WebGPU;
#elif defined(__APPLE__)
    return RHIBackendType::Metal;
#elif defined(_WIN32)
    return RHIBackendType::D3D12;
#else
    return RHIBackendType::Vulkan;  // Fallback
#endif
}

bool RHIFactory::isBackendAvailable(RHIBackendType backend) {
    switch (backend) {
        case RHIBackendType::Vulkan:
#ifdef RHI_BACKEND_VULKAN
            return true;
#else
            return false;
#endif

        case RHIBackendType::WebGPU:
#ifdef RHI_BACKEND_WEBGPU
            return true;
#else
            return false;
#endif

        case RHIBackendType::D3D12:
        case RHIBackendType::Metal:
            return false;  // Not yet implemented

        default:
            return false;
    }
}

const char* RHIFactory::getBackendName(RHIBackendType backend) {
    switch (backend) {
        case RHIBackendType::Vulkan:  return "Vulkan";
        case RHIBackendType::WebGPU:  return "WebGPU";
        case RHIBackendType::D3D12:   return "Direct3D 12";
        case RHIBackendType::Metal:   return "Metal";
        default:                      return "Unknown";
    }
}

} // namespace rhi
