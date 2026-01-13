#include "Application.hpp"
#include "src/ui/ImGuiManager.hpp"
#include "src/rendering/InstancedRenderData.hpp"

#include <iostream>
#include <stdexcept>
#include <functional>
#include <chrono>

Application::Application() {
    initWindow();
    initVulkan();
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
    mainLoop();
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

void Application::initVulkan() {
    // Create camera (use Perspective for easier debugging)
    float aspectRatio = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
    camera = std::make_unique<Camera>(aspectRatio, ProjectionMode::Perspective);

    // Create renderer (FDF mode removed - only building rendering now)
    renderer = std::make_unique<Renderer>(window, validationLayers, enableValidationLayers, false);

    // Phase 6: Initialize ImGui if enabled (now managed by Renderer)
    if (ENABLE_IMGUI) {
        renderer->initImGui(window);
    }

    // Initialize Game Logic Layer
    std::cout << "\n=== Initializing Game Logic Layer ===\n" << std::endl;

    // Get RHI device and queue from renderer
    auto* rhiDevice = renderer->getRHIDevice();
    auto* rhiQueue = renderer->getGraphicsQueue();

    // Create WorldManager
    worldManager = std::make_unique<WorldManager>(rhiDevice, rhiQueue);
    worldManager->initialize();

    // Initialize mock data generator
    mockDataGen = std::make_unique<MockDataGenerator>();

    // Create sample buildings in a grid pattern
    auto* buildingManager = worldManager->getBuildingManager();
    if (buildingManager) {
        std::cout << "Creating sample buildings...\n";

        // Create a 4x4 grid of buildings with better spacing
        int gridSize = 4;
        float spacing = 30.0f;  // 30 meters between buildings for better visibility
        float startX = -(gridSize - 1) * spacing / 2.0f;
        float startZ = -(gridSize - 1) * spacing / 2.0f;

        for (int x = 0; x < gridSize; x++) {
            for (int z = 0; z < gridSize; z++) {
                float posX = startX + x * spacing;
                float posZ = startZ + z * spacing;

                // Create building with varied heights (15m to 50m)
                float height = 15.0f + (x + z) * 5.0f;

                std::string ticker = "BUILDING_" + std::to_string(x) + "_" + std::to_string(z);
                uint64_t entityId = buildingManager->createBuilding(
                    ticker,
                    "NASDAQ",
                    glm::vec3(posX, 0.0f, posZ),
                    height
                );

                // Register with mock data generator
                float initialPrice = 100.0f + (x * 10.0f + z * 5.0f);
                mockDataGen->registerTicker(ticker, initialPrice);
            }
        }

        std::cout << "Created " << (gridSize * gridSize) << " buildings in a grid pattern\n";
    }

    std::cout << "Game Logic Layer initialized successfully!" << std::endl;
    std::cout << "Total buildings: " << worldManager->getTotalBuildingCount() << std::endl;
    std::cout << "Total sectors: " << worldManager->getSectorCount() << "\n" << std::endl;
}

void Application::mainLoop() {
    auto lastFrameTime = std::chrono::high_resolution_clock::now();

    while (!glfwWindowShouldClose(window)) {
        // Calculate delta time
        auto currentFrameTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
        lastFrameTime = currentFrameTime;

        glfwPollEvents();
        processInput();
        renderer->updateCamera(camera->getViewMatrix(), camera->getProjectionMatrix());

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
        }

        // Extract rendering data from game logic (clean layer separation)
        if (worldManager) {
            auto* buildingManager = worldManager->getBuildingManager();
            if (buildingManager && buildingManager->getBuildingCount() > 0) {
                // Update instance buffer if dirty
                if (buildingManager->isInstanceBufferDirty()) {
                    buildingManager->updateInstanceBuffer();
                }

                // Prepare rendering data (no game logic in Renderer)
                rendering::InstancedRenderData renderData;
                renderData.mesh = buildingManager->getBuildingMesh();
                renderData.instanceBuffer = buildingManager->getInstanceBuffer();
                renderData.instanceCount = static_cast<uint32_t>(buildingManager->getBuildingCount());
                renderData.needsUpdate = false;

                // Submit to renderer (clean interface)
                renderer->submitInstancedRenderData(renderData);
            }
        }

        // Phase 6: Render ImGui UI (now handled internally by Renderer)
        if (ENABLE_IMGUI && renderer->getImGuiManager()) {
            auto* imgui = renderer->getImGuiManager();
            imgui->newFrame();
            imgui->renderUI(
                *camera,
                renderer->isFdfMode(),
                renderer->getZScale(),
                [this]() { /* Mode toggle - would require renderer recreation */ },
                [this](const std::string& path) {
                    renderer->loadModel(path);
                }
            );
        }

        // Renderer handles both scene and ImGui rendering
        renderer->drawFrame();
    }
    renderer->waitIdle();
}

void Application::processInput() {
    // ESC to close
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // WASD for camera translation
    float moveSpeed = 1.0f;
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

    // Q/E for Z-scale adjustment (FDF mode only)
    static auto lastZScaleAdjust = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastZScaleAdjust).count();

    // Throttle Z-scale adjustments to avoid too frequent reloads (250ms cooldown)
    if (elapsed > 250) {
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            renderer->adjustZScale(-0.1f);
            lastZScaleAdjust = now;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            renderer->adjustZScale(0.1f);
            lastZScaleAdjust = now;
        }
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

void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_P:
            case GLFW_KEY_I:
                app->camera->toggleProjectionMode();
                break;
            case GLFW_KEY_R:
                app->camera->reset();
                break;
        }
    }
}
