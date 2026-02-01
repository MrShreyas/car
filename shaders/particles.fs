#version 330 core
out vec4 FragColor;

in vec4 ParticleColor;

void main()
{
    // Make it round?
    vec2 circ = 2.0 * gl_PointCoord - 1.0;
    if (dot(circ, circ) > 1.0) {
        discard;
    }
    FragColor = ParticleColor;
}
