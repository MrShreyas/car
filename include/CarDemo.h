#ifndef CARDEMO_H
#define CARDEMO_H

#include <Application.h>
#include <shader.h>
#include <camera.h>
#include <model.h>

#include <vector>
#include <string>

// Struct for placed models
struct PlacedModel {
    Model* model;
    glm::mat4 baseModelMatrix;
    glm::vec3 bboxMin;
    glm::vec3 bboxMax;
    bool movable;
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
    Model* m_CarModel;
    Model* m_CarModel2;
    
    std::vector<PlacedModel> m_PlacedModels;

    // IBL / Environment
    unsigned int m_EnvCubemap = 0;
    unsigned int m_IrradianceMap = 0;
    unsigned int m_PrefilterMap = 0;
    unsigned int m_BrdfLUTTexture = 0;
    float m_PrefilterMaxMip = 0.0f;

    // Car physics/state
    glm::vec3 m_CarPos;
    float m_CarYaw;
    float m_CarSpeed;
    
    // Constants
    const float CAR_ACCEL = 5.0f;
    const float CAR_FRICTION = 2.0f;
    const float CAR_TURN_SPEED = 60.0f;

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
};

#endif
