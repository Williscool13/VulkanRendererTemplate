#include <iostream>
#include <engine.h>

// defined here because needs implementation in translation unit
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
#include <stb_image/stb_image_write.h>

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#ifdef NDEBUG
#define USE_VALIDATION_LAYERS false
#else
#define USE_VALIDATION_LAYERS true
#endif

#define ENABLE_FRAME_STATISTICS true
#define USE_MSAA true
#define MSAA_SAMPLES VK_SAMPLE_COUNT_4_BIT

void MainEngine::init() {
	// Init Window
	{
		// We initialize SDL and create a window with it.
		SDL_Init(SDL_INIT_VIDEO);
		SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

		_window = SDL_CreateWindow(
			"Will Engine",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			_windowExtent.width,
			_windowExtent.height,
			window_flags);
	}

	init_vulkan();
	init_swapchain();
	init_commands();
	//init_sync_structures();

	fmt::print("Finished Initialization\n");
}

void MainEngine::run() {
	SDL_Event e;
	bool bQuit = false;
	SDL_SetRelativeMouseMode(SDL_FALSE);

	while (!bQuit) {
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				bQuit = true;
			}
		}
	}
}


void MainEngine::init_vulkan()
{
	
	VkResult res = volkInitialize();
	if (res != VK_SUCCESS) {
		throw std::runtime_error("Failed to initialize volk");
	}

	vkb::InstanceBuilder builder;

	// make the vulkan instance, with basic debug features
	auto inst_ret = builder.set_app_name("Will's Vulkan Renderer")
		.request_validation_layers(USE_VALIDATION_LAYERS)
		.use_default_debug_messenger()
		.require_api_version(1, 3)
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	// vulkan instance
	_instance = vkb_inst.instance;
	volkLoadInstance(_instance);
	_debug_messenger = vkb_inst.debug_messenger;

	// sdl vulkan surface
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);



	// vk 1.3
	VkPhysicalDeviceVulkan13Features features{};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features.dynamicRendering = true;
	features.synchronization2 = true;
	// vk 1.2
	VkPhysicalDeviceVulkan12Features features12{};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;

	VkPhysicalDeviceFeatures other_features{};
	other_features.multiDrawIndirect = true;
	// Descriptor Buffer Extension
	VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures = {};
	descriptorBufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
	descriptorBufferFeatures.descriptorBuffer = VK_TRUE;
	// Shader Object
	VkPhysicalDeviceShaderObjectFeaturesEXT enabledShaderObjectFeaturesEXT{};
	enabledShaderObjectFeaturesEXT.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
	enabledShaderObjectFeaturesEXT.shaderObject = VK_TRUE;

	// select gpu
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice targetDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features)
		.set_required_features_12(features12)
		.set_required_features(other_features)
		.add_required_extension(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)
		.add_required_extension(VK_EXT_SHADER_OBJECT_EXTENSION_NAME)
		.set_surface(_surface)
		.select()
		.value();

	vkb::DeviceBuilder deviceBuilder{ targetDevice };
	deviceBuilder.add_pNext(&descriptorBufferFeatures);
	deviceBuilder.add_pNext(&enabledShaderObjectFeaturesEXT);
	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	_physicalDevice = targetDevice.physical_device;

	
	// Graphics Queue
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// VMA
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _physicalDevice;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	VmaVulkanFunctions vulkanFunctions = {};
	vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
	allocatorInfo.pVulkanFunctions = &vulkanFunctions;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

}

void MainEngine::init_swapchain()
{
	create_swapchain(_windowExtent.width, _windowExtent.height);
	create_draw_images(_windowExtent.width, _windowExtent.height);
}

void MainEngine::init_commands()
{
	VkCommandPoolCreateInfo commandPoolInfo =
		vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo =
			vkinit::command_buffer_allocate_info(_frames[i]._commandPool);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
	}

	// Immediate Rendering
	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));
	VkCommandBufferAllocateInfo immCmdAllocInfo =
		vkinit::command_buffer_allocate_info(_immCommandPool);
	VK_CHECK(vkAllocateCommandBuffers(_device, &immCmdAllocInfo, &_immCommandBuffer));
}

void MainEngine::init_sync_structures()
{
	//create syncronization structures
	//one fence to control when the gpu has finished rendering the frame,
	//and 2 semaphores to syncronize rendering with swapchain
	//we want the fence to start signalled so we can wait on it on the first frame
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
	}

	// Immediate Rendeirng
	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
}

