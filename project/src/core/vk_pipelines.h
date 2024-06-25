#pragma once 
//#include "vk_types.h"
#include "big_header.h"
#include <vk_initializers.h>
#include <vk_descriptor_buffer.h>

namespace vkutil {
    bool load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
    void load_shader(std::string path, char*& data, size_t& size);
    void create_shader_objects(
        std::string vertexShader, std::string fragmentShader
        , VkDevice device, VkShaderEXT* shaders
        , uint32_t descriptorSetCount, VkDescriptorSetLayout* descriptorLayout
        , uint32_t pushConstantRangeCount, VkPushConstantRange* pushConstantRanges);
};

class ShaderObject {
public:
    enum class BlendMode {
        ALPHA_BLEND,
        ADDITIVE_BLEND,
        NO_BLEND
    };

    uint32_t _shaderCount = 3;
    VkShaderStageFlagBits _stages[3];
    VkShaderEXT _shaders[3];


    ShaderObject& init_input_assembly(VkPrimitiveTopology topology);
    ShaderObject& init_vertex_input(VkVertexInputBindingDescription2EXT vertex_description, std::vector<VkVertexInputAttributeDescription2EXT> attribute_descriptions);
    ShaderObject& init_rasterization(VkPolygonMode polygonMode, VkCullModeFlags cullMode, VkFrontFace frontFace);
    ShaderObject& init_multisampling(VkBool32 sampleShadingEnable, VkSampleCountFlagBits rasterizationSamples
        , float minSampleShading, const VkSampleMask* pSampleMask
        , VkBool32 alphaToCoverageEnable, VkBool32 alphaToOneEnable);
    // i cant find any set functions for this. maybe it is not needed
    //void setup_renderer(VkFormat colorattachmentFormat, VkFormat depthAttachmentFormat);
    ShaderObject& init_depth(VkBool32 depthTestEnable, VkBool32 depthWriteEnable, VkCompareOp compareOp
        , VkBool32 depthBiasEnable, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor
        , VkBool32 depthBoundsTestEnable, float minDepthBounds, float maxDepthBounds);

    ShaderObject& init_stencil(VkBool32 stencilTestEnable, VkStencilOpState front, VkStencilOpState back);
    ShaderObject& init_blending(ShaderObject::BlendMode mode);

    // shortcut setup functions
    ShaderObject& disable_multisampling();
    ShaderObject& enable_msaa(VkSampleCountFlagBits samples);
    ShaderObject& enable_depthtesting(bool depthWriteEnable, VkCompareOp op);
    ShaderObject& disable_depthtesting();


    // replaces dynamic states set up for original pipeline
    ShaderObject& bind_viewport(VkCommandBuffer cmd, float width, float height, float minDepth, float maxDepth);
    ShaderObject& bind_scissor(VkCommandBuffer cmd, int32_t offsetX, int32_t offsetY, uint32_t width, uint32_t height);
    // should be disabled usually
    //  https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#vkCmdSetRasterizerDiscardEnable
    ShaderObject& bind_rasterizaer_discard(VkCommandBuffer cmd, VkBool32 rasterizerDiscardEnable);
    ShaderObject& bind_input_assembly(VkCommandBuffer cmd);
    ShaderObject& bind_rasterization(VkCommandBuffer cmd);
    ShaderObject& bind_depth_test(VkCommandBuffer cmd);
    ShaderObject& bind_stencil(VkCommandBuffer cmd);
    ShaderObject& bind_multisampling(VkCommandBuffer cmd);
    ShaderObject& bind_blending(VkCommandBuffer cmd);
    ShaderObject& bind_shaders(VkCommandBuffer cmd);

private:
    // input assembly
    VkPrimitiveTopology _topology;
    bool _vertexInputEnabled = false;
    VkVertexInputBindingDescription2EXT _vertex_description;
    std::vector<VkVertexInputAttributeDescription2EXT> _attribute_descriptions;
    // rasterization
    VkPolygonMode _polygonMode;
    VkCullModeFlags _cullMode;
    VkFrontFace _frontFace;
    // multisampling
    VkSampleCountFlagBits _rasterizationSamples;
    uint32_t _pSampleMask; // not currently using sample mask, but if using need to manage memory
    VkBool32 _alphaToCoverageEnable;
    VkBool32 _alphaToOneEnable;
    // blending
    VkBool32 _colorBlendingEnabled;
    VkColorComponentFlags _colorWriteMask;
    VkColorBlendEquationEXT _colorBlendingEquation{};
    // depth
    VkBool32 _depthTestEnable;
    VkBool32 _depthWriteEnable;
    VkCompareOp _compareOp;
    // --
    VkBool32 _depthBiasEnable;
    float _depthBiasConstantFactor;
    float _depthBiasClamp;
    float _depthBiasSlopeFactor;
    // --
    VkBool32 _depthBoundsTestEnable;
    float _minDepthBounds;
    float _maxDepthBounds;

    // stencil
    //VkBool32 _stencilTestEnable;
    //VkStencilOpState _front;
    //VkStencilOpState _back;
};