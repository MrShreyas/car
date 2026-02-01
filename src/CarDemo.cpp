#include <CarDemo.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>

#ifdef HAS_TINYEXR
#include "tinyexr.h"
#endif

#include <ResourceManager.h>

CarDemo::CarDemo(const std::string& title, int width, int height)
    : Application(title, width, height),
      m_Camera(nullptr),
      m_Shader(nullptr),
      m_RaptorModel(nullptr),
      m_CarModel(nullptr),
      m_CarModel2(nullptr),
      m_RoadModel(nullptr),
      m_FirstMouse(true),
      m_StartTime(0.0f),

      m_PlayerVehicle(nullptr),
      m_EnterPressed(false)
{
}

CarDemo::~CarDemo()
{
    delete m_Camera;
    
    // Clean up vehicles
    for (auto v : m_Vehicles) {
        delete v;
    }
    m_Vehicles.clear();
    
    // Resources are managed by ResourceManager
    // delete m_Shader;
    // delete m_RaptorModel;
    // delete m_CarModel;
    // delete m_CarModel2;
    // delete m_RoadModel;
}

void CarDemo::OnInit()
{
    // Configure global opengl state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    // Initialize Camera
    m_Camera = new Camera(glm::vec3(0.0f, 0.0f, 4.0f));

    // Initialize Shaders
    m_Shader = ResourceManager::LoadShader("shaders/model_loading.vs", "shaders/model_loading.fs", "modelShader");
    m_ParticleShader = ResourceManager::LoadShader("shaders/particles.vs", "shaders/particles.fs", "particleShader");
    
    m_Particles = new ParticleSystem(m_ParticleShader);
    
    // Enable Point Size control
    glEnable(GL_PROGRAM_POINT_SIZE);

    // Initialize IBL
    initIBLFromEXR("river_alcove_1k.exr");
    
    if (m_EnvCubemap == 0)
    {
        std::cout << "[WARN] IBL not initialized (envCubemap == 0). PBR will fall back to no env lighting.\n";
    }

    // Initialize Models
    // Use the new single-file GLBs if available, falling back to folders if not
    // The ResourceManager handles paths relative to execution or 'models/' prefix depending on impl.
    // Assuming user put raptor.glb in 'models/raptor.glb'
    // GLB usually needs flipUV=false to map textures correctly
    m_RaptorModel = ResourceManager::LoadModel("models/ford-raptor/source/FordRaptor.glb", "raptor", false);
    m_RaptorModel2 = ResourceManager::LoadModel("models/ford-raptor/source/FordRaptor.glb", "raptor", false);
    
    // Check if user has a shelby glb too? If not keep old one for now.
    // If the Shelby is GLTF, it might also need false, but if it was working before (with global true), keep it true?
    // User said "all the textures are not loading properly" for the CAR before. So maybe switch car to false too if it's GLTF.
    // But Road definitely needs TRUE (Default).
    // m_CarModel = ResourceManager::LoadModel("models/shelby/ford_shelby.glb", "car", false);
    // m_CarModel2 = ResourceManager::LoadModel("models/shelby/ford_shelby.glb", "car", false); 
    // 2024_ford_shelby_super_snake_s650/scene.gltf
    // Road needs FlipUV = true (Default)
    m_RoadModel = ResourceManager::LoadModel("models/city_base_road/scene.gltf", "road", true);

    // Place models
    // Place models (Static)
    // Road
    placeModel(*m_RoadModel, glm::vec3(0.0f, 0.0f, 0.0f), 0.0f, 5.0f, false);
    
    // Build the terrain grid for fast collision BEFORE spawning vehicles
    buildTerrainGrid();
    
    // Define Stats
    VehicleStats raptorStats;
    raptorStats.name = "Raptor Offroad";
    raptorStats.maxSpeed = 30.0f;       // Slower top speed
    raptorStats.acceleration = 5.0f;   // High torque
    raptorStats.turnSpeed = 100.0f;      // Heavy handling
    raptorStats.friction = 8.0f;        // Tires grip well offroad
    raptorStats.mass = 2500.0f;

    VehicleStats mustangStats;
    mustangStats.name = "Shelby GT500";
    mustangStats.maxSpeed = 100.0f;     // Fast 
    mustangStats.acceleration = 20.0f;  // Balanced
    mustangStats.turnSpeed = 90.0f;     // Sharp
    mustangStats.friction = 4.0f;       // Drifts easier
    mustangStats.mass = 1600.0f;

    // Spawn Vehicles (Dynamic)
    // Raptor 1 (at origin)
    // spawnVehicle(m_RaptorModel2, glm::vec3(-5.0f, 0.0f, 0.0f), raptorStats); // Raptor Left
    spawnVehicle(m_RaptorModel, glm::vec3(0.0f, 0.0f, 0.0f), raptorStats);   // Raptor Center
    // spawnVehicle(m_CarModel, glm::vec3(5.0f, 0.0f, 0.0f), mustangStats);      // Mustang Right
    
    // Original "Player" car
    // spawnVehicle(m_CarModel2, glm::vec3(10.0f, 0.0f, 0.0f), mustangStats);

    m_StartTime = static_cast<float>(glfwGetTime());
}

