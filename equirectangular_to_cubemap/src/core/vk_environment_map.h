#pragma once
#include "big_header.h"
#include "vk_types.h"
#include "engine.h"

#include "vk_descriptors.h"
#include "vk_initializers.h"
#include "vk_descriptor_buffer.h"

#include <stb_image/stb_image.h>
#include <stb_image/stb_image_write.h>


struct EquiToCubePushConstantData {
	bool flipY;
	float pad;
	float pad2;
	float pad3;
};

class EnvironmentMap {
public:
	static const int specularPrefilteredMipLevels{ 10 };
	static const VkExtent3D specularPrefilteredBaseExtents;
	static const VkExtent3D lutImageExtent;
	static const int diffuseIrradianceMipLevel{ 5 };
	static const char* defaultEquiPath;


	EnvironmentMap(MainEngine* creator);
	~EnvironmentMap();

	// init sampler
	bool load_equirectangular_image(const char* equiPath, bool firstTimeSetup);
	void load_cubemap(bool firstTimeSetup);
	void create_cubemap_image(bool firstTimeSetup);

	void save_cubemap_image(const char* path);
	void save_lut_image(const char* path);



	bool flip_y{ false };

	float diffuse_sample_delta{ 0.025f };
	int specular_sample_count{ 2048};


	static bool layoutsCreated;
	static int useCount;
	static VkDescriptorSetLayout _equiImageDescriptorSetLayout;
	static VkDescriptorSetLayout _cubemapStorageDescriptorSetLayout;
	static VkDescriptorSetLayout _cubemapDescriptorSetLayout;
	static VkDescriptorSetLayout _lutDescriptorSetLayout;

	static VkDescriptorSetLayout _environmentMapDescriptorSetLayout; // contains 2 samplers -> diffuse/spec and lut

	DescriptorBufferSampler get_equi_image_descriptor_buffer() const { return _equiImageDescriptorBuffer; }
	DescriptorBufferSampler get_cubemap_descriptor_buffer() const { return _cubemapDescriptorBuffer; }

	AllocatedImage get_cubemap_image() const { return _cubemapImage; }
	AllocatedImage get_spec_diff_cubemap() const { return _specDiffCubemap; }
	AllocatedImage get_lut_image() const { return _lutImage; }
private:
	MainEngine* _creator;
	VkDevice _device;
	VmaAllocator _allocator;

	
	DescriptorBufferSampler _equiImageDescriptorBuffer;
	// the 2 below are identical, but one is for storage and one is for sampled
	DescriptorBufferSampler _cubemapStorageDescriptorBuffer;
	DescriptorBufferSampler _cubemapDescriptorBuffer;

	static DescriptorBufferSampler _lutDescriptorBuffer;


	DescriptorBufferSampler _environmentMapDescriptorBuffer;

	// Pipelines
	static VkPipelineLayout _equiToCubemapPipelineLayout;
	static VkPipeline _equiToCubemapPipeline;
	
	static VkPipelineLayout _cubemapToDiffusePipelineLayout;
	static VkPipeline _cubemapToDiffusePipeline;
	static VkPipelineLayout _cubemapToSpecularPipelineLayout;
	static VkPipeline _cubemapToSpecularPipeline;

	// Hardcoded LUT generation
	static VkPipelineLayout _lutPipelineLayout;
	static VkPipeline _lutPipeline;




	AllocatedImage _equiImage;
	AllocatedImage _cubemapImage;
	AllocatedImage _specDiffCubemap;
	//AllocatedCubemap _specDiffCubemap; // diffuse irradiance is at mip 5
	static AllocatedImage _lutImage; // same for all environment maps

	VkSampler _sampler;

	std::string _equiPath;
	uint32_t _cubemapResolution{ 1024 };


	void equi_to_cubemap_immediate();
	void cubemap_to_difffuse_specular_immediate(AllocatedCubemap& cubemapMips);
	void generate_lut_immediate();
};
