#include "vk_environment_map.h"


const VkExtent3D EnvironmentMap::specularPrefilteredBaseExtents = { 512, 512, 1 };
const VkExtent3D EnvironmentMap::lutImageExtent = { 512, 512, 1 };
const char* EnvironmentMap::defaultEquiPath = "src_images/dam_bridge_4k.hdr";

int EnvironmentMap::useCount = 0;
VkDescriptorSetLayout EnvironmentMap::_equiImageDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout EnvironmentMap::_cubemapStorageDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout EnvironmentMap::_cubemapDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout EnvironmentMap::_lutDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout EnvironmentMap::_environmentMapDescriptorSetLayout = VK_NULL_HANDLE;

VkPipelineLayout EnvironmentMap::_equiToCubemapPipelineLayout = VK_NULL_HANDLE;
VkPipeline EnvironmentMap::_equiToCubemapPipeline = VK_NULL_HANDLE;
VkPipelineLayout EnvironmentMap::_cubemapToDiffusePipelineLayout = VK_NULL_HANDLE;
VkPipeline EnvironmentMap::_cubemapToDiffusePipeline = VK_NULL_HANDLE;
VkPipelineLayout EnvironmentMap::_cubemapToSpecularPipelineLayout = VK_NULL_HANDLE;
VkPipeline EnvironmentMap::_cubemapToSpecularPipeline = VK_NULL_HANDLE;
VkPipelineLayout EnvironmentMap::_lutPipelineLayout = VK_NULL_HANDLE;
VkPipeline EnvironmentMap::_lutPipeline = VK_NULL_HANDLE;
DescriptorBufferSampler EnvironmentMap::_lutDescriptorBuffer = DescriptorBufferSampler();
DescriptorBufferSampler EnvironmentMap::_lutSamplerDescriptorBuffer = DescriptorBufferSampler();

AllocatedImage EnvironmentMap::_lutImage = {};

bool EnvironmentMap::layoutsCreated = false;


