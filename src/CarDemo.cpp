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

CarDemo::CarDemo(const std::string& title, int width, int height)
    : Application(title, width, height),
      m_Camera(nullptr),
      m_Shader(nullptr),
      m_RaptorModel(nullptr),
      m_CarModel(nullptr),
      m_CarModel2(nullptr),
      m_RoadModel(nullptr),
      m_CarPos(10.0f, 0.0f, 0.0f),
      m_CarYaw(0.0f),
      m_CarSpeed(0.0f),
      m_LastX(width / 2.0f),
      m_LastY(height / 2.0f),
      m_FirstMouse(true),
      m_StartTime(0.0f)
{
}

CarDemo::~CarDemo()
{
    delete m_Camera;
    delete m_Shader;
    delete m_RaptorModel;
    delete m_CarModel;
    delete m_CarModel2;
    delete m_RoadModel;
}

void CarDemo::OnInit()
{
    // Configure global opengl state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    // Initialize Camera
    m_Camera = new Camera(glm::vec3(0.0f, 0.0f, 4.0f));

    // Initialize Shaders
    m_Shader = new Shader("C:/Users/katal/OneDrive/Desktop/development/car/shaders/model_loading.vs", "C:/Users/katal/OneDrive/Desktop/development/car/shaders/model_loading.fs");

    // Initialize IBL
    initIBLFromEXR("C:/Users/katal/OneDrive/Desktop/development/car/river_alcove_1k.exr");
    
    if (m_EnvCubemap == 0)
    {
        std::cout << "[WARN] IBL not initialized (envCubemap == 0). PBR will fall back to no env lighting.\n";
    }

    // Initialize Models
    m_RaptorModel = new Model("C:/Users/katal/OneDrive/Desktop/development/car/models/ford_raptor/scene.gltf");
    m_CarModel = new Model("C:/Users/katal/OneDrive/Desktop/development/car/models/2024_ford_shelby_super_snake_s650/scene.gltf");
    m_CarModel2 = new Model("C:/Users/katal/OneDrive/Desktop/development/car/models/2024_ford_shelby_super_snake_s650/scene.gltf");
    m_RoadModel = new Model("C:/Users/katal/OneDrive/Desktop/development/car/models/city_base_road/scene.gltf");

    // Place models
    placeModel(*m_RoadModel, glm::vec3(0.0f, 0.0f, 0.0f), 0.0f, 5.0f, false);
    placeModel(*m_RaptorModel, glm::vec3(0.0f, 0.0f, 0.0f), -1.0f, 1.5f, false);
    placeModel(*m_CarModel, glm::vec3(5.0f, 0.0f, 0.0f), -1.0f, 1.5f, false);
    placeModel(*m_CarModel2, glm::vec3(0.0f, 0.0f, 0.0f), -1.0f, 1.5f, true);

    m_StartTime = static_cast<float>(glfwGetTime());
}

