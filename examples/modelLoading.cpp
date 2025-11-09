#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <shader.h>
#include <camera.h>
#include <model.h>
#include <string>

#include <iostream>
#include <cstdio>
#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include "stb_image_write.h"

#if defined(HAS_TINYEXR)
#include "tinyexr.h"
#endif

void framebuffer_size_callback(GLFWwindow *window, int width, int height);
void mouse_callback(GLFWwindow *window, double xpos, double ypos);
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);

static void glCheck(const char *where)
{
    GLenum err;
    bool ok = true;
    while ((err = glGetError()) != GL_NO_ERROR)
    {
        fprintf(stderr, "GL error at %s: 0x%X\n", where, err);
        // Extra diagnostics to aid debugging of INVALID_OPERATION (0x502)
        GLint curProg = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
        fprintf(stderr, "  GL_CURRENT_PROGRAM = %d\n", curProg);
        GLint vao = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        fprintf(stderr, "  GL_VERTEX_ARRAY_BINDING = %d\n", vao);
        GLint ebo = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);
        fprintf(stderr, "  GL_ELEMENT_ARRAY_BUFFER_BINDING = %d\n", ebo);
        GLint activeTex = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
        GLint maxTexUnits = 0;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTexUnits);
        fprintf(stderr, "  GL_ACTIVE_TEXTURE = 0x%X, MAX_COMBINED_TEXTURE_IMAGE_UNITS = %d\n", activeTex, maxTexUnits);
        // Print bindings for the first few texture units to avoid huge output
        int inspectUnits = std::min(maxTexUnits, 8);
        for (int u = 0; u < inspectUnits; ++u)
        {
            GLenum unit = GL_TEXTURE0 + u;
            glActiveTexture(unit);
            GLint bound2D = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound2D);
            fprintf(stderr, "    Unit %d (GL_TEXTURE0+%d) bound 2D=%d\n", u, u, bound2D);
        }
        // restore active texture
        glActiveTexture(activeTex);
        ok = false;
    }
    if (ok)
    {
        printf("GL OK: %s\n", where);
    }
    else
    {
        // If the user set SINGLE_ERROR_DUMP=1, stop the process after the first dump to avoid huge logs
        const char *sed = std::getenv("SINGLE_ERROR_DUMP");
        if (sed && std::string(sed) == "1")
        {
            std::cerr << "SINGLE_ERROR_DUMP=1: exiting after first GL error dump to avoid log flood." << std::endl;
            // flush stdout/stderr and exit so user can capture this single block
            fflush(stdout);
            fflush(stderr);
            exit(1);
        }
    }
}

// settings
const unsigned int SCR_WIDTH = 2000;
const unsigned int SCR_HEIGHT = 1000;

