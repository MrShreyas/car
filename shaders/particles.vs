#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;

out vec4 ParticleColor;

uniform mat4 view;
uniform mat4 projection;

void main()
{
    ParticleColor = aColor;
    gl_Position = projection * view * vec4(aPos, 1.0);
    gl_PointSize = 10.0; // Fixed size smoke puff
}
