#pragma once
#include "big_header.h"
#include "vk_types.h"

#include "engine.h"
#include "vk_descriptor_buffer.h"
#include "vk_loader.h"

class MainEngine;
struct LoadedGLTFMultiDraw;


struct MultiDrawBuffers {
	AllocatedBuffer indirectDrawBuffer;
	uint32_t instanceCount;
};


struct GLTFMetallic_RoughnessMultiDraw {
	bool hasTransparents() const;
	bool hasOpaques() const;

	std::shared_ptr<LoadedGLTFMultiDraw> scene_ptr;

	std::shared_ptr<ShaderObject> shaderObject;
	// holds indirect draw buffers. Transparent will not go through compute culling.

	VkPipelineLayout layout;


	AllocatedBuffer indexBuffer;

	// BINDING 0: BUFFER ADDRESSES
	DescriptorBufferUniform buffer_addresses;
	AllocatedBuffer buffer_addresses_underlying;
	AllocatedBuffer vertexBuffer;
	AllocatedBuffer materialBuffer;
	AllocatedBuffer instanceBuffer;
	// access correct instance data w/ gl_instanceID. 

	// BINDING 1: SCENE DATA
	DescriptorBufferUniform scene_data;
	AllocatedBuffer sceneDataBuffer;
	// BINDING 2: TEXTURE DATA
	//	when initializing, set descriptor count to be equal to the number of textures
	DescriptorBufferSampler texture_data;

	// BINDING 2 (Compute): INDIRECT DRAW BUFFER
	DescriptorBufferUniform compute_culling_data_buffer_address;
	AllocatedBuffer indirect_draw_buffer_underlying;
	MultiDrawBuffers opaqueDrawBuffers;
	MultiDrawBuffers transparentDrawBuffers;
	AllocatedBuffer boundingSphereBuffer;

	void build_pipelines(MainEngine* engine, bool use_msaa, VkSampleCountFlagBits sample_count, const char* vertShaderPath, const char* fragShaderPath);


	// buffer building
	size_t index_buffer_size = 0;
	size_t number_of_instances = 0;
	std::vector<InstanceData> instanceData;
	std::vector<MeshData> meshData;
	// store offsets of vertices
	std::vector<uint32_t> vertexOffsets;

	void load_gltf(MainEngine* engine, std::string& pathToScene);

	void build_buffers(MainEngine* engine);
	void recursive_node_process(LoadedGLTFMultiDraw& scene, Node& node, glm::mat4& topMatrix);
	void recursive_node_process_instance_data(LoadedGLTFMultiDraw& scene, Node& node, glm::mat4& topMatrix, int& current_model_index);
	void update_draw_data(SceneData& sceneData, glm::mat4& model_matrix);
	void update_model_matrix(glm::mat4& topMatrix);

	bool buffersBuilt{ false };

	const uint32_t buffer_addresses_descriptor_index = 0;
	const uint32_t scene_data_descriptor_index = 1;
	const uint32_t texture_data_descriptor_index = 2;
	const uint32_t compute_culling_data_descriptor_index = 2;
	const VkDeviceSize offsets = 0;
	void cull(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout pipelineLayout);
	void draw(VkCommandBuffer cmd, VkExtent2D drawExtents);



	void destroy(VkDevice device, VmaAllocator allocator);
};
