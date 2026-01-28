#include <Application.h>
#include <iostream>

// Static callback wrappers
static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
static void mouse_callback(GLFWwindow* window, double xpos, double ypos);
static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

Application* g_CurrentApp = nullptr;

Application::Application(const std::string& title, int width, int height)
    : m_Title(title), m_Width(width), m_Height(height), m_Window(nullptr)
{
    g_CurrentApp = this;
}

Application::~Application()
{
    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
    }
    glfwTerminate();
}

bool Application::Initialize()
{
    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_Window = glfwCreateWindow(m_Width, m_Height, m_Title.c_str(), NULL, NULL);
    if (!m_Window)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(m_Window);
    glfwSetFramebufferSizeCallback(m_Window, framebuffer_size_callback);
    glfwSetCursorPosCallback(m_Window, mouse_callback);
    glfwSetScrollCallback(m_Window, scroll_callback);

    // tell GLFW to capture our mouse
    glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    // Get actual framebuffer size right away
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(m_Window, &fbWidth, &fbHeight);
    framebuffer_size_callback(m_Window, fbWidth, fbHeight);

    OnInit();
    return true;
}

void Application::Run()
{
    while (!glfwWindowShouldClose(m_Window))
    {
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - m_LastFrameTime;
        m_LastFrameTime = currentFrame;

        OnProcessInput(deltaTime);
        OnUpdate(deltaTime);
        OnRender();

        glfwSwapBuffers(m_Window);
        glfwPollEvents();
    }
}

// Callbacks
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
    if (g_CurrentApp) g_CurrentApp->OnResize(width, height);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (g_CurrentApp) g_CurrentApp->OnMouseMove(xpos, ypos);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (g_CurrentApp) g_CurrentApp->OnMouseScroll(xoffset, yoffset);
}
