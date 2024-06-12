#include <vk_pipelines.h	>



bool vkutil::load_shader_module(const char* filePath,
	VkDevice device,
	VkShaderModule* outShaderModule)
{
	// open the file. With cursor at the end
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		return false;
	}

	// find what the size of the file is by looking up the location of the cursor
	// because the cursor is at the end, it gives the size directly in bytes
	size_t fileSize = (size_t)file.tellg();

	// spirv expects the buffer to be on uint32, so make sure to reserve a int
	// vector big enough for the entire file
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	// put file cursor at beginning
	file.seekg(0);

	// load the entire file into the buffer
	file.read((char*)buffer.data(), fileSize);

	// now that the file is loaded into the buffer, we can close it
	file.close();

	// create a new shader module, using the buffer we loaded
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	// codeSize has to be in bytes, so multply the ints in the buffer by size of
	// int to know the real size of the buffer
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	// check that the creation goes well.
	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		return false;
	}
	*outShaderModule = shaderModule;
	return true;
}



void vkutil::load_shader(std::string path, char* &data, size_t &size ) {
	VkShaderCreateInfoEXT shaderCreateInfo{};
	// open the file. With cursor at the end
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	if (file.is_open()) {
		size = file.tellg();
		// put file cursor at beginning
		file.seekg(0);
		// load the entire file into the buffer
		data = new char[size];
		file.read(data, size);
		file.close();
		assert(size > 0);
	}
}

void vkutil::create_shader_objects(
	std::string vertexShader
	, std::string fragmentShader
	, VkDevice device
	, VkShaderEXT* shaders
	, uint32_t descriptorSetCount
	, VkDescriptorSetLayout* descriptorLayout
	, uint32_t pushConstantRangeCount
	, VkPushConstantRange* pushConstantRanges
) 
{
	VkShaderCreateInfoEXT shaderCreateInfos[2]{};
	size_t shaderCodeSizes[2]{};
	char* shaderCodes[2]{};

	vkutil::load_shader(vertexShader, shaderCodes[0], shaderCodeSizes[0]);
	vkutil::load_shader(fragmentShader, shaderCodes[1], shaderCodeSizes[1]);

	shaderCreateInfos[0].sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
	shaderCreateInfos[0].flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT;
	shaderCreateInfos[0].codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
	shaderCreateInfos[0].pCode = shaderCodes[0];
	shaderCreateInfos[0].codeSize = shaderCodeSizes[0];
	shaderCreateInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderCreateInfos[0].nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderCreateInfos[0].pName = "main";
	shaderCreateInfos[0].setLayoutCount = descriptorSetCount;
	shaderCreateInfos[0].pSetLayouts = descriptorLayout;
	shaderCreateInfos[0].pushConstantRangeCount = pushConstantRangeCount;
	shaderCreateInfos[0].pPushConstantRanges = pushConstantRanges;

	shaderCreateInfos[1].sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
	shaderCreateInfos[1].flags = VK_SHADER_CREATE_LINK_STAGE_BIT_EXT;
	shaderCreateInfos[1].codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
	shaderCreateInfos[1].pCode = shaderCodes[1];
	shaderCreateInfos[1].codeSize = shaderCodeSizes[1];
	shaderCreateInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderCreateInfos[1].nextStage = 0;
	shaderCreateInfos[1].pName = "main";
	shaderCreateInfos[1].setLayoutCount = descriptorSetCount;
	shaderCreateInfos[1].pSetLayouts = descriptorLayout;
	shaderCreateInfos[1].pushConstantRangeCount = pushConstantRangeCount;
	shaderCreateInfos[1].pPushConstantRanges = pushConstantRanges;

	VK_CHECK(vkCreateShadersEXT(device, 2, shaderCreateInfos, nullptr, shaders));

	shaders[2] = VK_NULL_HANDLE;

	delete[] shaderCodes[0];
	delete[] shaderCodes[1];

	fmt::print("Created Shader Object with vertex shader: {}\nand fragment shader: {}\n", vertexShader, fragmentShader);
}


void ShaderObject::init_input_assembly(VkPrimitiveTopology topology)
{
	_topology = topology;
}

