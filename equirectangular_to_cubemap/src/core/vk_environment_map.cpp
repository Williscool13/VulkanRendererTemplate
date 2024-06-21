#include "vk_environment_map.h"


const VkExtent3D EnvironmentMap::specularPrefilteredBaseExtents = { 512, 512, 1 };
const char* EnvironmentMap::defaultEquiPath = "src_images/dam_bridge_4k.hdr";
VkDescriptorSetLayout EnvironmentMap::_equiImageDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout EnvironmentMap::_cubemapStorageDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout EnvironmentMap::_cubemapDescriptorSetLayout = VK_NULL_HANDLE;
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
		fmt::print("Layouts not created yet, doing first time setup\n");
		////  Equirectangular Image
		//{
		//	DescriptorLayoutBuilder layoutBuilder;
		//	layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

		//	_equiImageDescriptorSetLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT
		//		, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

		//}
		////  STORAGE cubemaps - 0 is equi, 1 is diff irr, 2 is spec pref, 3 to 13 is for 10 mip levels of spec pref
		//{
		//	DescriptorLayoutBuilder layoutBuilder;
		//	layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

		//	_cubemapStorageDescriptorSetLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT
		//		, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);
		//}
		//
		////  SAMPLER cubemaps
		//{
		//	DescriptorLayoutBuilder layoutBuilder;
		//	layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

		//	_cubemapDescriptorSetLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
		//		, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

		//}
		_equiImageDescriptorSetLayout = _creator->_equiImageDescriptorSetLayout;
		_cubemapStorageDescriptorSetLayout = _creator->_cubemapStorageDescriptorSetLayout;
		_cubemapDescriptorSetLayout = _creator->_cubemapDescriptorSetLayout;

		layoutsCreated = true;
	}
	_cubemapStorageDescriptorBuffer = DescriptorBufferSampler(creator->_instance, creator->_device
		, creator->_physicalDevice, creator->_allocator, _cubemapStorageDescriptorSetLayout, 12);
	_cubemapDescriptorBuffer = DescriptorBufferSampler(creator->_instance, creator->_device
		, creator->_physicalDevice, creator->_allocator, _cubemapDescriptorSetLayout, 2);
	_equiImageDescriptorBuffer = DescriptorBufferSampler(creator->_instance, creator->_device
		, creator->_physicalDevice, creator->_allocator, _equiImageDescriptorSetLayout, 1);

	load_equirectangular_image(defaultEquiPath, true);
	load_cubemap(true);
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
}

EnvironmentMap::~EnvironmentMap()
{
	_cubemapStorageDescriptorBuffer.destroy(_device, _allocator);
	_cubemapDescriptorBuffer.destroy(_device, _allocator);
	_equiImageDescriptorBuffer.destroy(_device, _allocator);
	// the below are destroyed by the engine
	//vkDestroyDescriptorSetLayout(_device, _equiImageDescriptorSetLayout, nullptr);
	//vkDestroyDescriptorSetLayout(_device, _cubemapStorageDescriptorSetLayout, nullptr);
	//vkDestroyDescriptorSetLayout(_device, _cubemapDescriptorSetLayout, nullptr);
	vkDestroySampler(_device, _sampler, nullptr);

	_creator->_resourceConstructor->destroy_image(_equiImage);
	_creator->_resourceConstructor->destroy_image(_cubemapImage);
	_creator->_resourceConstructor->destroy_image(_specDiffCubemap);
	fmt::print("EnvironmentMap Destroyed\n");
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

		_creator->draw_equi_to_cubemap_immediate(_cubemapImage, extents, _equiImageDescriptorBuffer, _cubemapStorageDescriptorBuffer);

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

		_creator->draw_cubemap_to_diffuse_specular_immediate(specDiffCubemap, _cubemapDescriptorBuffer, _cubemapStorageDescriptorBuffer);
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
