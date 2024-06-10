#include "vk_descriptor_buffer.h"


VkPhysicalDeviceDescriptorBufferPropertiesEXT DescriptorBuffer::descriptor_buffer_properties = {};
bool DescriptorBuffer::device_properties_retrieved = false;

DescriptorBuffer::DescriptorBuffer(VkInstance instance, VkDevice device
	, VkPhysicalDevice physicalDevice, VmaAllocator allocator, VkDescriptorSetLayout descriptorSetLayout, int maxObjectCount)
{
	// Get Descriptor Buffer Properties
	if (!device_properties_retrieved) {
		fmt::print("Retrieving Descriptor Buffer Properties\n");
		VkPhysicalDeviceProperties2KHR device_properties{};
		descriptor_buffer_properties = {};
		descriptor_buffer_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
		device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
		device_properties.pNext = &descriptor_buffer_properties;
		vkGetPhysicalDeviceProperties2(physicalDevice, &device_properties);
		device_properties_retrieved = true;
	}

	// Descriptor Set Layout
	this->descriptor_set_layout = descriptorSetLayout;
	// Buffer Size
	vkGetDescriptorSetLayoutSizeEXT(device, descriptorSetLayout, &descriptor_buffer_size);
	descriptor_buffer_size = aligned_size(descriptor_buffer_size, descriptor_buffer_properties.descriptorBufferOffsetAlignment);
	// Buffer Offset
	vkGetDescriptorSetLayoutBindingOffsetEXT(device, descriptorSetLayout, 0u, &descriptor_buffer_offset);

	free_indices = std::stack<int>();
	for (int i = maxObjectCount - 1; i >= 0; i--) { free_indices.push(i); }
}


void DescriptorBuffer::destroy(VkDevice device, VmaAllocator allocator) {
	if (is_buffer_mapped) { vmaDestroyBuffer(allocator, descriptor_buffer.buffer, descriptor_buffer.allocation); }
}

void DescriptorBuffer::free_descriptor_buffer(int index)
{
	free_indices.push(index);
}

VkDeviceSize DescriptorBuffer::aligned_size(VkDeviceSize value, VkDeviceSize alignment) {
	return (value + alignment - 1) & ~(alignment - 1);
}

inline VkDeviceAddress DescriptorBuffer::get_device_address(VkDevice device, VkBuffer buffer)
{
	VkBufferDeviceAddressInfo deviceAdressInfo{};
	deviceAdressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	deviceAdressInfo.buffer = buffer;
	uint64_t address = vkGetBufferDeviceAddress(device, &deviceAdressInfo);

	return address;
}


DescriptorBufferSampler::DescriptorBufferSampler(VkInstance instance, VkDevice device
	, VkPhysicalDevice physicalDevice, VmaAllocator allocator, VkDescriptorSetLayout descriptorSetLayout, int maxObjectCount)
	: DescriptorBuffer(instance, device, physicalDevice, allocator, descriptorSetLayout, maxObjectCount)
{
	// Allocate Buffer
	VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = descriptor_buffer_size * maxObjectCount;
	bufferInfo.usage =
		VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		| VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo
		, &descriptor_buffer.buffer
		, &descriptor_buffer.allocation
		, &descriptor_buffer.info));

	descriptor_buffer_gpu_address = get_device_address(device, descriptor_buffer.buffer);
	buffer_ptr = descriptor_buffer.info.pMappedData;
	is_buffer_mapped = true;
}

int DescriptorBufferSampler::setup_data(VkDevice device, std::vector<DescriptorImageData> data) {
	if (free_indices.empty()) {
		fmt::print("Ran out of space in DescriptorBufferUniform\n");
		abort();
		return -1;
	}

	int index = free_indices.top();
	free_indices.pop();

	uint64_t accum_offset{ descriptor_buffer_offset };

	if (descriptor_buffer_properties.combinedImageSamplerDescriptorSingleArray == VK_FALSE) {
		fmt::print("This implementation does not support combinedImageSamplerDescriptorSingleArray\n");
		return -1;
	}

	for (int i = 0; i < data.size(); i++) {
		// image_descriptor_info
		DescriptorImageData currentData = data[i];
		for (int j = 0; j < currentData.count; j++) {
			if (currentData.image_info == nullptr) {
				size_t descriptor_size;
				switch (currentData.type) {
				case VK_DESCRIPTOR_TYPE_SAMPLER:
					descriptor_size = descriptor_buffer_properties.samplerDescriptorSize; break;
				case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
					descriptor_size = descriptor_buffer_properties.combinedImageSamplerDescriptorSize; break;
				case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
					descriptor_size = descriptor_buffer_properties.sampledImageDescriptorSize; break;
				case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
					descriptor_size = descriptor_buffer_properties.storageImageDescriptorSize; break;
				}
				accum_offset += descriptor_size;
				continue;
			}

			VkDescriptorGetInfoEXT image_descriptor_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
			image_descriptor_info.type = currentData.type;
			size_t descriptor_size{};

			switch (currentData.type) {
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				image_descriptor_info.data.pSampler = &currentData.image_info[j].sampler;
				descriptor_size = descriptor_buffer_properties.samplerDescriptorSize;
				break;
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				image_descriptor_info.data.pCombinedImageSampler = &currentData.image_info[j];
				descriptor_size = descriptor_buffer_properties.combinedImageSamplerDescriptorSize;
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				image_descriptor_info.data.pSampledImage = &currentData.image_info[j];
				descriptor_size = descriptor_buffer_properties.sampledImageDescriptorSize;
				break;
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				image_descriptor_info.data.pStorageImage = &currentData.image_info[j];
				descriptor_size = descriptor_buffer_properties.storageImageDescriptorSize;
				break;
			default:
				fmt::print("DescriptorBufferImage::setup_data() called with a non-image/sampler descriptor type\n");
				return -1;
			}

			char* buffer_ptr_offset = (char*)buffer_ptr + accum_offset + index * descriptor_buffer_size;
			vkGetDescriptorEXT(
				device,
				&image_descriptor_info,
				descriptor_size,
				buffer_ptr_offset
			);

			accum_offset += descriptor_size;
		}
	}

	return index;

}

