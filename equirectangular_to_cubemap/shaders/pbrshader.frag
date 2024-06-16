#version 460	

#extension GL_EXT_buffer_reference : require
#extension GL_GOOGLE_include_directive : require
#include "indirect_input_structures.glsl"

// All calculations are done in world space
layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec2 inUV;
layout (location = 4) flat in uint inMaterialIndex;
layout (location = 0) out vec4 outFragColor;



layout(set = 2, binding = 0) uniform sampler samplers[32];
layout(set = 2, binding = 1) uniform texture2D textures[255];




vec3 lambert(vec3 kD, vec3 albedo)
{
	return kD * albedo / 3.14159265359;
};

float D_GGX(vec3 N, vec3 H, float roughness){
	// Brian Karis says GGX NDF has a longer "tail" that appears more natural
	float a = roughness * roughness;
	float a2 = a * a; // disney reparameterization
	float NdotH = max(dot(N, H), 0.0f);
	float NdotH2 = NdotH * NdotH;

	float num = a2;
	float premult = NdotH2 * (a2 - 1.0f) + 1.0f;
	float denom = 3.14159265359 * premult * premult;
	return num / denom;
};

float G_SCHLICKGGX(float NdotX, float k){
	float num = NdotX;
	float denom = NdotX * (1.0f - k) + k;
	return num / denom;
};


float G_SCHLICKGGX_SMITH(vec3 N, vec3 V, vec3 L, float roughness){
	// height correlated smith method

	// "Disney reduced hotness" - Brian Karis
	float r = (roughness + 1);
	float k = (r * r) / 8.0f;

	float NDotV = max(dot(N, V), 0.0f);
	float NDotL = max(dot(N, L), 0.0f);

	float ggx2 = G_SCHLICKGGX(NDotV, k);
	float ggx1 = G_SCHLICKGGX(NDotL, k);
	return ggx1 * ggx2;
};

float unreal_fresnel_power(vec3 V, vec3 H){
	float VDotH = dot(V, H);
	return (-5.55473 * VDotH - 6.98316) * VDotH;
	
};

vec3 F_SCHLICK(vec3 V, vec3 H, vec3 F0){
	float VdotH = max(dot(V, H), 0.0f);

	// classic
	//return F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);

	// unreal optimized
	return F0 + (1 - F0) * pow(2, unreal_fresnel_power(V, H));
}


void main() 
{
	Material m = buffer_addresses.materialBufferDeviceAddress.materials[inMaterialIndex];
	uint colorSamplerIndex = uint(m.texture_sampler_indices.x);
	uint colorImageIndex =	 uint(m.texture_image_indices.x);
	vec4 _col = texture(sampler2D(textures[colorImageIndex], samplers[colorSamplerIndex]), inUV);
	
	uint metalSamplerIndex = uint(m.texture_sampler_indices.y);
	uint metalImageIndex = uint(m.texture_image_indices.y);
	vec4 _metal_rough_sample = texture(sampler2D(textures[metalImageIndex], samplers[metalSamplerIndex]), inUV);

	
	_col *= m.color_factor; 

	if (_col.w < m.alpha_cutoff.w) { discard; }

	vec3 albedo = inColor * _col.xyz;
	float metallic = _metal_rough_sample.b * m.metal_rough_factors.x;
	float roughness = _metal_rough_sample.g * m.metal_rough_factors.y;
	//vec3 metal = texture(sampler2D(metalI, metalS), inUV).xyz;

	vec3 ambient = albedo * sceneData.ambientColor.xyz;


	//outFragColor = vec4(color * lightValue *  sceneData.sunlightColor.w + ambient , _col.w);
	
	outFragColor = vec4(vec3(m.metal_rough_factors.x), 1.0f);

	vec3 light_color = sceneData.sunlightColor.xyz * sceneData.sunlightColor.w;
	vec3 N = normalize(inNormal);
	vec3 V = normalize(sceneData.cameraPos.xyz - inPosition);
	vec3 L = normalize(sceneData.sunlightDirection.xyz); // for point lights, light.pos - inPosition
	vec3 H = normalize(V + L);
	
	// SPECULAR
	float NDF = D_GGX(N, H, roughness);
	float G = G_SCHLICKGGX_SMITH(N, V, L, roughness);
	vec3 F0 = mix(vec3(0.04), albedo, metallic);
	vec3 F = F_SCHLICK(V, H, F0);

	vec3 numerator = NDF * G * F;
	float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f);
	vec3 specular = numerator / max(denominator, 0.001f);
	

	vec3 kS = F;
	vec3 kD = vec3(1.0f) - kS;
	kD *= 1.0f - metallic;

	// DIFFUSE
	float nDotL = max(dot(N, L), 0.0f);
	vec3 diffuse = lambert(kD, albedo);

	vec3 final_color = (diffuse + specular) * light_color * nDotL;
	final_color += ambient;

	vec3 corrected_final_color = final_color / (final_color + vec3(1.0f)); // Reinhard
	corrected_final_color = pow(corrected_final_color, vec3(1.0f / 2.2f)); // gamma correction

	//outFragColor = vec4(final_color, _col.w); 
	outFragColor = vec4(corrected_final_color, _col.w);

	// cool debug to show distance from camera!
	//outFragColor = vec4(vec3(distance(inPosition, sceneData.cameraPos.xyz) / 100.0f), 1.0f);
} ;