EnvironmentMap::EnvironmentMap(MainEngine* creator)
{
	_creator = creator;
	_device = creator->_device;
	_allocator = creator->_allocator;

	VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	sampl.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampl.minLod = 0;
	sampl.maxLod = VK_LOD_CLAMP_NONE;
	sampl.magFilter = VK_FILTER_LINEAR;
	sampl.minFilter = VK_FILTER_LINEAR;

	vkCreateSampler(_device, &sampl, nullptr, &_sampler);

	if (!layoutsCreated) {
		// initializes static pipelines/layouts
		fmt::print("Layouts not created yet, doing first time setup\n");
		//  Equirectangular Image
		{
			DescriptorLayoutBuilder layoutBuilder;
			layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
			_equiImageDescriptorSetLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

		}
		//  STORAGE cubemaps - 0 is equi, 1 is diff irr, 2 is spec pref, 3 to 13 is for 10 mip levels of spec pref
		{
			DescriptorLayoutBuilder layoutBuilder;
			layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
			_cubemapStorageDescriptorSetLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT
				, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);
		}
		//  SAMPLER cubemaps
		{
			DescriptorLayoutBuilder layoutBuilder;
			layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
			_cubemapDescriptorSetLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
				, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

		}
		// LUT generation
		{
			DescriptorLayoutBuilder layoutBuilder;
			layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

			_lutDescriptorSetLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT
				, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

		}

		// Full Cubemap Descriptor
		{
			DescriptorLayoutBuilder layoutBuilder;
			layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // diffuse/spec
			layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // lut
			_environmentMapDescriptorSetLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT
				, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);
		}


		// equi to cubemap pipeline
		{
			VkPushConstantRange pushConstantRange = {};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(EquiToCubePushConstantData);

			VkDescriptorSetLayout layouts[]{ _equiImageDescriptorSetLayout, _cubemapStorageDescriptorSetLayout };

			VkPipelineLayoutCreateInfo layout_info{};
			layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layout_info.setLayoutCount = 2;
			layout_info.pSetLayouts = layouts;
			layout_info.pushConstantRangeCount = 1;
			layout_info.pPushConstantRanges = &pushConstantRange;

			VK_CHECK(vkCreatePipelineLayout(_device, &layout_info, nullptr, &_equiToCubemapPipelineLayout));


			VkShaderModule computeShader;
			if (!vkutil::load_shader_module("shaders/equitoface.comp.spv", _device, &computeShader)) {
				fmt::print("Error when building the compute shader (equitoface.comp.spv)\n"); abort();
			}

			VkPipelineShaderStageCreateInfo stageinfo{};
			stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageinfo.pNext = nullptr;
			stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			stageinfo.module = computeShader;
			stageinfo.pName = "main"; // entry point in shader

			VkComputePipelineCreateInfo computePipelineCreateInfo{};
			computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			computePipelineCreateInfo.pNext = nullptr;
			computePipelineCreateInfo.layout = _equiToCubemapPipelineLayout;
			computePipelineCreateInfo.stage = stageinfo;
			computePipelineCreateInfo.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

			VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_equiToCubemapPipeline));

			vkDestroyShaderModule(_device, computeShader, nullptr);
		}

		// cubemap to diffuse pipeline
		{
			VkDescriptorSetLayout layouts[]{ _cubemapDescriptorSetLayout, _cubemapStorageDescriptorSetLayout };

			VkPushConstantRange pushConstantRange = {};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(CubeToDiffusePushConstantData);

			VkPipelineLayoutCreateInfo layout_info{};
			layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layout_info.setLayoutCount = 2;
			layout_info.pSetLayouts = layouts;
			layout_info.pushConstantRangeCount = 1;
			layout_info.pPushConstantRanges = &pushConstantRange;

			VK_CHECK(vkCreatePipelineLayout(_device, &layout_info, nullptr, &_cubemapToDiffusePipelineLayout));

			VkShaderModule computeShader;
			if (!vkutil::load_shader_module("shaders/cubetodiffspec.comp.spv", _device, &computeShader)) {
				fmt::print("Error when building the compute shader (cubetodiffspec.comp.spv)\n"); abort();
			}

			VkPipelineShaderStageCreateInfo stageinfo{};
			stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageinfo.pNext = nullptr;
			stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			stageinfo.module = computeShader;
			stageinfo.pName = "main"; // entry point in shader

			VkComputePipelineCreateInfo computePipelineCreateInfo{};
			computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			computePipelineCreateInfo.pNext = nullptr;
			computePipelineCreateInfo.layout = _cubemapToDiffusePipelineLayout;
			computePipelineCreateInfo.stage = stageinfo;
			computePipelineCreateInfo.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

			VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_cubemapToDiffusePipeline));

			vkDestroyShaderModule(_device, computeShader, nullptr);
		}

		// cubemap to specular pipeline
		{
			VkDescriptorSetLayout layouts[]{ _cubemapDescriptorSetLayout, _cubemapStorageDescriptorSetLayout };

			VkPushConstantRange pushConstantRange = {};
			pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(CubeToPrefilteredConstantData);

			VkPipelineLayoutCreateInfo layout_info{};
			layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layout_info.setLayoutCount = 2;
			layout_info.pSetLayouts = layouts;
			layout_info.pushConstantRangeCount = 1;
			layout_info.pPushConstantRanges = &pushConstantRange;

			VK_CHECK(vkCreatePipelineLayout(_device, &layout_info, nullptr, &_cubemapToSpecularPipelineLayout));

			VkShaderModule computeShader;
			if (!vkutil::load_shader_module("shaders/cubetospecprefilter.comp.spv", _device, &computeShader)) {
				fmt::print("Error when building the compute shader (cubetospecprefilter.comp.spv)\n"); abort();
			}

			VkPipelineShaderStageCreateInfo stageinfo{};
			stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageinfo.pNext = nullptr;
			stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			stageinfo.module = computeShader;
			stageinfo.pName = "main"; // entry point in shader

			VkComputePipelineCreateInfo computePipelineCreateInfo{};
			computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			computePipelineCreateInfo.pNext = nullptr;
			computePipelineCreateInfo.layout = _cubemapToSpecularPipelineLayout;
			computePipelineCreateInfo.stage = stageinfo;
			computePipelineCreateInfo.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

			VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_cubemapToSpecularPipeline));

			vkDestroyShaderModule(_device, computeShader, nullptr);
		}

		// lut generation pipeline	
		{
			VkDescriptorSetLayout layouts[1]{ _lutDescriptorSetLayout };

			VkPipelineLayoutCreateInfo layout_info{};
			layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			layout_info.setLayoutCount = 1;
			layout_info.pSetLayouts = layouts;
			layout_info.pushConstantRangeCount = 0;
			layout_info.pPushConstantRanges = nullptr;

			VK_CHECK(vkCreatePipelineLayout(_device, &layout_info, nullptr, &_lutPipelineLayout));

			VkShaderModule computeShader;
			if (!vkutil::load_shader_module("shaders/brdflut.comp.spv", _device, &computeShader)) {
				fmt::print("Error when building the compute shader (brdflut.comp.spv)\n"); abort();
			}

			VkPipelineShaderStageCreateInfo stageinfo{};
			stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageinfo.pNext = nullptr;
			stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			stageinfo.module = computeShader;
			stageinfo.pName = "main"; // entry point in shader

			VkComputePipelineCreateInfo computePipelineCreateInfo{};
			computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			computePipelineCreateInfo.pNext = nullptr;
			computePipelineCreateInfo.layout = _lutPipelineLayout;
			computePipelineCreateInfo.stage = stageinfo;
			computePipelineCreateInfo.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

			VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_lutPipeline));

			vkDestroyShaderModule(_device, computeShader, nullptr);
		}

		// Create LUT here, because its the same for all environment maps
		{
			_lutDescriptorBuffer = DescriptorBufferSampler(creator->_instance, creator->_device
				, creator->_physicalDevice, creator->_allocator, _lutDescriptorSetLayout, 1);

			_lutImage = creator->_resourceConstructor->create_image(lutImageExtent, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

			VkDescriptorImageInfo lutDescriptorInfo{};
			lutDescriptorInfo.sampler = nullptr; // not sampled (storage)
			lutDescriptorInfo.imageView = _lutImage.imageView;
			lutDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			std::vector<DescriptorImageData> descriptor = {
				{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &lutDescriptorInfo, 1 }
			};


			_lutDescriptorBuffer.setup_data(_device, descriptor); // index 0, obviously
			generate_lut_immediate();

			_lutSamplerDescriptorBuffer = DescriptorBufferSampler(creator->_instance, creator->_device
				, creator->_physicalDevice, creator->_allocator, _equiImageDescriptorSetLayout, 1);
			lutDescriptorInfo.sampler = _sampler;
			std::vector<DescriptorImageData> lut_sampler_descriptor = {
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &lutDescriptorInfo, 1 }
			};
			_lutSamplerDescriptorBuffer.setup_data(_device, lut_sampler_descriptor);

		}



		layoutsCreated = true;
	}

	_cubemapStorageDescriptorBuffer = DescriptorBufferSampler(creator->_instance, creator->_device
		, creator->_physicalDevice, creator->_allocator, _cubemapStorageDescriptorSetLayout, 12);
	_cubemapDescriptorBuffer = DescriptorBufferSampler(creator->_instance, creator->_device
		, creator->_physicalDevice, creator->_allocator, _cubemapDescriptorSetLayout, 2);
	_equiImageDescriptorBuffer = DescriptorBufferSampler(creator->_instance, creator->_device
		, creator->_physicalDevice, creator->_allocator, _equiImageDescriptorSetLayout, 1);

	_environmentMapDescriptorBuffer = DescriptorBufferSampler(creator->_instance, creator->_device
		, creator->_physicalDevice, creator->_allocator, _environmentMapDescriptorSetLayout, 1);


	load_equirectangular_image(defaultEquiPath, true);
	load_cubemap(true);

	VkDescriptorImageInfo diffSpecDescriptorInfo{};
	diffSpecDescriptorInfo.sampler = _sampler;
	diffSpecDescriptorInfo.imageView = _specDiffCubemap.imageView;
	diffSpecDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo lutDescriptorInfo{};
	lutDescriptorInfo.sampler = _sampler;
	lutDescriptorInfo.imageView = _lutImage.imageView;
	lutDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	std::vector<DescriptorImageData> combined_descriptor = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &diffSpecDescriptorInfo, 1 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &lutDescriptorInfo, 1 }
	};

	_environmentMapDescriptorBuffer.setup_data(_device, combined_descriptor);


	useCount++;
}