// camera
Camera camera(glm::vec3(0.0f, 0.0f, 2.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// per-model offsets (so we can place/move the second model independently)
glm::vec3 carOffset = glm::vec3(3.0f, 0.0f, 0.0f);
// toggle to display brief help for model controls
bool showModelControlHelp = true;
// control mode: false = camera control (arrow keys move camera), true = model control (arrow keys move CarModel)
bool controlModeModel = false;
// lock models in place by default so camera movement won't accidentally move them
bool carLocked = true;

// timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

int main()
{
    // glfw: initialize and configure
    // ------------------------------
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // glfw window creation
    // --------------------
    GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // tell GLFW to capture our mouse
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // Quick EXR-only probe mode: if the user set EXR_DUMP_ONLY=1, attempt to load the EXR
    // and print the result, then exit. This lets us capture tinyexr diagnostics without
    // launching the full GLFW window/render loop.
#if defined(HAS_TINYEXR)
    if (const char *dump = std::getenv("EXR_DUMP_ONLY"))
    {
        if (std::string(dump) == "1")
        {
            std::string exrPath;
            char *envPath = std::getenv("EXR_PATH");
            if (envPath != nullptr)
                exrPath = std::string(envPath);
            if (exrPath.empty())
                exrPath = "C:/development/car/river_alcove_1k.exr";
            std::cout << "[EXR_DUMP_ONLY] EXR path: '" << exrPath << "'" << std::endl;
            const char *err = nullptr;
            float *img = nullptr;
            int w = 0, h = 0;
            int ret = LoadEXR(&img, &w, &h, exrPath.c_str(), &err);
            if (ret == TINYEXR_SUCCESS && img != nullptr)
            {
                std::cout << "[EXR_DUMP_ONLY] LoadEXR succeeded: " << w << "x" << h << " (RGBA float)" << std::endl;
                free(img);
                // exit after reporting
                return 0;
            }
            else
            {
                if (err)
                {
                    std::cerr << "[EXR_DUMP_ONLY] tinyexr load error: " << err << std::endl;
                    FreeEXRErrorMessage(err);
                }
                else
                {
                    std::cerr << "[EXR_DUMP_ONLY] LoadEXR failed (unknown error)" << std::endl;
                }
                return 1;
            }
        }
    }
#endif

    // tell stb_image.h whether to flip loaded texture's on the y-axis (before loading model).
    // Assimp is called with aiProcess_FlipUVs when loading the model, so don't also flip images here
    // or you'll double-flip UVs and get incorrect texture mapping. Set to false for glTF/Assimp.
    stbi_set_flip_vertically_on_load(false);

    // configure global opengl state
    // -----------------------------
    glEnable(GL_DEPTH_TEST);
    // enable alpha blending for glTF materials that use alpha mode=BLEND (e.g., glass)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // build and compile shaders
    // -------------------------
    Shader ourShader("C:/development/car/shaders/model_loading.vs", "C:/development/car/shaders/model_loading.fs");
    // Secondary shader instance for the second model (uses the same shader source files
    // but kept as a separate variable so we can tweak uniforms or behavior independently).
    Shader carShader("C:/development/car/shaders/model_loading.vs", "C:/development/car/shaders/model_loading.fs");

    // load models
    // -----------
    // FileSystem helper is not present in this project; use the literal path instead
    Model ourModel("C:/development/car/ford_raptor/scene.gltf");
    Model CarModel("C:/development/car/models/2024_ford_shelby_super_snake_s650/scene.gltf");

    std::cout << "Loaded Model objects (ourModel and CarModel constructed)." << std::endl;

    // Print a concise summary so the user can quickly confirm the model loaded
    {
        size_t meshCount = ourModel.meshes.size();
        size_t texCount = ourModel.textures_loaded.size();
        size_t totalVerts = 0;
        for (const auto &m : ourModel.meshes)
            totalVerts += m.vertices.size();
        std::cout << "Model summary: meshes=" << meshCount << " totalVertices=" << totalVerts << " texturesLoaded=" << texCount << std::endl;
        if (meshCount == 0)
        {
            std::cout << "WARNING: Model has 0 meshes. Nothing will render." << std::endl;
        }
    }

    // Compute bounding box for the second model (CarModel) so we can place it independently
    glm::vec3 carBBoxMin(FLT_MAX), carBBoxMax(-FLT_MAX);
    for (const auto &mesh : CarModel.meshes)
    {
        for (const auto &v : mesh.vertices)
        {
            carBBoxMin.x = std::min(carBBoxMin.x, v.Position.x);
            carBBoxMin.y = std::min(carBBoxMin.y, v.Position.y);
            carBBoxMin.z = std::min(carBBoxMin.z, v.Position.z);
            carBBoxMax.x = std::max(carBBoxMax.x, v.Position.x);
            carBBoxMax.y = std::max(carBBoxMax.y, v.Position.y);
            carBBoxMax.z = std::max(carBBoxMax.z, v.Position.z);
        }
    }
    glm::vec3 carBBoxCenter = (carBBoxMin + carBBoxMax) * 0.5f;
    glm::vec3 carBBoxSize = carBBoxMax - carBBoxMin;
    float carBBoxDiag = glm::length(carBBoxSize);
    std::cout << "CarModel AABB: min=" << carBBoxMin.x << "," << carBBoxMin.y << "," << carBBoxMin.z
              << " max=" << carBBoxMax.x << "," << carBBoxMax.y << "," << carBBoxMax.z
              << " center=" << carBBoxCenter.x << "," << carBBoxCenter.y << "," << carBBoxCenter.z
              << " size=" << carBBoxSize.x << "," << carBBoxSize.y << "," << carBBoxSize.z
              << " diag=" << carBBoxDiag << std::endl;

    std::cout << "Finished CarModel bbox compute." << std::endl;

    // Small helper to place multiple models with fixed model matrices so they don't move relative to world
    struct PlacedModel
    {
        Model *model;
        glm::vec3 bboxMin;
        glm::vec3 bboxMax;
        // baseModelMatrix is the static transform computed at placement time. At draw-time
        // we may left-multiply a translation (for example `carOffset`) when the model is movable.
        glm::mat4 baseModelMatrix;
        bool movable = false; // whether to apply runtime offset (carOffset) at draw time
    };

    std::vector<PlacedModel> placedModels;

    // helper lambda: center model by its bbox center, apply scale, then translate to worldPos
    // `movable` indicates whether a runtime `carOffset` should be applied at draw-time.
    auto placeModel = [&](Model &m, const glm::vec3 &bboxMinLocal, const glm::vec3 &bboxMaxLocal, const glm::vec3 &worldPos, float scale = 1.0f, bool movable = false)
    {
        glm::vec3 center = (bboxMinLocal + bboxMaxLocal) * 0.5f;
        glm::mat4 mm = glm::mat4(1.0f);
        // move model so its bbox center is at origin, then scale, then translate to world position
        mm = glm::translate(mm, worldPos);
        mm = glm::scale(mm, glm::vec3(scale));
        mm = glm::translate(mm, -center);
        PlacedModel pm;
        pm.model = &m;
        pm.bboxMin = bboxMinLocal;
        pm.bboxMax = bboxMaxLocal;
        pm.baseModelMatrix = mm;
        pm.movable = movable;
        placedModels.push_back(pm);
    };

    // place the main model and secondary car later (after we compute the model bbox)

    // If AUTO_FRAME=1 we will compute a combined world-space AABB for all placed models and position the camera to frame them
    if (const char *af = std::getenv("AUTO_FRAME"))
    {
        if (std::string(af) == "1")
        {
            // compute combined AABB by transforming bbox corners of each placed model
            glm::vec3 combinedMin(FLT_MAX), combinedMax(-FLT_MAX);
            for (const auto &pm : placedModels)
            {
                // 8 corners of local bbox
                glm::vec3 corners[8] = {
                    {pm.bboxMin.x, pm.bboxMin.y, pm.bboxMin.z},
                    {pm.bboxMax.x, pm.bboxMin.y, pm.bboxMin.z},
                    {pm.bboxMin.x, pm.bboxMax.y, pm.bboxMin.z},
                    {pm.bboxMax.x, pm.bboxMax.y, pm.bboxMin.z},
                    {pm.bboxMin.x, pm.bboxMin.y, pm.bboxMax.z},
                    {pm.bboxMax.x, pm.bboxMin.y, pm.bboxMax.z},
                    {pm.bboxMin.x, pm.bboxMax.y, pm.bboxMax.z},
                    {pm.bboxMax.x, pm.bboxMax.y, pm.bboxMax.z}};
                for (int i = 0; i < 8; ++i)
                {
                    glm::vec4 wc = pm.baseModelMatrix * glm::vec4(corners[i], 1.0f);
                    combinedMin.x = std::min(combinedMin.x, wc.x);
                    combinedMin.y = std::min(combinedMin.y, wc.y);
                    combinedMin.z = std::min(combinedMin.z, wc.z);
                    combinedMax.x = std::max(combinedMax.x, wc.x);
                    combinedMax.y = std::max(combinedMax.y, wc.y);
                    combinedMax.z = std::max(combinedMax.z, wc.z);
                }
            }
            glm::vec3 combinedCenter = (combinedMin + combinedMax) * 0.5f;
            glm::vec3 combinedSize = combinedMax - combinedMin;
            float combinedDiag = glm::length(combinedSize);
            // position camera to look at combined center from +Z with some offset based on diagonal
            float dist = combinedDiag * 0.8f;
            if (dist < 5.0f)
                dist = 5.0f;
            camera.Position = combinedCenter + glm::vec3(0.0f, combinedSize.y * 0.3f, dist);
            camera.Yaw = -90.0f;
            camera.Pitch = -10.0f;
            camera.ProcessMouseMovement(0.0f, 0.0f);
            std::cout << "AUTO_FRAME applied to all placed models: camera.Position=" << camera.Position.x << "," << camera.Position.y << "," << camera.Position.z << std::endl;
        }
    }

    // Compute axis-aligned bounding box of loaded model in model space (after per-node transforms baked into vertices)
    glm::vec3 bboxMin(FLT_MAX), bboxMax(-FLT_MAX);
    for (const auto &mesh : ourModel.meshes)
    {
        for (const auto &v : mesh.vertices)
        {
            bboxMin.x = std::min(bboxMin.x, v.Position.x);
            bboxMin.y = std::min(bboxMin.y, v.Position.y);
            bboxMin.z = std::min(bboxMin.z, v.Position.z);
            bboxMax.x = std::max(bboxMax.x, v.Position.x);
            bboxMax.y = std::max(bboxMax.y, v.Position.y);
            bboxMax.z = std::max(bboxMax.z, v.Position.z);
        }
    }
    glm::vec3 bboxCenter = (bboxMin + bboxMax) * 0.5f;
    glm::vec3 bboxSize = bboxMax - bboxMin;
    float bboxDiag = glm::length(bboxSize);
    std::cout << "Model AABB: min=" << bboxMin.x << "," << bboxMin.y << "," << bboxMin.z
              << " max=" << bboxMax.x << "," << bboxMax.y << "," << bboxMax.z
              << " center=" << bboxCenter.x << "," << bboxCenter.y << "," << bboxCenter.z
              << " size=" << bboxSize.x << "," << bboxSize.y << "," << bboxSize.z
              << " diag=" << bboxDiag << std::endl;

    std::cout << "Finished ourModel bbox compute." << std::endl;

    // Now place the main model at world origin (on ground) and the car to the +X side
    // Place the main model at world origin (on ground).
    placeModel(ourModel, bboxMin, bboxMax, glm::vec3(0.0f, -bboxSize.y * 0.5f, 0.0f));
    // Place the car to the +X side and keep it stationary by default (movable=false).
    // The car can still be made movable later by toggling a control if desired.
    placeModel(CarModel, carBBoxMin, carBBoxMax, glm::vec3(3.0f, -carBBoxSize.y * 0.5f, 0.0f), 1.0f, false);

    // Optional auto-framing: if AUTO_FRAME=1, move camera back so whole model fits in view.
    if (const char *af = std::getenv("AUTO_FRAME"))
    {
        if (std::string(af) == "1")
        {
            // Position camera on +Z axis looking at center, distance based on diagonal.
            float dist = bboxDiag * 0.8f; // heuristic
            if (dist < 5.0f)
                dist = 5.0f; // minimum reasonable distance
            camera.Position = bboxCenter + glm::vec3(0.0f, bboxSize.y * 0.3f, dist);
            camera.Yaw = -90.0f;   // keep looking down -Z
            camera.Pitch = -10.0f; // slight downward tilt
            // Recompute internal camera vectors
            // (hack: trigger mouse movement updateCameraVectors via small offsets)
            camera.ProcessMouseMovement(0.0f, 0.0f);
            std::cout << "AUTO_FRAME applied: camera.Position=" << camera.Position.x << "," << camera.Position.y << "," << camera.Position.z << std::endl;
        }
    }

    // Wireframe debug if WIREFRAME=1
    if (const char *wf = std::getenv("WIREFRAME"))
    {
        if (std::string(wf) == "1")
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            std::cout << "Wireframe mode enabled (WIREFRAME=1)." << std::endl;
        }
    }

    // --- Debug textured-quad helper (used when DEBUG_TEXTURE=1 is set) ---
    unsigned int debugQuadVAO = 0, debugQuadVBO = 0;
    Shader debugQuadShader("C:/development/car/shaders/debug_quad.vs", "C:/development/car/shaders/debug_flat.fs");
    {
        // positions (clip space) + texcoords
        float quadVertices[] = {
            // pos      // tex
            -1.0f, 1.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
            1.0f, -1.0f, 1.0f, 0.0f,

            -1.0f, 1.0f, 0.0f, 1.0f,
            1.0f, -1.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f};
        glGenVertexArrays(1, &debugQuadVAO);
        glGenBuffers(1, &debugQuadVBO);
        glBindVertexArray(debugQuadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, debugQuadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    // flag to print a single draw-time message
    bool printedDrawMessage = false;
    bool debugCaptured = false;

    // Optional pause before entering the render loop to inspect initialization.
    // Set PAUSE_BEFORE_RENDER=1 in the environment to enable.
    if (const char *pbr = std::getenv("PAUSE_BEFORE_RENDER"))
    {
        if (std::string(pbr) == "1")
        {
            std::cout << "PAUSE_BEFORE_RENDER=1 set. Initialization complete. Press Enter to continue to the render loop..." << std::endl;
            std::cin.get();
        }
    }

    // --- Procedural HDR environment cubemap (used as a sample HDR for IBL) ---
    unsigned int envCubemap;
    unsigned int irradianceMap = 0;
    unsigned int prefilterMap = 0;
    unsigned int brdfLUTTexture = 0;
    const int envSize = 128;
    glGenTextures(1, &envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    printf("Created and bound envCubemap (tex id=%u)\n", envCubemap);
    glCheck("glBindTexture envCubemap");
    // If the user provided an EXR path as argv[1] and tinyexr is available (HAS_TINYEXR),
    // load the equirectangular EXR and convert to cubemap. Otherwise fall back to the
    // procedural HDR generation below.
// Attempt EXR loading if tinyexr is available and user hasn't disabled EXR via EXR_DISABLE env var
#if defined(HAS_TINYEXR)
    bool exrLoaded = false;
    std::string exrPath;
    char *envDisable = std::getenv("EXR_DISABLE");
    bool exrDisabled = (envDisable != nullptr && std::string(envDisable) == "1");
    if (exrDisabled)
    {
        std::cout << "EXR loading disabled via EXR_DISABLE=1; using procedural HDR fallback." << std::endl;
    }
    else
    {
        std::cout << "tinyexr support compiled in; attempting to load EXR if present." << std::endl;
        char *envPath = std::getenv("EXR_PATH");
        if (envPath != nullptr)
        {
            exrPath = std::string(envPath);
        }
        if (exrPath.empty())
            exrPath = "C:/development/car/river_alcove_1k.exr";
        std::cout << "EXR path: '" << exrPath << "'" << std::endl;
        if (!exrPath.empty())
        {
            const char *err = nullptr;
            int ret = 0;
            float *img = nullptr;
            int w = 0, h = 0;
            ret = LoadEXR(&img, &w, &h, exrPath.c_str(), &err);
            if (ret == TINYEXR_SUCCESS && img != nullptr)
            {
                // img is RGBA floats (w*h*4). We'll upload it as an HDR equirectangular texture
                // and perform GPU-based equirect->cubemap + irradiance + prefilter + BRDF LUT.
                unsigned int hdrTexture;
                glGenTextures(1, &hdrTexture);
                glBindTexture(GL_TEXTURE_2D, hdrTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGBA, GL_FLOAT, img);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                // create capture FBO
                unsigned int captureFBO, captureRBO;
                glGenFramebuffers(1, &captureFBO);
                glGenRenderbuffers(1, &captureRBO);
                glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
                glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

                // create cubemap to render to
                const unsigned int envSizeGPU = 512;
                glGenTextures(1, &envCubemap);
                glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
                for (unsigned int i = 0; i < 6; ++i)
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, envSizeGPU, envSizeGPU, 0, GL_RGB, GL_FLOAT, nullptr);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                // set up capture projection and views
                glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
                glm::mat4 captureViews[] = {
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))};

                // load capture shaders
                Shader equirectToCubemapShader("C:/development/car/shaders/cubemap.vs", "C:/development/car/shaders/equirectangular_to_cubemap.fs");
                Shader irradianceShader("C:/development/car/shaders/cubemap.vs", "C:/development/car/shaders/irradiance_convolution.fs");
                Shader prefilterShader("C:/development/car/shaders/cubemap.vs", "C:/development/car/shaders/prefilter.fs");
                Shader brdfShader("C:/development/car/shaders/brdf.vs", "C:/development/car/shaders/brdf.fs");

                // create cube VAO/VBO helper (renderCube)
                unsigned int cubeVAO = 0, cubeVBO = 0;
                {
                    float vertices[] = {
                        // positions
                        -1.0f, -1.0f, -1.0f,
                        1.0f, -1.0f, -1.0f,
                        1.0f, 1.0f, -1.0f,
                        1.0f, 1.0f, -1.0f,
                        -1.0f, 1.0f, -1.0f,
                        -1.0f, -1.0f, -1.0f,

                        -1.0f, -1.0f, 1.0f,
                        1.0f, -1.0f, 1.0f,
                        1.0f, 1.0f, 1.0f,
                        1.0f, 1.0f, 1.0f,
                        -1.0f, 1.0f, 1.0f,
                        -1.0f, -1.0f, 1.0f,

                        -1.0f, 1.0f, 1.0f,
                        -1.0f, 1.0f, -1.0f,
                        -1.0f, -1.0f, -1.0f,
                        -1.0f, -1.0f, -1.0f,
                        -1.0f, -1.0f, 1.0f,
                        -1.0f, 1.0f, 1.0f,

                        1.0f, 1.0f, 1.0f,
                        1.0f, 1.0f, -1.0f,
                        1.0f, -1.0f, -1.0f,
                        1.0f, -1.0f, -1.0f,
                        1.0f, -1.0f, 1.0f,
                        1.0f, 1.0f, 1.0f,

                        -1.0f, -1.0f, -1.0f,
                        1.0f, -1.0f, -1.0f,
                        1.0f, -1.0f, 1.0f,
                        1.0f, -1.0f, 1.0f,
                        -1.0f, -1.0f, 1.0f,
                        -1.0f, -1.0f, -1.0f,

                        -1.0f, 1.0f, -1.0f,
                        1.0f, 1.0f, -1.0f,
                        1.0f, 1.0f, 1.0f,
                        1.0f, 1.0f, 1.0f,
                        -1.0f, 1.0f, 1.0f,
                        -1.0f, 1.0f, -1.0f};
                    glGenVertexArrays(1, &cubeVAO);
                    glGenBuffers(1, &cubeVBO);
                    glBindVertexArray(cubeVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    glBindVertexArray(0);
                }

                // create a 2D BRDF LUT texture
                glGenTextures(1, &brdfLUTTexture);
                glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                // render BRDF LUT
                glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
                glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTTexture, 0);
                glViewport(0, 0, 512, 512);
                brdfShader.use();
                // render fullscreen quad (we'll create a simple quad VAO)
                unsigned int quadVAO = 0, quadVBO = 0;
                {
                    float quadVertices[] = {
                        // positions   // texCoords
                        -1.0f, 1.0f, 0.0f, 1.0f,
                        -1.0f, -1.0f, 0.0f, 0.0f,
                        1.0f, -1.0f, 1.0f, 0.0f,

                        -1.0f, 1.0f, 0.0f, 1.0f,
                        1.0f, -1.0f, 1.0f, 0.0f,
                        1.0f, 1.0f, 1.0f, 1.0f};
                    glGenVertexArrays(1, &quadVAO);
                    glGenBuffers(1, &quadVBO);
                    glBindVertexArray(quadVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
                }
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glBindVertexArray(quadVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);

                // create irradiance map
                glGenTextures(1, &irradianceMap);
                glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
                const unsigned int irradianceSize = 32;
                for (unsigned int i = 0; i < 6; ++i)
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, irradianceSize, irradianceSize, 0, GL_RGB, GL_FLOAT, nullptr);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                // convert HDR equirectangular to cubemap (render to envCubemap)
                equirectToCubemapShader.use();
                equirectToCubemapShader.setInt("equirectangularMap", 0);
                equirectToCubemapShader.setMat4("projection", captureProjection);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, hdrTexture);

                glViewport(0, 0, envSizeGPU, envSizeGPU);
                glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
                for (unsigned int i = 0; i < 6; ++i)
                {
                    equirectToCubemapShader.setMat4("view", captureViews[i]);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glBindVertexArray(cubeVAO);
                    glDrawArrays(GL_TRIANGLES, 0, 36);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                // then generate mipmaps for the cubemap so OpenGL can sample it at different roughness levels
                glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
                glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

                // create prefilter cubemap
                glGenTextures(1, &prefilterMap);
                glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
                const unsigned int prefilterSize = 128;
                for (unsigned int i = 0; i < 6; ++i)
                    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, prefilterSize, prefilterSize, 0, GL_RGB, GL_FLOAT, nullptr);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                // generate mipmaps for the prefilter map
                glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

                // run a quasi Monte-Carlo simulation on the environment map to create a prefilter (per mip)
                prefilterShader.use();
                prefilterShader.setInt("environmentMap", 0);
                prefilterShader.setMat4("projection", captureProjection);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

                glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
                unsigned int maxMipLevels = 5;
                for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
                {
                    unsigned int mipWidth = prefilterSize * std::pow(0.5, mip);
                    unsigned int mipHeight = mipWidth;
                    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
                    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
                    glViewport(0, 0, mipWidth, mipHeight);

                    float roughness = (float)mip / (float)(maxMipLevels - 1);
                    prefilterShader.setFloat("roughness", roughness);
                    for (unsigned int i = 0; i < 6; ++i)
                    {
                        prefilterShader.setMat4("view", captureViews[i]);
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap, mip);
                        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                        glBindVertexArray(cubeVAO);
                        glDrawArrays(GL_TRIANGLES, 0, 36);
                    }
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                // create irradiance by convoluting the environment map
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
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap, 0);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    glBindVertexArray(cubeVAO);
                    glDrawArrays(GL_TRIANGLES, 0, 36);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                // bind generated textures to known units for later use
                // we'll set uniforms in the main render loop
                // free CPU image
                free(img);
                exrLoaded = true;
                std::cout << "EXR loaded and GPU IBL maps generated." << std::endl;
            }
            else
            {
                if (err)
                {
                    fprintf(stderr, "tinyexr load error: %s\n", err);
                    FreeEXRErrorMessage(err);
                }
                else
                {
                    std::cout << "tinyexr: LoadEXR returned failure." << std::endl;
                }
            }
        }
    }