void ShaderObject::init_vertex_input(VkVertexInputBindingDescription2EXT vertex_description,
	std::vector<VkVertexInputAttributeDescription2EXT> attribute_descriptions)
{
	_vertex_description = vertex_description;
	_attribute_descriptions = attribute_descriptions;

	_vertexInputEnabled = true;
}

void ShaderObject::init_rasterization(VkPolygonMode polygonMode, VkCullModeFlags cullMode, VkFrontFace frontFace)
{
	_polygonMode = polygonMode;
	_cullMode = cullMode;
	_frontFace = frontFace;
}

void ShaderObject::init_multisampling(VkBool32 sampleShadingEnable, VkSampleCountFlagBits rasterizationSamples
	, float minSampleShading, const VkSampleMask* pSampleMask, VkBool32 alphaToCoverageEnable, VkBool32 alphaToOneEnable)
{
	throw std::logic_error("The method or operation is not implemented.");
}

void ShaderObject::init_depth(VkBool32 depthTestEnable, VkBool32 depthWriteEnable, VkCompareOp compareOp
	, VkBool32 depthBiasEnable, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor
	, VkBool32 depthBoundsTestEnable, float minDepthBounds, float maxDepthBounds)
{
	_depthTestEnable = depthTestEnable;
	_depthWriteEnable = depthWriteEnable;
	_compareOp = compareOp;

	_depthBiasEnable = depthBiasEnable;
	_depthBiasConstantFactor = depthBiasConstantFactor;
	_depthBiasClamp = depthBiasClamp;
	_depthBiasSlopeFactor = depthBiasSlopeFactor;

	_depthBoundsTestEnable = depthBoundsTestEnable;
	_minDepthBounds = minDepthBounds;
	_maxDepthBounds = maxDepthBounds;
}

void ShaderObject::init_blending(ShaderObject::BlendMode mode)
{
	switch (mode) {
	case BlendMode::ALPHA_BLEND: {
		_colorBlendingEnabled = VK_TRUE;
		_colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		//_colorBlendingEquation.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		_colorBlendingEquation.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		//_colorBlendingEquation.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		_colorBlendingEquation.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		_colorBlendingEquation.colorBlendOp = VK_BLEND_OP_ADD;
		_colorBlendingEquation.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		_colorBlendingEquation.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		_colorBlendingEquation.alphaBlendOp = VK_BLEND_OP_ADD;
		break;
	}
	case BlendMode::ADDITIVE_BLEND: {
		_colorBlendingEnabled = VK_TRUE;
		_colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		_colorBlendingEquation.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		_colorBlendingEquation.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		_colorBlendingEquation.colorBlendOp = VK_BLEND_OP_ADD;
		_colorBlendingEquation.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		_colorBlendingEquation.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		_colorBlendingEquation.alphaBlendOp = VK_BLEND_OP_ADD;
		break;
	}
	case BlendMode::NO_BLEND:
		_colorBlendingEnabled = VK_FALSE;
		_colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		break;
	}
}

void ShaderObject::init_stencil(VkBool32 stencilTestEnable, VkStencilOpState front, VkStencilOpState back)
{
	throw std::logic_error("The method or operation is not implemented.");
}


void ShaderObject::disable_multisampling()
{
	_rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	_pSampleMask = 0xFF;
	_alphaToCoverageEnable = VK_FALSE;
	_alphaToOneEnable = VK_FALSE;
}

void ShaderObject::enable_msaa(VkSampleCountFlagBits samples)
{

	_rasterizationSamples = samples;
	_pSampleMask = 0xFF;
	_alphaToCoverageEnable = VK_FALSE;
	_alphaToOneEnable = VK_FALSE;
}

void ShaderObject::enable_depthtesting(bool depthWriteEnable, VkCompareOp op)
{
	init_depth(
		VK_TRUE, depthWriteEnable, op, // test/write
		VK_FALSE, 0.0f, 0.0f, 0.0f, // bias
		VK_FALSE, 0.0f, 1.0f // bounds
	);
}

void ShaderObject::disable_depthtesting()
{
	init_depth(
		VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS, // test/write
		VK_FALSE, 0.0f, 0.0f, 0.0f, // bias
		VK_FALSE, 0.0f, 1.0f // bounds
	);
}

