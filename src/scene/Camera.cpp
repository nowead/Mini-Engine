#include "Camera.hpp"
#include "src/utils/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>

Camera::Camera(float aspectRatio)
    : position(0.0f, 200.0f, 280.0f),  // Aerial city overview
      target(0.0f, 0.0f, 0.0f),        // Look at building grid center
      up(0.0f, 1.0f, 0.0f),
      yaw(glm::radians(0.0f)),
      pitch(glm::radians(35.0f)),
      distance(250.0f),
      aspectRatio(aspectRatio),
      fov(glm::radians(55.0f)),        // Narrower FOV for city overview
      nearPlane(0.5f),
      farPlane(50000.0f) {
    // Don't call updateCameraVectors() - use fixed position/target
    LOG_DEBUG("Camera") << "Initialized at pos=(0, 200, 280), target=(0, 0, 0), FOV=55deg";
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, target, up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    glm::mat4 proj = glm::perspective(fov, aspectRatio, nearPlane, farPlane);
#ifndef __EMSCRIPTEN__
    // Vulkan NDC has Y pointing down, flip it
    // WebGPU uses OpenGL-style coordinates, so no flip needed
    proj[1][1] *= -1;
#endif
    return proj;
}

void Camera::rotate(float deltaX, float deltaY) {
    yaw -= deltaX * 0.005f;  // Rotation sensitivity (inverted)
    pitch -= deltaY * 0.005f;  // Rotation sensitivity (inverted)

    // Clamp pitch to avoid gimbal lock
    pitch = std::clamp(pitch, glm::radians(-89.0f), glm::radians(89.0f));

    updateCameraVectors();
}

void Camera::translate(float deltaRight, float deltaForward, float deltaUp) {
    // Pan position+target together. Forward uses the horizontal projection
    // of the view direction so W/S don't tilt the camera when the user is
    // looking down at the scene. Up is world Y so Q/E is a clean elevator.
    glm::vec3 viewDir = glm::normalize(target - position);
    glm::vec3 right   = glm::normalize(glm::cross(viewDir, up));

    glm::vec3 forwardHoriz = glm::vec3(viewDir.x, 0.0f, viewDir.z);
    float horizLen = glm::length(forwardHoriz);
    if (horizLen > 1e-4f) {
        forwardHoriz /= horizLen;
    } else {
        // Camera is looking nearly straight up/down — fall back to the
        // strafe-orthogonal direction so forward still does something sane.
        forwardHoriz = glm::normalize(glm::cross(up, right));
    }

    constexpr float kPanSpeed = 0.05f;
    glm::vec3 translation = right       * deltaRight   * kPanSpeed
                          + forwardHoriz * deltaForward * kPanSpeed
                          + up           * deltaUp      * kPanSpeed;

    position += translation;
    target   += translation;
}

void Camera::zoom(float delta) {
    // Adaptive zoom speed: faster when further away
    float zoomSpeed = std::max(1.5f, distance * 0.08f);
    distance -= delta * zoomSpeed;
    distance = std::clamp(distance, 1.0f, 10000.0f);  // Allow zooming out for 100K stress test
    updateCameraVectors();
}

void Camera::setAspectRatio(float newAspectRatio) {
    aspectRatio = newAspectRatio;
}

void Camera::reset() {
    target = glm::vec3(0.0f, 0.0f, 0.0f);
    up = glm::vec3(0.0f, 1.0f, 0.0f);
    yaw = glm::radians(0.0f);
    pitch = glm::radians(35.0f);
    distance = 250.0f;
    updateCameraVectors();
}

void Camera::updateCameraVectors() {
    // Calculate new position based on spherical coordinates
    // Using yaw and pitch to orbit around the target
    float x = distance * cos(pitch) * sin(yaw);
    float y = distance * sin(pitch);
    float z = distance * cos(pitch) * cos(yaw);

    position = target + glm::vec3(x, y, z);

    // Up vector remains constant for now
    up = glm::vec3(0.0f, 1.0f, 0.0f);
}