void MainEngine::create_draw_images(uint32_t width, uint32_t height) {
	// Draw Image
	{
		_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		VkExtent3D drawImageExtent = { width, height, 1 };
		_drawImage.imageExtent = drawImageExtent;
		VkImageUsageFlags drawImageUsages{};
		drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
		VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);
		VmaAllocationCreateInfo rimg_allocinfo = {};
		rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);// only found on GPU
		vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

		VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image
			, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));
	}

	// MSAA pre-resolve image

	if (USE_MSAA) {
		_drawImageBeforeMSAA.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		VkExtent3D msaaImageExtent = { width, height, 1 };
		VkImageUsageFlags msaaImageUsages{};
		msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		msaaImageUsages |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		VkImageCreateInfo msaaimg_info = vkinit::image_create_info(_drawImageBeforeMSAA.imageFormat, msaaImageUsages, msaaImageExtent);
		//msaaimg_info = vkinit::image_create_info(_drawImageBeforeMSAA.imageFormat, msaaImageUsages, msaaImageExtent);
		msaaimg_info.samples = MSAA_SAMPLES;
		VmaAllocationCreateInfo msaaimg_allocinfo = {};
		msaaimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		msaaimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);// only found on GPU
		vmaCreateImage(_allocator, &msaaimg_info, &msaaimg_allocinfo, &_drawImageBeforeMSAA.image, &_drawImageBeforeMSAA.allocation, nullptr);

		VkImageViewCreateInfo rview_info_before_msaa = vkinit::imageview_create_info(
			_drawImageBeforeMSAA.imageFormat, _drawImageBeforeMSAA.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(_device, &rview_info_before_msaa, nullptr, &_drawImageBeforeMSAA.imageView));
	}


	// Depth Image
	{

		_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
		VkExtent3D depthImageExtent = { width, height, 1 };
		_depthImage.imageExtent = depthImageExtent;
		VkImageUsageFlags depthImageUsages{};
		depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthImage.imageFormat, depthImageUsages, depthImageExtent);

		if (USE_MSAA) { dimg_info.samples = MSAA_SAMPLES; }

		VmaAllocationCreateInfo dimg_allocinfo = {};
		dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);// only found on GPU
		vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);

		VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image
			, VK_IMAGE_ASPECT_DEPTH_BIT);
		VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));
	}


}

void MainEngine::create_swapchain(uint32_t width, uint32_t height) {
	vkb::SwapchainBuilder swapchainBuilder{ _physicalDevice,_device,_surface };

	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		//.use_default_format_selection()
		.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	_swapchainExtent = vkbSwapchain.extent;

	// Swapchain and SwapchainImages
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

}

void MainEngine::destroy_swapchain() {
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	for (int i = 0; i < _swapchainImageViews.size(); i++) {
		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}
}

void MainEngine::destroy_draw_iamges() {
	vkDestroyImageView(_device, _drawImage.imageView, nullptr);
	vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
	vkDestroyImageView(_device, _depthImage.imageView, nullptr);
	vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
	if (USE_MSAA) {
		vkDestroyImageView(_device, _drawImageBeforeMSAA.imageView, nullptr);
		vmaDestroyImage(_allocator, _drawImageBeforeMSAA.image, _drawImageBeforeMSAA.allocation);
	}
}

void MainEngine::cleanup() {
	fmt::print("================================================================================\n");
	fmt::print("Cleaning up\n");
	SDL_SetRelativeMouseMode(SDL_FALSE);

	vkDeviceWaitIdle(_device);
	//loadedMultiDrawScenes.clear();

	//vkDestroyDescriptorSetLayout(_device, bufferAddressesDescriptorSetLayout, nullptr);
	//vkDestroyDescriptorSetLayout(_device, sceneDataDescriptorSetLayout, nullptr);
	//vkDestroyDescriptorSetLayout(_device, textureDescriptorSetLayout, nullptr);
	//vkDestroyDescriptorSetLayout(_device, computeCullingDescriptorSetLayout, nullptr);


	_mainDeletionQueue.flush();


	for (int i = 0; i < FRAME_OVERLAP; i++) {
		vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

		//destroy sync objects
		vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
		vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
		vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);
	}

	vkDestroyCommandPool(_device, _immCommandPool, nullptr);
	vkDestroyFence(_device, _immFence, nullptr);

	destroy_draw_iamges();
	destroy_swapchain();

	vmaDestroyAllocator(_allocator);

	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyDevice(_device, nullptr);

	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	vkDestroyInstance(_instance, nullptr);

	SDL_DestroyWindow(_window);
	fmt::print("Cleanup done\n");
	fmt::print("================================================================================\n");
}


void MainEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmd = _immCommandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	function(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdSubmitInfo = vkinit::command_buffer_submit_info(cmd);
	VkSubmitInfo2 submitInfo = vkinit::submit_info(&cmdSubmitInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, _immFence));

	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 1000000000));
}