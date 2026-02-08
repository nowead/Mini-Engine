#include "Application.hpp"
#ifndef __EMSCRIPTEN__
#include "src/ui/ImGuiManager.hpp"
#include "src/utils/GpuProfiler.hpp"
#endif
#include "src/rendering/InstancedRenderData.hpp"
#include "src/utils/Logger.hpp"

#include <iostream>
#include <stdexcept>
#include <functional>
#include <chrono>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

Application::Application() {
    initWindow();
    initRenderer();
    initGameLogic();
}

Application::~Application() {
    // RAII cleanup: Members destroyed in reverse declaration order
    // 1. ~Renderer() - cleans up ImGui (if initialized), calls waitIdle(), cleans up Vulkan resources
    // 2. ~Camera() - no special cleanup needed

    // Manual cleanup for raw pointers only
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

void Application::run() {
    // Initialize frame timing
    lastFrameTime = std::chrono::high_resolution_clock::now();

#ifdef __EMSCRIPTEN__
    // WebGPU: Use emscripten_set_main_loop for browser's requestAnimationFrame
    emscripten_set_main_loop_arg(
        [](void* arg) {
            auto* app = static_cast<Application*>(arg);
            app->mainLoopFrame();
        },
        this,
        0,  // Use browser's requestAnimationFrame (typically 60 FPS)
        1   // Simulate infinite loop
    );
#else
    // Native: Traditional game loop
    mainLoop();
#endif
}

void Application::initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE, nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);
}

void Application::initRenderer() {
    // Create camera
    float aspectRatio = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
    camera = std::make_unique<Camera>(aspectRatio);

    // Create renderer
    renderer = std::make_unique<Renderer>(window, validationLayers, enableValidationLayers);

    // Initialize ImGui
    renderer->initImGui(window);
}

void Application::initGameLogic() {
    // Get RHI device and queue from renderer
    auto* rhiDevice = renderer->getRHIDevice();
    auto* rhiQueue = renderer->getGraphicsQueue();

    // Create WorldManager
    worldManager = std::make_unique<WorldManager>(rhiDevice, rhiQueue);
    worldManager->initialize();

    // Initialize mock data generator
    mockDataGen = std::make_unique<MockDataGenerator>();

    // Initialize Particle System
    particleSystem = std::make_unique<effects::ParticleSystem>(rhiDevice, rhiQueue);

    // Create sample buildings in a grid pattern
    auto* buildingManager = worldManager->getBuildingManager();
    if (buildingManager) {
        int gridSize = 4;
        float spacing = 30.0f;
        float startX = -(gridSize - 1) * spacing / 2.0f;
        float startZ = -(gridSize - 1) * spacing / 2.0f;

        for (int x = 0; x < gridSize; x++) {
            for (int z = 0; z < gridSize; z++) {
                float posX = startX + x * spacing;
                float posZ = startZ + z * spacing;
                float height = 15.0f + (x + z) * 5.0f;

                std::string ticker = "BUILDING_" + std::to_string(x) + "_" + std::to_string(z);
                buildingManager->createBuilding(
                    ticker,
                    "NASDAQ",
                    glm::vec3(posX, 0.0f, posZ),
                    height
                );

                float initialPrice = 100.0f + (x * 10.0f + z * 5.0f);
                mockDataGen->registerTicker(ticker, initialPrice);
            }
        }
    }
}

void Application::mainLoop() {
    // Native: Traditional game loop
    while (!glfwWindowShouldClose(window)) {
        mainLoopFrame();
    }
    renderer->waitIdle();
}

