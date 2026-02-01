#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <Shader.h>

struct Particle {
    glm::vec3 Position;
    glm::vec3 Velocity;
    glm::vec4 Color;
    float Life;
};

class ParticleSystem {
public:
    ParticleSystem(Shader* shader);
    ~ParticleSystem();

    void Update(float deltaTime);
    void Draw(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos);
    
    void Emit(const glm::vec3& pos, const glm::vec3& vel, const glm::vec4& color, float life);

private:
    std::vector<Particle> m_Particles;
    Shader* m_Shader;
    
    unsigned int m_VAO, m_VBO;
    void initRenderData();
};