void DescriptorBufferSampler::set_data(VkDevice device, std::vector<DescriptorImageData> data, int index) {
	uint64_t accum_offset{ descriptor_buffer_offset };

	for (int i = 0; i < data.size(); i++) {
		// image_descriptor_info
		VkDescriptorGetInfoEXT image_descriptor_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
		image_descriptor_info.type = data[i].type;
		size_t descriptor_size{};


		switch (data[i].type) {
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			VkSampler samplers[32]; //32 matches upper limit of samplerCount
			DescriptorImageData d = data[i];
			if (d.count > 32) { fmt::print("Sampler count exceeds 32\n"); return; }
			for (int j = 0; j < d.count; j++) { samplers[j] = d.image_info[j].sampler; }
			image_descriptor_info.data.pSampler = samplers;
			descriptor_size = descriptor_buffer_properties.samplerDescriptorSize * d.count;
			break;
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			image_descriptor_info.data.pCombinedImageSampler = data[i].image_info;
			descriptor_size = descriptor_buffer_properties.combinedImageSamplerDescriptorSize * data[i].count;
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			image_descriptor_info.data.pSampledImage = data[i].image_info;
			descriptor_size = descriptor_buffer_properties.sampledImageDescriptorSize * data[i].count;
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			image_descriptor_info.data.pStorageImage = data[i].image_info;
			descriptor_size = descriptor_buffer_properties.storageImageDescriptorSize * data[i].count;
			break;
		default:
			fmt::print("DescriptorBufferImage::setup_data() called with a non-image/sampler descriptor type\n");
			return;
		}

		// pointer to start point
		char* buffer_ptr_offset = (char*)buffer_ptr + accum_offset + index * descriptor_buffer_size;
		vkGetDescriptorEXT(
			device,
			&image_descriptor_info,
			descriptor_size,
			buffer_ptr_offset
		);

		accum_offset += descriptor_size;
	}
}


VkDescriptorBufferBindingInfoEXT DescriptorBufferSampler::get_descriptor_buffer_binding_info(VkDevice device)
{
	//VkDeviceAddress address = get_device_address(device, descriptor_buffer.buffer);

	VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info{};
	descriptor_buffer_binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	descriptor_buffer_binding_info.address = this->descriptor_buffer_gpu_address;
	descriptor_buffer_binding_info.usage
		= VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;

	return descriptor_buffer_binding_info;
}



DescriptorBufferUniform::DescriptorBufferUniform(VkInstance instance, VkDevice device
	, VkPhysicalDevice physicalDevice, VmaAllocator allocator, VkDescriptorSetLayout descriptorSetLayout, int maxObjectCount)
	: DescriptorBuffer(instance, device, physicalDevice, allocator, descriptorSetLayout, maxObjectCount)
{
	// Allocate Buffer
	VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = descriptor_buffer_size * maxObjectCount;
	bufferInfo.usage =
		VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo
		, &descriptor_buffer.buffer
		, &descriptor_buffer.allocation
		, &descriptor_buffer.info));

	descriptor_buffer_gpu_address = get_device_address(device, descriptor_buffer.buffer);
	buffer_ptr = descriptor_buffer.info.pMappedData;
	is_buffer_mapped = true;
}

int DescriptorBufferUniform::setup_data(VkDevice device, const AllocatedBuffer& uniform_buffer, size_t allocSize) {
	if (free_indices.empty()) {
		fmt::print("Ran out of space in DescriptorBufferUniform\n");
		abort();
		return -1;
	}

	int index = free_indices.top();
	free_indices.pop();


	VkDeviceAddress ad = get_device_address(device, uniform_buffer.buffer);

	VkDescriptorAddressInfoEXT addr_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT };
	addr_info.address = ad;
	addr_info.range = allocSize;
	addr_info.format = VK_FORMAT_UNDEFINED;

	VkDescriptorGetInfoEXT buffer_descriptor_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT };
	buffer_descriptor_info.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	buffer_descriptor_info.data.pUniformBuffer = &addr_info;


	uint64_t accum_offset{ descriptor_buffer_offset };
	// at index 0, should be -> pointer + offset + 0 * descriptor_buffer_size
	// at index 1, should be -> pointer + offset + 1 * descriptor_buffer_size etc.
	char* buffer_ptr_offset = (char*)buffer_ptr + accum_offset + index * descriptor_buffer_size;

	vkGetDescriptorEXT(
		device
		, &buffer_descriptor_info
		, descriptor_buffer_properties.uniformBufferDescriptorSize
		, (char*)buffer_ptr_offset
	);

	accum_offset += descriptor_buffer_properties.uniformBufferDescriptorSize;
	// If accumulating, add descriptor_buffer_properties.uniformBufferDescriptorSize to accum_offset

	return index;

}

VkDescriptorBufferBindingInfoEXT DescriptorBufferUniform::get_descriptor_buffer_binding_info(VkDevice device) {
	//VkDeviceAddress address = get_device_address(device, descriptor_buffer.buffer);

	VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info{};
	descriptor_buffer_binding_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	descriptor_buffer_binding_info.address = this->descriptor_buffer_gpu_address;
	descriptor_buffer_binding_info.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;

	return descriptor_buffer_binding_info;
}

