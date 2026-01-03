#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <shader.h>
#include <camera.h>
#include <model.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <cfloat>
#include <cmath>

#ifdef HAS_TINYEXR
#include "tinyexr.h"
#endif

// ============================================================================
// Settings
// ============================================================================
const unsigned int SCR_WIDTH  = 2000;
const unsigned int SCR_HEIGHT = 1000;

// camera
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// IBL / environment
unsigned int envCubemap      = 0;
unsigned int irradianceMap   = 0;
unsigned int prefilterMap    = 0;
unsigned int brdfLUTTexture  = 0;
float        prefilterMaxMip = 0.0f;

// ============================================================================
// Custom data
// ============================================================================
struct PlacedModel
{
    Model* model;
    glm::mat4 baseModelMatrix;
    glm::vec3 bboxMin;
    glm::vec3 bboxMax;
    bool movable;
};

std::vector<PlacedModel> placedModels;
glm::vec3 carOffset(0.0f);

// ============================================================================
// Forward declarations
// ============================================================================
void processInput(GLFWwindow* window);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

// IBL helpers
#ifdef HAS_TINYEXR
unsigned int LoadHDR_EXR_2D(const char* filename);
#endif
void initIBLFromEXR(const std::string& exrPath);

// geometry helpers
void renderCube();
void renderQuad();

// ============================================================================
// Place model
// desiredHeight is final world-space height (Y size) you want
// ============================================================================
void placeModel(Model &m, glm::vec3 position, float heightOffset, float desiredHeight, bool movable)
{
    glm::mat4 modelMat = glm::mat4(1.0f);
    modelMat = glm::translate(modelMat, position);
    modelMat = glm::translate(modelMat, glm::vec3(0.0f, heightOffset, 0.0f));

    // Assumes Model::GetNormalizationScale() returns 1.0f / originalHeight
    float normScale  = m.GetNormalizationScale();
    float finalScale = desiredHeight * normScale;
    modelMat = glm::scale(modelMat, glm::vec3(finalScale));

    glm::vec3 minW, maxW;
    m.CalculateAABB(modelMat, minW, maxW);

    placedModels.push_back({ &m, modelMat, minW, maxW, movable });
}

