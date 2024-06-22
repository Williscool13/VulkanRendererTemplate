#pragma once
#include "big_header.h"
#include "vk_types.h"
#include "engine.h"

#include "vk_images.h"
#include "vk_initializers.h"

class VulkanResourceConstructor {
public:
	VulkanResourceConstructor() = delete;
	VulkanResourceConstructor(MainEngine* creator);

	// Vulkan Buffers
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	AllocatedBuffer create_staging_buffer(size_t allocSize);
	void copy_buffer(AllocatedBuffer src, AllocatedBuffer dst, VkDeviceSize size);
	VkDeviceAddress get_buffer_address(AllocatedBuffer buffer);
	void destroy_buffer(const AllocatedBuffer& buffer);

	// Vulkan Images
	AllocatedImage create_image(void* data, size_t dataSize, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_cubemap(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	void destroy_image(const AllocatedImage& img);

private:
	MainEngine* _creator;
	VkDevice _device;
	VmaAllocator _allocator;
};

