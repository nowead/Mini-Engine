#pragma once
#ifndef __EMSCRIPTEN__

#include "RenderGraphResource.hpp"
#include <functional>
#include <vector>
#include <rhi/RHICommandBuffer.hpp>

namespace rendergraph {

enum class RGPassType { Render, Compute };

/// A single read or write dependency declared for a pass.
struct RGDep {
    RGResourceHandle resource;
    RGAccess         access;
};

/// Full pass node stored inside the RenderGraph.
struct RGPassNode {
    std::string              name;
    RGPassType               type;
    std::vector<RGDep>       reads;
    std::vector<RGDep>       writes;
    std::function<void(rhi::RHICommandEncoder*)> execute;
};

} // namespace rendergraph

#endif // !__EMSCRIPTEN__
