struct Vertex
{
	vec3 position;
	float pad;
	vec3 normal;
	float pad2;
	vec4 color;
	vec2 uv;
	uint material_index; 
	float pad3;
};

struct Material
{
	vec4 color_factor;
	vec4 metal_rough_factors;
	vec4 texture_image_indices;
	vec4 texture_sampler_indices;
	vec4 alpha_cutoff; // only use X, can pack with other values in future
};

struct Model
{
	mat4 model;
	uint vertex_offset;
	uint index_count;
	uint mesh_index;
	float pad;
};


layout(buffer_reference, std430) readonly buffer VertexBuffer
{
	Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer ModelData
{
	Model models[];
};

layout(buffer_reference, std430) readonly buffer MaterialData
{
	Material materials[];
};

layout(set = 0, binding = 0) uniform addresses
{
	VertexBuffer vertexbufferDeviceAddress;
	MaterialData materialBufferDeviceAddress;
	ModelData modelBufferDeviceAddress;
} buffer_addresses;

layout(set = 1, binding = 0) uniform GlobalUniform
{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
	vec4 cameraPos;
} sceneData;