#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <ResourceManager.h>
#include <Model.h>

// Physics/Handling stats
struct VehicleStats {
    float maxSpeed = 50.0f;
    float acceleration = 15.0f;
    float brakeForce = 30.0f;
    float turnSpeed = 80.0f;
    float friction = 5.0f;
    float mass = 1500.0f; // Could be used for collision logic later
    std::string name = "Vehicle"; 
};

class Vehicle {
public:
    Vehicle(Model* model, const glm::vec3& startPos, const VehicleStats& stats, float initialYaw = 0.0f);
    ~Vehicle() = default;

    // Core Loop
    void HandleInput(bool accel, bool brake, bool left, bool right, float deltaTime);
    void UpdatePhysics(float deltaTime, float terrainHeight, float gravity = 9.81f);
    
    // Pass time or use internal state for animation
    void Draw(Shader& shader, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos);
    
    // Getters
    const VehicleStats& GetStats() const { return m_Stats; }

    // Getters / Setters
    glm::vec3 GetPosition() const { return m_Position; }
    float GetSpeed() const { return m_CurrentSpeed; }
    glm::vec3 GetForwardVector() const;
    glm::mat4 GetModelMatrix() const;
    
    // Camera helper
    glm::vec3 GetCameraTarget() const; 

    // Properties
    void SetProperties(float maxSpeed, float accel, float turnSpeed);

private:
    Model* m_Model;
    
    // Physics State
    glm::vec3 m_Position;
    glm::vec3 m_Velocity;
    float m_Yaw; // Degrees
    float m_CurrentSpeed; // Forward speed (scalar)
    
    // Properties
    VehicleStats m_Stats;
    float m_TurnFactor = 0.0f; // Current turning state (-1 to 1)

    // Visuals
    float m_Scale = 1.0f;
    float m_WheelRotation = 0.0f; // Radians
};
