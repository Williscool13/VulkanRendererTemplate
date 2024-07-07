const uint MAX_MIP_LEVEL = 9;
const uint DIFF_IRRA_MIP_LEVEL = 5;
const bool FLIP_ENVIRONMENT_MAP_Y = true;

const float MAX_REFLECTION_LOD = 4.0;


layout(set = 3, binding = 0) uniform samplerCube environmentDiffuseAndSpecular;
layout(set = 3, binding = 1) uniform sampler2D lut;


vec3 SpecularPrefiltered(vec3 V, vec3 N, float roughness)
{
	float r = MAX_MIP_LEVEL * roughness;
	vec3 ENV_N = N;
	if (FLIP_ENVIRONMENT_MAP_Y) { ENV_N.y = -ENV_N.y; }

	vec3 R = reflect(-V, N);
	vec3 prefilteredColor = textureLod(environmentDiffuseAndSpecular, R, roughness * MAX_REFLECTION_LOD).rgb;
	return prefilteredColor;
	//float low = floor(r);
	//float high = low + 1.0;
	//float frac = fract(r);
	//return mix(textureLod(environmentDiffuseAndSpecular, ENV_N, low).rgb, textureLod(environmentDiffuseAndSpecular, ENV_N, high).rgb, frac);
}

vec3 DiffuseIrradiance(vec3 N)
{
	vec3 ENV_N = N;
	if (FLIP_ENVIRONMENT_MAP_Y) { ENV_N.y = -ENV_N.y; }
	return textureLod(environmentDiffuseAndSpecular, ENV_N, DIFF_IRRA_MIP_LEVEL).rgb;
}