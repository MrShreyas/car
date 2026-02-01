#include "ParticleSystem.h"
#include <glad/glad.h>
#include <vector>

ParticleSystem::ParticleSystem(Shader* shader) : m_Shader(shader)
{
    initRenderData();
}

ParticleSystem::~ParticleSystem()
{
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
}

void ParticleSystem::Emit(const glm::vec3& pos, const glm::vec3& vel, const glm::vec4& color, float life)
{
    Particle p;
    p.Position = pos;
    p.Velocity = vel;
    p.Color = color;
    p.Life = life;
    m_Particles.push_back(p);
}

void ParticleSystem::Update(float deltaTime)
{
    for (auto it = m_Particles.begin(); it != m_Particles.end(); )
    {
        it->Life -= deltaTime;
        if (it->Life <= 0.0f)
        {
            it = m_Particles.erase(it);
        }
        else
        {
            it->Position += it->Velocity * deltaTime;
            it->Velocity.y += 0.5f * deltaTime; // Smoke rises
            it->Color.a = it->Life; // Fade out
            ++it;
        }
    }
}

void ParticleSystem::Draw(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos)
{
    // Minimal Billboard Renderer
    // For simplicity, we'll assume the shader handles billboard calculation or we send quads.
    // Actually, let's just send POINTS and let the Geometry Shader expand them? 
    // Or easier: Standard CPU-side billboarding for now? No that's slow.
    // Let's use camera-facing logic in logic or shader.
    // Simplest: Render small cubes or quads.
    
    // We will update VBO each frame for this simple demo system.
    // Format: Pos (3), Color (4)
    std::vector<float> data;
    for (const auto& p : m_Particles) {
        data.push_back(p.Position.x);
        data.push_back(p.Position.y);
        data.push_back(p.Position.z);
        data.push_back(p.Color.r);
        data.push_back(p.Color.g);
        data.push_back(p.Color.b);
        data.push_back(p.Color.a);
    }
    
    if (data.empty()) return;

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_DYNAMIC_DRAW);
    
    // Draw as points, and let shader handle size/appearance?
    // User asked for "Skid maks, particles". 
    // Let's use GL_POINTS with glPointSize(10.0f) for smoke puffs. Easiest.
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    m_Shader->use();
    m_Shader->setMat4("view", view);
    m_Shader->setMat4("projection", projection);
    
    glDrawArrays(GL_POINTS, 0, (GLsizei)m_Particles.size());
    
    glDisable(GL_BLEND);
}

void ParticleSystem::initRenderData()
{
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    
    // Pos (3) + Color (4)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glBindVertexArray(0);
}