// ============================================================================
// MAIN
// ============================================================================
int main()
{
    // GLFW init
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Static Models + HDR IBL", NULL, NULL);
    if (!window)
    {
        std::cout << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSYNC (optional)

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD\n";
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    // If your monitor is sRGB and your shader outputs linear color:
    // glEnable(GL_FRAMEBUFFER_SRGB);

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    // ------------------------------------------------------------------------
    // Shaders
    // ------------------------------------------------------------------------
    Shader ourShader("c:/development/car/shaders/model_loading.vs",
                     "c:/development/car/shaders/model_loading.fs");
    Shader carShader("c:/development/car/shaders/model_loading.vs",
                     "c:/development/car/shaders/model_loading.fs");

    // ------------------------------------------------------------------------
    // IBL from HDR EXR
    // ------------------------------------------------------------------------
    initIBLFromEXR("c:/development/car/river_alcove_1k.exr");

    if (envCubemap == 0)
    {
        std::cout << "[WARN] IBL not initialized (envCubemap == 0). "
                     "PBR will fall back to no env lighting.\n";
    }

    // ------------------------------------------------------------------------
    // Models
    // ------------------------------------------------------------------------
    Model RaptorModel("c:/development/car/models/ford_raptor/scene.gltf");
    Model CarModel("c:/development/car/models/2024_ford_shelby_super_snake_s650/scene.gltf");
    Model CarModel2("c:/development/car/models/2024_ford_shelby_super_snake_s650/scene.gltf");

    // desiredHeight = final Y-size in world units
    placeModel(RaptorModel, glm::vec3(0.0f,  0.0f, 0.0f),  -1.0f, 1.5f, false);
    placeModel(CarModel,   glm::vec3(5.0f,  0.0f, 0.0f),  -1.0f, 1.5f, false);
    placeModel(CarModel2,  glm::vec3(10.0f, 0.0f, 0.0f),  -1.0f, 1.5f, false);

    float startTime = static_cast<float>(glfwGetTime());

    // ========================================================================
    // Render loop
    // ========================================================================
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);
        camera.Update(deltaTime);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height); // *** keep aspect ratio correct on resize

        glm::mat4 projection = glm::perspective(
            glm::radians(camera.Zoom),
            (float)width / (float)height,
            0.1f,
            100.0f
        );
        glm::mat4 view = camera.GetViewMatrix();

        // simple fade example, if you really need it
        float startFadeTime = 2.0f;
        float endFadeTime   = 4.0f;
        float progress = glm::clamp(
            (currentFrame - startTime - startFadeTime) /
            (endFadeTime - startFadeTime),
            0.0f, 1.0f
        );
        float transparency = 1.0f - progress;

        // --------------------------------------------------------------------
        // Bind IBL textures & set per-frame uniforms
        // --------------------------------------------------------------------
        if (envCubemap != 0)
        {
            // 10: irradiance, 11: prefiltered, 12: BRDF LUT
            glActiveTexture(GL_TEXTURE0 + 10);
            glBindTexture(GL_TEXTURE_CUBE_MAP, (irradianceMap ? irradianceMap : envCubemap));

            glActiveTexture(GL_TEXTURE0 + 11);
            glBindTexture(GL_TEXTURE_CUBE_MAP, (prefilterMap ? prefilterMap : envCubemap));

            glActiveTexture(GL_TEXTURE0 + 12);
            glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
        }

        // -------- ourShader (raptor) ---------------------------------------
        ourShader.use();
        ourShader.setMat4("projection", projection);
        ourShader.setMat4("view", view);
        ourShader.setVec3("viewPos", camera.Position);

        // *** IMPORTANT: match shader uniform names ***
        ourShader.setInt("irradianceMap", 10);
        ourShader.setInt("prefilterMap", 11);    // <--- was "prefilteredMap"
        ourShader.setInt("brdfLUT", 12);
        ourShader.setFloat("prefilterMaxMip", prefilterMaxMip);
        ourShader.setFloat("materialTransparency", transparency);

        // -------- carShader (shelby cars) ----------------------------------
        carShader.use();
        carShader.setMat4("projection", projection);
        carShader.setMat4("view", view);
        carShader.setVec3("viewPos", camera.Position);
        carShader.setInt("irradianceMap", 10);
        carShader.setInt("prefilterMap", 11);    // <--- same fix here
        carShader.setInt("brdfLUT", 12);
        carShader.setFloat("prefilterMaxMip", prefilterMaxMip);
        carShader.setFloat("materialTransparency", transparency);

        // --------------------------------------------------------------------
        // Draw all placed models
        // --------------------------------------------------------------------
        for (const auto &pm : placedModels)
        {
            Shader* shader =
                (&CarModel == pm.model || &CarModel2 == pm.model)
                ? &carShader
                : &ourShader;

            shader->use();
            shader->setFloat("materialTransparency", transparency);

            glm::mat4 finalModel = pm.baseModelMatrix;
            if (pm.movable)
            {
                glm::mat4 offsetMat = glm::translate(glm::mat4(1.0f), carOffset);
                finalModel = offsetMat * pm.baseModelMatrix;
            }

            shader->setMat4("model", finalModel);
            shader->setVec3("viewPos", camera.Position);

            pm.model->Draw(*shader, finalModel, camera.Position);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

// ============================================================================
// Input
// ============================================================================
void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    float speed = 4.5f * deltaTime;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD,  deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT,     deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT,    deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)    camera.ProcessKeyboard(JUMP,     deltaTime);
    
    // Move car(s) with numpad
    if (glfwGetKey(window, GLFW_KEY_KP_4) == GLFW_PRESS) carOffset.x -= speed;
    if (glfwGetKey(window, GLFW_KEY_KP_6) == GLFW_PRESS) carOffset.x += speed;
    if (glfwGetKey(window, GLFW_KEY_KP_8) == GLFW_PRESS) carOffset.z -= speed;
    if (glfwGetKey(window, GLFW_KEY_KP_2) == GLFW_PRESS) carOffset.z += speed;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    if (height == 0) height = 1;
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn)
{
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

// ============================================================================
// IBL helpers
// ============================================================================
#ifdef HAS_TINYEXR
unsigned int LoadHDR_EXR_2D(const char* filename)
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
    // TinyEXR returns RGBA float; upload as RGB32F, ignore A
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGBA, GL_FLOAT, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    free(img);
    return hdrTex;
}
#endif

