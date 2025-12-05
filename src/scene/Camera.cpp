#include "Camera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>

Camera::Camera(float aspectRatio, ProjectionMode mode)
    : position(0.0f, 0.0f, 5.0f),
      target(0.0f, 0.0f, 0.0f),
      up(0.0f, 1.0f, 0.0f),
      yaw(glm::radians(45.0f)),
      pitch(glm::radians(-30.0f)),
      distance(10.0f),
      projectionMode(mode),
      aspectRatio(aspectRatio),
      fov(glm::radians(45.0f)),
      nearPlane(0.1f),
      farPlane(100.0f),
      orthoSize(5.0f) {
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, target, up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    if (projectionMode == ProjectionMode::Perspective) {
        glm::mat4 proj = glm::perspective(fov, aspectRatio, nearPlane, farPlane);
        // Vulkan NDC has Y pointing down, flip it
        proj[1][1] *= -1;
        return proj;
    } else {
        // Isometric projection
        float left = -orthoSize * aspectRatio;
        float right = orthoSize * aspectRatio;
        float bottom = -orthoSize;
        float top = orthoSize;

        glm::mat4 proj = glm::ortho(left, right, bottom, top, nearPlane, farPlane);
        // Vulkan NDC has Y pointing down, flip it
        proj[1][1] *= -1;
        return proj;
    }
}

void Camera::rotate(float deltaX, float deltaY) {
    yaw += deltaX * 0.005f;  // Rotation sensitivity
    pitch += deltaY * 0.005f;

    // Clamp pitch to avoid gimbal lock
    pitch = std::clamp(pitch, glm::radians(-89.0f), glm::radians(89.0f));

    updateCameraVectors();
}

void Camera::translate(float deltaX, float deltaY) {
    // Calculate right and up vectors
    glm::vec3 forward = glm::normalize(target - position);
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 upVector = glm::normalize(glm::cross(right, forward));

    // Translate both position and target to maintain view direction
    glm::vec3 translation = right * deltaX * 0.01f + upVector * deltaY * 0.01f;
    position += translation;
    target += translation;
}

void Camera::zoom(float delta) {
    if (projectionMode == ProjectionMode::Perspective) {
        // Move camera closer/farther from target
        distance -= delta * 0.5f;
        distance = std::clamp(distance, 1.0f, 50.0f);
        updateCameraVectors();
    } else {
        // Change orthographic size
        orthoSize -= delta * 0.1f;
        orthoSize = std::clamp(orthoSize, 0.5f, 20.0f);
    }
}

void Camera::toggleProjectionMode() {
    if (projectionMode == ProjectionMode::Perspective) {
        projectionMode = ProjectionMode::Isometric;
    } else {
        projectionMode = ProjectionMode::Perspective;
    }
}

void Camera::setProjectionMode(ProjectionMode mode) {
    projectionMode = mode;
}

void Camera::setAspectRatio(float newAspectRatio) {
    aspectRatio = newAspectRatio;
}

void Camera::reset() {
    position = glm::vec3(0.0f, 0.0f, 5.0f);
    target = glm::vec3(0.0f, 0.0f, 0.0f);
    up = glm::vec3(0.0f, 1.0f, 0.0f);
    yaw = glm::radians(45.0f);
    pitch = glm::radians(-30.0f);
    distance = 10.0f;
    orthoSize = 5.0f;
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
