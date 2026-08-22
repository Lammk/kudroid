#version 450
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

layout(location = 0) out vec3 vColor;

void main() {
    vColor = aColor;
    // Vẽ trực tiếp tọa độ NDC cực đại chiếm giữa màn hình
    gl_Position = vec4(aPosition.x * 1.5, aPosition.y * 1.5, 0.0, 1.0);
}
