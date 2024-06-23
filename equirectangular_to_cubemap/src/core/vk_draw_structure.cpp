#include "vk_draw_structure.h"

VkDescriptorSetLayout GLTFMetallic_RoughnessMultiDraw::bufferAddressesDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout GLTFMetallic_RoughnessMultiDraw::textureDescriptorSetLayout = VK_NULL_HANDLE;
bool GLTFMetallic_RoughnessMultiDraw::layoutsCreated = false;
int GLTFMetallic_RoughnessMultiDraw::useCount = 0;

bool GLTFMetallic_RoughnessMultiDraw::hasTransparents() const { return transparentDrawBuffers.instanceCount > 0; }

bool GLTFMetallic_RoughnessMultiDraw::hasOpaques() const { return opaqueDrawBuffers.instanceCount > 0; }

GLTFMetallic_RoughnessMultiDraw::GLTFMetallic_RoughnessMultiDraw(
	MainEngine* engine, std::string& pathToScene, const char* vertShaderPath, const char* fragShaderPath
	, bool use_msaa, VkSampleCountFlagBits sample_count)
{
	creator = engine;
	if (!layoutsCreated) {
		{
			DescriptorLayoutBuilder layoutBuilder;
			layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
			bufferAddressesDescriptorSetLayout = layoutBuilder.build(engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT
				, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);
		}
		{
			DescriptorLayoutBuilder layoutBuilder;
			layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_SAMPLER, 32); // I dont expect any models to have more than 32 samplers
			layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 255); // 255 is upper limit of textures in this engine

			textureDescriptorSetLayout = layoutBuilder.build(engine->_device, VK_SHADER_STAGE_FRAGMENT_BIT
				, nullptr, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);
		}

		layoutsCreated = true;
	}

	buffer_addresses = DescriptorBufferUniform(engine->_instance, engine->_device
		, engine->_physicalDevice, engine->_allocator, bufferAddressesDescriptorSetLayout, 1);
	texture_data = DescriptorBufferSampler(engine->_instance, engine->_device
		, engine->_physicalDevice, engine->_allocator, textureDescriptorSetLayout, 1);


	VkDescriptorSetLayout layouts[] = {
		bufferAddressesDescriptorSetLayout,
		textureDescriptorSetLayout,
		engine->get_scene_data_descriptor_set_layout(),
	};

	// Pipeline Layout
	{
		VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
		mesh_layout_info.setLayoutCount = 3;
		mesh_layout_info.pSetLayouts = layouts;
		mesh_layout_info.pPushConstantRanges = nullptr;
		mesh_layout_info.pushConstantRangeCount = 0;

		VkPipelineLayout newLayout;
		VK_CHECK(vkCreatePipelineLayout(engine->_device, &mesh_layout_info, nullptr, &newLayout));

		renderPipelineLayout = newLayout;

	}

	// Shader Object
	{
		shaderObject = std::make_shared<ShaderObject>();

		shaderObject->init_input_assembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		shaderObject->init_rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
		if (use_msaa) {
			shaderObject->enable_msaa(sample_count);
		}
		else {
			shaderObject->disable_multisampling();
		}
		shaderObject->init_blending(ShaderObject::BlendMode::NO_BLEND);
		//shaderObject->enable_depthtesting(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
		shaderObject->enable_depthtesting(true, VK_COMPARE_OP_LESS_OR_EQUAL);


		shaderObject->_stages[0] = VK_SHADER_STAGE_VERTEX_BIT;
		shaderObject->_stages[1] = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderObject->_stages[2] = VK_SHADER_STAGE_GEOMETRY_BIT;


		vkutil::create_shader_objects(
			vertShaderPath, fragShaderPath
			, engine->_device, shaderObject->_shaders
			, 3, layouts
			, 0, nullptr
		);
	}


	auto _scene = loadGltfMultiDraw(engine, pathToScene);
	if (_scene == nullptr) {
		fmt::print("Failed to load GLTF file\n");
		return;
	}
	else {
		fmt::print("Loaded GLTF model at path: {}\n", pathToScene);
	}
	scene_ptr = *_scene;

	build_buffers(engine);

	useCount++;
}