float CarDemo::getTerrainHeight(float x, float z)
{
    if (!m_RoadModel) {
        return 0.0f;
    }

    glm::vec3 rayOrigin(x, 1000.0f, z);
    glm::vec3 rayDir(0.0f, -1.0f, 0.0f);
    float maxHeight = -1000.0f;
    bool found = false;

    for (const auto& placedModel : m_PlacedModels)
    {
        if (placedModel.model == m_RoadModel)
        {
            for (const auto& mesh : placedModel.model->meshes)
            {
                for (size_t i = 0; i < mesh.indices.size(); i += 3)
                {
                    glm::vec3 v0 = placedModel.baseModelMatrix * glm::vec4(mesh.vertices[mesh.indices[i]].Position, 1.0f);
                    glm::vec3 v1 = placedModel.baseModelMatrix * glm::vec4(mesh.vertices[mesh.indices[i+1]].Position, 1.0f);
                    glm::vec3 v2 = placedModel.baseModelMatrix * glm::vec4(mesh.vertices[mesh.indices[i+2]].Position, 1.0f);

                    float t = 0.0f;
                    if (rayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t))
                    {
                        float intersectionHeight = rayOrigin.y - t;
                        if (intersectionHeight > maxHeight)
                        {
                            maxHeight = intersectionHeight;
                            found = true;
                        }
                    }
                }
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
    // Physics / Game Logic moved to OnProcessInput or here
    // In main.cpp, car physics was in processInput, which is called every frame.
    // So we'll keep it there or here. 
    // Application::Run calls OnProcessInput then OnUpdate.
    float roadHeight = getTerrainHeight(m_Camera->Position.x, m_Camera->Position.z);
    m_Camera->Update(deltaTime, roadHeight + 1.0f); // add a small offset to avoid being inside the road
    m_Camera->Position.y = roadHeight + 1.0f; // Force camera to stick to ground
    
    // I'll leave car physics in OnProcessInput to match main.cpp structure where input directly affects state
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

    for (const auto &pm : m_PlacedModels)
    {
        glm::mat4 finalModel = pm.baseModelMatrix;
        if (pm.movable && pm.model == m_CarModel2)
        {
            glm::mat4 dyn = glm::mat4(1.0f);
            dyn = glm::translate(dyn, m_CarPos);
            dyn = glm::rotate(dyn, glm::radians(m_CarYaw), glm::vec3(0.0f, 1.0f, 0.0f));
            finalModel = dyn * pm.baseModelMatrix;
        }

        m_Shader->setMat4("model", finalModel);
        pm.model->Draw(*m_Shader, finalModel, m_Camera->Position);
    }
}

bool isPointInAABB(const glm::vec3& point, const glm::vec3& min, const glm::vec3& max) {
    return (point.x >= min.x && point.x <= max.x) &&
           (point.y >= min.y && point.y <= max.y) &&
           (point.z >= min.z && point.z <= max.z);
}

void CarDemo::OnProcessInput(float deltaTime)
{
    if (glfwGetKey(m_Window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(m_Window, true);

    glm::vec3 oldPos = m_Camera->Position;

    // Camera movement
    if (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS) m_Camera->ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS) m_Camera->ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS) m_Camera->ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS) m_Camera->ProcessKeyboard(RIGHT, deltaTime);
    if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_PRESS) m_Camera->ProcessKeyboard(JUMP, deltaTime);

    for (const auto& pm : m_PlacedModels)
    {
        if (pm.model != m_RoadModel && isPointInAABB(m_Camera->Position, pm.bboxMin, pm.bboxMax))
        {
            m_Camera->Position = oldPos;
            break;
        }
    }


    // Car driving
    if (glfwGetKey(m_Window, GLFW_KEY_UP) == GLFW_PRESS)
        m_CarSpeed += CAR_ACCEL * deltaTime;
    if (glfwGetKey(m_Window, GLFW_KEY_DOWN) == GLFW_PRESS)
        m_CarSpeed -= CAR_ACCEL * deltaTime;

    if (std::fabs(m_CarSpeed) > 0.01f)
    {
        float dir = (m_CarSpeed > 0.0f) ? 1.0f : -1.0f;
        if (glfwGetKey(m_Window, GLFW_KEY_LEFT) == GLFW_PRESS)
            m_CarYaw += CAR_TURN_SPEED * deltaTime * dir;
        if (glfwGetKey(m_Window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            m_CarYaw -= CAR_TURN_SPEED * deltaTime * dir;
    }

    glm::vec3 forward(std::sin(glm::radians(m_CarYaw)), 0.0f, -std::cos(glm::radians(m_CarYaw)));
    m_CarPos += forward * m_CarSpeed * deltaTime;

    if (m_CarSpeed > 0.0f)
    {
        m_CarSpeed -= CAR_FRICTION * deltaTime;
        if (m_CarSpeed < 0.0f) m_CarSpeed = 0.0f;
    }
    else if (m_CarSpeed < 0.0f)
    {
        m_CarSpeed += CAR_FRICTION * deltaTime;
        if (m_CarSpeed > 0.0f) m_CarSpeed = 0.0f;
    }

    m_CarPos.y = 0.0f;
    // m_Camera->Position.y = 0.0f;
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

    m_PlacedModels.push_back({ &m, modelMat, minW, maxW, movable });

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

    Shader equirectToCubemap("C:/Users/katal/OneDrive/Desktop/development/car/shaders/cubemap.vs", "C:/Users/katal/OneDrive/Desktop/development/car/shaders/equirectangular_to_cubemap.fs");
    Shader irradianceShader("C:/Users/katal/OneDrive/Desktop/development/car/shaders/cubemap.vs", "C:/Users/katal/OneDrive/Desktop/development/car/shaders/irradiance_convolution.fs");
    Shader prefilterShader("C:/Users/katal/OneDrive/Desktop/development/car/shaders/cubemap.vs", "C:/Users/katal/OneDrive/Desktop/development/car/shaders/prefilter.fs");
    Shader brdfShader("C:/Users/katal/OneDrive/Desktop/development/car/shaders/brdf.vs", "C:/Users/katal/OneDrive/Desktop/development/car/shaders/brdf.fs");

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
