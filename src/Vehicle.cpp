#include "Vehicle.h"
#include <algorithm>
#include <cmath>
#include <iostream>

Vehicle::Vehicle(Model* model, const glm::vec3& startPos, const VehicleStats& stats, float initialYaw)
    : m_Model(model), m_Position(startPos), m_Stats(stats), m_Yaw(initialYaw), m_CurrentSpeed(0.0f), m_Velocity(0.0f)
{
    // Initialize defaults
    std::cout << "Vehicle created " << stats.name << std::endl;
}

void Vehicle::SetProperties(float maxSpeed, float accel, float turnSpeed)
{
    m_Stats.maxSpeed = maxSpeed;
    m_Stats.acceleration = accel;
    m_Stats.turnSpeed = turnSpeed;
}

void Vehicle::HandleInput(bool accel, bool brake, bool left, bool right, float deltaTime)
{
    // Acceleration / Braking
    if (accel) {
        m_CurrentSpeed += m_Stats.acceleration * deltaTime;
    } else if (brake) {
        m_CurrentSpeed -= m_Stats.brakeForce * deltaTime; // Reverse or Brake
    } else {
        // Friction / Coasting
        if (m_CurrentSpeed > 0.0f) {
            m_CurrentSpeed -= m_Stats.friction * deltaTime;
            if (m_CurrentSpeed < 0.0f) m_CurrentSpeed = 0.0f;
        } else if (m_CurrentSpeed < 0.0f) {
            m_CurrentSpeed += m_Stats.friction * deltaTime;
            if (m_CurrentSpeed > 0.0f) m_CurrentSpeed = 0.0f;
        }
    }

    // Clamp speed
    // Use simple clamping for now, can add drag later
    if (m_CurrentSpeed > m_Stats.maxSpeed) m_CurrentSpeed = m_Stats.maxSpeed;
    if (m_CurrentSpeed < -m_Stats.maxSpeed / 2.0f) m_CurrentSpeed = -m_Stats.maxSpeed / 2.0f;

    // Steering
    m_TurnFactor = 0.0f;
    if (std::abs(m_CurrentSpeed) > 0.1f) {
        float dir = (m_CurrentSpeed >= 0.0f) ? 1.0f : -1.0f;
        
        // Speed-sensitive steering (turn less at high speeds for stability)
        float speedFactor = 1.0f - (std::abs(m_CurrentSpeed) / m_Stats.maxSpeed) * 0.5f;
        
        if (left) m_TurnFactor = 1.0f;
        if (right) m_TurnFactor = -1.0f;

        m_Yaw += m_TurnFactor * m_Stats.turnSpeed * speedFactor * dir * deltaTime;
        
        // Accumulate wheel spin
        // Dist = speed * time. Angle = Dist / Radius.
        float dist = m_CurrentSpeed * deltaTime;
        m_WheelRotation += dist / 0.35f; 
    } else {
        // Still allow steering while stopped?
        if (left) m_TurnFactor = 1.0f;
        if (right) m_TurnFactor = -1.0f;
    }
}


void Vehicle::UpdatePhysics(float deltaTime, float terrainHeight, float gravity)
{
    // 1. Move based on Yaw + Speed
    glm::vec3 forward = GetForwardVector();
    
    // Update velocity (Kinematic mainly for now, can be dynamic later)
    m_Velocity = forward * m_CurrentSpeed;

    // Integrate Position
    m_Position += m_Velocity * deltaTime;

    // 2. Terrain Snap / Gravity
    // Simple logic: If above ground, fall. If below, snap up.
    if (m_Position.y > terrainHeight) {
       // Gravity
       m_Position.y = terrainHeight; 
    }
    
    // Hard snap to terrain
    m_Position.y = terrainHeight; 

    // 3. World Bounds Collision (Simple)
    // Prevent driving off grid (assuming +/- 200 world)
    // If we hit edge, bounce or stop
    if (std::abs(m_Position.x) > 180.0f) {
        m_Position.x = glm::sign(m_Position.x) * 180.0f;
        m_CurrentSpeed = -m_CurrentSpeed * 0.5f; // Bounce
    }
    if (std::abs(m_Position.z) > 180.0f) {
        m_Position.z = glm::sign(m_Position.z) * 180.0f;
        m_CurrentSpeed = -m_CurrentSpeed * 0.5f; // Bounce
    } 
}

glm::vec3 Vehicle::GetForwardVector() const
{
    float yawRad = glm::radians(m_Yaw);
    // Standard OpenGL forward is -Z. 
    // Rotation +90 (Left around Y) moves -Z to -X.
    // sin(90) = 1 (+X). So we need -sin(yaw).
    return glm::normalize(glm::vec3(-std::sin(yawRad), 0.0f, -std::cos(yawRad)));
}

glm::mat4 Vehicle::GetModelMatrix() const
{
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, m_Position);
    model = glm::rotate(model, glm::radians(m_Yaw), glm::vec3(0.0f, 1.0f, 0.0f));
    
    // Scale? We need to know the model's desired scale. 
    // This is currently dynamic in CarDemo. 
    // We should probably normalize models or store scale in Vehicle.
    // For now, let's assume the Model class handles local transforms or we add a Scale property.
    // Adding a fixed scale for now based on standard car size assumption (handled in CarDemo placeModel previously)
    // We can iterate this later.
    return model;
}

glm::vec3 Vehicle::GetCameraTarget() const
{
    // Look slightly above the car center
    return m_Position + glm::vec3(0.0f, 1.5f, 0.0f);
}

void Vehicle::Draw(Shader& shader, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos)
{
    glm::mat4 model = GetModelMatrix();
    shader.setMat4("model", model);

    // Calculate wheel rotation based on speed
    // Circumference C = 2*pi*r. Distance = speed * dt. Angle += Dist / r.
    // Assume radius approx 0.35m
    float r = 0.35f;
    // We update rotation state here (or in UpdatePhysics)
    // Since we don't have dt here, we rely on accumulated state updated in UpdatePhysics?
    // Let's just update it in UpdatePhysics then.
    // But for now, we use m_WheelRotation.

    std::map<std::string, glm::mat4> overrides;

    // Rotate wheels
    // Front Left/Right (Steer + Spin)
    // Rear Left/Right (Spin only)
    
    // NOTE: Names must match Blender export!
    // Standard names: "wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"
    // Also user might have named them differently.
    
    glm::mat4 spin = glm::rotate(glm::mat4(1.0f), m_WheelRotation, glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate X (Forward)
    
    // Steering
    // Map TurnFactor (-1..1) to angle (e.g. -30..30 deg)
    // User reported inverted steering. Negating here.
    float steerAngle = -m_TurnFactor * glm::radians(30.0f);
    glm::mat4 steer = glm::rotate(glm::mat4(1.0f), steerAngle, glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate Y (Up)

    // Combined Front
    // Order: Rotate Spin (Local X) -> Then Steer (Parent Y) or Local Y?
    // Wheels usually spin around axle (X), steer around pivot (Y).
    
    glm::mat4 frontL = steer * spin;
    glm::mat4 frontR = steer * spin;
    glm::mat4 rear = spin;

    overrides["wheel_fl"] = frontL;
    overrides["wheel_fr"] = frontR;
    overrides["wheel_rl"] = rear;
    overrides["wheel_rr"] = rear;
    
    // Raptor names might be different? "Wheel_FL", etc. Case sensitive.
    overrides["Wheel_FL"] = frontL;
    overrides["Wheel_FR"] = frontR;
    overrides["Wheel_RL"] = rear;
    overrides["Wheel_RR"] = rear;

    m_Model->Draw(shader, model, camPos, &overrides);
}
