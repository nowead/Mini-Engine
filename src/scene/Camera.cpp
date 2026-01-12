#include "Camera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <iostream>

Camera::Camera(float aspectRatio, ProjectionMode mode)
    : position(0.0f, 0.0f, 5.0f),
      target(0.0f, 25.0f, 0.0f),  // Look at middle of buildings (height ~25m)
      up(0.0f, 1.0f, 0.0f),
      yaw(glm::radians(45.0f)),
      pitch(glm::radians(30.0f)),  // Positive pitch to look DOWN at buildings from above
      distance(80.0f),  // Farther distance to see all buildings in grid
      projectionMode(mode),
      aspectRatio(aspectRatio),
      fov(glm::radians(45.0f)),
      nearPlane(0.1f),
      farPlane(1000.0f),  // Larger far plane to render distant objects
      orthoSize(40.0f) {  // Larger ortho size to see entire building grid
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
    yaw -= deltaX * 0.005f;  // Rotation sensitivity (inverted)
    pitch -= deltaY * 0.005f;  // Rotation sensitivity (inverted)

    // Clamp pitch to avoid gimbal lock
    pitch = std::clamp(pitch, glm::radians(-89.0f), glm::radians(89.0f));

    updateCameraVectors();
}

void Camera::translate(float deltaX, float deltaY) {
    // Calculate right and up vectors
    glm::vec3 forward = glm::normalize(target - position);
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 upVector = glm::normalize(glm::cross(right, forward));

    // Translate both position and target to maintain view direction (inverted)
    glm::vec3 translation = right * -deltaX * 0.01f + upVector * -deltaY * 0.01f;
    position += translation;
    target += translation;
}

void Camera::zoom(float delta) {
    if (projectionMode == ProjectionMode::Perspective) {
        // Move camera closer/farther from target
        distance -= delta * 0.5f;
        distance = std::clamp(distance, 1.0f, 200.0f);  // Allow zooming out much further
        updateCameraVectors();
    } else {
        // Change orthographic size
        orthoSize -= delta * 0.1f;
        orthoSize = std::clamp(orthoSize, 1.0f, 100.0f);  // Allow much wider isometric view
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
    target = glm::vec3(0.0f, 25.0f, 0.0f);  // Match constructor
    up = glm::vec3(0.0f, 1.0f, 0.0f);
    yaw = glm::radians(45.0f);
    pitch = glm::radians(30.0f);  // Match constructor
    distance = 80.0f;  // Match initial distance
    orthoSize = 40.0f;  // Match initial ortho size
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
