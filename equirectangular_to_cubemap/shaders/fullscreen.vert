#version 450

layout(location = 0) out vec2 TexCoord;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0), // Bottom-left corner
        vec2( 3.0, -1.0), // Far right of the screen, which combined with the third vertex forms a full-screen quad
        vec2(-1.0,  3.0)  // Far top of the screen, which combined with the other vertices forms a full-screen quad
    );
    
    vec2 pos = positions[gl_VertexIndex];

    TexCoord = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}