EnvironmentMap::~EnvironmentMap()
{
	useCount--;
	if (useCount == 0) {

		vkDestroyDescriptorSetLayout(_device, _equiImageDescriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _cubemapStorageDescriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _cubemapDescriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _lutDescriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _environmentMapDescriptorSetLayout, nullptr);
		_equiImageDescriptorSetLayout = VK_NULL_HANDLE;
		_cubemapStorageDescriptorSetLayout = VK_NULL_HANDLE;
		_cubemapDescriptorSetLayout = VK_NULL_HANDLE;
		_lutDescriptorSetLayout = VK_NULL_HANDLE;
		_environmentMapDescriptorSetLayout = VK_NULL_HANDLE;

		vkDestroyPipelineLayout(_device, _equiToCubemapPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _equiToCubemapPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _cubemapToDiffusePipelineLayout, nullptr);
		vkDestroyPipeline(_device, _cubemapToDiffusePipeline, nullptr);
		vkDestroyPipelineLayout(_device, _cubemapToSpecularPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _cubemapToSpecularPipeline, nullptr);
		vkDestroyPipelineLayout(_device, _lutPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _lutPipeline, nullptr);



		_equiToCubemapPipelineLayout = VK_NULL_HANDLE;
		_equiToCubemapPipeline = VK_NULL_HANDLE;
		_cubemapToDiffusePipelineLayout = VK_NULL_HANDLE;
		_cubemapToDiffusePipeline = VK_NULL_HANDLE;
		_cubemapToSpecularPipelineLayout = VK_NULL_HANDLE;
		_cubemapToSpecularPipeline = VK_NULL_HANDLE;

		vkDestroySampler(_device, _sampler, nullptr);

		_lutDescriptorBuffer.destroy(_device, _allocator);
		_creator->_resourceConstructor->destroy_image(_lutImage);
		_lutImage = {};
		_lutDescriptorBuffer = DescriptorBufferSampler();



		layoutsCreated = false;
		fmt::print("The last EnvironmentMap has been destroyed, uninitializing layouts and pipelines \n");
	}


	_cubemapStorageDescriptorBuffer.destroy(_device, _allocator);
	_cubemapDescriptorBuffer.destroy(_device, _allocator);
	_equiImageDescriptorBuffer.destroy(_device, _allocator);
	_environmentMapDescriptorBuffer.destroy(_device, _allocator);

	_creator->_resourceConstructor->destroy_image(_equiImage);
	_creator->_resourceConstructor->destroy_image(_cubemapImage);
	_creator->_resourceConstructor->destroy_image(_specDiffCubemap);
	fmt::print("EnvironmentMap Destroyed\n");
}