void CarDemo::buildTerrainGrid()
{
    // 1. Setup grid dimensions
    m_CellSizeX = m_WorldSizeX / GRID_RESOLUTION;
    m_CellSizeZ = m_WorldSizeZ / GRID_RESOLUTION;
    m_TerrainGrid.clear();
    m_TerrainGrid.resize(GRID_RESOLUTION * GRID_RESOLUTION);

    std::cout << "[Grid] Building Terrain Grid... Resolution: " << GRID_RESOLUTION << " Sizes: " << m_CellSizeX << ", " << m_CellSizeZ << "\n";

    // 2. Iterate all static objects (Roads) and add their triangles
    // 2. Iterate all static objects (Roads) and add their triangles
    for (const auto& placedModel : m_PlacedModels)
    {
        // Only include the static road/terrain for now
        if (placedModel.model == m_RoadModel) 
        {
            // Use Traverse to get world-space meshes
            // But we need to combine with placedModel.baseModelMatrix
            glm::mat4 base = placedModel.baseModelMatrix;
            
            placedModel.model->Traverse([&](const Mesh& mesh, const glm::mat4& nodeGlobalTransform) {
                // Final transform = base * nodeGlobal
                glm::mat4 finalTransform = base * nodeGlobalTransform;
                
                for (size_t i = 0; i < mesh.indices.size(); i += 3)
                {
                    glm::vec3 v0 = glm::vec3(finalTransform * glm::vec4(mesh.vertices[mesh.indices[i]].Position, 1.0f));
                    glm::vec3 v1 = glm::vec3(finalTransform * glm::vec4(mesh.vertices[mesh.indices[i+1]].Position, 1.0f));
                    glm::vec3 v2 = glm::vec3(finalTransform * glm::vec4(mesh.vertices[mesh.indices[i+2]].Position, 1.0f));

                    addTriangleToGrid(v0, v1, v2);
                }
            });
        }
    }
    
    // Debug stats
    size_t totalTris = 0;
    size_t maxTris = 0;
    for(const auto& cell : m_TerrainGrid) {
        totalTris += cell.size();
        if(cell.size() > maxTris) maxTris = cell.size();
    }
    std::cout << "[Grid] Built. Average Tris/Cell: " << (totalTris / (float)m_TerrainGrid.size()) 
              << " Max Tris/Cell: " << maxTris << "\n";
}

