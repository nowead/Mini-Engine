#pragma once

#include <glm/glm.hpp>

/**
 * @brief Camera class for view and projection transformations
 *
 * Responsibilities:
 * - Manage camera position and orientation
 * - Provide view and projection matrices (perspective only)
 * - Handle user input for camera controls
 */
class Camera {
public:
    /**
     * @brief Construct camera with initial parameters
     * @param aspectRatio Viewport aspect ratio (width/height)
     */
    Camera(float aspectRatio);

    /**
     * @brief Get view matrix
     * @return View transformation matrix
     */
    glm::mat4 getViewMatrix() const;

    /**
     * @brief Get projection matrix
     * @return Projection transformation matrix
     */
    glm::mat4 getProjectionMatrix() const;

    /**
     * @brief Rotate camera
     * @param deltaX Rotation around Y axis (yaw)
     * @param deltaY Rotation around X axis (pitch)
     */
    void rotate(float deltaX, float deltaY);

    /**
     * @brief Translate camera
     * @param deltaX Translation along X axis
     * @param deltaY Translation along Y axis
     */
    void translate(float deltaX, float deltaY);

    /**
     * @brief Zoom camera (move along view direction)
     * @param delta Zoom amount (positive = zoom in, negative = zoom out)
     */
    void zoom(float delta);

    /**
     * @brief Update aspect ratio (call when window resizes)
     * @param aspectRatio New aspect ratio
     */
    void setAspectRatio(float aspectRatio);

    /**
     * @brief Reset camera to default position and orientation
     */
    void reset();

private:
    // Camera parameters
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 up;

    // Rotation angles
    float yaw;    // Rotation around Y axis
    float pitch;  // Rotation around X axis

    // Zoom distance
    float distance;

    // Projection parameters
    float aspectRatio;
    float fov;        // Field of view for perspective projection
    float nearPlane;
    float farPlane;

    // Update camera vectors based on rotation angles
    void updateCameraVectors();
};