bool EnvironmentMap::load_equirectangular_image(const char* path, bool firstTimeSetup)
{
	int width, height, channels;
	float* data = stbi_loadf(path, &width, &height, &channels, 4);
	if (data) {
		fmt::print("Loaded Equirectangular Image \"{}\": {}x{}x{}\n", path, width, height, channels);
		if (!firstTimeSetup) { _creator->_resourceConstructor->destroy_image(_equiImage); }
		_equiImage = _creator->_resourceConstructor->create_image(data, width * height * 4 * sizeof(float), VkExtent3D{ (uint32_t)width, (uint32_t)height, 1 }, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT, true);
		stbi_image_free(data);
	}
	else {
		if (firstTimeSetup) {
			fmt::print("Failed to load Initial Image. Did you delete \\src_images\\dam_bridge_4k.hdr?\n");
			abort();
		}
		else {
			fmt::print("Failed to load Equirectangular Image\n");
		}
		return false;
	}

	VkDescriptorImageInfo equiImageDescriptorInfo{};
	equiImageDescriptorInfo.sampler = _sampler;
	equiImageDescriptorInfo.imageView = _equiImage.imageView;
	equiImageDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// needs to match the order of the bindings in the layout
	std::vector<DescriptorImageData> combined_descriptor = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &equiImageDescriptorInfo, 1 }
	};

	if (firstTimeSetup) { _equiImageDescriptorBuffer.setup_data(_device, combined_descriptor); }
	else { _equiImageDescriptorBuffer.set_data(_device, combined_descriptor, 0); }

	_equiPath = path;
	return true;
}


