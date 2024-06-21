#pragma once
#include "big_header.h"
#include "vk_types.h"
#include "engine.h"

#include "vk_descriptors.h"
#include "vk_initializers.h"
#include "vk_descriptor_buffer.h"

#include <stb_image/stb_image.h>

class EnvironmentMap {
public:
	static const int specularPrefilteredMipLevels{ 10 };
	static const VkExtent3D specularPrefilteredBaseExtents;
	static const int diffuseIrradianceMipLevel{ 5 };
	static const char* defaultEquiPath;


	EnvironmentMap(MainEngine* creator);
	~EnvironmentMap();

	// init sampler
	bool load_equirectangular_image(const char* equiPath, bool firstTimeSetup);
	void load_cubemap(bool firstTimeSetup);
	void create_cubemap_image(bool firstTimeSetup);


	// getters
	DescriptorBufferSampler get_equi_image_descriptor_buffer() const { return _equiImageDescriptorBuffer; }
	DescriptorBufferSampler get_cubemap_descriptor_buffer() const { return _cubemapDescriptorBuffer; }

private:
	MainEngine* _creator;
	VkDevice _device;
	VmaAllocator _allocator;

	static bool layoutsCreated;
	static VkDescriptorSetLayout _equiImageDescriptorSetLayout;
	static VkDescriptorSetLayout _cubemapStorageDescriptorSetLayout;
	static VkDescriptorSetLayout _cubemapDescriptorSetLayout;
	DescriptorBufferSampler _equiImageDescriptorBuffer;
	// [STORAGE] 0 is raw cubemap, 1 is irradiance, 2 is prefiltered
	DescriptorBufferSampler _cubemapStorageDescriptorBuffer;
	// [SAMPLED] 0 is raw cubemap, 1 is irradiance, 2 is prefiltered
	DescriptorBufferSampler _cubemapDescriptorBuffer;


	AllocatedImage _equiImage;
	AllocatedImage _cubemapImage;
	AllocatedImage _specDiffCubemap;
	//AllocatedCubemap _specDiffCubemap; // diffuse irradiance is at mip 5

	VkSampler _sampler;

	std::string _equiPath;
	float _cubemapResolution{ 1024.0f };

};
