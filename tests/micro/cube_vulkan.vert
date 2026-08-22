#version 450
layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform PushConstants {
    vec2 uRotation;
    float uAspect;
    float padding;
} pc;

const vec3 kCubePositions[36] = vec3[36](
    // Front Face (Neon Orange)
    vec3(-0.4, -0.4,  0.4), vec3( 0.4, -0.4,  0.4), vec3( 0.4,  0.4,  0.4),
    vec3( 0.4,  0.4,  0.4), vec3(-0.4,  0.4,  0.4), vec3(-0.4, -0.4,  0.4),
    // Back Face (Cyan)
    vec3(-0.4, -0.4, -0.4), vec3(-0.4,  0.4, -0.4), vec3( 0.4,  0.4, -0.4),
    vec3( 0.4,  0.4, -0.4), vec3( 0.4, -0.4, -0.4), vec3(-0.4, -0.4, -0.4),
    // Top Face (Lime Green)
    vec3(-0.4,  0.4, -0.4), vec3(-0.4,  0.4,  0.4), vec3( 0.4,  0.4,  0.4),
    vec3( 0.4,  0.4,  0.4), vec3( 0.4,  0.4, -0.4), vec3(-0.4,  0.4, -0.4),
    // Bottom Face (Gold Yellow)
    vec3(-0.4, -0.4, -0.4), vec3( 0.4, -0.4, -0.4), vec3( 0.4, -0.4,  0.4),
    vec3( 0.4, -0.4,  0.4), vec3(-0.4, -0.4,  0.4), vec3(-0.4, -0.4, -0.4),
    // Right Face (Hot Magenta)
    vec3( 0.4, -0.4, -0.4), vec3( 0.4,  0.4, -0.4), vec3( 0.4,  0.4,  0.4),
    vec3( 0.4,  0.4,  0.4), vec3( 0.4, -0.4,  0.4), vec3( 0.4, -0.4, -0.4),
    // Left Face (Emerald Teal)
    vec3(-0.4, -0.4, -0.4), vec3(-0.4, -0.4,  0.4), vec3(-0.4,  0.4,  0.4),
    vec3(-0.4,  0.4,  0.4), vec3(-0.4,  0.4, -0.4), vec3(-0.4, -0.4, -0.4)
);

const vec3 kCubeColors[6] = vec3[6](
    vec3(1.0, 0.25, 0.05), // Front: Neon Orange
    vec3(0.0, 0.65, 1.0),  // Back: Cyan
    vec3(0.15, 1.0, 0.2),  // Top: Neon Lime Green
    vec3(1.0, 0.85, 0.0),  // Bottom: Gold Yellow
    vec3(1.0, 0.0, 0.85),  // Right: Hot Magenta
    vec3(0.0, 0.9, 0.65)   // Left: Emerald Teal
);

void main() {
    int face = gl_VertexIndex / 6;
    fragColor = kCubeColors[face % 6];

    vec3 pos = kCubePositions[gl_VertexIndex % 36];
    float cx = cos(pc.uRotation.x);
    float sx = sin(pc.uRotation.x);
    float cy = cos(pc.uRotation.y);
    float sy = sin(pc.uRotation.y);

    vec3 p1 = vec3(pos.x * cy + pos.z * sy, pos.y, -pos.x * sy + pos.z * cy);
    vec3 p2 = vec3(p1.x, p1.y * cx - p1.z * sx, p1.y * sx + p1.z * cx);

    float asp = (pc.uAspect > 0.05) ? pc.uAspect : 0.462f;
    gl_Position = vec4(p2.x * 1.5, -p2.y * 1.5 * asp, 0.5, 1.0);
}
