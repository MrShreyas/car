#ifndef CARDEMO_H
#define CARDEMO_H

#include <Application.h>
#include <shader.h>
#include <camera.h>
#include <model.h>
#include <vehicle.h>
#include <ParticleSystem.h>

#include <vector>
#include <string>

// Struct for placed models
struct PlacedModel {
    Model* model;
    glm::mat4 baseModelMatrix; // Still useful for static objects like Roads
    glm::vec3 bboxMin;
    glm::vec3 bboxMax;
    bool movable; // kept for legacy or simple objects, but Vehicles use Vehicle class
    // Legacy fields removed
};

class CarDemo : public Application
{
public:
    CarDemo(const std::string& title, int width, int height);
    virtual ~CarDemo();

    virtual void OnInit() override;
    virtual void OnUpdate(float deltaTime) override;
    virtual void OnRender() override;
    virtual void OnProcessInput(float deltaTime) override;
    virtual void OnResize(int width, int height) override;
    virtual void OnMouseMove(double xpos, double ypos) override;
    virtual void OnMouseScroll(double xoffset, double yoffset) override;

private:
    // Scene objects
    Camera* m_Camera;
    Shader* m_Shader;
    
    // Models
    Model* m_RaptorModel;
    Model* m_RaptorModel2;
    Model* m_CarModel;
    Model* m_CarModel2;
    Model* m_RoadModel;
    
    std::vector<PlacedModel> m_PlacedModels;

    // IBL / Environment
    unsigned int m_EnvCubemap = 0;
    unsigned int m_IrradianceMap = 0;
    unsigned int m_PrefilterMap = 0;
    unsigned int m_BrdfLUTTexture = 0;
    float m_PrefilterMaxMip = 0.0f;

    // Vehicle System
    std::vector<Vehicle*> m_Vehicles;        // All active vehicles
    Vehicle* m_PlayerVehicle = nullptr;      // The one we are driving (if any)
    bool m_EnterPressed = false; 
    
    // Particles
    ParticleSystem* m_Particles = nullptr;
    Shader* m_ParticleShader = nullptr; 

    // Constants
    // Helper functions
    void renderCrosshair();
    bool checkVehicleEntry();
    void spawnVehicle(Model* model, glm::vec3 pos, const VehicleStats& stats);

    // Input state
    float m_LastX;
    float m_LastY;
    bool m_FirstMouse;
    float m_StartTime;

    // Helpers
    void placeModel(Model& m, glm::vec3 position, float heightOffset, float desiredHeight, bool movable);
    void initIBLFromEXR(const std::string& exrPath);
    unsigned int loadHDR_EXR_2D(const char* filename);
    
    // Geometry helpers
    void renderCube();
    void renderQuad();
    
    // Internal geometry state
    unsigned int cubeVAO = 0;
    unsigned int cubeVBO = 0;
    unsigned int quadVAO = 0;
    unsigned int quadVBO = 0;

    bool rayTriangleIntersect(
        const glm::vec3& rayOrigin, const glm::vec3& rayDir,
        const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
        float& t);
        
    float getTerrainHeight(float x, float z);

    // --- Spatial Grid Optimization ---
    struct WorldTriangle {
        glm::vec3 v0, v1, v2;
    };
    
    // Grid parameters
    static const int GRID_RESOLUTION = 100; // 100x100 grid
    float m_WorldMinX = -200.0f;
    float m_WorldMinZ = -200.0f;
    float m_WorldSizeX = 400.0f;
    float m_WorldSizeZ = 400.0f;
    float m_CellSizeX = 0.0f;
    float m_CellSizeZ = 0.0f;

    // The grid: flat vector of vectors (or map). 
    // We'll use a 1D vector of vectors for cache coherence.
    // Index = z_index * GRID_RESOLUTION + x_index
    std::vector<std::vector<WorldTriangle>> m_TerrainGrid;

    void buildTerrainGrid();
    void addTriangleToGrid(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2);
};

#endif