void Application::mainLoopFrame() {
#ifdef __EMSCRIPTEN__
    // Check for window close (ESC key or close button)
    if (glfwWindowShouldClose(window)) {
        emscripten_cancel_main_loop();
        return;
    }
#endif

    // Calculate delta time
    auto currentFrameTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
    lastFrameTime = currentFrameTime;

    glfwPollEvents();
    processInput();
    renderer->updateCamera(camera->getViewMatrix(), camera->getProjectionMatrix(), camera->getPosition());

        // NEW: Update Game World
        if (worldManager) {
            // Update price data periodically
            priceUpdateTimer += deltaTime;
            if (priceUpdateTimer >= priceUpdateInterval) {
                priceUpdateTimer = 0.0f;

                // Generate mock price updates
                PriceUpdateBatch updates = mockDataGen->generateUpdates();
                worldManager->updateMarketData(updates);
            }

            // Update animations
            worldManager->update(deltaTime);
            
            // DEBUG: Force dramatic height change on CENTER building to test shadow updates
            static float debugTime = 0.0f;
            debugTime += deltaTime;
            auto* buildingManager = worldManager->getBuildingManager();
            if (buildingManager) {
                // Use center building (grid position 1,1 or 2,2)
                auto* centerBuilding = buildingManager->getBuildingByTicker("BUILDING_1_1");
                if (!centerBuilding) {
                    centerBuilding = buildingManager->getBuildingByTicker("BUILDING_2_2");
                }
                if (centerBuilding) {
                    // Oscillate between 20 and 150 height
                    float newHeight = 85.0f + 65.0f * std::sin(debugTime * 1.5f);
                    centerBuilding->currentHeight = newHeight;
                    centerBuilding->targetHeight = newHeight;
                    buildingManager->markObjectBufferDirty();
                }
            }
        }

        // Extract rendering data from game logic (clean layer separation)
        if (worldManager) {
            auto* buildingManager = worldManager->getBuildingManager();
            if (buildingManager) {
                // Always update instance buffer if dirty (even with 0 buildings, we have ground)
                if (buildingManager->isObjectBufferDirty()) {
                    buildingManager->updateObjectBuffer();
                }

                // Always submit render data (ground plane + buildings)
                rendering::InstancedRenderData renderData;
                renderData.mesh = buildingManager->getBuildingMesh();
                renderData.objectBuffer = buildingManager->getObjectBuffer();
                // Instance count = buildings + ground plane (1)
                renderData.instanceCount = static_cast<uint32_t>(buildingManager->getBuildingCount() + 1);

                // Submit to renderer (clean interface)
                renderer->submitInstancedRenderData(renderData);
            }
        }

        // Update Particle System
        if (particleSystem) {
            particleSystem->update(deltaTime);
            // Submit particle system to renderer
            renderer->submitParticleSystem(particleSystem.get());
        }

        // Render ImGui UI
#ifndef __EMSCRIPTEN__
        if (auto* imgui = renderer->getImGuiManager()) {
            imgui->newFrame();

            uint32_t buildingCount = 0;
            if (worldManager && worldManager->getBuildingManager()) {
                buildingCount = static_cast<uint32_t>(worldManager->getBuildingManager()->getBuildingCount());
            }
            imgui->renderUI(*camera, buildingCount, particleSystem.get());

            // Handle particle effect requests from UI
            auto particleRequest = imgui->getAndClearParticleRequest();
            if (particleRequest.requested && particleSystem) {
                particleSystem->spawnEffect(
                    particleRequest.type,
                    particleRequest.position,
                    particleRequest.duration
                );
            }

            // Phase 3.3: Apply lighting settings from UI
            auto& lighting = imgui->getLightingSettings();
            renderer->setSunDirection(lighting.sunDirection);
            renderer->setSunIntensity(lighting.sunIntensity);
            renderer->setSunColor(lighting.sunColor);
            renderer->setAmbientIntensity(lighting.ambientIntensity);
            renderer->setShadowBias(lighting.shadowBias);
            renderer->setShadowStrength(lighting.shadowStrength);
            renderer->setExposure(lighting.exposure);

            // Phase 4.1: Pass GPU timing data to ImGui
            if (auto* profiler = renderer->getGpuProfiler()) {
                ImGuiManager::GpuTimingData gpuTiming;
                gpuTiming.cullingMs = profiler->getElapsedMs(GpuProfiler::TimerId::FrustumCulling);
                gpuTiming.shadowMs = profiler->getElapsedMs(GpuProfiler::TimerId::ShadowPass);
                gpuTiming.mainPassMs = profiler->getElapsedMs(GpuProfiler::TimerId::MainRenderPass);
                imgui->setGpuTimingData(gpuTiming);
            }

            // Phase 4.1: Handle stress test building count change
            auto scaleReq = imgui->getAndClearScaleRequest();
            if (scaleReq.requested) {
                regenerateBuildings(scaleReq.targetCount);
            }
        }
#endif

    // Renderer handles both scene and ImGui rendering
    renderer->drawFrame();
}