GLTFMetallic_RoughnessMultiDraw::~GLTFMetallic_RoughnessMultiDraw()
{
	useCount--;
	if (useCount == 0) {
		vkDestroyDescriptorSetLayout(creator->_device, bufferAddressesDescriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(creator->_device, textureDescriptorSetLayout, nullptr);
		layoutsCreated = false;
	}


	buffer_addresses.destroy(creator->_device, creator->_allocator);
	texture_data.destroy(creator->_device, creator->_allocator);
	compute_culling_data_buffer_address.destroy(creator->_device, creator->_allocator);

	vmaDestroyBuffer(creator->_allocator, vertexBuffer.buffer, vertexBuffer.allocation);
	vmaDestroyBuffer(creator->_allocator, indexBuffer.buffer, indexBuffer.allocation);
	vmaDestroyBuffer(creator->_allocator, instanceBuffer.buffer, instanceBuffer.allocation);
	vmaDestroyBuffer(creator->_allocator, materialBuffer.buffer, materialBuffer.allocation);
	vmaDestroyBuffer(creator->_allocator, buffer_addresses_underlying.buffer, buffer_addresses_underlying.allocation);

	if (opaqueDrawBuffers.instanceCount > 0) {
		vmaDestroyBuffer(creator->_allocator, opaqueDrawBuffers.indirectDrawBuffer.buffer, opaqueDrawBuffers.indirectDrawBuffer.allocation);
	}
	if (transparentDrawBuffers.instanceCount > 0) {
		vmaDestroyBuffer(creator->_allocator, transparentDrawBuffers.indirectDrawBuffer.buffer, transparentDrawBuffers.indirectDrawBuffer.allocation);
	}
	//vmaDestroyBuffer(creator->_allocator, indirect_draw_buffer_underlying.buffer, indirect_draw_buffer_underlying.allocation);,
	//vmaDestroyBuffer(creator->_allocator, boundingSphereBuffer.buffer, boundingSphereBuffer.allocation);

	vkDestroyPipelineLayout(creator->_device, renderPipelineLayout, nullptr);

	// TODO: can probably make the shaders static too?
	vkDestroyShaderEXT(creator->_device, shaderObject->_shaders[0], nullptr);
	vkDestroyShaderEXT(creator->_device, shaderObject->_shaders[1], nullptr);

	scene_ptr.reset();

	fmt::print("Destroyed GLTFMetallic_RoughnessMultiDraw\n");

}

void GLTFMetallic_RoughnessMultiDraw::build_buffers(MainEngine* engine)
{
	if (buffersBuilt) { return; }
	buffersBuilt = true;

	LoadedGLTFMultiDraw& scene = *this->scene_ptr;

	size_t vertexOffset{ 0 };
	std::vector<MultiDrawVertex> allVertices;
	std::vector<BoundingSphere> meshBoundingSpheres;
	meshBoundingSpheres.reserve(scene.meshes.size());
	for (RawMeshData& r : scene.meshes) {
		vertexOffsets.push_back(static_cast<uint32_t>(vertexOffset));
		vertexOffset += r.vertices.size();
		allVertices.insert(allVertices.end(), r.vertices.begin(), r.vertices.end());

		BoundingSphere bounds = BoundingSphere(r);
		meshBoundingSpheres.push_back(bounds);


		MeshData mdata{};
		mdata.index_buffer_offset = static_cast<uint32_t>(index_buffer_size);
		mdata.indices = r.indices;
		index_buffer_size += r.indices.size() * sizeof(r.indices[0]);
		assert(r.indices.size() % 3 == 0);
		mdata.transparent = r.hasTransparent;

		meshData.push_back(mdata);
	}



	glm::mat4 mMatrix = glm::mat4(1.0f);
	for (auto& n : scene.topNodes) {
		recursive_node_process(scene, *n.get(), mMatrix);
	}

	// Vertex Data (Per Sub-Mesh), Index Data (Per Instance), Instance Data (Per Instance), Material Data (Per Material)
	{
		number_of_instances = instanceData.size();
		vertexBuffer = engine->_resourceConstructor->create_buffer(allVertices.size() * sizeof(MultiDrawVertex)
			, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
			, VMA_MEMORY_USAGE_GPU_ONLY);
		indexBuffer = engine->_resourceConstructor->create_buffer(index_buffer_size
			, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
			, VMA_MEMORY_USAGE_GPU_ONLY);
		materialBuffer = engine->_resourceConstructor->create_buffer(scene.materials.size() * sizeof(MaterialData)
			, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			, VMA_MEMORY_USAGE_GPU_ONLY);

		AllocatedBuffer staging_vertex = engine->_resourceConstructor->create_staging_buffer(allVertices.size() * sizeof(MultiDrawVertex));
		AllocatedBuffer staging_index = engine->_resourceConstructor->create_staging_buffer(index_buffer_size);
		AllocatedBuffer staging_material = engine->_resourceConstructor->create_staging_buffer(scene.materials.size() * sizeof(MaterialData));

		memcpy(staging_vertex.info.pMappedData, allVertices.data(), allVertices.size() * sizeof(MultiDrawVertex));
		for (size_t i = 0; i < number_of_instances; i++) {
			MeshData& d = meshData[instanceData[i].meshIndex];
			memcpy(
				(char*)staging_index.info.pMappedData + d.index_buffer_offset
				, d.indices.data()
				, d.indices.size() * sizeof(uint32_t));
		}
		memcpy(staging_material.info.pMappedData, scene.materials.data(), scene.materials.size() * sizeof(MaterialData));
		engine->_resourceConstructor->copy_buffer(staging_vertex, vertexBuffer, allVertices.size() * sizeof(MultiDrawVertex));
		engine->_resourceConstructor->copy_buffer(staging_index, indexBuffer, index_buffer_size);
		engine->_resourceConstructor->copy_buffer(staging_material, materialBuffer, scene.materials.size() * sizeof(MaterialData));


		instanceBuffer = engine->_resourceConstructor->create_buffer(instanceData.size() * sizeof(InstanceData)
			, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			, VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(instanceBuffer.info.pMappedData, instanceData.data(), instanceData.size() * sizeof(InstanceData));


		engine->_resourceConstructor->destroy_buffer(staging_index);
		engine->_resourceConstructor->destroy_buffer(staging_vertex);
		engine->_resourceConstructor->destroy_buffer(staging_material);
	}


	// Indirect Draw Buffers
	{
		constexpr auto default_indirect_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		auto           indirect_flags = default_indirect_flags;
		indirect_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;


		// Opaque Draws
		std::vector<VkDrawIndexedIndirectCommand> cpu_commands;
		size_t opaque_command_count = 0;
		for (size_t i = 0; i < number_of_instances; ++i) {
			MeshData& md = meshData[instanceData[i].meshIndex];
			if (md.transparent) { continue; }
			opaque_command_count++;

			VkDrawIndexedIndirectCommand cmd{};
			cmd.firstIndex = md.index_buffer_offset / (sizeof(md.indices[0]));
			cmd.indexCount = static_cast<uint32_t>(md.indices.size());
			cmd.vertexOffset = static_cast<uint32_t>(0); // supplied by instance data
			cmd.firstInstance = static_cast<uint32_t>(i);
			cmd.instanceCount = 1;
			cpu_commands.push_back(cmd);
		}

		if (opaque_command_count > 0) {
			opaqueDrawBuffers.indirectDrawBuffer = engine->_resourceConstructor->create_buffer(opaque_command_count * sizeof(VkDrawIndexedIndirectCommand)
				, indirect_flags, VMA_MEMORY_USAGE_GPU_ONLY);
			opaqueDrawBuffers.instanceCount = static_cast<uint32_t>(opaque_command_count);

			AllocatedBuffer staging_indirect = engine->_resourceConstructor->create_staging_buffer(opaque_command_count * sizeof(VkDrawIndexedIndirectCommand));
			memcpy(staging_indirect.info.pMappedData, cpu_commands.data(), opaque_command_count * sizeof(VkDrawIndexedIndirectCommand));
			engine->_resourceConstructor->copy_buffer(staging_indirect, opaqueDrawBuffers.indirectDrawBuffer, opaque_command_count * sizeof(VkDrawIndexedIndirectCommand));

			engine->_resourceConstructor->destroy_buffer(staging_indirect);
		}

		// Transparent Draws
		std::vector<VkDrawIndexedIndirectCommand> cpu_commands_transparent;
		size_t transparent_command_count = 0;
		for (size_t i = 0; i < number_of_instances; ++i) {
			MeshData& md = meshData[instanceData[i].meshIndex];
			if (!md.transparent) { continue; }
			transparent_command_count++;

			VkDrawIndexedIndirectCommand cmd{};
			cmd.firstIndex = md.index_buffer_offset / (sizeof(md.indices[0]));
			cmd.indexCount = static_cast<uint32_t>(md.indices.size());
			cmd.vertexOffset = static_cast<uint32_t>(0);// supplied by instance data
			cmd.firstInstance = static_cast<uint32_t>(i);
			cmd.instanceCount = 1;
			cpu_commands_transparent.push_back(cmd);
		}
		if (transparent_command_count > 0) {
			transparentDrawBuffers.indirectDrawBuffer = engine->_resourceConstructor->create_buffer(transparent_command_count * sizeof(VkDrawIndexedIndirectCommand)
				, indirect_flags, VMA_MEMORY_USAGE_GPU_ONLY);
			transparentDrawBuffers.instanceCount = static_cast<uint32_t>(transparent_command_count);

			AllocatedBuffer staging_indirect_transparent = engine->_resourceConstructor->create_staging_buffer(transparent_command_count * sizeof(VkDrawIndexedIndirectCommand));
			memcpy(staging_indirect_transparent.info.pMappedData, cpu_commands_transparent.data(), transparent_command_count * sizeof(VkDrawIndexedIndirectCommand));
			engine->_resourceConstructor->copy_buffer(staging_indirect_transparent, transparentDrawBuffers.indirectDrawBuffer, transparent_command_count * sizeof(VkDrawIndexedIndirectCommand));

			engine->_resourceConstructor->destroy_buffer(staging_indirect_transparent);
		}
	}

	// Descriptors (Binding 0, 1, 2)
	{
		//  ADDRESSES
		VkDeviceAddress addresses[3];
		addresses[0] = engine->_resourceConstructor->get_buffer_address(vertexBuffer);
		addresses[1] = engine->_resourceConstructor->get_buffer_address(materialBuffer);
		addresses[2] = engine->_resourceConstructor->get_buffer_address(instanceBuffer);
		buffer_addresses_underlying = engine->_resourceConstructor->create_buffer(sizeof(addresses), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(buffer_addresses_underlying.info.pMappedData, addresses, sizeof(addresses));
		buffer_addresses.setup_data(engine->_device, buffer_addresses_underlying, sizeof(addresses));

		//  TEXTURES/SAMPLERS
		std::vector<DescriptorImageData> texture_descriptors;
		std::vector<VkDescriptorImageInfo> samplerDescriptors;
		assert(scene.samplers.size() <= 32);
		for (int i = 0; i < scene.samplers.size(); i++) {
			samplerDescriptors.push_back(
				{ .sampler = scene.samplers[i] }
			);
		};
		texture_descriptors.push_back({ VK_DESCRIPTOR_TYPE_SAMPLER, samplerDescriptors.data(), scene.samplers.size() });

		size_t samplers_remaining = 32 - scene.samplers.size();
		if (samplers_remaining > 0) {
			texture_descriptors.push_back({ VK_DESCRIPTOR_TYPE_SAMPLER, nullptr, samplers_remaining });
		}

		std::vector<VkDescriptorImageInfo> textureDescriptors;
		for (int i = 0; i < scene.images.size(); i++) {
			textureDescriptors.push_back(
				{ .imageView = scene.images[i].imageView, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
			);
		};
		texture_descriptors.push_back({ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, textureDescriptors.data() , scene.images.size() });

		size_t remaining = 255 - scene.images.size();
		// if there is another binding after the 255 textures, need to pushback the remainder to offset

		texture_data.setup_data(engine->_device, texture_descriptors);
	}

	// Indirect Draw Buffer Addresses (Binding 3)
	/* {
		boundingSphereBuffer = engine->_resourceConstructor->create_buffer(meshBoundingSpheres.size() * sizeof(BoundingSphere)
			, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			, VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(boundingSphereBuffer.info.pMappedData, meshBoundingSpheres.data(), meshBoundingSpheres.size() * sizeof(BoundingSphere));

		indirect_draw_buffer_underlying = engine->_resourceConstructor->create_buffer(sizeof(ComputeCullingData)
			, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			, VMA_MEMORY_USAGE_CPU_TO_GPU);
		ComputeCullingData data{};
		data.opaqueCommandBufferAddress = engine->_resourceConstructor->get_buffer_address(opaqueDrawBuffers.indirectDrawBuffer);
		data.opaqueCommandBufferCount = opaqueDrawBuffers.instanceCount;
		if (transparentDrawBuffers.instanceCount > 0) {
			data.transparentCommandBufferAddress = engine->_resourceConstructor->get_buffer_address(transparentDrawBuffers.indirectDrawBuffer);
			data.transparentCommandBufferCount = transparentDrawBuffers.instanceCount;
		}
		data.meshBoundsAddress = engine->_resourceConstructor->get_buffer_address(boundingSphereBuffer);

		memcpy(indirect_draw_buffer_underlying.info.pMappedData, &data, sizeof(ComputeCullingData));
		compute_culling_data_buffer_address.setup_data(engine->_device, indirect_draw_buffer_underlying, sizeof(ComputeCullingData));
	}*/
}

void GLTFMetallic_RoughnessMultiDraw::recursive_node_process(LoadedGLTFMultiDraw& scene, Node& node, glm::mat4& topMatrix)
{
	if (MeshNodeMultiDraw* meshNode = dynamic_cast<MeshNodeMultiDraw*>(&node)) {
		MeshData& d = meshData[meshNode->meshIndex];

		meshNode->instanceIndex = static_cast<uint32_t>(instanceData.size());
		InstanceData instance{};
		instance.modelMatrix = topMatrix * meshNode->worldTransform;
		instance.vertexOffset = vertexOffsets[meshNode->meshIndex];
		instance.indexCount = static_cast<uint32_t>(d.indices.size());
		instance.meshIndex = meshNode->meshIndex;
		instanceData.push_back(instance);
	}

	for (auto& child : node.children) {
		recursive_node_process(scene, *child, topMatrix);
	}
}

void GLTFMetallic_RoughnessMultiDraw::recursive_node_process_instance_data(LoadedGLTFMultiDraw& scene, Node& node, glm::mat4& topMatrix, int& current_model_index) {
	if (MeshNodeMultiDraw* meshNode = dynamic_cast<MeshNodeMultiDraw*>(&node)) {
		RawMeshData& d = scene.meshes[meshNode->meshIndex];
		instanceData[current_model_index].modelMatrix = topMatrix * meshNode->worldTransform;
		instanceData[current_model_index].vertexOffset = vertexOffsets[meshNode->meshIndex];
		instanceData[current_model_index].indexCount = static_cast<uint32_t>(d.indices.size());
		current_model_index++;
		// Order doesnt particularly matter, though it should be same order as during initial setup
	}

	for (auto& child : node.children) {
		recursive_node_process_instance_data(scene, *child, topMatrix, current_model_index);
	}
}

void GLTFMetallic_RoughnessMultiDraw::update_draw_data(glm::mat4& model_matrix)
{
	update_model_matrix(model_matrix);
}

void GLTFMetallic_RoughnessMultiDraw::update_model_matrix(glm::mat4& topMatrix)
{
	LoadedGLTFMultiDraw& scene = *this->scene_ptr;
	int current_model_index{ 0 };
	for (auto& n : scene.topNodes) {
		recursive_node_process_instance_data(scene, *n.get(), topMatrix, current_model_index);
	}

	memcpy(instanceBuffer.info.pMappedData, instanceData.data(), instanceData.size() * sizeof(InstanceData));
}


void GLTFMetallic_RoughnessMultiDraw::cull(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout pipelineLayout)
{
	fmt::print("This function is not supported in this application.\n");
	abort();
	// GPU Frustum Culling
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	VkDescriptorBufferBindingInfoEXT compute_culling_binding_info[3]{};

	compute_culling_binding_info[0] = buffer_addresses.get_descriptor_buffer_binding_info();
	
	compute_culling_binding_info[1] = creator->get_scene_data_descriptor_buffer().get_descriptor_buffer_binding_info();
	compute_culling_binding_info[2] = compute_culling_data_buffer_address.get_descriptor_buffer_binding_info();
	vkCmdBindDescriptorBuffersEXT(cmd, 3, compute_culling_binding_info);



	vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &buffer_addresses_descriptor_index, &offsets);
	vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, 1, &scene_data_descriptor_index, &offsets);
	vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 2, 1, &compute_culling_data_descriptor_index, &offsets);

	vkCmdDispatch(cmd, static_cast<uint32_t>(
		std::ceil(opaqueDrawBuffers.instanceCount + transparentDrawBuffers.instanceCount / 64.0f)), 1, 1);

}

void GLTFMetallic_RoughnessMultiDraw::draw(VkCommandBuffer cmd, VkExtent2D drawExtent)
{
	// Pipeline Binding
	{
		shaderObject->bind_viewport(cmd, static_cast<float>(drawExtent.width), static_cast<float>(drawExtent.height), 0.0f, 1.0f);
		shaderObject->bind_scissor(cmd, 0, 0, drawExtent.width, drawExtent.height);
		shaderObject->bind_input_assembly(cmd);
		shaderObject->bind_rasterization(cmd);
		shaderObject->bind_stencil(cmd);
		shaderObject->bind_multisampling(cmd);
		shaderObject->bind_shaders(cmd);
		shaderObject->bind_rasterizaer_discard(cmd, VK_FALSE);

		VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info[3]{};
		descriptor_buffer_binding_info[0] = buffer_addresses.get_descriptor_buffer_binding_info();
		descriptor_buffer_binding_info[1] = texture_data.get_descriptor_buffer_binding_info();
		descriptor_buffer_binding_info[2] = creator->get_scene_data_descriptor_buffer().get_descriptor_buffer_binding_info();
		vkCmdBindDescriptorBuffersEXT(cmd, 3, descriptor_buffer_binding_info);

		vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipelineLayout, 0, 1, &buffer_addresses_descriptor_index, &offsets);
		vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipelineLayout, 1, 1, &texture_data_descriptor_index, &offsets);
		vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipelineLayout, 2, 1, &scene_data_descriptor_index, &offsets);

		vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	}

	// Opaque Rendering
	if (opaqueDrawBuffers.instanceCount > 0) {
		shaderObject->enable_depthtesting(true, VK_COMPARE_OP_LESS_OR_EQUAL);
		shaderObject->init_blending(ShaderObject::BlendMode::NO_BLEND);
		shaderObject->bind_depth_test(cmd);
		shaderObject->bind_blending(cmd);

		vkCmdDrawIndexedIndirect(cmd, opaqueDrawBuffers.indirectDrawBuffer.buffer, 0, opaqueDrawBuffers.instanceCount, sizeof(VkDrawIndexedIndirectCommand));
	}


	// Transparent Rendering
	if (transparentDrawBuffers.instanceCount > 0) {
		shaderObject->enable_depthtesting(false, VK_COMPARE_OP_LESS_OR_EQUAL);
		shaderObject->init_blending(ShaderObject::BlendMode::ADDITIVE_BLEND);
		shaderObject->bind_depth_test(cmd);
		shaderObject->bind_blending(cmd);

		vkCmdDrawIndexedIndirect(cmd, transparentDrawBuffers.indirectDrawBuffer.buffer, 0, transparentDrawBuffers.instanceCount, sizeof(VkDrawIndexedIndirectCommand));
	}
}