void EnvironmentMap::load_cubemap(bool firstTimeSetup)
{
	auto start = std::chrono::system_clock::now();

	assert(_equiImage.imageExtent.width % 4 == 0);
	_cubemapResolution = _equiImage.imageExtent.width / 4;
	VkExtent3D extents = { _cubemapResolution, _cubemapResolution, 1 };

	// Equi -> Cubemap - recreate in case resolution changed
	{
		if (!firstTimeSetup) {
			_creator->_resourceConstructor->destroy_image(_cubemapImage);
		}
		_cubemapImage = _creator->_resourceConstructor->create_cubemap(extents, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

		// add new cubemap image to descriptor buffer
		{
			VkDescriptorImageInfo cubemapDescriptor{};
			cubemapDescriptor.sampler = _sampler;
			cubemapDescriptor.imageView = _cubemapImage.imageView;
			cubemapDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			std::vector<DescriptorImageData> storage_image = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &cubemapDescriptor, 1 } };
			if (firstTimeSetup) { _cubemapStorageDescriptorBuffer.setup_data(_device, storage_image); }
			else { _cubemapStorageDescriptorBuffer.set_data(_device, storage_image, 0); }

			cubemapDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			std::vector<DescriptorImageData> combined_descriptor = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &cubemapDescriptor, 1 } };
			if (firstTimeSetup) { _cubemapDescriptorBuffer.setup_data(_device, combined_descriptor); }
			else { _cubemapDescriptorBuffer.set_data(_device, combined_descriptor, 0); }

		};

		equi_to_cubemap_immediate();
		//_creator->draw_equi_to_cubemap_immediate(_cubemapImage, extents, _equiImageDescriptorBuffer, _cubemapStorageDescriptorBuffer);

		// can safely destroy the cubemap image view in the storage buffer
		_cubemapStorageDescriptorBuffer.free_descriptor_buffer(0);
	}


	auto end0 = std::chrono::system_clock::now();
	auto elapsed0 = std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
	fmt::print("Cubemap Created in {} seconds\n", elapsed0.count() / 1000000.0f);


	{
		if (firstTimeSetup) {
			_specDiffCubemap = _creator->_resourceConstructor->create_cubemap(specularPrefilteredBaseExtents, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, true);

			VkDescriptorImageInfo specDiffMapDescriptor{};
			specDiffMapDescriptor.sampler = _sampler;
			specDiffMapDescriptor.imageView = _specDiffCubemap.imageView;
			specDiffMapDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			std::vector<DescriptorImageData> spec_diff_storage_descriptor = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &specDiffMapDescriptor, 1 } };
			_cubemapStorageDescriptorBuffer.setup_data(_device, spec_diff_storage_descriptor); // index 1 - Probably Unnecessary. Test Later.

			specDiffMapDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			std::vector<DescriptorImageData> spec_diff_combined_descriptor = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &specDiffMapDescriptor, 1 } };
			_cubemapDescriptorBuffer.setup_data(_device, spec_diff_combined_descriptor); // index 1
		}


		int diffuseIndex = specularPrefilteredMipLevels - 5;

		AllocatedCubemap specDiffCubemap = {};
		specDiffCubemap.allocatedImage = _specDiffCubemap;
		specDiffCubemap.mipLevels = specularPrefilteredMipLevels;
		specDiffCubemap.cubemapImageViews = std::vector<CubemapImageView>(specularPrefilteredMipLevels);
		assert(specularPrefilteredBaseExtents.width == specularPrefilteredBaseExtents.height);

		for (int i = 0; i < specularPrefilteredMipLevels; i++) {

			CubemapImageView image_view{};
			VkImageViewCreateInfo view_info = vkinit::cubemapview_create_info(_specDiffCubemap.imageFormat, _specDiffCubemap.image, VK_IMAGE_ASPECT_COLOR_BIT);
			view_info.subresourceRange.baseMipLevel = i;
			VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &image_view.imageView));

			uint32_t length = static_cast<uint32_t>(specularPrefilteredBaseExtents.width / pow(2, i)); // w and h always equal
			image_view.imageExtent = { length, length, 1 };
			float roughness{};
			int j = i;
			if (i > 5) { j = i - 1; }
			if (i == 5) { roughness = -1; } // diffuse irradiance map
			else { roughness = static_cast<float>(j) / static_cast<float>(specularPrefilteredMipLevels - 2); }

			image_view.roughness = roughness;

			VkDescriptorImageInfo prefilteredCubemapStorage{};
			prefilteredCubemapStorage.sampler = nullptr; // sampler not actually used in storage image
			prefilteredCubemapStorage.imageView = image_view.imageView;
			prefilteredCubemapStorage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			std::vector<DescriptorImageData> prefiltered_cubemap_storage_descriptor = {
				{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &prefilteredCubemapStorage, 1 }
			};

			int descriptorBufferIndex = _cubemapStorageDescriptorBuffer.setup_data(_device, prefiltered_cubemap_storage_descriptor);
			image_view.descriptorBufferIndex = descriptorBufferIndex;

			specDiffCubemap.cubemapImageViews[i] = image_view;

		}

		cubemap_to_difffuse_specular_immediate(specDiffCubemap);
		//_creator->draw_cubemap_to_diffuse_specular_immediate(specDiffCubemap, _cubemapDescriptorBuffer, _cubemapStorageDescriptorBuffer);
		// can safely destroy all the mip level image views
		for (int i = 0; i < specDiffCubemap.mipLevels; i++) {
			vkDestroyImageView(_device, specDiffCubemap.cubemapImageViews[i].imageView, nullptr);
			_cubemapStorageDescriptorBuffer.free_descriptor_buffer(specDiffCubemap.cubemapImageViews[i].descriptorBufferIndex);
		}

	}



	auto end1 = std::chrono::system_clock::now();
	auto elapsed1 = std::chrono::duration_cast<std::chrono::microseconds>(end1 - end0);
	fmt::print("Specular and Diffuse Maps Created in: {} seconds\n", elapsed1.count() / 1000000.0f);

	auto end3 = std::chrono::system_clock::now();
	auto elapsed3 = std::chrono::duration_cast<std::chrono::microseconds>(end3 - start);
	fmt::print("Total Cubemap Load Time: {} seconds\n", elapsed3.count() / 1000000.0f);
}

