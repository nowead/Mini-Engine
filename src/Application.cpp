#include "Application.hpp"
#include "src/ui/ImGuiManager.hpp"

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
    // Create camera
    float aspectRatio = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
    camera = std::make_unique<Camera>(aspectRatio, ProjectionMode::Isometric);

    // Create renderer
    renderer = std::make_unique<Renderer>(window, validationLayers, enableValidationLayers, USE_FDF_MODE);
    renderer->loadModel(MODEL_PATH);

    // Only load texture for OBJ models
    if (!USE_FDF_MODE) {
        renderer->loadTexture(TEXTURE_PATH);
    }

    // Phase 6: Initialize ImGui if enabled (now managed by Renderer)
    if (ENABLE_IMGUI) {
        renderer->initImGui(window);
    }
}

void Application::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        processInput();
        renderer->updateCamera(camera->getViewMatrix(), camera->getProjectionMatrix());

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
