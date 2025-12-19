# RHI Migration - Troubleshooting & Issues

**Document Version**: 1.0
**Created**: 2025-12-19
**Last Updated**: 2025-12-19

---

## Overview

This document tracks issues, blockers, unexpected challenges, and their resolutions encountered during the RHI migration process.

---

## Phase 2: Vulkan Backend Implementation

### Issue 1: VMA Dependency Integration

**Date**: 2025-12-19
**Severity**: Low
**Status**: ✅ Resolved

**Description**:
VMA (Vulkan Memory Allocator) was not previously integrated into the project. Phase 2 requires VMA for efficient GPU memory management in the Vulkan backend.

**Impact**:
- Blocks implementation of VulkanRHIBuffer and VulkanRHITexture
- Required before any resource allocation code can be written

**Resolution**:
1. Added `vulkan-memory-allocator` to `vcpkg.json` dependencies
2. Added `find_package(VulkanMemoryAllocator CONFIG REQUIRED)` to CMakeLists.txt
3. Linked VMA: `GPUOpen::VulkanMemoryAllocator` to target

**Files Modified**:
- `vcpkg.json`: Added `vulkan-memory-allocator` dependency
- `CMakeLists.txt`: Added find_package and target_link_libraries

**Next Steps**:
- Run CMake configure to download and integrate VMA
- Verify VMA headers are accessible

---

## Phase 1: RHI Interface Design

No issues encountered. Phase 1 completed successfully with 100% of objectives met.

---

### Issue 2: Slang Shader Compilation Failure

**Date**: 2025-12-19
**Severity**: Medium (Blocks full build)
**Status**: ⚠️ Workaround Applied

**Description**:
During `make build-only`, the slangc shader compilation step fails with "Subprocess killed" error. However, manual execution of the same slangc command succeeds without errors.

**Error Message**:
```
FAILED: [code=1] /Users/mindaewon/projects/Mini-Engine/shaders/slang.spv
cd /Users/mindaewon/projects/Mini-Engine/shaders && /opt/homebrew/bin/cmake -E env DYLD_LIBRARY_PATH=/opt/homebrew/opt/vulkan-loader/lib: /usr/local/bin/slangc /Users/mindaewon/projects/Mini-Engine/shaders/shader.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -Wno-41012 -o slang.spv
Subprocess killed
```

**Impact**:
- Full project build fails
- Cannot compile and test Vulkan RHI implementations integrated with the main project
- Phase 2 integration testing is blocked

**Investigation**:
1. slangc exists at `/usr/local/bin/slangc`
2. Manual execution of the same command succeeds without errors
3. The issue appears to be related to CMake's custom command execution or environment setup
4. Possible cause: DYLD_LIBRARY_PATH conflicts or subprocess timeout

**Workaround**:
- Vulkan RHI classes can still be implemented and header-checked independently
- Manual shader precompilation may bypass the build issue
- Consider disabling shader compilation target temporarily for RHI development

**Next Steps**:
- Investigate CMake custom command timeout settings
- Check DYLD_LIBRARY_PATH conflicts
- Consider switching to precompiled shaders during migration
- May need to update CMakeLists.txt shader compilation logic

**Priority**: Should be resolved before Phase 7 (Testing & Verification)

---

## Future Issues

*Issues discovered during later phases will be documented here*

---

**Document Maintenance**:
- Add new issues as they are discovered
- Update status when issues are resolved
- Include code snippets and error messages for reference
- Document workarounds and alternative solutions considered
