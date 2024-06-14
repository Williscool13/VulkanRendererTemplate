#include <vk_loader.h>

VkFilter extract_filter(fastgltf::Filter filter)
{
	switch (filter) {
		// nearest samplers
	case fastgltf::Filter::Nearest:
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::NearestMipMapLinear:
		return VK_FILTER_NEAREST;

		// linear samplers
	case fastgltf::Filter::Linear:
	case fastgltf::Filter::LinearMipMapNearest:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
	switch (filter) {
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::LinearMipMapNearest:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case fastgltf::Filter::NearestMipMapLinear:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}

std::string getFileExtension(const std::string& filename) {
	size_t dotPos = filename.find_last_of(".");
	if (dotPos == std::string::npos) {
		return ""; // No extension found
	}
	return filename.substr(dotPos + 1);
}

bool isKTXFile(const std::string& extension) {
	return extension == "ktx" || extension == "ktx2";
}

std::optional<AllocatedImage> load_image(MainEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image, std::filesystem::path filepath)
{
	AllocatedImage newImage{};

	int width{}, height{}, nrChannels{};

 	std::visit(
		fastgltf::visitor{ 
			[](auto& arg) {},
			[&](fastgltf::sources::URI& filePath) {
				assert(filePath.fileByteOffset == 0); // We don't support offsets with stbi.
				assert(filePath.uri.isLocalPath()); // We're only capable of loading
				// local files.
				const std::string path(filePath.uri.path().begin(), filePath.uri.path().end()); // Thanks C++.
				std::string fullpath = filepath.string() + "\\" + path;
				std::string extension = getFileExtension(fullpath);

				if (isKTXFile(extension)) {
					ktxTexture* kTexture;
					KTX_error_code ktxresult;

					ktxresult = ktxTexture_CreateFromNamedFile(
						fullpath.c_str(),
						KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
						&kTexture);


					if (ktxresult == KTX_SUCCESS) {
						VkImageFormatProperties formatProperties;
						VkResult result = vkGetPhysicalDeviceImageFormatProperties(engine->_physicalDevice
							, ktxTexture_GetVkFormat(kTexture), VK_IMAGE_TYPE_2D
							, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0, &formatProperties);
						if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
							fmt::print("Image found with format not supported\n");
							VkExtent3D imagesize;
							imagesize.width = 1;
							imagesize.height = 1;
							imagesize.depth = 1;
							unsigned char data[4] = { 255, 0, 255, 1 };
							newImage = engine->create_image(data, 4, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);
						}
						else {
							unsigned char* data = (unsigned char*)ktxTexture_GetData(kTexture);
							VkExtent3D imageExtents{};
							imageExtents.width = kTexture->baseWidth;
							imageExtents.height = kTexture->baseHeight;
							imageExtents.depth = 1;
							newImage = engine->create_image(data, kTexture->dataSize, imageExtents, ktxTexture_GetVkFormat(kTexture), VK_IMAGE_USAGE_SAMPLED_BIT, false);
						}
						
					}

					ktxTexture_Destroy(kTexture);
				}
				else {
					unsigned char* data = stbi_load(fullpath.c_str(), &width, &height, &nrChannels, 4);
					if (data) {
						VkExtent3D imagesize;
						imagesize.width = width;
						imagesize.height = height;
						imagesize.depth = 1;
						size_t size = width * height * 4;
						newImage = engine->create_image(data, size, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

						stbi_image_free(data);
					}
				}
				
				
			},
			[&](fastgltf::sources::Array& vector) {
				unsigned char* data = stbi_load_from_memory(vector.bytes.data(), static_cast<int>(vector.bytes.size()),
					&width, &height, &nrChannels, 4);
				if (data) {
					VkExtent3D imagesize;
					imagesize.width = width;
					imagesize.height = height;
					imagesize.depth = 1;
					size_t size = width * height * 4;
					newImage = engine->create_image(data, size, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT,false);

					stbi_image_free(data);
				}
			},
			[&](fastgltf::sources::BufferView& view) {
				auto& bufferView = asset.bufferViews[view.bufferViewIndex];
				auto& buffer = asset.buffers[bufferView.bufferIndex];
				// We only care about VectorWithMime here, because we
				// specify LoadExternalBuffers, meaning all buffers
				// are already loaded into a vector.
				std::visit(
					fastgltf::visitor { 
						[](auto& arg) {},
						[&](fastgltf::sources::Array& vector) {
							unsigned char* data = stbi_load_from_memory(vector.bytes.data() + bufferView.byteOffset, static_cast<int>(bufferView.byteLength), &width, &height, &nrChannels, 4);
							if (data) {
								VkExtent3D imagesize;
								imagesize.width = width;
								imagesize.height = height;
								imagesize.depth = 1;
								size_t size = width * height * 4;
								newImage = engine->create_image(data, size, imagesize, VK_FORMAT_R8G8B8A8_UNORM,
									VK_IMAGE_USAGE_SAMPLED_BIT,false);
								stbi_image_free(data);
							}
						} 
					} , buffer.data);
			},
	}, image.data);

	// if any of the attempts to load the data failed, we havent written the image
	// so handle is null
	if (newImage.image == VK_NULL_HANDLE) {
		fmt::print("Image failed to load: {}\n", image.name.c_str());
		return {};
	}
	else {
		return newImage;
	}
}

std::optional<std::vector<float*>> loadCubemapFaces(MainEngine* engine, std::string_view filePath)
{
	/*if (faceFiles.size() != 6) {
		throw std::runtime_error("Cubemap requires exactly 6 faces.");
	}

	int width, height, channels;
	std::vector<float*> facesData;

	for (const auto& file : faceFiles) {
		float* data = stbi_loadf(file.c_str(), &width, &height, &channels, 4);
		if (!data) {
			throw std::runtime_error("Failed to load HDR image: " + file);
		}
		facesData.push_back(data);
	}

	return facesData;*/
	return {};

}

std::optional<std::shared_ptr<LoadedGLTFMultiDraw>> loadGltfMultiDraw(MainEngine* engine, std::string_view filePath)
{
	fmt::print("Loading GLTF: {}\n", filePath);

	std::shared_ptr<LoadedGLTFMultiDraw> scene = std::make_shared<LoadedGLTFMultiDraw>();
	scene->creator = engine;
	LoadedGLTFMultiDraw& file = *scene.get();

	fastgltf::Parser parser{};

	constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember
		| fastgltf::Options::AllowDouble
		//| fastgltf::Options::LoadGLBBuffers 
		| fastgltf::Options::LoadExternalBuffers;

	fastgltf::GltfDataBuffer data;
	data.FromPath(filePath);
	fastgltf::Asset gltf;

	auto gltfFile = fastgltf::MappedGltfFile::FromPath(filePath);
	if (!bool(gltfFile)) { fmt::print("Failed to open glTF file: {}\n", fastgltf::getErrorMessage(gltfFile.error())); return {}; }

	std::filesystem::path path = filePath;
	auto load = parser.loadGltf(gltfFile.get(), path.parent_path(), gltfOptions);
	if (!load) { fmt::print("Failed to load glTF: {}\n", fastgltf::to_underlying(load.error())); return {}; }

	gltf = std::move(load.get());

	// Load Samplers
	//  sampler 0 is the default nearest sampler
	size_t samplerOffset = 1;
	file.samplers.resize(gltf.samplers.size() + samplerOffset);
	file.samplers[0] = engine->_defaultSamplerNearest;

	for (int i = 0; i < gltf.samplers.size(); i++) {
		fastgltf::Sampler& sampler = gltf.samplers[i];
		VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
		sampl.maxLod = VK_LOD_CLAMP_NONE;
		sampl.minLod = 0;

		sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
		sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		sampl.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		VkSampler newSampler;
		vkCreateSampler(engine->_device, &sampl, nullptr, &newSampler);

		file.samplers[i+samplerOffset] = newSampler;
	}
	assert(file.samplers.size() == gltf.samplers.size() + samplerOffset);

	// Load Images
	//  image 0 is the default white image
	size_t imageOffset = 1;
	file.images.resize(gltf.images.size() + imageOffset);
	file.images[0] = engine->_whiteImage;

	for (int i = 0; i < gltf.images.size(); i++) {

		fastgltf::Image& image = gltf.images[i];
		std::optional<AllocatedImage> img = load_image(engine, gltf, image, path.parent_path());

		if (img.has_value()) {
			file.images[i+imageOffset] = *img;
		}
		else {
			file.images[i + imageOffset] = engine->_errorCheckerboardImage;
			fmt::print("Image failed to load: {}\n", image.name.c_str());
		}
	}
	assert(file.images.size() == gltf.images.size() + imageOffset);

	size_t materialOffset = 1;
	file.materials.resize(gltf.materials.size() + materialOffset);
	{
		MaterialData data;
		data.color_factor = glm::vec4(1.f);
		data.metal_rough_factors = glm::vec4(1.f);
		data.alphaCutoff = glm::vec4(0.f);
		data.alphaCutoff.y = static_cast<float>(MaterialPass::MainColor);
		data.texture_image_indices = glm::vec4(0);
		data.texture_sampler_indices = glm::vec4(0);
		file.materials[0] = data;
	}

	for (int i=0;i<gltf.materials.size();i++) {
		MaterialData data;
		data.color_factor = glm::vec4(
			  gltf.materials[i].pbrData.baseColorFactor[0]
			, gltf.materials[i].pbrData.baseColorFactor[1]
			, gltf.materials[i].pbrData.baseColorFactor[2]
			, gltf.materials[i].pbrData.baseColorFactor[3]
		);

		data.metal_rough_factors.x = gltf.materials[i].pbrData.metallicFactor;
		data.metal_rough_factors.y = gltf.materials[i].pbrData.roughnessFactor;
		data.alphaCutoff.x = 0.0f;


		data.texture_image_indices.x = 0;
		data.texture_sampler_indices.x = 0;
		data.texture_image_indices.y = 0;
		data.texture_sampler_indices.y = 0;
		MaterialPass matType = MaterialPass::MainColor;

		if (gltf.materials[i].alphaMode == fastgltf::AlphaMode::Blend) {
			matType = MaterialPass::Transparent;
		}
		else if (gltf.materials[i].alphaMode == fastgltf::AlphaMode::Mask) {
			data.alphaCutoff.x = gltf.materials[i].alphaCutoff;
		}
		data.alphaCutoff.y = static_cast<float>(matType);

		// grab textures from gltf file
		if (gltf.materials[i].pbrData.baseColorTexture.has_value()) {
			size_t img = 0;
			size_t sam = 0;
			size_t texture_index = gltf.materials[i].pbrData.baseColorTexture.value().textureIndex;
			if (gltf.textures[texture_index].imageIndex.has_value()) {
				img = gltf.textures[texture_index].imageIndex.value();
			}
			else {
				fmt::print("Texture has no image index\n");
				abort();
			}
			if (gltf.textures[texture_index].samplerIndex.has_value()) {
				sam = gltf.textures[texture_index].samplerIndex.value();
			}
			else {
				fmt::print("Texture has no sampler index\n");
				abort();
			}
			data.texture_image_indices.x   = static_cast<float>(img + imageOffset);
			data.texture_sampler_indices.x = static_cast<float>(sam + samplerOffset);
		}

		if (gltf.materials[i].pbrData.metallicRoughnessTexture.has_value()) {
			size_t img = 0;
			size_t sam = 0;
			size_t texture_index = gltf.materials[i].pbrData.metallicRoughnessTexture.value().textureIndex;
			if (gltf.textures[texture_index].imageIndex.has_value()) {
				img = gltf.textures[texture_index].imageIndex.value();
			}
			else {
				fmt::print("Metallic Texture has no image index\n");
				abort();
			}
			if (gltf.textures[texture_index].samplerIndex.has_value()) {
				sam = gltf.textures[texture_index].samplerIndex.value();
			}
			else {
				fmt::print("Metallic Texture has no sampler index\n");
				abort();
			}
			data.texture_image_indices.y = static_cast<float>(img + imageOffset);
			data.texture_sampler_indices.y = static_cast<float>(sam + samplerOffset);
		}

		file.materials[i+materialOffset] = data;
	}
	assert(file.materials.size() == gltf.materials.size() + materialOffset);

	std::vector<MultiDrawVertex> vertices;
	std::vector<uint32_t> indices;
	for (fastgltf::Mesh& mesh : gltf.meshes) {
		indices.clear();
		vertices.clear();
		bool hasTransparentPrimitives = false;
		for (auto&& p : mesh.primitives) {
			size_t initial_vtx = vertices.size();
			size_t materialIndex = 0;

			if (p.materialIndex.has_value()) {
				materialIndex = p.materialIndex.value() + 1;
				MaterialPass matType = static_cast<MaterialPass>(file.materials[materialIndex].alphaCutoff.y);
				if (matType == MaterialPass::Transparent) { hasTransparentPrimitives = true; }
			}


			// load indexes
			{
				fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
				indices.reserve(indices.size() + indexaccessor.count);

				fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor,
					[&](std::uint32_t idx) {
						indices.push_back(idx + static_cast<uint32_t>(initial_vtx));
					});
			}

			// load vertex positions
			{
				fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
				vertices.resize(vertices.size() + posAccessor.count);

				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
					[&](glm::vec3 v, size_t index) {
						MultiDrawVertex newvtx;
						newvtx.position = v;
						newvtx.normal = { 1, 0, 0 };
						newvtx.color = glm::vec4{ 1.f };
						newvtx.uv = glm::vec2(0, 0);
						newvtx.materialIndex = static_cast<uint32_t>(materialIndex);

						vertices[initial_vtx + index] = newvtx;
					});
			}

			// load vertex normals
			auto normals = p.findAttribute("NORMAL");
			if (normals != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second],
					[&](glm::vec3 v, size_t index) {
						vertices[initial_vtx + index].normal = v;
					});
			}

			// load UVs
			auto uv = p.findAttribute("TEXCOORD_0");
			if (uv != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second],
					[&](glm::vec2 v, size_t index) {
						vertices[initial_vtx + index].uv = v;
					});
			}

			// load vertex colors
			auto colors = p.findAttribute("COLOR_0");
			if (colors != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second],
					[&](glm::vec4 v, size_t index) {
						vertices[initial_vtx + index].color = v;
					});
			}

		}

		file.meshes.push_back(
			{ vertices, indices, hasTransparentPrimitives }
		);

	}

	std::vector<std::shared_ptr<Node>> nodes;
	for (fastgltf::Node& node : gltf.nodes) {
		std::shared_ptr<Node> newNode;
		if (node.meshIndex.has_value()) {
			newNode = std::make_shared<MeshNodeMultiDraw>();
			static_cast<MeshNodeMultiDraw*>(newNode.get())->meshIndex = static_cast<uint32_t>(*node.meshIndex);
		}
		else {
			newNode = std::make_shared<Node>();
		}


		nodes.push_back(newNode);
		file.nodes.push_back(newNode);

		std::visit(
			fastgltf::visitor{
				[&](fastgltf::math::fmat4x4 matrix) {
					memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
				},
				[&](fastgltf::TRS transform) {
					glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
					glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
					glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

					glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
					glm::mat4 rm = glm::toMat4(rot);
					glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

					newNode->localTransform = tm * rm * sm;
				}
			}
			, node.transform
					);
	}

	// Node Hierarchy
	for (int i = 0; i < gltf.nodes.size(); i++) {
		fastgltf::Node& node = gltf.nodes[i];
		std::shared_ptr<Node>& sceneNode = nodes[i];
		for (auto& c : node.children) {
			sceneNode->children.push_back(nodes[c]);
			nodes[c]->parent = sceneNode;
		}
	}

	// find the top nodes, with no parents
	for (auto& node : nodes) {
		if (node->parent.lock() == nullptr) {
			file.topNodes.push_back(node);
			node->refreshTransform(glm::mat4{ 1.f });
		}
	}

	return scene;
}

void LoadedGLTFMultiDraw::clearAll()
{
	VkDevice dv = creator->_device;

	for (auto& image : images) {

		if (image.image == creator->_errorCheckerboardImage.image
			|| image.image == creator->_whiteImage.image
			|| image.image == creator->_blackImage.image) {
			//dont destroy the default images
			continue;
		}

		creator->destroy_image(image);
	}

	for (auto& sampler : samplers) {
		if (sampler == creator->_defaultSamplerNearest
			|| sampler == creator->_defaultSamplerLinear) {
			//dont destroy the default samplers
			continue;
		}
		vkDestroySampler(dv, sampler, nullptr);
	}

	fmt::print("Destroying Loaded GLTF Multi Draw Struct");
}
