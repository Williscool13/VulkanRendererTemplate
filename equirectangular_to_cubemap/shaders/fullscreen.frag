#version 450

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D sourceImage;

void main() {
    vec3 color = texture(sourceImage, TexCoord).rgb;
    vec3 corrected_color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(corrected_color, 1.0);
}