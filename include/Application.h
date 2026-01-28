#ifndef APPLICATION_H
#define APPLICATION_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>

class Application
{
public:
    Application(const std::string& title, int width, int height);
    virtual ~Application();

    bool Initialize();
    void Run();
    
    // Virtual methods for subclasses to implement
    virtual void OnInit() {}
    virtual void OnUpdate(float deltaTime) {}
    virtual void OnRender() {}
    virtual void OnProcessInput(float deltaTime) {}
    virtual void OnResize(int width, int height) {}
    virtual void OnMouseMove(double xpos, double ypos) {}
    virtual void OnMouseScroll(double xoffset, double yoffset) {}

    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    GLFWwindow* GetWindow() const { return m_Window; }

protected:
    std::string m_Title;
    int m_Width;
    int m_Height;
    GLFWwindow* m_Window;
    
    float m_LastFrameTime = 0.0f;
};

#endif
