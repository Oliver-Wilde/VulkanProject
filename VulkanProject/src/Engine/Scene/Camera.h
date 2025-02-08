#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
public:
    // You can store:
    glm::vec3 position;
    float yaw = 0.0f; // rotation around Y axis
    float pitch = 0.0f; // rotation around X axis

    // Movement speeds
    float moveSpeed = 2.0f;   // units per second
    float turnSpeed = 0.1f;   // mouse sensitivity

    Camera(const glm::vec3& startPos = glm::vec3(5.0f, 5.0f, 5.0f))
        : position(startPos)
    {}

    // Generate the view matrix from position/yaw/pitch
    glm::mat4 getViewMatrix() const
    {
        // 1) compute "forward" from yaw/pitch
        glm::vec3 direction;
        direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        direction.y = sin(glm::radians(pitch));
        direction.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        glm::vec3 forward = glm::normalize(direction);

        // 2) cross to get a real up vector
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        // 3) lookAt from position
        return glm::lookAt(position, position + forward, up);
    }
};