#else
    std::cout << "tinyexr not compiled in. Using procedural HDR fallback." << std::endl;
#endif
    // Decide whether to use the procedural HDR fallback (true) or the loaded EXR cubemap (false)
    bool useProcedural = true;
#if defined(HAS_TINYEXR)
    useProcedural = !exrLoaded;
#else
    useProcedural = true;
#endif
    if (useProcedural)
    {
        // generate HDR-like data per face
        glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f));
        float sunIntensity = 6.0f;
        float sunPower = 64.0f;
        for (unsigned int face = 0; face < 6; ++face)
        {
            std::vector<float> data(envSize * envSize * 3);
            for (int y = 0; y < envSize; ++y)
            {
                for (int x = 0; x < envSize; ++x)
                {
                    // normalized device coords in [-1,1]
                    float u = (2.0f * (x + 0.5f) / envSize) - 1.0f;
                    float v = (2.0f * (y + 0.5f) / envSize) - 1.0f;
                    glm::vec3 dir;
                    switch (face)
                    {
                    case 0:
                        dir = glm::vec3(1.0f, -v, -u);
                        break; // +X
                    case 1:
                        dir = glm::vec3(-1.0f, -v, u);
                        break; // -X
                    case 2:
                        dir = glm::vec3(u, 1.0f, v);
                        break; // +Y
                    case 3:
                        dir = glm::vec3(u, -1.0f, -v);
                        break; // -Y
                    case 4:
                        dir = glm::vec3(u, -v, 1.0f);
                        break; // +Z
                    case 5:
                        dir = glm::vec3(-u, -v, -1.0f);
                        break; // -Z
                    }
                    dir = glm::normalize(dir);
                    // sky gradient
                    float t = glm::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
                    glm::vec3 sky = glm::mix(glm::vec3(0.02f), glm::vec3(0.6f, 0.7f, 0.9f), t);
                    // sun spot
                    float sun = pow(glm::max(glm::dot(dir, sunDir), 0.0f), sunPower) * sunIntensity;
                    glm::vec3 color = sky + glm::vec3(sun);
                    int idx = (y * envSize + x) * 3;
                    data[idx + 0] = color.r;
                    data[idx + 1] = color.g;
                    data[idx + 2] = color.b;
                }
            }
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB16F, envSize, envSize, 0, GL_RGB, GL_FLOAT, data.data());
            printf("Procedural -> uploaded cubemap face %u\n", face);
            glCheck("glTexImage2D procedural face");
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        // generate mipmaps to approximate prefiltered environment
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        printf("Generated mipmaps for envCubemap\n");
        glCheck("glGenerateMipmap envCubemap");

        // end of cubemap setup; fall-through to the render loop below
    }

    // render loop
    // -----------
    // bool screenshotTaken = false;
    // int _debugFrameCount = 0;
    while (!glfwWindowShouldClose(window))
    {
        static bool entered = false;
        if (!entered)
        {
            std::cout << "Entering render loop." << std::endl;
            entered = true;
        }
        // per-frame time logic
        // --------------------
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // input
        // -----
        processInput(window);

        // render
        // ------
        glClearColor(0.8f, 0.8f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // Ensure the viewport matches the actual framebuffer size (some earlier FBO/code may have changed it)
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        // view/projection transformations
        // Adjust far plane dynamically if AUTO_FRAME enabled and model is large
        float farPlane = 100.0f;
        if (bboxDiag > 90.0f)
            farPlane = bboxDiag * 2.0f;
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, farPlane);
        glm::mat4 view = camera.GetViewMatrix();

        // set per-shader uniforms while the respective shader is bound (avoids setting uniforms
        // on the wrong currently-bound program).
        ourShader.use();
        ourShader.setInt("irradianceMap", 10);
        ourShader.setInt("prefilteredMap", 11);
        ourShader.setInt("brdfLUT", 12);
        ourShader.setFloat("prefilterMaxMip", std::log2((float)128));
        ourShader.setMat4("projection", projection);
        ourShader.setMat4("view", view);
        ourShader.setVec3("viewPos", camera.Position);

        carShader.use();
        carShader.setInt("irradianceMap", 10);
        carShader.setInt("prefilteredMap", 11);
        carShader.setInt("brdfLUT", 12);
        carShader.setFloat("prefilterMaxMip", std::log2((float)128));
        carShader.setMat4("projection", projection);
        carShader.setMat4("view", view);
        carShader.setVec3("viewPos", camera.Position);

        // bind IBL textures once (bindings are global state)
        glActiveTexture(GL_TEXTURE0 + 10);
        glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap ? irradianceMap : envCubemap);
        glActiveTexture(GL_TEXTURE0 + 11);
        glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap ? prefilterMap : envCubemap);
        glActiveTexture(GL_TEXTURE0 + 12);
        glBindTexture(GL_TEXTURE_2D, brdfLUTTexture);

        // render the loaded model
        glm::mat4 model = glm::mat4(1.0f);
        // Center model around origin based on computed bbox so it's in front of camera
        model = glm::translate(model, -bboxCenter + glm::vec3(0.0f, -bboxSize.y * 0.5f, 0.0f));
        // render the loaded model
        glm::mat4 carmodel = glm::mat4(1.0f);
    // Center carmodel around its own bbox center so it's placed correctly in world space
    // (previously this used the main model's `bboxCenter` which mis-centered the car).
    carmodel = glm::translate(carmodel, -carBBoxCenter + glm::vec3(5.0f, -carBBoxSize.y * 0.5f, 0.0f));

        // If model extremely large, scale it down for visibility (heuristic)
        if (bboxDiag > 200.0f)
        {
            float scaleFactor = 200.0f / bboxDiag;
            model = glm::scale(model, glm::vec3(scaleFactor));
            carmodel = glm::scale(carmodel, glm::vec3(scaleFactor));
        }
        else
        {
            model = glm::scale(model, glm::vec3(1.0f));
            carmodel = glm::scale(carmodel, glm::vec3(1.0f));
        }
        // Set default model matrices for heuristics (not strictly required when using placedModels)
        ourShader.setMat4("model", model);
        carShader.setMat4("model", carmodel);

        // Debug: print once that we're about to draw
        if (!printedDrawMessage)
        {
            std::cout << "[render debug] Drawing placed models..." << std::endl;
            printedDrawMessage = true;
        }

        // Draw all placed models using their stored baseModelMatrix. If a model is marked
        // movable, apply the runtime `carOffset` (left-multiplied so it translates in world space).
        if (!placedModels.empty())
        {
            for (const auto &pm : placedModels)
            {
                Shader *sh = (&CarModel == pm.model) ? &carShader : &ourShader;
                sh->use();
                glm::mat4 finalModel = pm.baseModelMatrix;
                if (pm.movable)
                {
                    finalModel = glm::translate(glm::mat4(1.0f), carOffset) * finalModel;
                }
                sh->setMat4("model", finalModel);
                // Updated Draw signature: provide model matrix and camera position for transparent sorting
                pm.model->Draw(*sh, finalModel, camera.Position);
            }
            // restore default shader state
            ourShader.use();
            ourShader.setMat4("model", model);
        }
        else
        {
            // fallback to direct draws if no placedModels present
            ourModel.Draw(ourShader, model, camera.Position);
            glm::mat4 carModelMat = glm::translate(glm::mat4(1.0f), -carBBoxCenter + glm::vec3(0.0f, -carBBoxSize.y * 0.5f, 0.0f) + carOffset);
            if (carBBoxDiag > 200.0f)
            {
                float scaleFactor = 200.0f / carBBoxDiag;
                carModelMat = glm::scale(carModelMat, glm::vec3(scaleFactor));
            }
            ourShader.setMat4("model", carModelMat);
            CarModel.Draw(ourShader, carModelMat, camera.Position);
            ourShader.setMat4("model", model);
        }

        // Check GL errors and optionally capture the framebuffer once for offline inspection
        // glCheck("after model draw");
        // Print model-control help periodically (user can disable by setting showModelControlHelp=false)
        if (showModelControlHelp)
        {
            static float lastHelpPrint = 0.0f;
            float t = glfwGetTime();
            if (t - lastHelpPrint > 3.0f)
            {
                std::cout << "Model controls: Arrow keys move CarModel on X/Z, PageUp/PageDown move Y, R resets car offset." << std::endl;
                lastHelpPrint = t;
            }
        }

        // Debug: print placed models' world-space origin positions (throttled)
        {
            static float lastModelPrint = 0.0f;
            float t = glfwGetTime();
            const float modelPrintInterval = 0.5f; // seconds
            if (t - lastModelPrint > modelPrintInterval)
            {
                lastModelPrint = t;
                if (!placedModels.empty())
                {
                    std::vector<glm::vec3> worldPositions;
                    worldPositions.reserve(placedModels.size());
                    for (size_t i = 0; i < placedModels.size(); ++i)
                    {
                        const auto &pm = placedModels[i];
                        glm::mat4 finalModel = pm.baseModelMatrix;
                        if (pm.movable)
                            finalModel = glm::translate(glm::mat4(1.0f), carOffset) * finalModel;
                        glm::vec4 wp = finalModel * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                        glm::vec3 worldPos = glm::vec3(wp);
                        worldPositions.push_back(worldPos);
                        std::cout << "[ModelPos] placedModels[" << i << "] ptr=" << pm.model << " movable=" << (pm.movable ? "YES" : "NO")
                                  << " worldPos=" << worldPos.x << "," << worldPos.y << "," << worldPos.z << std::endl;
                    }
                    // pairwise check: are any two models effectively at the same world position?
                    const float sameEps = 1e-3f; // distance threshold
                    bool anySame = false;
                    for (size_t a = 0; a < worldPositions.size(); ++a)
                    {
                        for (size_t b = a + 1; b < worldPositions.size(); ++b)
                        {
                            float d = glm::length(worldPositions[a] - worldPositions[b]);
                            if (d <= sameEps)
                            {
                                std::cout << "[ModelPos] placedModels[" << a << "] and placedModels[" << b << "] are at the SAME world location (d=" << d << ")" << std::endl;
                                anySame = true;
                            }
                        }
                    }
                    if (!anySame)
                        std::cout << "[ModelPos] All placed models are at different world locations." << std::endl;
                }
                else
                {
                    // If fallback drawing is used, print main model and car fallback positions
                    glm::vec4 mainWP = model * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    glm::vec4 carWP = carmodel * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    std::cout << "[ModelPos] (fallback) mainModel worldPos=" << mainWP.x << "," << mainWP.y << "," << mainWP.z << std::endl;
                    std::cout << "[ModelPos] (fallback) carModel worldPos=" << carWP.x << "," << carWP.y << "," << carWP.z << std::endl;
                    float d = glm::length(glm::vec3(mainWP) - glm::vec3(carWP));
                    if (d <= 1e-3f)
                        std::cout << "[ModelPos] (fallback) mainModel and carModel are at the SAME world location (d=" << d << ")" << std::endl;
                    else
                        std::cout << "[ModelPos] (fallback) mainModel and carModel are at different world locations (d=" << d << ")" << std::endl;
                }
            }
        }
        if (!debugCaptured)
        {
            if (const char *dc = std::getenv("DEBUG_CAPTURE"))
            {
                if (std::string(dc) == "1")
                {
                    glFinish();
                    int w = SCR_WIDTH;
                    int h = SCR_HEIGHT;
                    std::vector<unsigned char> pixels(w * h * 4);
                    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    // flip vertically
                    std::vector<unsigned char> flipped(w * h * 4);
                    for (int y = 0; y < h; ++y)
                    {
                        memcpy(&flipped[y * w * 4], &pixels[(h - 1 - y) * w * 4], w * 4);
                    }
                    const char *outPath = "frame_debug.png";
                    if (stbi_write_png(outPath, w, h, 4, flipped.data(), w * 4))
                    {
                        std::cout << "Saved framebuffer to: " << outPath << std::endl;
                    }
                    else
                    {
                        std::cerr << "Failed to save framebuffer to: " << outPath << std::endl;
                    }
                    // optionally exit after capture so you can inspect the file
                    std::cout << "DEBUG_CAPTURE done; exiting." << std::endl;
                    debugCaptured = true;
                    // terminate loop and program
                    glfwSwapBuffers(window);
                    glfwPollEvents();
                    glfwTerminate();
                    return 0;
                }
            }
        }

        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
    glfwTerminate();
    return 0;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.ProcessKeyboard(RIGHT, deltaTime);

    // Controls: arrows/PageUp/PageDown act on either camera or model depending on `controlModeModel`.
    // false = arrow keys move camera, true = arrow keys move CarModel.
    static bool h_was = false;
    static bool r_was = false;
    static bool m_was = false;
    float moveSpeed = 3.0f * deltaTime; // units per second scaled by frame
    if (!controlModeModel)
    {
        // arrow keys move camera in camera-mode
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
            camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
            camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
            camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            camera.ProcessKeyboard(RIGHT, deltaTime);
        // PageUp/PageDown adjust camera height (Y axis)
        if (glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS)
            camera.Position.y += moveSpeed;
        if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS)
            camera.Position.y -= moveSpeed;
    }
    else
    {
        // arrow keys move the CarModel in model-mode
        if (!carLocked)
        {
            if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
                carOffset.z -= moveSpeed;
            if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
                carOffset.z += moveSpeed;
            if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
                carOffset.x -= moveSpeed;
            if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
                carOffset.x += moveSpeed;
            if (glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS)
                carOffset.y += moveSpeed;
            if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS)
                carOffset.y -= moveSpeed;
        }
    }

    bool h_now = (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS);
    if (h_now && !h_was)
    {
        showModelControlHelp = !showModelControlHelp;
        std::cout << "Toggled model control help: " << (showModelControlHelp ? "ON" : "OFF") << std::endl;
    }
    h_was = h_now;

    bool r_now = (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS);
    if (r_now && !r_was)
    {
        carOffset = glm::vec3(3.0f, 0.0f, 0.0f);
        std::cout << "CarModel offset reset to " << carOffset.x << "," << carOffset.y << "," << carOffset.z << std::endl;
    }
    r_was = r_now;

    // Toggle control mode: M switches between camera (default) and model control
    bool m_now = (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS);
    if (m_now && !m_was)
    {
        controlModeModel = !controlModeModel;
        std::cout << "Control mode: " << (controlModeModel ? "MODEL (arrows move model)" : "CAMERA (arrows move camera)") << std::endl;
    }
    m_was = m_now;

    // Toggle lock for car model movement (L)
    static bool l_was = false;
    bool l_now = (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS);
    if (l_now && !l_was)
    {
        carLocked = !carLocked;
        std::cout << "CarModel movement " << (carLocked ? "LOCKED" : "UNLOCKED") << std::endl;
    }
    l_was = l_now;
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
}

// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void mouse_callback(GLFWwindow *window, double xposIn, double yposIn)
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
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    camera.ProcessMouseScroll(static_cast<float>(yoffset));
}
