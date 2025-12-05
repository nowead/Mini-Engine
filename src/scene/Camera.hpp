#pragma once

#include "src/utils/VulkanCommon.hpp"
#include <glm/glm.hpp>

/**
 * @brief Projection mode for camera
 */
enum class ProjectionMode {
    Perspective,
    Isometric
};

/**
 * @brief Camera class for view and projection transformations
 *
 * Responsibilities:
 * - Manage camera position and orientation
 * - Provide view and projection matrices
 * - Support both perspective and isometric projections
 * - Handle user input for camera controls
 */
class Camera {
public:
    /**
     * @brief Construct camera with initial parameters
     * @param aspectRatio Viewport aspect ratio (width/height)
     * @param mode Initial projection mode
     */
    Camera(float aspectRatio, ProjectionMode mode = ProjectionMode::Isometric);

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
     * @brief Toggle between perspective and isometric projection
     */
    void toggleProjectionMode();

    /**
     * @brief Set projection mode
     * @param mode New projection mode
     */
    void setProjectionMode(ProjectionMode mode);

    /**
     * @brief Get current projection mode
     */
    ProjectionMode getProjectionMode() const { return projectionMode; }

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
    ProjectionMode projectionMode;
    float aspectRatio;
    float fov;        // Field of view for perspective projection
    float nearPlane;
    float farPlane;

    // Isometric projection parameters
    float orthoSize;  // Half-size of orthographic view

    // Update camera vectors based on rotation angles
    void updateCameraVectors();
};
