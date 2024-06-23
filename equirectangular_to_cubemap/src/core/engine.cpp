#include <iostream>
#include <engine.h>

// defined here because needs implementation in translation unit
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
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
	fmt::print("================================================================================\n");
	fmt::print("Initializing Program\n");
	auto start = std::chrono::system_clock::now();

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
	init_sync_structures();

	_resourceConstructor = std::make_shared<VulkanResourceConstructor>(this);

	init_default_data();

	init_dearimgui();
	init_descriptors();
	
	_environmentMap = std::make_shared<EnvironmentMap>(this);

	init_pipelines();
	

	std::string structurePath = { "src_assets\\MetalRoughSpheres.glb" };
	_metallicSpheres = std::make_shared<GLTFMetallic_RoughnessMultiDraw>(this, structurePath, "shaders\\pbrshader.vert.spv", "shaders\\pbrshader.frag.spv", USE_MSAA, MSAA_SAMPLES);
	//std::string structurePath = { "src_assets\\primitives.gltf" };
	/*auto metalRoughSpheres = loadGltfMultiDraw(this, structurePath); */
	

	auto end1 = std::chrono::system_clock::now();
	auto elapsed1 = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start);
	fmt::print("Finished Initialization in {} seconds\n", elapsed1.count() / 1000000.0f);
	fmt::print("================================================================================\n");
}

void MainEngine::run() {
	SDL_Event e;
	bool bQuit = false;
	bool stop_rendering = false;
	SDL_SetRelativeMouseMode(SDL_FALSE);

	while (!bQuit) {
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) { bQuit = true; continue; }
			if (e.type == SDL_WINDOWEVENT) {
				if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
					stop_rendering = true;
				}
				if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
					stop_rendering = false;
				}
			}

			ImGui_ImplSDL2_ProcessEvent(&e);
		}

		if (resize_requested) { resize_swapchain(); }


		if (stop_rendering) {
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		layout_imgui();

		// actual rendering
		draw();
	}
}

void MainEngine::draw()
{
	auto start = std::chrono::system_clock::now();

	update_scene_data();

	// GPU -> CPU sync (fence)
	VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
	get_current_frame()._deletionQueue.flush();
	VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

	// GPU -> GPU sync (semaphore)
	uint32_t swapchainImageIndex;
	VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (e == VK_ERROR_OUT_OF_DATE_KHR) { resize_requested = true; fmt::print("Swapchain out of date, resize requested\n"); return; }

	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
	VK_CHECK(vkResetCommandBuffer(cmd, 0));
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); // only submit once

	_drawExtent.height = static_cast<uint32_t>(std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * _renderScale);
	_drawExtent.width = static_cast<uint32_t>(std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * _renderScale);

	VkClearColorValue clearColor = { 0.1f, 0.1f, 0.1f, 1.0f };
	VkImageSubresourceRange subresourceRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	auto start2 = std::chrono::system_clock::now();

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	switch (currentRenderView) {
	case 0:
		draw_fullscreen(cmd);
		break;
	case 1:
		draw_environment(cmd);
		break;

	}

	vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

	//vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	//vkCmdClearColorImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &subresourceRange);
	vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	draw_imgui(cmd, _swapchainImageViews[swapchainImageIndex]);
	vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	auto end2 = std::chrono::system_clock::now();
	auto elapsed2 = std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2);

	// Submission
	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame()._swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR, get_current_frame()._renderSemaphore);
	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);
	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence)); // when cmd is no longer used, fence is signaled and next command can be recorded/queued
	// Present
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &swapchainImageIndex;
	VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
		resize_requested = true;
		fmt::print("present failed - out of date, resize requested\n");
	}

	//increase the number of frames drawn
	_frameNumber++;

	auto end = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	frameTime = elapsed.count() / 1000.0f;
	drawTime = elapsed2.count() / 1000.0f;
}

