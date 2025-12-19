#pragma once

/**
 * @file RHI.hpp
 * @brief Convenience header for including all RHI interfaces
 *
 * This header provides a single include point for the entire RHI abstraction layer.
 * Include this file to access all RHI types, interfaces, and functionality.
 *
 * The RHI (Render Hardware Interface) provides a platform-independent abstraction
 * over graphics APIs such as Vulkan, WebGPU, Direct3D 12, and Metal.
 */

// Core types and enumerations
#include "RHITypes.hpp"

// Resource interfaces
#include "RHIBuffer.hpp"
#include "RHITexture.hpp"
#include "RHISampler.hpp"
#include "RHIShader.hpp"

// Pipeline interfaces
#include "RHIBindGroup.hpp"
#include "RHIPipeline.hpp"
#include "RHIRenderPass.hpp"

// Command recording
#include "RHICommandBuffer.hpp"

// Presentation
#include "RHISwapchain.hpp"

// Queue and synchronization
#include "RHIQueue.hpp"
#include "RHISync.hpp"

// Device capabilities
#include "RHICapabilities.hpp"

// Device interface
#include "RHIDevice.hpp"

/**
 * @namespace rhi
 * @brief RHI (Render Hardware Interface) namespace
 *
 * All RHI types and interfaces are defined within this namespace.
 */
