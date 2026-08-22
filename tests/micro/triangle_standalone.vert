#version 450
layout(location = 0) out vec3 fragColor;

const vec2 positions[3] = vec2[3](
    vec2( 0.0, -0.7),
    vec2( 0.7,  0.7),
    vec2(-0.7,  0.7)
);

const vec3 colors[3] = vec3[3](
    vec3(1.0, 0.0, 0.0), // Pure Red
    vec3(0.0, 1.0, 0.0), // Pure Green
    vec3(0.0, 0.0, 1.0)  // Pure Blue
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
