#version 450



vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0), // Bottom-left corner
        vec2( 3.0, -1.0), // Far right of the screen, which combined with the third vertex forms a full-screen quad
        vec2(-1.0,  3.0)  // Far top of the screen, which combined with the other vertices forms a full-screen quad
);

layout(location = 0) out vec3 fragPosition;

void main() {
    vec2 pos = positions[gl_VertexIndex];

    gl_Position = vec4(pos, 0.0, 1.0);
    fragPosition = vec3(pos, 0.99999);
}