void initIBLFromEXR(const std::string& exrPath)
{
#ifndef HAS_TINYEXR
    std::cerr << "HAS_TINYEXR not defined; cannot load EXR. Define HAS_TINYEXR and link tinyexr.\n";
    envCubemap = 0;
    irradianceMap = 0;
    prefilterMap = 0;
    brdfLUTTexture = 0;
    prefilterMaxMip = 0.0f;
    return;
#else
    unsigned int hdrTexture = LoadHDR_EXR_2D(exrPath.c_str());
    if (hdrTexture == 0)
    {
        std::cerr << "Failed to load HDR EXR for IBL: " << exrPath << std::endl;
        envCubemap = 0;
        irradianceMap = 0;
        prefilterMap = 0;
        brdfLUTTexture = 0;
        prefilterMaxMip = 0.0f;
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

    glGenTextures(1, &envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     envSize, envSize, 0, GL_RGB, GL_FLOAT, nullptr);
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

    Shader equirectToCubemap("c:/development/car/shaders/cubemap.vs",
                             "c:/development/car/shaders/equirectangular_to_cubemap.fs");
    Shader irradianceShader("c:/development/car/shaders/cubemap.vs",
                            "c:/development/car/shaders/irradiance_convolution.fs");
    Shader prefilterShader("c:/development/car/shaders/cubemap.vs",
                           "c:/development/car/shaders/prefilter.fs");
    Shader brdfShader("c:/development/car/shaders/brdf.vs",
                      "c:/development/car/shaders/brdf.fs");

    // 1) Equirectangular -> cubemap
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
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // 2) BRDF LUT
    glGenTextures(1, &brdfLUTTexture);
    glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);
    glViewport(0, 0, 512, 512);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    brdfShader.use();
    renderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 3) Irradiance map
    const unsigned int irradianceSize = 32;
    glGenTextures(1, &irradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     irradianceSize, irradianceSize, 0, GL_RGB, GL_FLOAT, nullptr);
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
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glViewport(0, 0, irradianceSize, irradianceSize);
    for (unsigned int i = 0; i < 6; ++i)
    {
        irradianceShader.setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 4) Prefilter map
    const unsigned int prefilterSize = 128;
    glGenTextures(1, &prefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     prefilterSize, prefilterSize, 0, GL_RGB, GL_FLOAT, nullptr);
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
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    unsigned int maxMipLevels = 5;
    for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
    {
        unsigned int mipWidth  = static_cast<unsigned int>(prefilterSize * std::pow(0.5f, (float)mip));
        unsigned int mipHeight = mipWidth;

        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (float)mip / (float)(maxMipLevels - 1);
        prefilterShader.setFloat("roughness", roughness);
        for (unsigned int i = 0; i < 6; ++i)
        {
            prefilterShader.setMat4("view", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCube();
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    prefilterMaxMip = (float)(maxMipLevels - 1);

    std::cout << "IBL from EXR initialized successfully.\n";
#endif
}

// ============================================================================
// Geometry helpers (cube, quad)
// ============================================================================
unsigned int cubeVAO = 0, cubeVBO = 0;
void renderCube()
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

unsigned int quadVAO = 0, quadVBO = 0;
void renderQuad()
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
