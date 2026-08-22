#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform PushConstants {
    vec2 uRotation;
    float uAspect;
    float padding;
} pc;

void main() {
    fragColor = inColor;

    float cx = cos(pc.uRotation.x);
    float sx = sin(pc.uRotation.x);
    float cy = cos(pc.uRotation.y);
    float sy = sin(pc.uRotation.y);

    // Xoay quanh trục Y
    vec3 p1 = vec3(
        inPos.x * cy + inPos.z * sy,
        inPos.y,
        -inPos.x * sy + inPos.z * cy
    );

    // Xoay quanh trục X
    vec3 p2 = vec3(
        p1.x,
        p1.y * cx - p1.z * sx,
        p1.y * sx + p1.z * cx
    );

    float asp = (pc.uAspect > 0.05) ? pc.uAspect : 0.462f;
    float z = p2.z + 2.5; // Phối cảnh Perspective tự nhiên
    gl_Position = vec4(p2.x, -p2.y * asp, p2.z * 0.4 + 1.0, z);
}
