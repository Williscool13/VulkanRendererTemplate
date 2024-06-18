#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform samplerCube environmentMap;

const uint MAX_MIP_LEVEL = 9;

layout(push_constant) uniform PushConstants {
    float lod;
    float pad;
    float pad1;
    float pad2;
} pushConstants;


void main()
{
    // sample environment map
	vec3 direction = normalize(fragPosition);
	//vec3 envColor = texture(environmentMap, direction).rgb;
	vec3 envColor = textureLod(environmentMap, direction, pushConstants.lod).rgb;
	
    
	// HDR to sRGB
	envColor = envColor / (envColor + vec3(1.0));
	envColor = pow(envColor, vec3(1.0 / 2.2));

    outColor = vec4(envColor, 1.0);
}