void CarDemo::addTriangleToGrid(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
{
    // Compute AABB of triangle
    float minX = std::min({v0.x, v1.x, v2.x});
    float maxX = std::max({v0.x, v1.x, v2.x});
    float minZ = std::min({v0.z, v1.z, v2.z});
    float maxZ = std::max({v0.z, v1.z, v2.z});

    // Convert to grid coordinates
    int minXIndex = static_cast<int>((minX - m_WorldMinX) / m_CellSizeX);
    int maxXIndex = static_cast<int>((maxX - m_WorldMinX) / m_CellSizeX);
    int minZIndex = static_cast<int>((minZ - m_WorldMinZ) / m_CellSizeZ);
    int maxZIndex = static_cast<int>((maxZ - m_WorldMinZ) / m_CellSizeZ);

    // Clamp to grid bounds
    minXIndex = std::max(0, std::min(GRID_RESOLUTION - 1, minXIndex));
    maxXIndex = std::max(0, std::min(GRID_RESOLUTION - 1, maxXIndex));
    minZIndex = std::max(0, std::min(GRID_RESOLUTION - 1, minZIndex));
    maxZIndex = std::max(0, std::min(GRID_RESOLUTION - 1, maxZIndex));

    // Add to all overlapping cells
    for (int z = minZIndex; z <= maxZIndex; ++z) {
        for (int x = minXIndex; x <= maxXIndex; ++x) {
            m_TerrainGrid[z * GRID_RESOLUTION + x].push_back({v0, v1, v2});
        }
    }
}

float CarDemo::getTerrainHeight(float x, float z)
{
    // 1. Calculate grid index
    int gridX = static_cast<int>((x - m_WorldMinX) / m_CellSizeX);
    int gridZ = static_cast<int>((z - m_WorldMinZ) / m_CellSizeZ);

    // 2. Check bounds
    if (gridX < 0 || gridX >= GRID_RESOLUTION || gridZ < 0 || gridZ >= GRID_RESOLUTION) {
        return 0.0f; // Off grid
    }

    // 3. Ray cast against triangles in this cell
    const auto& triangles = m_TerrainGrid[gridZ * GRID_RESOLUTION + gridX];
    
    glm::vec3 rayOrigin(x, 1000.0f, z);
    glm::vec3 rayDir(0.0f, -1.0f, 0.0f);
    float maxHeight = -1000.0f;
    bool found = false;

    // Optimization check: if no triangles, early out
    if (triangles.empty()) return 0.0f;

    for (const auto& tri : triangles)
    {
        float t = 0.0f;
        if (rayTriangleIntersect(rayOrigin, rayDir, tri.v0, tri.v1, tri.v2, t))
        {
            float intersectionHeight = rayOrigin.y - t;
            if (intersectionHeight > maxHeight)
            {
                maxHeight = intersectionHeight;
                found = true;
            }
        }
    }

    return found ? maxHeight : 0.0f;
}

bool CarDemo::rayTriangleIntersect(
    const glm::vec3& rayOrigin, const glm::vec3& rayDir,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
    float& t)
{
    glm::vec3 e1 = v1 - v0;
    glm::vec3 e2 = v2 - v0;
    glm::vec3 pvec = glm::cross(rayDir, e2);
    float det = glm::dot(e1, pvec);

    if (det > -1e-8 && det < 1e-8) {
        return false;
    }

    float invDet = 1.0f / det;
    glm::vec3 tvec = rayOrigin - v0;
    float u = glm::dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    glm::vec3 qvec = glm::cross(tvec, e1);
    float v = glm::dot(rayDir, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    t = glm::dot(e2, qvec) * invDet;
    return t > 1e-8;
}

void CarDemo::OnUpdate(float deltaTime)
{
    // Physics is currently in OnProcessInput.
    // Here we handle Camera updates (TPP vs Free).

    if (m_PlayerVehicle)
    {
        // TPP Camera Logic
        glm::vec3 carPos = m_PlayerVehicle->GetPosition();
        glm::vec3 forward = m_PlayerVehicle->GetForwardVector();
        
        // 1. Calculate Target Position (Behind and Above)
        // Ensure "Behind" means opposite to forward vector.
        float distance = 7.0f;
        float height = 3.5f;
        
        // We subtract forward to go behind (Standard OpenGL: Forward is -Z)
        // If car is moving Forward, camera should be at Pos - Forward*Dist
        // But let's check our vector math in Vehicle.cpp
        // "forward = (sin(yaw), 0, -cos(yaw))" -> This is standard -Z forward.
        // So `Pos - Forward` is Behind.
        
        glm::vec3 targetPos = carPos - (forward * distance) + glm::vec3(0.0f, height, 0.0f);

        // 2. Smooth Interpolation
        // Use a spring-like dampening or simple mix
        float lerpSpeed = 5.0f;
        m_Camera->Position = glm::mix(m_Camera->Position, targetPos, lerpSpeed * deltaTime);
        
        // 3. Look At
        // Camera should look at the car (plus offset)
        glm::vec3 lookTarget = m_PlayerVehicle->GetCameraTarget();
        m_Camera->Front = glm::normalize(lookTarget - m_Camera->Position);
        
        // 4. Update Vectors
        m_Camera->Right = glm::normalize(glm::cross(m_Camera->Front, m_Camera->WorldUp));
        m_Camera->Up    = glm::normalize(glm::cross(m_Camera->Right, m_Camera->Front));
    }
    else
    {
        // Free Cam (Walking)
        // Terrain following logic handled in OnProcessInput mostly, providing collisions.
        // Just ensure we don't clip through ground here too?
        // Input handles movement.
    }
}

void CarDemo::OnRender()
{
    float currentFrame = static_cast<float>(glfwGetTime());
    // deltaTime is calculated in Application::Run and passed to OnUpdate/OnProcessInput
    // We don't need to recalculate it here.

    // Clear to a distinct color (Blue-ish) to verify rendering context
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 projection = glm::perspective(glm::radians(m_Camera->Zoom), (float)m_Width / (float)m_Height, 0.1f, 100.0f);
    glm::mat4 view = m_Camera->GetViewMatrix();
    static bool debugAim = false;
    if (debugAim && !m_PlacedModels.empty())
    {
        const auto &pm0 = m_PlacedModels[0];
        glm::vec3 center = 0.5f * (pm0.bboxMin + pm0.bboxMax);
        view = glm::lookAt(m_Camera->Position, center, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    float startFadeTime = 2.0f;
    float endFadeTime = 4.0f;
    float progress = glm::clamp((currentFrame - m_StartTime - startFadeTime) / (endFadeTime - startFadeTime), 0.0f, 1.0f);
    // FORCE OPAQUE FOR DEBUGGING
    float transparency = 0.0f; // 1.0f - progress;

    // Ensure texture units are bound even if IBL failed (fallback to 0 or simple binding)
    if (m_EnvCubemap != 0)
    {
        glActiveTexture(GL_TEXTURE0 + 10);
        glBindTexture(GL_TEXTURE_CUBE_MAP, (m_IrradianceMap ? m_IrradianceMap : m_EnvCubemap));
        glActiveTexture(GL_TEXTURE0 + 11);
        glBindTexture(GL_TEXTURE_CUBE_MAP, (m_PrefilterMap ? m_PrefilterMap : m_EnvCubemap));
        glActiveTexture(GL_TEXTURE0 + 12);
        glBindTexture(GL_TEXTURE_2D, m_BrdfLUTTexture);
    }
    else
    {
        // Fallback: bind 0 to avoid sampling garbage? 
        // Or better, relying on direct light only. 
        // Ideally we should have a 1x1 white cubemap, but for now just ensure we don't crash.
        // The shader samples 10, 11, 12. If they are not bound, results are undefined.
        // Let's bind 0 (GL_TEXTURE0's content?) No, bind 0 means "no texture".
        glActiveTexture(GL_TEXTURE0 + 10); glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glActiveTexture(GL_TEXTURE0 + 11); glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glActiveTexture(GL_TEXTURE0 + 12); glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Shader Setup
    static bool printedShader = false;
    if (m_Shader && !printedShader) { std::cout << "DEBUG: Shader ID: " << m_Shader->ID << std::endl; printedShader = true; }
    m_Shader->use();

    static bool debugWire = false;
    if (debugWire)
    {
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
        glPointSize(4.0f);
    }
    else
    {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    m_Shader->setMat4("projection", projection);
    m_Shader->setMat4("view", view);
    m_Shader->setVec3("viewPos", m_Camera->Position);
    m_Shader->setInt("irradianceMap", 10);
    m_Shader->setInt("prefilterMap", 11);
    m_Shader->setInt("brdfLUT", 12);
    m_Shader->setFloat("prefilterMaxMip", m_PrefilterMaxMip);
    m_Shader->setFloat("materialTransparency", transparency);

    // Draw static models (Roads)
    for (const auto &pm : m_PlacedModels)
    {
        // Skip movable logic here, they are Vehicles now (mostly)
        // If we kept 'movable' flag usage in PlacedModel, check it
        if (pm.movable) continue; 
        
        m_Shader->setMat4("model", pm.baseModelMatrix);
        pm.model->Draw(*m_Shader, pm.baseModelMatrix, m_Camera->Position);
    }
    
    // Draw Vehicles
    for (auto v : m_Vehicles)
    {
        v->Draw(*m_Shader, view, projection, m_Camera->Position);
    }
    
    // Draw Particles
    if (m_Particles) {
        m_Particles->Draw(view, projection, m_Camera->Position);
    }
    
    // Draw UI (Crosshair)
    renderCrosshair();
}

void CarDemo::renderCrosshair()
{
    // Simple fixed pipeline-style render (or reuse a simple shader if needed)
    // For now, simpler to just clear depth and draw a tiny quad in center if we have a shader, 
    // but we need a simple 2D shader. 
    // Actually, let's use the quadVAO and an identity projection?
    // Not critical for logic, let's rely on print "Press Enter" for now if rendering is complex without a UI shader.
    // We'll skip complex UI rendering to avoid breaking the build with missing shaders.
}

bool CarDemo::checkVehicleEntry() 
{
    if (m_PlayerVehicle) return false; // Already in car

    // Find closest vehicle
    Vehicle* closest = nullptr;
    float minDst = 5.0f; // range

    for (auto v : m_Vehicles) {
        float dst = glm::distance(m_Camera->Position, v->GetPosition());
        if (dst < minDst) {
            minDst = dst;
            closest = v;
        }
    }

    if (closest) {
        m_PlayerVehicle = closest;
        std::cout << "Entered Vehicle: " << closest->GetStats().name << "\n";
        return true;
    }

    return false;
}

void CarDemo::spawnVehicle(Model* model, glm::vec3 pos, const VehicleStats& stats)
{
    // Auto-snap to terrain
    float h = getTerrainHeight(pos.x, pos.z);
    pos.y = h;
    
    Vehicle* v = new Vehicle(model, pos, stats);
    m_Vehicles.push_back(v);
}

bool isPointInAABB(const glm::vec3& point, const glm::vec3& min, const glm::vec3& max) {
    return (point.x >= min.x && point.x <= max.x) &&
           (point.y >= min.y && point.y <= max.y) &&
           (point.z >= min.z && point.z <= max.z);
}

void CarDemo::OnProcessInput(float deltaTime)
{
// --- REFACTORED INPUT ---

    // Toggle Enter Key
    if (glfwGetKey(m_Window, GLFW_KEY_ENTER) == GLFW_PRESS) {
        if (!m_EnterPressed) {
            m_EnterPressed = true;
            if (m_PlayerVehicle) {
                // Exit Vehicle
                glm::vec3 exitPos = m_PlayerVehicle->GetPosition() + glm::vec3(-3.0f, 0.0f, 0.0f);
                m_Camera->Position = glm::vec3(exitPos.x, getTerrainHeight(exitPos.x, exitPos.z) + 1.8f, exitPos.z);
                m_PlayerVehicle = nullptr;
                std::cout << "Exited Vehicle.\n";
            } else {
                // Try Enter Vehicle
                checkVehicleEntry();
            }
        }
    } else {
        m_EnterPressed = false;
    }

    if (m_PlayerVehicle)
    {
        // --- DRIVING MODE ---
        // Pass Input to Vehicle Class
        bool accel = (glfwGetKey(m_Window, GLFW_KEY_UP) == GLFW_PRESS) || (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS);
        bool brake = (glfwGetKey(m_Window, GLFW_KEY_DOWN) == GLFW_PRESS) || (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS);
        bool left  = (glfwGetKey(m_Window, GLFW_KEY_LEFT) == GLFW_PRESS) || (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS);
        bool right = (glfwGetKey(m_Window, GLFW_KEY_RIGHT) == GLFW_PRESS) || (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS);

        // Update Vehicle Physics
        m_PlayerVehicle->HandleInput(accel, brake, left, right, deltaTime);
        
    // Update Physics for ALL vehicles (Gravity/Collision)
    for (auto v : m_Vehicles) {
        glm::vec3 pos = v->GetPosition();
        float h = getTerrainHeight(pos.x, pos.z);
        // If it's the player, we already handled input.
        // UpdatePhysics integrates velocity and applies gravity.
        v->UpdatePhysics(deltaTime, h);
        v->UpdatePhysics(deltaTime, h);
    }
    
    // Emit Particles contextually
    if (m_PlayerVehicle) {
        // If speed is high or turning sharp, emit smoke
        // Simple heuristic: if |speed| > 10
        if (std::abs(m_PlayerVehicle->GetSpeed()) > 5.0f) {
            glm::vec3 pos = m_PlayerVehicle->GetPosition();
            glm::vec3 forward = m_PlayerVehicle->GetForwardVector();
            glm::vec3 offset = -forward * 2.0f; // Behind car
            // Emit 2 smoke puffs (wheels)
            m_Particles->Emit(pos + offset + glm::vec3(0.8f, 0.2f, 0.0f), glm::vec3(0.0f), glm::vec4(0.8f, 0.8f, 0.8f, 0.5f), 1.0f);
            m_Particles->Emit(pos + offset + glm::vec3(-0.8f, 0.2f, 0.0f), glm::vec3(0.0f), glm::vec4(0.8f, 0.8f, 0.8f, 0.5f), 1.0f);
        }
    }
    
    m_Particles->Update(deltaTime);
    }
    else
    {
        // --- WALKING MODE ---
        // Camera movement
        if (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS) m_Camera->ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS) m_Camera->ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS) m_Camera->ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS) m_Camera->ProcessKeyboard(RIGHT, deltaTime);
        // if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_PRESS) m_Camera->ProcessKeyboard(JUMP, deltaTime);

        // Keep camera above terrain
        float h = getTerrainHeight(m_Camera->Position.x, m_Camera->Position.z);
        if (m_Camera->Position.y < h + 1.8f) m_Camera->Position.y = h + 1.8f;
    }
}

void CarDemo::OnResize(int width, int height)
{
    m_Width = width;
    m_Height = height;
    // glViewport is handled in Application callback wrapper, but we can override if needed.
    // Application calls glViewport(0,0, width, height) before calling OnResize.
}

void CarDemo::OnMouseMove(double xposIn, double yposIn)
{
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (m_FirstMouse)
    {
        m_LastX = xpos;
        m_LastY = ypos;
        m_FirstMouse = false;
    }

    float xoffset = xpos - m_LastX;
    float yoffset = m_LastY - ypos;

    m_LastX = xpos;
    m_LastY = ypos;

    m_Camera->ProcessMouseMovement(xoffset, yoffset);
}

void CarDemo::OnMouseScroll(double xoffset, double yoffset)
{
    m_Camera->ProcessMouseScroll(static_cast<float>(yoffset));
}

void CarDemo::placeModel(Model& m, glm::vec3 position, float heightOffset, float desiredHeight, bool movable)
{
    glm::mat4 modelMat = glm::mat4(1.0f);

    if (!movable)
        modelMat = glm::translate(modelMat, position);

    modelMat = glm::translate(modelMat, glm::vec3(0.0f, heightOffset, 0.0f));

    float normScale = m.GetNormalizationScale();
    float finalScale = desiredHeight * normScale;
    modelMat = glm::scale(modelMat, glm::vec3(finalScale));

    glm::vec3 minW, maxW;
    m.CalculateAABB(modelMat, minW, maxW);

    PlacedModel pm;
    pm.model = &m;
    pm.baseModelMatrix = modelMat;
    pm.bboxMin = minW;
    pm.bboxMax = maxW;
    pm.movable = movable;


    m_PlacedModels.push_back(pm);

    std::cout << "DEBUG: Placed Model AABB: Min(" << minW.x << ", " << minW.y << ", " << minW.z 
              << ") Max(" << maxW.x << ", " << maxW.y << ", " << maxW.z << ")" << std::endl;
}

#ifdef HAS_TINYEXR
unsigned int CarDemo::loadHDR_EXR_2D(const char* filename)
{
    const char* err = nullptr;
    float* img = nullptr;
    int w = 0, h = 0;
    int ret = LoadEXR(&img, &w, &h, filename, &err);
    if (ret != TINYEXR_SUCCESS || !img)
    {
        if (err)
        {
            std::cerr << "[EXR] LoadEXR error: " << err << std::endl;
            FreeEXRErrorMessage(err);
        }
        else
        {
            std::cerr << "[EXR] Failed to load EXR: " << filename << std::endl;
        }
        return 0;
    }

    unsigned int hdrTex;
    glGenTextures(1, &hdrTex);
    glBindTexture(GL_TEXTURE_2D, hdrTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGBA, GL_FLOAT, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    free(img);
    return hdrTex;
}
#else
unsigned int CarDemo::loadHDR_EXR_2D(const char* filename)
{
    return 0;
}
#endif

void CarDemo::initIBLFromEXR(const std::string& exrPath)
{
#ifndef HAS_TINYEXR
    std::cerr << "HAS_TINYEXR not defined; cannot load EXR. Define HAS_TINYEXR and link tinyexr.\n";
    m_EnvCubemap = 0;
    m_IrradianceMap = 0;
    m_PrefilterMap = 0;
    m_BrdfLUTTexture = 0;
    m_PrefilterMaxMip = 0.0f;
    return;
#else
    unsigned int hdrTexture = loadHDR_EXR_2D(exrPath.c_str());
    if (hdrTexture == 0)
    {
        std::cerr << "Failed to load HDR EXR for IBL: " << exrPath << std::endl;
        m_EnvCubemap = 0;
        m_IrradianceMap = 0;
        m_PrefilterMap = 0;
        m_BrdfLUTTexture = 0;
        m_PrefilterMaxMip = 0.0f;
        return;
    }

    unsigned int captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    const unsigned int envSize = 512;

    glGenTextures(1, &m_EnvCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_EnvCubemap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, envSize, envSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    glm::mat4 captureViews[] =
    {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f, 0.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,-1.0f, 0.0f), glm::vec3(0.0f, 0.0f,-1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, 0.0f, 1.0f), glm::vec3(0.0f,-1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, 0.0f,-1.0f), glm::vec3(0.0f,-1.0f, 0.0f))
    };

    Shader equirectToCubemap("shaders/cubemap.vs", "shaders/equirectangular_to_cubemap.fs");
    Shader irradianceShader("shaders/cubemap.vs", "shaders/irradiance_convolution.fs");
    Shader prefilterShader("shaders/cubemap.vs", "shaders/prefilter.fs");
    Shader brdfShader("shaders/brdf.vs", "shaders/brdf.fs");

    equirectToCubemap.use();
    equirectToCubemap.setInt("equirectangularMap", 0);
    equirectToCubemap.setMat4("projection", captureProjection);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);

    glViewport(0, 0, envSize, envSize);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; ++i)
    {
        equirectToCubemap.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_EnvCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_EnvCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glGenTextures(1, &m_BrdfLUTTexture);
    glBindTexture(GL_TEXTURE_2D, m_BrdfLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_BrdfLUTTexture, 0);
    glViewport(0, 0, 512, 512);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    brdfShader.use();
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const unsigned int irradianceSize = 32;
    glGenTextures(1, &m_IrradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_IrradianceMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, irradianceSize, irradianceSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    irradianceShader.use();
    irradianceShader.setInt("environmentMap", 0);
    irradianceShader.setMat4("projection", captureProjection);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_EnvCubemap);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, irradianceSize, irradianceSize);
    glViewport(0, 0, irradianceSize, irradianceSize);
    for (unsigned int i = 0; i < 6; ++i)
    {
        irradianceShader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_IrradianceMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const unsigned int prefilterSize = 128;
    glGenTextures(1, &m_PrefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_PrefilterMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, prefilterSize, prefilterSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    prefilterShader.use();
    prefilterShader.setInt("environmentMap", 0);
    prefilterShader.setMat4("projection", captureProjection);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_EnvCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    unsigned int maxMipLevels = 5;
    for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
    {
        unsigned int mipWidth = static_cast<unsigned int>(prefilterSize * std::pow(0.5f, (float)mip));
        unsigned int mipHeight = mipWidth;

        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (float)mip / (float)(maxMipLevels - 1);
        prefilterShader.setFloat("roughness", roughness);
        for (unsigned int i = 0; i < 6; ++i)
        {
            prefilterShader.setMat4("view", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_PrefilterMap, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCube();
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_PrefilterMaxMip = (float)(maxMipLevels - 1);

    std::cout << "IBL from EXR initialized successfully.\n";
#endif
}

void CarDemo::renderCube()
{
    if (cubeVAO == 0)
    {
        float vertices[] = {
            // positions          
            -1.0f,-1.0f,-1.0f,  1.0f,-1.0f,-1.0f,  1.0f, 1.0f,-1.0f,
             1.0f, 1.0f,-1.0f, -1.0f, 1.0f,-1.0f, -1.0f,-1.0f,-1.0f,

            -1.0f,-1.0f, 1.0f,  1.0f,-1.0f, 1.0f,  1.0f, 1.0f, 1.0f,
             1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f,-1.0f, 1.0f,

            -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,-1.0f, -1.0f,-1.0f,-1.0f,
            -1.0f,-1.0f,-1.0f, -1.0f,-1.0f, 1.0f, -1.0f, 1.0f, 1.0f,

             1.0f, 1.0f, 1.0f,  1.0f, 1.0f,-1.0f,  1.0f,-1.0f,-1.0f,
             1.0f,-1.0f,-1.0f,  1.0f,-1.0f, 1.0f,  1.0f, 1.0f, 1.0f,

            -1.0f,-1.0f,-1.0f,  1.0f,-1.0f,-1.0f,  1.0f,-1.0f, 1.0f,
             1.0f,-1.0f, 1.0f, -1.0f,-1.0f, 1.0f, -1.0f,-1.0f,-1.0f,

            -1.0f, 1.0f,-1.0f,  1.0f, 1.0f,-1.0f,  1.0f, 1.0f, 1.0f,
             1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,-1.0f
        };
        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    }
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void CarDemo::renderQuad()
{
    if (quadVAO == 0)
    {
        float quadVertices[] = {
            // positions   // texcoords
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,

            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    }
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
