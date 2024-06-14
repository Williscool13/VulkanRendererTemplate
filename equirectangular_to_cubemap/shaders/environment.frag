#version 450

// Input from the vertex shader
layout(location = 0) in vec3 fragPosition;

// Cubemap sampler
layout(set = 0, binding = 0) uniform samplerCube environmentMap;

// Output color
layout(location = 0) out vec4 outColor;


layout(set = 1, binding = 0) uniform GlobalUniform {
	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
	vec4 cameraPos;
} sceneData;

void main()
{
    // sample environment map
	vec3 direction = normalize(fragPosition);// - sceneData.cameraPos.xyz);
	vec3 envColor = texture(environmentMap, direction).rgb;
    
	// HDR to sRGB
	envColor = pow(envColor, vec3(1.0 / 2.2));

    outColor = vec4(envColor, 1.0);
}