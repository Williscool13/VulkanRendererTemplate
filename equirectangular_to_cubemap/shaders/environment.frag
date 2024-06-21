#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform samplerCube environmentMap;

const uint MAX_MIP_LEVEL = 9;

layout(push_constant) uniform PushConstants {
    float lod;
    float diffuseMipLevel;
	bool isDiffuse;
    float pad2;
} pushConstants;


void main()
{
    // sample environment map
	vec3 direction = normalize(fragPosition);
	//vec3 envColor = texture(environmentMap, direction).rgb;
	float test = 0.0;
	vec3 envColor = vec3(0.0);
	if (pushConstants.isDiffuse){
		envColor = textureLod(environmentMap, direction, pushConstants.diffuseMipLevel).rgb;
		test = pushConstants.diffuseMipLevel;
	} else {
		float low = floor(pushConstants.lod);
		float high = low + 1.0;
		if (low >= pushConstants.diffuseMipLevel){
			low += 1;
		}
		if (high >= pushConstants.diffuseMipLevel){
			high += 1;
		}

		float frac = fract(pushConstants.lod);

		envColor = mix(textureLod(environmentMap, direction, low).rgb, textureLod(environmentMap, direction, high).rgb, frac);
		test = mix(low, high, frac);
	}
	
    
	// HDR to sRGB
	envColor = envColor / (envColor + vec3(1.0));
	envColor = pow(envColor, vec3(1.0 / 2.2));

    outColor = vec4(vec3(test / 9.0f), 1.0);
}