void MainEngine::draw_fullscreen(VkCommandBuffer cmd)
{
	VkRenderingAttachmentInfo depthAttachment;
	VkRenderingAttachmentInfo colorAttachment;
	if (USE_MSAA) {
		VkClearValue colorClearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
		colorAttachment = vkinit::attachment_info(_drawImageBeforeMSAA.imageView, &colorClearValue, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorAttachment.resolveImageView = _drawImage.imageView;
		colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
		VkClearValue depthClearValue = { 1.0f, 0 };
		depthAttachment = vkinit::attachment_info(_depthImage.imageView, &depthClearValue, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
		depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
	}
	else {
		VkClearValue colorClearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
		colorAttachment = vkinit::attachment_info(_drawImage.imageView, &colorClearValue, VK_IMAGE_LAYOUT_GENERAL);
		VkClearValue depthClearValue = { 1.0f, 0 };
		depthAttachment = vkinit::attachment_info(_depthImage.imageView, &depthClearValue, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	}
	VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);


	vkCmdBeginRendering(cmd, &renderInfo);

	_fullscreenPipeline.bind_viewport(cmd, static_cast<float>(_drawExtent.width), static_cast<float>(_drawExtent.height), 0.0f, 1.0f);
	_fullscreenPipeline.bind_scissor(cmd, 0, 0, _drawExtent.width, _drawExtent.height);
	_fullscreenPipeline.bind_input_assembly(cmd);
	_fullscreenPipeline.bind_rasterization(cmd);
	_fullscreenPipeline.bind_depth_test(cmd);
	_fullscreenPipeline.bind_stencil(cmd);
	_fullscreenPipeline.bind_multisampling(cmd);
	_fullscreenPipeline.bind_blending(cmd);
	_fullscreenPipeline.bind_shaders(cmd);
	_fullscreenPipeline.bind_rasterizaer_discard(cmd, VK_FALSE);

	VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info = _environmentMap->get_equi_image_descriptor_buffer().get_descriptor_buffer_binding_info();
	vkCmdBindDescriptorBuffersEXT(cmd, 1, &descriptor_buffer_binding_info);

	constexpr uint32_t image_buffer_index = 0;
	VkDeviceSize image_buffer_offset = 0;
	vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _fullscreenPipelineLayout
		, 0, 1, &image_buffer_index, &image_buffer_offset);

	vkCmdDraw(cmd, 3, 1, 0, 0);
	vkCmdEndRendering(cmd);
}

void MainEngine::draw_environment(VkCommandBuffer cmd) {
	VkRenderingAttachmentInfo depthAttachment;
	VkRenderingAttachmentInfo colorAttachment;
	if (USE_MSAA) {
		VkClearValue colorClearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
		colorAttachment = vkinit::attachment_info(_drawImageBeforeMSAA.imageView, &colorClearValue, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorAttachment.resolveImageView = _drawImage.imageView;
		colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
		VkClearValue depthClearValue = { 1.0f, 0 };
		depthAttachment = vkinit::attachment_info(_depthImage.imageView, &depthClearValue, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
		depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
	}
	else {
		VkClearValue colorClearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
		colorAttachment = vkinit::attachment_info(_drawImage.imageView, &colorClearValue, VK_IMAGE_LAYOUT_GENERAL);
		VkClearValue depthClearValue = { 1.0f, 0 };
		depthAttachment = vkinit::attachment_info(_depthImage.imageView, &depthClearValue, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	}
	VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);

	vkCmdBeginRendering(cmd, &renderInfo);

	_environmentPipeline.bind_viewport(cmd, static_cast<float>(_drawExtent.width), static_cast<float>(_drawExtent.height), 0.0f, 1.0f);
	_environmentPipeline.bind_scissor(cmd, 0, 0, _drawExtent.width, _drawExtent.height);
	_environmentPipeline.bind_input_assembly(cmd);
	_environmentPipeline.bind_rasterization(cmd);
	_environmentPipeline.bind_depth_test(cmd);
	_environmentPipeline.bind_stencil(cmd);
	_environmentPipeline.bind_multisampling(cmd);
	_environmentPipeline.bind_blending(cmd);
	_environmentPipeline.bind_shaders(cmd);
	_environmentPipeline.bind_rasterizaer_discard(cmd, VK_FALSE);

	VkDescriptorBufferBindingInfoEXT bindings[2] = {
		_environmentMapSceneDataDescriptorBuffer.get_descriptor_buffer_binding_info(),
		_environmentMap->get_cubemap_descriptor_buffer().get_descriptor_buffer_binding_info(),
		//_cubemapDescriptorBuffer.get_descriptor_buffer_binding_info(),
	};

	vkCmdBindDescriptorBuffersEXT(cmd, 2, bindings);

	constexpr uint32_t scene_data_index = 0;
	constexpr uint32_t image_buffer_index = 1;
	VkDeviceSize buffer_offset = 0;
	//VkDeviceSize image_buffer_offset = _cubemapDescriptorBuffer.descriptor_buffer_size * environmentMapType;
	VkDeviceSize image_buffer_offset = _environmentMap->get_cubemap_descriptor_buffer().descriptor_buffer_size * environmentMapType;
	vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _environmentPipelineLayout
		, 0, 1, &scene_data_index, &buffer_offset);
	vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _environmentPipelineLayout
		, 1, 1, &image_buffer_index, &image_buffer_offset);

	EnvironmentDrawPushConstantData pushConstantData;
	pushConstantData.diffuseMipLevel = diffuseIrradianceMipLevel;
	if (cubemapType == 0) {
		pushConstantData.levelOfDetail = -1;
		pushConstantData.isDiffuse = VK_TRUE;
	}
	else {
		pushConstantData.isDiffuse = VK_FALSE;
		pushConstantData.levelOfDetail = levelOfDetail;
	}

	vkCmdPushConstants(cmd, _environmentPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(EnvironmentDrawPushConstantData), &pushConstantData);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	_metallicSpheres->draw(cmd, _drawExtent);

	vkCmdEndRendering(cmd);

}

void MainEngine::init_vulkan()
{

	VkResult res = volkInitialize();
	if (res != VK_SUCCESS) {
		throw std::runtime_error("Failed to initialize volk");
	}

	vkb::InstanceBuilder builder;

	// make the vulkan instance, with basic debug features
	auto inst_ret = builder.set_app_name("Equirectangular-To-Cubemap")
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

#pragma region DearImGui
void MainEngine::init_dearimgui()
{
	// DYNAMIC RENDERING (NOT RENDER PASS)
	VkDescriptorPoolSize pool_sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
	};
	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	//VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForVulkan(_window);
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _physicalDevice;
	init_info.Device = _device;
	init_info.QueueFamily = _graphicsQueueFamily;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.Subpass = 0;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	//dynamic rendering parameters for imgui to use
	init_info.UseDynamicRendering = true;
	init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;


	ImGui_ImplVulkan_Init(&init_info);
	ImGui_ImplVulkan_CreateFontsTexture();
}

void MainEngine::layout_imgui()
{
	// imgui new frame
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	if (ImGui::Begin("Main")) {
		const char* items[] = { "Equirectangular Fullscreen", "Environment Cubemap" };


		if (ImGui::BeginCombo("Render View", items[currentRenderView], 0)) {
			for (int i = 0; i < std::size(items); i++) {
				bool is_selected = (currentRenderView == i);
				if (ImGui::Selectable(items[i], is_selected)) {
					currentRenderView = i;
				}
				if (is_selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}



		if (currentRenderView == 1) {
			ImGui::SliderInt("Environment Map Type", &environmentMapType, 0, 1);
		}
		if (currentRenderView == 1 && environmentMapType == 1) {
			ImGui::SliderInt("Cubemap Type", &cubemapType, 0, 1);
			if (cubemapType == 1) {
				ImGui::SliderFloat("Level of Detail", &levelOfDetail, 0.0f, 8.0f);
			}
		}


	
		if (ImGui::Button("Save LUT")) {
			_environmentMap->save_lut_image("images\\testlut.png");
		}


		if (ImGui::CollapsingHeader("Load Cubemap (From Equirectangular Image)")) {
			ImGui::InputText("Path to Equirecangular Image", &_equirectangularPath);
			EnvironmentMap& current = *_environmentMap;
			ImGui::Checkbox("Flip Y", &current.flip_y);
			ImGui::SliderFloat("Diffuse Irradiance Sample Delta", &current.diffuse_sample_delta, 0.005f, 0.5f);
			ImGui::SliderInt("Specular Prefilter Sample Count", &current.specular_sample_count, 1, 2048);

			if (ImGui::Button("Load Image")) {
				bool initialize = false;
				if (_environmentMap->load_equirectangular_image(_equirectangularPath.data(), initialize)) {
					_environmentMap->load_cubemap(initialize);
				}
			}


		}

		/*if (ImGui::CollapsingHeader("Cubemap Save")) {
			ImGui::Checkbox("Enable Custom Save Path", &_customOutputPath);
			if (_customOutputPath) {
				ImGui::InputText("Cubemap Save Path", &_cubemapSavePath);
			}

			if (ImGui::Button("Save Cubemap")) {
				fmt::print("--------------------------------------------------------------------------------\n");
				fmt::print("Saving Cubemap\n");
				std::string p;
				if (_customOutputPath) {
					p = _cubemapSavePath;
				}
				else {
					p = getParentFolder(_cubemapImagePath);
				}
				save_cubemap(p.data());
				fmt::print("Cubemap Saved\n");
				fmt::print("--------------------------------------------------------------------------------\n");

			}

			if (ImGui::Button("Save Diffuse Irradiance")) {
				fmt::print("--------------------------------------------------------------------------------\n");
				fmt::print("Saving Diffuse Irradiance\n");
				std::string p;
				if (_customOutputPath) {
					p = _cubemapSavePath;
				}
				else {
					p = getParentFolder(_cubemapImagePath);
				}
				save_diffuse_irradiance(p.data());
				fmt::print("Diffuse Irradiance Saved\n");
				fmt::print("--------------------------------------------------------------------------------\n");
			}

		}*/





		if (ImGui::CollapsingHeader("Frame Data")) {
			ImGui::Text("Frame Time: %.2f ms", frameTime);
			ImGui::Text("Draw Time: %.2f ms", drawTime);
		}

		ImGui::SliderFloat("Render Scale", &_renderScale, 0.1f, _maxRenderScale);
	}
	ImGui::End();
	ImGui::Render();

}

void MainEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);
	vkCmdBeginRendering(cmd, &renderInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	vkCmdEndRendering(cmd);
}
#pragma endregion

void MainEngine::init_default_data()
{
#pragma region Basic Textures
	//3 default textures, white, grey, black. 1 pixel each
	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	_whiteImage = _resourceConstructor->create_image((void*)&white, 4, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
	_greyImage = _resourceConstructor->create_image((void*)&grey, 4, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
	_blackImage = _resourceConstructor->create_image((void*)&black, 4, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	//checkerboard image
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16 > pixels{}; //for 16x16 checkerboard texture
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 16; y++) {
			pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
		}
	}
	_errorCheckerboardImage = _resourceConstructor->create_image(pixels.data(), 16 * 16 * 4, VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
#pragma endregion

#pragma region Default Samplers
	VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	sampl.magFilter = VK_FILTER_NEAREST;
	sampl.minFilter = VK_FILTER_NEAREST;
	sampl.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampl.minLod = 0;
	sampl.maxLod = VK_LOD_CLAMP_NONE;


	vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest);

	sampl.magFilter = VK_FILTER_LINEAR;
	sampl.minFilter = VK_FILTER_LINEAR;

	vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

	_mainDeletionQueue.push_function([&]() {
		_resourceConstructor->destroy_image(_whiteImage);
		_resourceConstructor->destroy_image(_greyImage);
		_resourceConstructor->destroy_image(_blackImage);
		_resourceConstructor->destroy_image(_errorCheckerboardImage);
		vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
		vkDestroySampler(_device, _defaultSamplerLinear, nullptr);
		});
#pragma endregion
}

void MainEngine::init_descriptors()
{
	{
		DescriptorLayoutBuilder layoutBuilder;
		layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		singleUniformDescriptorSetLayout = layoutBuilder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT
			, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

	}

	_environmentMapSceneDataBuffer = _resourceConstructor->create_buffer(sizeof(CubemapSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	_environmentMapSceneDataDescriptorBuffer = DescriptorBufferUniform(_instance, _device
		, _physicalDevice, _allocator, singleUniformDescriptorSetLayout, 1);
	_environmentMapSceneDataDescriptorBuffer.setup_data(_device, _environmentMapSceneDataBuffer, sizeof(CubemapSceneData));

	_sceneDataBuffer = _resourceConstructor->create_buffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	_sceneDataDescriptorBuffer = DescriptorBufferUniform(_instance, _device, _physicalDevice, _allocator, singleUniformDescriptorSetLayout, 1);
	_sceneDataDescriptorBuffer.setup_data(_device, _sceneDataBuffer, sizeof(SceneData));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyDescriptorSetLayout(_device, singleUniformDescriptorSetLayout, nullptr);

		_resourceConstructor->destroy_buffer(_sceneDataBuffer);
		_sceneDataDescriptorBuffer.destroy(_device, _allocator);

		_resourceConstructor->destroy_buffer(_environmentMapSceneDataBuffer);
		_environmentMapSceneDataDescriptorBuffer.destroy(_device, _allocator);
		});
}

void MainEngine::init_pipelines()
{
	

	assert(EnvironmentMap::layoutsCreated);

	// Rendering Pipelines (Used in main draw loop)
	//  Environment Map Background
	{
		VkDescriptorSetLayout layouts[2] =
		{ singleUniformDescriptorSetLayout, EnvironmentMap::_cubemapDescriptorSetLayout };

		VkPushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(EnvironmentDrawPushConstantData);

		VkPipelineLayoutCreateInfo layout_info = vkinit::pipeline_layout_create_info();
		layout_info.setLayoutCount = 2;
		layout_info.pSetLayouts = layouts;
		layout_info.pPushConstantRanges = &pushConstantRange;
		layout_info.pushConstantRangeCount = 1;

		VK_CHECK(vkCreatePipelineLayout(_device, &layout_info, nullptr, &_environmentPipelineLayout));

		_environmentPipeline = {};

		_environmentPipeline.init_input_assembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		_environmentPipeline.init_rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
		if (USE_MSAA) _environmentPipeline.enable_msaa(MSAA_SAMPLES);
		else _environmentPipeline.disable_multisampling();

		_environmentPipeline.init_blending(ShaderObject::BlendMode::NO_BLEND);
		_environmentPipeline.enable_depthtesting(false, VK_COMPARE_OP_LESS_OR_EQUAL);

		_environmentPipeline._stages[0] = VK_SHADER_STAGE_VERTEX_BIT;
		_environmentPipeline._stages[1] = VK_SHADER_STAGE_FRAGMENT_BIT;
		_environmentPipeline._stages[2] = VK_SHADER_STAGE_GEOMETRY_BIT;

		vkutil::create_shader_objects(
			"shaders/environment.vert.spv", "shaders/environment.frag.spv"
			, _device, _environmentPipeline._shaders
			, 2, layouts
			, 1, &pushConstantRange
		);

		_mainDeletionQueue.push_function([=]() {
			vkDestroyPipelineLayout(_device, _environmentPipelineLayout, nullptr);
			vkDestroyShaderEXT(_device, _environmentPipeline._shaders[0], nullptr);
			vkDestroyShaderEXT(_device, _environmentPipeline._shaders[1], nullptr);
			});
	}

	//  Fullscreen Background Pipeline
	{
		VkDescriptorSetLayout layouts[1] = { EnvironmentMap::_equiImageDescriptorSetLayout };


		VkPipelineLayoutCreateInfo layout_info = vkinit::pipeline_layout_create_info();
		layout_info.setLayoutCount = 1;
		layout_info.pSetLayouts = layouts;;
		layout_info.pPushConstantRanges = nullptr;
		layout_info.pushConstantRangeCount = 0;

		VK_CHECK(vkCreatePipelineLayout(_device, &layout_info, nullptr, &_fullscreenPipelineLayout));


		_fullscreenPipeline = {};

		_fullscreenPipeline.init_input_assembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		_fullscreenPipeline.init_rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
		if (USE_MSAA) _fullscreenPipeline.enable_msaa(MSAA_SAMPLES);
		else _fullscreenPipeline.disable_multisampling();

		_fullscreenPipeline.init_blending(ShaderObject::BlendMode::NO_BLEND);
		_fullscreenPipeline.disable_depthtesting();


		_fullscreenPipeline._stages[0] = VK_SHADER_STAGE_VERTEX_BIT;
		_fullscreenPipeline._stages[1] = VK_SHADER_STAGE_FRAGMENT_BIT;
		_fullscreenPipeline._stages[2] = VK_SHADER_STAGE_GEOMETRY_BIT;


		vkutil::create_shader_objects(
			"shaders/fullscreen.vert.spv", "shaders/fullscreen.frag.spv"
			, _device, _fullscreenPipeline._shaders
			, 1, layouts
			, 0, nullptr
		);


		_mainDeletionQueue.push_function([=]() {
			vkDestroyPipelineLayout(_device, _fullscreenPipelineLayout, nullptr);
			vkDestroyShaderEXT(_device, _fullscreenPipeline._shaders[0], nullptr);
			vkDestroyShaderEXT(_device, _fullscreenPipeline._shaders[1], nullptr);
			});
	}

	
}

void MainEngine::update_scene_data()
{
	float time = SDL_GetTicks64() / 1000.0f;
	float angle = time / 10.0f * glm::radians(90.0f);
	float aspect = static_cast<float>(_windowExtent.width) / static_cast<float>(_windowExtent.height);

	glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 tra = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 10.0f));
	glm::vec4 camera_position = rot * tra * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	glm::vec4 camera_view_direction = glm::normalize(glm::vec4(0.0f) - camera_position);
	glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);


	CubemapSceneData environmentMapSceneData{};
	environmentMapSceneData.view = glm::lookAt(glm::vec3(0.0f), glm::vec3(camera_view_direction), glm::vec3(0.0f, 1.0f, 0.0f));
	environmentMapSceneData.proj = projection;
	environmentMapSceneData.viewproj = environmentMapSceneData.proj * environmentMapSceneData.view;

	CubemapSceneData* p_cubemap_scene_data = (CubemapSceneData*)_environmentMapSceneDataBuffer.info.pMappedData;
	memcpy(p_cubemap_scene_data, &environmentMapSceneData, sizeof(CubemapSceneData));


	SceneData sceneData = {};
	sceneData.view = glm::lookAt(glm::vec3(camera_position), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	sceneData.proj = projection;
	sceneData.proj[1][1] *= -1; // invert Y
	sceneData.viewproj = sceneData.proj * sceneData.view;
	sceneData.ambientColor = glm::vec4(.1f);
	sceneData.sunlightColor = glm::vec4(1.0f, 1.0f, 1.0f, 2.0f);
	sceneData.sunlightDirection = glm::vec4(0, 1, 0.5f, 1.f); // inverted to match openGL up/down
	sceneData.cameraPosition = glm::vec4(camera_position);


	SceneData *p_scene_data = (SceneData*)_sceneDataBuffer.info.pMappedData;
	memcpy(p_scene_data, &sceneData, sizeof(SceneData));


	glm::mat4 object_model = glm::mat4(1.0f);
	_metallicSpheres->update_draw_data(object_model);

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
	auto start = std::chrono::system_clock::now();
	SDL_SetRelativeMouseMode(SDL_FALSE);
	vkDeviceWaitIdle(_device);

	_mainDeletionQueue.flush();

	_environmentMap.reset();


	// IMGUI
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(_device, imguiPool, nullptr);

	// Draw Structures
	_metallicSpheres.reset();
	//_metallicSpheres->destroy(_device, _allocator);


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
	auto end = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	fmt::print("Cleanup done in {} seconds\n", elapsed.count() / 1000000.0f);
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

	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 10000000000000000));
}

void MainEngine::resize_swapchain() {
	vkDeviceWaitIdle(_device);

	destroy_swapchain();
	destroy_draw_iamges();
	int w, h;
	// get new window size
	SDL_GetWindowSize(_window, &w, &h);
	_windowExtent.width = w;
	_windowExtent.height = h;

	fmt::print("New Screen Resolution: {}x{}\n", w, h);
	// will always be one
	_maxRenderScale = std::min(
		(float)_drawImage.imageExtent.width / (float)_windowExtent.width
		, (float)_drawImage.imageExtent.height / (float)_windowExtent.height
	);
	_maxRenderScale = std::max(_maxRenderScale, 1.0f);
	_maxRenderScale = 1.0f;
	_renderScale = std::min(_maxRenderScale, _renderScale);
	create_swapchain(_windowExtent.width, _windowExtent.height);
	create_draw_images(_windowExtent.width, _windowExtent.height);

	resize_requested = false;
}

std::string MainEngine::getParentFolder(const std::string& filePath) {
	std::filesystem::path path(filePath);
	std::filesystem::path parentPath = path.parent_path();
	return parentPath.string();
}