void EnvironmentMap::save_cubemap_image(const char* path)
{
	size_t data_size = _cubemapResolution * _cubemapResolution * 4 * 6 * sizeof(float);
	AllocatedBuffer _stagingBuffer = _creator->_resourceConstructor->create_staging_buffer(data_size);

	_creator->immediate_submit([&](VkCommandBuffer cmd) {
		VkImageSubresourceLayers subresource = {};
		subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresource.mipLevel = 0;
		subresource.baseArrayLayer = 0;
		subresource.layerCount = 1;

		std::vector<VkBufferImageCopy> bufferCopyRegions;
		VkDeviceSize offset = 0;

		for (uint32_t face = 0; face < 6; face++) {
			subresource.baseArrayLayer = face;

			VkBufferImageCopy bufferCopyRegion{};
			bufferCopyRegion.imageSubresource = subresource;
			bufferCopyRegion.imageExtent = { _cubemapResolution, _cubemapResolution, 1 };
			bufferCopyRegion.bufferOffset = offset;
			bufferCopyRegion.bufferRowLength = 0;
			bufferCopyRegion.bufferImageHeight = 0;

			bufferCopyRegions.push_back(bufferCopyRegion);
			offset += _cubemapResolution * _cubemapResolution * 4 * sizeof(float);
		}

		vkutil::transition_image(cmd, _cubemapImage.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

		vkCmdCopyImageToBuffer(cmd, _cubemapImage.image, VK_IMAGE_LAYOUT_GENERAL
			, _stagingBuffer.buffer, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

		vkutil::transition_image(cmd, _cubemapImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


		});

	void* data = _stagingBuffer.info.pMappedData;
	float* imageData = static_cast<float*>(data);

	std::string directory = path;

	for (uint32_t face = 0; face < 6; face++) {
		std::string facePath = directory + "\\" + "cubemap_face_" + std::to_string(face) + ".hdr";;
		stbi_write_hdr(facePath.c_str(), _cubemapResolution, _cubemapResolution, 4, imageData + (_cubemapResolution * _cubemapResolution * 4 * face));
		fmt::print("Saved face {} to: {}\n", face, facePath);
	}

	_creator->_resourceConstructor->destroy_buffer(_stagingBuffer);
}

void EnvironmentMap::save_lut_image(const char* path)
{
	// Lut image format is VK_FORMAT_R16G16_SFLOAT (half float)
	size_t data_size = lutImageExtent.width * lutImageExtent.height * 2 * sizeof(float);
	AllocatedBuffer _stagingBuffer = _creator->_resourceConstructor->create_staging_buffer(data_size);

	_creator->immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferImageCopy bufferCopyRegion{};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent = lutImageExtent;
		bufferCopyRegion.bufferOffset = 0;
		bufferCopyRegion.bufferRowLength = 0;
		bufferCopyRegion.bufferImageHeight = 0;

		vkutil::transition_image(cmd, _lutImage.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

		vkCmdCopyImageToBuffer(cmd, _lutImage.image, VK_IMAGE_LAYOUT_GENERAL, _stagingBuffer.buffer, 1, &bufferCopyRegion);

		vkutil::transition_image(cmd, _lutImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

	void* data = _stagingBuffer.info.pMappedData;
	float* imageData = static_cast<float*>(data);

	uint8_t* byteImageData = new uint8_t[lutImageExtent.width * lutImageExtent.height * 4];
	float pow_eight = pow(2, 8) - 1;
	for (size_t i = 0; i < lutImageExtent.width * lutImageExtent.height; ++i) {
		float v1 = imageData[i * 2];
		float v2 = imageData[i * 2 + 1];
		uint8_t a = static_cast<uint8_t>(v1 * pow_eight);
		uint8_t b = static_cast<uint8_t>(v2 * pow_eight);
		byteImageData[i * 4] = a;
		byteImageData[i * 4 + 1] = b;
		byteImageData[i * 4 + 2] = 0;
		byteImageData[i * 4 + 3] = 255;
	}


	stbi_write_png(path, lutImageExtent.width, lutImageExtent.height, 4, byteImageData, lutImageExtent.width * 4);

	delete[] byteImageData;
	_creator->_resourceConstructor->destroy_buffer(_stagingBuffer);
}



void EnvironmentMap::equi_to_cubemap_immediate()
{
	_creator->immediate_submit([&](VkCommandBuffer cmd) {
		VkExtent3D extents = { _cubemapResolution, _cubemapResolution, 1 };

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _equiToCubemapPipeline);

		//vkutil::transition_image(cmd, _equiImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		vkutil::transition_image(cmd, _cubemapImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info[2] = {
			_equiImageDescriptorBuffer.get_descriptor_buffer_binding_info(),
			_cubemapStorageDescriptorBuffer.get_descriptor_buffer_binding_info(),
		};

		vkCmdBindDescriptorBuffersEXT(cmd, 2, descriptor_buffer_binding_info);
		uint32_t equiImage_index = 0;
		uint32_t cubemap_index = 1;

		VkDeviceSize offset = 0;

		vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _equiToCubemapPipelineLayout
			, 0, 1, &equiImage_index, &offset);
		vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _equiToCubemapPipelineLayout
			, 1, 1, &cubemap_index, &offset);


		EquiToCubePushConstantData pushData{};
		pushData.flipY = flip_y;
		vkCmdPushConstants(cmd, _equiToCubemapPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(EquiToCubePushConstantData), &pushData);

		assert(extents.width % 16 == 0 && extents.height % 16 == 0);
		vkCmdDispatch(cmd, extents.width / 16, extents.height / 16, 6);

		vkutil::transition_image(cmd, _cubemapImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});
}

void EnvironmentMap::cubemap_to_difffuse_specular_immediate(AllocatedCubemap& cubemapMips)
{
	_creator->immediate_submit([&](VkCommandBuffer cmd) {

		auto start = std::chrono::system_clock::now();

		vkutil::transition_image(cmd, cubemapMips.allocatedImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);


		VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info[2] = {
			_cubemapDescriptorBuffer.get_descriptor_buffer_binding_info(),
			_cubemapStorageDescriptorBuffer.get_descriptor_buffer_binding_info(),
		};
		uint32_t cubemap_index = 0;
		uint32_t storage_cubemap_index = 1;
		VkDeviceSize zero_offset = 0;
		// Diffuse 
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cubemapToDiffusePipeline);
			vkCmdBindDescriptorBuffersEXT(cmd, 2, descriptor_buffer_binding_info);
			vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cubemapToDiffusePipelineLayout, 0, 1, &cubemap_index, &zero_offset);

			CubemapImageView& diffuse = cubemapMips.cubemapImageViews[5];
			assert(diffuse.roughness == -1);

			VkDeviceSize diffusemap_offset = _cubemapStorageDescriptorBuffer.descriptor_buffer_size * diffuse.descriptorBufferIndex;
			vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cubemapToDiffusePipelineLayout, 1, 1, &storage_cubemap_index, &diffusemap_offset);

			CubeToDiffusePushConstantData pushData{};
			pushData.sampleDelta = diffuse_sample_delta;
			vkCmdPushConstants(cmd, _cubemapToDiffusePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CubeToDiffusePushConstantData), &pushData);

			// width and height should be 32, dont need bounds check in shader code
			uint32_t xDispatch = static_cast<uint32_t>(std::ceil(diffuse.imageExtent.width / 8.0f));
			uint32_t yDispatch = static_cast<uint32_t>(std::ceil(diffuse.imageExtent.height / 8.0f));

			vkCmdDispatch(cmd, xDispatch, yDispatch, 6);
		}



		auto end0 = std::chrono::system_clock::now();
		auto elapsed0 = std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
		fmt::print("Diffuse Irradiance Map Created in {} seconds\n", elapsed0.count() / 1000000.0f);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cubemapToSpecularPipeline);
		vkCmdBindDescriptorBuffersEXT(cmd, 2, descriptor_buffer_binding_info);
		vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cubemapToSpecularPipelineLayout, 0, 1, &cubemap_index, &zero_offset);

		for (int i = 0; i < specularPrefilteredMipLevels; i++) {
			if (i == 5) continue; // skip the diffuse map mip level
			CubemapImageView& current = cubemapMips.cubemapImageViews[i];

			VkDeviceSize irradiancemap_offset = _cubemapStorageDescriptorBuffer.descriptor_buffer_size * current.descriptorBufferIndex;
			vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cubemapToSpecularPipelineLayout, 1, 1, &storage_cubemap_index, &irradiancemap_offset);

			CubeToPrefilteredConstantData pushData{};
			pushData.roughness = current.roughness;
			pushData.imageWidth = current.imageExtent.width;
			pushData.imageHeight = current.imageExtent.height;
			pushData.sampleCount = static_cast<uint32_t>(specular_sample_count);
			vkCmdPushConstants(cmd, _cubemapToSpecularPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CubeToDiffusePushConstantData), &pushData);

			uint32_t xDispatch = static_cast<uint32_t>(std::ceil(current.imageExtent.width / 8.0f));
			uint32_t yDispatch = static_cast<uint32_t>(std::ceil(current.imageExtent.height / 8.0f));


			vkCmdDispatch(cmd, xDispatch, yDispatch, 6);
		}


		vkutil::transition_image(cmd, cubemapMips.allocatedImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		auto end1 = std::chrono::system_clock::now();
		auto elapsed1 = std::chrono::duration_cast<std::chrono::microseconds>(end1 - end0);
		fmt::print("Diffuse Irradiance Map Created in {} seconds\n", elapsed1.count() / 1000000.0f);

		});
}

void EnvironmentMap::generate_lut_immediate() {
	_creator->immediate_submit([&](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, _lutImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _lutPipeline);

		VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info[1] = {
			_lutDescriptorBuffer.get_descriptor_buffer_binding_info(),
		};

		uint32_t lut_index = 0;
		VkDeviceSize zero_offset = 0;

		vkCmdBindDescriptorBuffersEXT(cmd, 1, descriptor_buffer_binding_info);
		vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _lutPipelineLayout, 0, 1, &lut_index, &zero_offset);

		// must be divisible by 16, dont want to bother bounds checking in shader code
		assert(lutImageExtent.width % 8 == 0 && lutImageExtent.height % 8 == 0);
		uint32_t x_disp = lutImageExtent.width / 8;
		uint32_t y_disp = lutImageExtent.height / 8;
		uint32_t z_disp = 1;
		vkCmdDispatch(cmd, x_disp, y_disp, z_disp);

		vkutil::transition_image(cmd, _lutImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});
}