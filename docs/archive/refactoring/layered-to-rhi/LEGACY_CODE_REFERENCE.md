# Legacy Code Reference Guide

**Version**: 2.0
**Date**: 2025-12-19
**Status**: ✅ Migration Strategy Decided - Clean Re-implementation

---

## Strategy Decision

After analysis, we chose **Clean Re-implementation** over **Wrapper Pattern**.

### Why Clean Re-implementation?

| Aspect | Wrapper | Re-implementation | Winner |
|--------|---------|-------------------|--------|
| Abstraction Leakage | High | Low | Re-impl |
| Long-term Flexibility | Low | High | Re-impl |
| VMA Integration | Difficult | Natural | Re-impl |
| Backend Consistency | Inconsistent | Consistent | Re-impl |
| Maintenance | Two codebases | One codebase | Re-impl |

### Benefits Achieved

1. **Clean RHI Interface** - No legacy type exposure
2. **VMA Integration** - Modern memory management
3. **Future Backend Ready** - WebGPU, Metal can follow same pattern
4. **Single Source of Truth** - RHI is the only Vulkan abstraction

---

## Legacy Code Status

### Files to be Removed (Phase 7)

| File | Lines | Status | Notes |
|------|-------|--------|-------|
| `src/core/VulkanDevice.cpp/hpp` | ~350 | ⏳ Pending removal | Replaced by VulkanRHIDevice |
| `src/resources/VulkanBuffer.cpp/hpp` | ~60 | ⏳ Pending removal | Replaced by VulkanRHIBuffer + VMA |
| `src/resources/VulkanImage.cpp/hpp` | ~90 | ⏳ Pending removal | Replaced by VulkanRHITexture + VMA |
| `src/rendering/VulkanSwapchain.cpp/hpp` | ~100 | ⏳ Pending removal | Replaced by VulkanRHISwapchain |
| `src/rendering/VulkanPipeline.cpp/hpp` | ~150 | ⏳ Pending removal | Replaced by VulkanRHIPipeline |
| `src/core/CommandManager.cpp/hpp` | ~70 | ⏳ Pending removal | Replaced by VulkanRHICommandEncoder |
| `src/rendering/SyncManager.cpp/hpp` | ~60 | ⏳ Pending removal | Replaced by VulkanRHISync |

**Total Legacy**: ~880 lines (to be removed after migration complete)

### RHI Replacement Code

| File | Lines | Status |
|------|-------|--------|
| `src/rhi/vulkan/VulkanRHIDevice.cpp/hpp` | ~600 | ✅ Complete |
| `src/rhi/vulkan/VulkanRHIBuffer.cpp/hpp` | ~200 | ✅ Complete |
| `src/rhi/vulkan/VulkanRHITexture.cpp/hpp` | ~250 | ✅ Complete |
| `src/rhi/vulkan/VulkanRHISwapchain.cpp/hpp` | ~350 | ✅ Complete |
| `src/rhi/vulkan/VulkanRHIPipeline.cpp/hpp` | ~400 | ✅ Complete |
| `src/rhi/vulkan/VulkanRHICommandEncoder.cpp/hpp` | ~350 | ✅ Complete |
| `src/rhi/vulkan/VulkanRHISync.cpp/hpp` | ~100 | ✅ Complete |
| Other RHI files | ~1,700 | ✅ Complete |

**Total RHI Vulkan Backend**: ~3,950 lines

---

## Legacy Code as Reference

Legacy code serves as **reference documentation** for:
- Vulkan initialization patterns
- Platform-specific handling (macOS MoltenVK)
- Debug callback setup
- Queue family selection

---

## Migration Timeline

1. **Phase 4** (Current): Renderer uses both legacy and RHI
2. **Phase 5-6**: ResourceManager, SceneManager, ImGui migrate to RHI
3. **Phase 7**: Remove legacy Vulkan code completely

---

*Last Updated: 2025-12-19*
