#pragma once
#include "big_header.h"
#include "vk_types.h"

#include "vk_initializers.h"
#include "vk_images.h"
#include "vk_descriptors.h"
#include "vk_descriptor_buffer.h"
#include "vk_pipelines.h"
#include "vk_draw_structure.h"
#include "vk_loader.h"

constexpr unsigned int FRAME_OVERLAP = 2;
constexpr int specularPrefilteredMipLevels = 10;
constexpr VkExtent3D specularPrefilteredBaseExtents = { 512, 512, 1 }; 

struct LoadedGLTFMultiDraw;
struct GLTFMetallic_RoughnessMultiDraw;

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};

struct FrameData {
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;
	VkSemaphore _swapchainSemaphore, _renderSemaphore;
	VkFence _renderFence;

	// Frame Lifetime Deletion Queue
	DeletionQueue _deletionQueue;
};

struct EnvironmentDrawPushConstantData {
	float levelOfDetail;
	float pad;
	float pad2;
	float pad3;
};

struct EquiToCubePushConstantData {
	bool flipY;
	float pad;
	float pad2;
	float pad3;
};

struct CubeToDiffusePushConstantData {
	float sampleDelta;
	float pad;
	float pad2;
	float pad3;
};

struct CubeToPrefilteredConstantData {
	float roughness;
	uint32_t imageWidth;
	uint32_t imageHeight;
	uint32_t sampleCount;
};


class MainEngine {
public:
	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;
	VkSurfaceKHR _surface;
	VmaAllocator _allocator;

	VkExtent2D _windowExtent{ 1700 , 900 };
	struct SDL_Window* _window{ nullptr };
	bool resize_requested{ false };

	int _frameNumber{ 0 };
	float frameTime{ 0.0f };
	float drawTime{ 0.0f };

	// Graphics Queue Family
	FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	DeletionQueue _mainDeletionQueue;

	//draw resources
	AllocatedImage _drawImage;
	AllocatedImage _drawImageBeforeMSAA;
	AllocatedImage _depthImage;
	VkExtent2D _drawExtent;
	float _renderScale{ 1.0f };
	float _maxRenderScale{ 1.0f };

	// Swapchain
	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;

	// immediate submit structures
	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;
	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	// Dear ImGui
	VkDescriptorPool imguiPool;

	// Default textures/samplers
	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;
	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;

	// Application Data
	std::string _equirectangularPath;
	std::string _cubemapSavePath;
	bool _flipY{ false };
	bool _customOutputPath{ false };
	float _diffuseSampleDelta{ 0.025f };
	float _specularSampleCount{ 1024.0f };

	int currentRenderView{ 1 };
	int environmentMapType{ 0 };
	float levelOfDetail{ 0.0f };
	
	std::string _cubemapImagePath{};
	AllocatedImage _equiImage; // equi image
	AllocatedImage splitCubemapImage; // cubemap image
	AllocatedImage _diffuseIrradianceImage; // irradiance image
	AllocatedCubemap _prefilteredSpecularCubemapMips; // prefiltered image
	uint32_t _cubemapResolution{ 1024 };

#pragma region Images
	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, size_t dataSize, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_cubemap(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	int get_channel_count(VkFormat format);
	void destroy_image(const AllocatedImage& img);
	void destroy_cubemapMips(const AllocatedCubemap& img);
#pragma endregion

#pragma region VkBuffers
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	AllocatedBuffer create_staging_buffer(size_t allocSize);
	void copy_buffer(AllocatedBuffer src, AllocatedBuffer dst, VkDeviceSize size);
	VkDeviceAddress get_buffer_address(AllocatedBuffer buffer);
	void destroy_buffer(const AllocatedBuffer& buffer);
	void resize_swapchain();
	std::string getParentFolder(const std::string& filePath);
#pragma endregion


	VkPipelineLayout _fullscreenPipelineLayout;
	ShaderObject _fullscreenPipeline;

	// equi to cubemap pipeline
	VkPipelineLayout _cubemapPipelineLayout;
	VkPipeline _cubemapPipeline;

	// cubemap to diffuse irradiance and prefiltered specular pipeline
	VkPipelineLayout _diffuseIrradiancePipelineLayout;
	VkPipeline _diffuseIrradiancePipeline;

	// cubemap to prefiltered specular pipeline
	VkPipelineLayout _prefilteredSpecularPipelineLayout;
	VkPipeline _prefilteredSpecularPipeline;


	VkPipelineLayout _environmentPipelineLayout;
	ShaderObject _environmentPipeline;
	

	// 0 is equi image
	VkDescriptorSetLayout _equiImageDescriptorSetLayout;
	DescriptorBufferSampler _equiImageDescriptorBuffer;
	// [STORAGE] 0 is raw cubemap, 1 is irradiance, 2 is prefiltered
	VkDescriptorSetLayout _cubemapStorageDescriptorSetLayout;
	DescriptorBufferSampler _cubemapStorageDescriptorBuffer;
	// [SAMPLED] 0 is raw cubemap, 1 is irradiance, 2 is prefiltered
	VkDescriptorSetLayout _cubemapDescriptorSetLayout;
	DescriptorBufferSampler _cubemapDescriptorBuffer;

	AllocatedBuffer _environmentMapSceneDataBuffer;
	DescriptorBufferUniform _environmentMapSceneDataDescriptorBuffer;

	VkDescriptorSetLayout bufferAddressesDescriptorSetLayout;
	VkDescriptorSetLayout sceneDataDescriptorSetLayout;
	VkDescriptorSetLayout textureDescriptorSetLayout;
	std::shared_ptr<GLTFMetallic_RoughnessMultiDraw> _metallicSpheres;

	void init();
	void run();
	void cleanup();

	void draw();

private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();
	void init_default_data();

	void init_dearimgui();
	void layout_imgui();
	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

	void draw_fullscreen(VkCommandBuffer cmd);
	void draw_environment(VkCommandBuffer cmd);

	void init_pipelines();

	void update_scene_data();


	void create_swapchain(uint32_t width, uint32_t height);
	void create_draw_images(uint32_t width, uint32_t height);
	void destroy_swapchain();
	void destroy_draw_iamges();

	bool load_equirectangular(const char* path);
	void load_cubemap();
	void save_cubemap(const char* path);
	void save_diffuse_irradiance(const char* path);

	void draw_equi_to_cubemap_immediate();
	void draw_cubemap_to_diffuse_immediate();
	void draw_cubemap_to_prefiltered_specular_immediate(int mipLevels);
};