void Application::processInput() {
    // ESC to close
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // WASD for camera translation
    float moveSpeed = 2.0f;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        camera->translate(0.0f, moveSpeed);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        camera->translate(0.0f, -moveSpeed);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        camera->translate(-moveSpeed, 0.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        camera->translate(moveSpeed, 0.0f);
    }

}

void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    app->renderer->handleFramebufferResize();

    // Update camera aspect ratio
    if (height > 0) {
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        app->camera->setAspectRatio(aspectRatio);
    }
}

void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            app->mousePressed = true;
            glfwGetCursorPos(window, &app->lastMouseX, &app->lastMouseY);
        } else if (action == GLFW_RELEASE) {
            app->mousePressed = false;
            app->firstMouse = true;
        }
    }
}

void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (!app->mousePressed) {
        return;
    }

    if (app->firstMouse) {
        app->lastMouseX = xpos;
        app->lastMouseY = ypos;
        app->firstMouse = false;
        return;
    }

    float deltaX = static_cast<float>(xpos - app->lastMouseX);
    float deltaY = static_cast<float>(ypos - app->lastMouseY);

    app->camera->rotate(deltaX, deltaY);

    app->lastMouseX = xpos;
    app->lastMouseY = ypos;
}

void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    app->camera->zoom(static_cast<float>(yoffset));
}

void Application::regenerateBuildings(int targetCount) {
    if (!worldManager) return;
    auto* buildingManager = worldManager->getBuildingManager();
    if (!buildingManager) return;

    // Wait for GPU to finish using current buffers
    renderer->waitIdle();

    // Destroy existing buildings
    buildingManager->destroyAllBuildings();
    mockDataGen = std::make_unique<MockDataGenerator>();

    // Calculate grid dimensions
    int gridSize = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(targetCount))));
    float spacing = std::max(15.0f, 30.0f * (16.0f / static_cast<float>(std::max(targetCount, 16))));
    spacing = std::clamp(spacing, 8.0f, 30.0f);
    float startX = -(gridSize - 1) * spacing / 2.0f;
    float startZ = -(gridSize - 1) * spacing / 2.0f;

    int created = 0;
    for (int x = 0; x < gridSize && created < targetCount; x++) {
        for (int z = 0; z < gridSize && created < targetCount; z++) {
            float posX = startX + x * spacing;
            float posZ = startZ + z * spacing;
            float height = 10.0f + static_cast<float>(rand() % 50);

            std::string ticker = "B_" + std::to_string(created);
            buildingManager->createBuilding(ticker, "STRESS", glm::vec3(posX, 0.0f, posZ), height);
            mockDataGen->registerTicker(ticker, 100.0f + static_cast<float>(rand() % 200));
            created++;
        }
    }

    buildingManager->markObjectBufferDirty();

    // Auto-adjust camera to fit the new grid
    float gridExtent = gridSize * spacing;
    float cameraDistance = std::max(150.0f, gridExtent * 0.8f);
    camera->setDistance(cameraDistance);

    // Also adjust shadow scene radius for large scenes
    float sceneRadius = std::max(200.0f, gridExtent * 0.6f);
    renderer->setShadowSceneRadius(sceneRadius);

    LOG_INFO("StressTest") << "Regenerated " << created << " buildings (grid " << gridSize << "x" << gridSize
                           << ", spacing " << spacing << "m, camera dist " << cameraDistance << "m)";
}

void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_R:
                app->camera->reset();
                break;
        }
    }
}