void ShaderObject::bind_viewport(VkCommandBuffer cmd, float width, float height, float minDepth, float maxDepth)
{
	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;
	//vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetViewportWithCount(cmd, 1, &viewport);
}

void ShaderObject::bind_scissor(VkCommandBuffer cmd, int32_t offsetX, int32_t offsetY, uint32_t width, uint32_t height)
{
	VkRect2D scissor = {};
	scissor.offset = { offsetX, offsetY };
	scissor.extent = { width, height };
	//vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdSetScissorWithCount(cmd, 1, &scissor);
}

void ShaderObject::bind_rasterizaer_discard(VkCommandBuffer cmd, VkBool32 rasterizerDiscardEnable)
{
	vkCmdSetRasterizerDiscardEnable(cmd, rasterizerDiscardEnable);
}

void ShaderObject::bind_input_assembly(VkCommandBuffer cmd)
{

	vkCmdSetPrimitiveTopologyEXT(cmd, _topology);
	vkCmdSetPrimitiveRestartEnable(cmd, VK_FALSE);
	// unused, using buffer device address instead
	if (_vertexInputEnabled) {
		vkCmdSetVertexInputEXT(cmd, 1, &_vertex_description, static_cast<uint32_t>(_attribute_descriptions.size()), _attribute_descriptions.data());
	}
	else {
		vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);
	}
	
}

void ShaderObject::bind_rasterization(VkCommandBuffer cmd)
{
	// Draw Mode
	vkCmdSetPolygonModeEXT(cmd, _polygonMode);
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#vkCmdSetLineWidth
	vkCmdSetLineWidth(cmd, 1.0f);

	// Culling
	vkCmdSetCullMode(cmd, _cullMode);
	vkCmdSetFrontFace(cmd, _frontFace);
}

void ShaderObject::bind_depth_test(VkCommandBuffer cmd) 
{
	vkCmdSetDepthTestEnable(cmd, _depthTestEnable);
	vkCmdSetDepthWriteEnable(cmd, _depthWriteEnable);
	vkCmdSetDepthCompareOp(cmd, _compareOp);
	
	vkCmdSetDepthBoundsTestEnable(cmd, _depthBoundsTestEnable);
	vkCmdSetDepthBounds(cmd, _minDepthBounds, _maxDepthBounds);

	vkCmdSetDepthBiasEnable(cmd, _depthBiasEnable);
	if (_depthBiasEnable) { vkCmdSetDepthBias(cmd, _depthBiasConstantFactor, _depthBiasClamp, _depthBiasSlopeFactor); }
	// only if depthClamp feature is enabled
	//vkCmdSetDepthClampEnableEXT();
}

void ShaderObject::bind_stencil(VkCommandBuffer cmd) {
	vkCmdSetStencilTestEnable(cmd, VK_FALSE);
	// below are the 4 functions to set stencil state
	//vkCmdSetStencilOp(); 
	//vkCmdSetStencilCompareMask();
	//vkCmdSetStencilWriteMask();
	//vkCmdSetStencilReference(); 
}

void ShaderObject::bind_multisampling(VkCommandBuffer cmd)
{
	//vkCmdSetSampleShadingEnableEXT(cmd, _sampleShadingEnable);
	vkCmdSetRasterizationSamplesEXT(cmd, _rasterizationSamples);
	vkCmdSetSampleMaskEXT(cmd, _rasterizationSamples, &_pSampleMask);
	vkCmdSetAlphaToCoverageEnableEXT(cmd, _alphaToCoverageEnable);
	vkCmdSetAlphaToOneEnableEXT(cmd, _alphaToOneEnable);
}

void ShaderObject::bind_blending(VkCommandBuffer cmd)
{
	vkCmdSetColorBlendEnableEXT(cmd, 0, 1, &_colorBlendingEnabled);
	vkCmdSetColorWriteMaskEXT(cmd, 0, 1, &_colorWriteMask);
	vkCmdSetColorBlendEquationEXT(cmd, 0, 1, &_colorBlendingEquation);

	//vkCmdSetBlendConstants for blend equations that use constants
	
}

void ShaderObject::bind_shaders(VkCommandBuffer cmd)
{
	vkCmdBindShadersEXT(cmd, _shaderCount, _stages, _shaders);
}
