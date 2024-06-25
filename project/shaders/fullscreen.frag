#version 450

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D sourceImage;

void main() {
    FragColor = texture(sourceImage, TexCoord);
}