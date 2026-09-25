#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <filesystem>

#include <tiny_gltf.h>
#include <unordered_set>

#include "stb_image.h"

#include "allocator.h"
#include "loader.h"
#include "util.h"
#include "init.h"

//Helper for octahedral projection
static glm::uint32 octahedral(glm::vec3 v) {
    if (glm::length(v) == 0.f) return glm::packHalf2x16(glm::vec2(-2));

    v /= (glm::abs(v.x) + glm::abs(v.y) + glm::abs(v.z));
    glm::vec2 sign = glm::sign(glm::vec2(v));
    if (sign.x == 0) sign.x = 1.f;
    if (sign.y == 0) sign.y = 1.f;

    glm::vec2 newV = (v.z >= 0.f) ? glm::vec2(v) : (1.f - glm::abs(glm::vec2(v.y, v.x))) * sign;
    return glm::packHalf2x16(newV);
}

//Adapted from the Vulkan docs tutorial
Scene loader::loadScene(VkDevice &device, Allocator &allocator, const std::string &file, vkb::DispatchTable &dispatchTable) {
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;

	std::string err;
	std::string warn;

	bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, file);

	if (!warn.empty()) std::cout << "Loader warning: " << warn << std::endl;
	if (!err.empty()) std::cout << "Loader error: " << err << std::endl;
	if (!ret) throw std::runtime_error("Loader failed to load glTF file!");

	Scene newScene{};

    //Load geometry
	for (const auto &mesh : model.meshes) {
		Mesh newMesh{};

		for (const auto &subMesh : mesh.primitives) {
            if (subMesh.mode != TINYGLTF_MODE_TRIANGLES) continue;
            SubMesh newSubMesh{};

            std::vector<Vertex> vertices;
            std::vector<glm::vec3> pos;

            const tinygltf::Accessor &posAccessor = model.accessors[subMesh.attributes.at("POSITION")];
            const tinygltf::BufferView &posBufferView = model.bufferViews[posAccessor.bufferView];
            const tinygltf::Buffer &posBuffer = model.buffers[posBufferView.buffer];

            //uv's
            bool hasUvs = subMesh.attributes.find("TEXCOORD_0") != subMesh.attributes.end();
            const tinygltf::Accessor *uvAccessor = nullptr;
            const tinygltf::BufferView *uvBufferView = nullptr;
            const tinygltf::Buffer *uvBuffer = nullptr;

            if (hasUvs) {
                uvAccessor = &model.accessors[subMesh.attributes.at("TEXCOORD_0")];
                uvBufferView = &model.bufferViews[uvAccessor->bufferView];
                uvBuffer = &model.buffers[uvBufferView->buffer];
            }

            //Normals
            bool hasNors = subMesh.attributes.find("NORMAL") != subMesh.attributes.end();
            const tinygltf::Accessor *norAccessor = nullptr;
            const tinygltf::BufferView *norBufferView = nullptr;
            const tinygltf::Buffer *norBuffer = nullptr;

            if (hasNors) {
                norAccessor = &model.accessors[subMesh.attributes.at("NORMAL")];
                norBufferView = &model.bufferViews[norAccessor->bufferView];
                norBuffer = &model.buffers[norBufferView->buffer];
            }

            //Tangents
            bool hasTans = subMesh.attributes.find("TANGENT") != subMesh.attributes.end();
            const tinygltf::Accessor *tanAccessor = nullptr;
            const tinygltf::BufferView *tanBufferView = nullptr;
            const tinygltf::Buffer *tanBuffer = nullptr;

            if (hasTans) {
                tanAccessor = &model.accessors[subMesh.attributes.at("TANGENT")];
                tanBufferView = &model.bufferViews[tanAccessor->bufferView];
                tanBuffer = &model.buffers[tanBufferView->buffer];
            }

            vertices.reserve(posAccessor.count);
            pos.reserve(posAccessor.count);
            newSubMesh.vertexCount = posAccessor.count;
            for (size_t i = 0; i < posAccessor.count; ++i) {
                const float *newPos = reinterpret_cast<const float *>(&posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset + i * 12]);
                pos.emplace_back(newPos[0], newPos[1], newPos[2]);

                Vertex newVertex{};

                if (hasUvs) {
                    const float *uv = reinterpret_cast<const float *>(&uvBuffer->data[uvBufferView->byteOffset + uvAccessor->byteOffset + i * 8]);
                    newVertex.uv = glm::packHalf2x16(glm::vec2(uv[0], uv[1]));
                }

                glm::vec3 newNor(0);
                if (hasNors) {
                    const float *nor = reinterpret_cast<const float *>(&norBuffer->data[norBufferView->byteOffset + norAccessor->byteOffset + i * 12]);
                    newNor = glm::vec3(nor[0], nor[1], nor[2]);

                    //Encode the normal with octahedral projection
                    newVertex.nor = octahedral(newNor);
                }
                else {
                    newVertex.nor = glm::packHalf2x16(glm::vec2(-2)); //Signal to compute the normal on the spot
                }
                
                glm::vec3 newTan;
                if (hasTans) {
                    const float *tan = reinterpret_cast<const float *>(&tanBuffer->data[tanBufferView->byteOffset + tanAccessor->byteOffset + i * 16]);
                    newTan = glm::vec3(tan[0], tan[1], tan[2]);

                    //Encode the normal with octahedral projection
                    newVertex.tan = octahedral(newTan);

                    //Store the handedness
                    newVertex.w = tan[3];
                }
                else {
                    newVertex.tan = glm::packHalf2x16(glm::vec2(-2)); //Compute the tangent on the spot
                    newVertex.w = 1.f; //Default to right-handedness
                }

                vertices.push_back(newVertex);
            }

            std::vector<uint32_t> indices;

            if (subMesh.indices >= 0) { //subMesh has indices
                const tinygltf::Accessor &indexAccessor = model.accessors[subMesh.indices];
                const tinygltf::BufferView &indexBufferView = model.bufferViews[indexAccessor.bufferView];
                const tinygltf::Buffer &indexBuffer = model.buffers[indexBufferView.buffer];

                const unsigned char *indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];
                size_t indexCount = indexAccessor.count;
                size_t indexStride = 0;

                newSubMesh.indexCount = indexAccessor.count;
                // Determine index stride based on component type
                if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    indexStride = sizeof(uint16_t);
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    indexStride = sizeof(uint32_t);
                }
                else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    indexStride = sizeof(uint8_t);
                }
                else {
                    throw std::runtime_error("Unsupported index component type");
                }

                indices.reserve(indexCount);

                for (size_t i = 0; i < indexCount; i++) {
                    uint32_t index = 0;

                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        index = *reinterpret_cast<const uint16_t *>(indexData + i * indexStride);
                    }
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                    {
                        index = *reinterpret_cast<const uint32_t *>(indexData + i * indexStride);
                    }
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        index = *reinterpret_cast<const uint8_t *>(indexData + i * indexStride);
                    }

                    indices.push_back(index);
                }
            }
            else { //subMesh doesn't have indices
                indices.reserve(posAccessor.count);
                for (int i = 0; i < posAccessor.count; ++i) indices.push_back(i);
            }

            newSubMesh.materialIdx = subMesh.material;

            newSubMesh.posBuffer = allocator.uploadBuffer(pos.data(), pos.size() * sizeof(glm::vec3),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_GPU_ONLY);

            newSubMesh.vertexBuffer = allocator.uploadBuffer(vertices.data(), vertices.size() * sizeof(Vertex),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

            newSubMesh.indexBuffer = allocator.uploadBuffer(indices.data(), indices.size() * sizeof(uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_GPU_ONLY);

            newMesh.subMeshes.push_back(newSubMesh);
		}

        newScene.meshes.push_back(newMesh);
	}

    std::unordered_set<int> srgbTextures;

    //Load materials
    std::vector<Material> materials;
    materials.reserve(model.materials.size());
    for (const auto &material : model.materials) {
        Material newMaterial{};

        auto pbr = material.pbrMetallicRoughness;

        newMaterial.color = glm::vec4(pbr.baseColorFactor[0], 
            pbr.baseColorFactor[1],
            pbr.baseColorFactor[2],
            pbr.baseColorFactor[3]);

        newMaterial.metallic = pbr.metallicFactor;
        newMaterial.roughness = pbr.roughnessFactor;

        //Extract emission data
        {
            //Check if it supports emissive strength
            auto ext = material.extensions.find("KHR_materials_emissive_strength");
            bool hasEmissiveStrength = ext != material.extensions.end();

            newMaterial.emission = glm::vec4(material.emissiveFactor[0],
                material.emissiveFactor[1],
                material.emissiveFactor[2],
                hasEmissiveStrength ? ext->second.Get("emissiveStrength").GetNumberAsDouble() : 1.f);
        }

        //Extract transmissiveness data
        {
            auto ext = material.extensions.find("KHR_materials_transmission");
            bool hasTransmission = ext != material.extensions.end();

            newMaterial.transmissiveness = hasTransmission ? ext->second.Get("transmissionFactor").GetNumberAsDouble() : 0.f;
        }

        //Extract IOR data
        {
            auto ext = material.extensions.find("KHR_materials_ior");
            bool hasIor = ext != material.extensions.end();

            newMaterial.ior = hasIor ? ext->second.Get("ior").GetNumberAsDouble() : 1.5f;
        }

        //TODO: Extract subsurface scattering and anisotropy data?

        //Store the texture indices
        newMaterial.colorTexIdx = pbr.baseColorTexture.index;
        newMaterial.norTexIdx = material.normalTexture.index;

        newMaterial.metallicRoughnessTexIdx = pbr.metallicRoughnessTexture.index;
        newMaterial.emissionTexIdx = material.emissiveTexture.index;

        if (pbr.baseColorTexture.index >= 0) srgbTextures.insert(model.textures[pbr.baseColorTexture.index].source);
        if (material.emissiveTexture.index >= 0) srgbTextures.insert(model.textures[material.emissiveTexture.index].source);

        materials.push_back(newMaterial);
    }
    newScene.materials = allocator.uploadBuffer(materials.data(), materials.size() * sizeof(Material), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    //Load the samplers
    {
        newScene.samplerCreateInfos.reserve(model.samplers.size());
        std::vector<VkHostAddressRangeEXT> hostAddressRanges;
        hostAddressRanges.reserve(model.samplers.size() + 1);

        size_t stride = util::alignUp(properties.dhProperties.samplerDescriptorSize, properties.dhProperties.samplerDescriptorAlignment);
        newScene.samplerHeap = allocator.createBuffer((model.samplers.size() + 1) * stride + properties.dhProperties.minSamplerHeapReservedRange,
            VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT, VMA_MEMORY_USAGE_CPU_TO_GPU, true, properties.dhProperties.samplerHeapAlignment);

        //TODO: Support more sampler types
        //Default sampler
        {
            VkSamplerCreateInfo samplerInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
            samplerInfo.minLod = 0;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            newScene.samplerCreateInfos.push_back(samplerInfo);

            VkHostAddressRangeEXT hostAddressRange{};
            hostAddressRange.address = reinterpret_cast<char *>(newScene.samplerHeap.info.pMappedData);
            hostAddressRange.size = properties.dhProperties.samplerDescriptorSize;
            hostAddressRanges.push_back(hostAddressRange);
        }

        for (int i = 0; i < model.samplers.size(); ++i) {
            const auto &sampler = model.samplers[i];
            VkSamplerCreateInfo samplerInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
            samplerInfo.minLod = 0;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            newScene.samplerCreateInfos.push_back(samplerInfo);
            
            VkHostAddressRangeEXT hostAddressRange{};
            hostAddressRange.address = reinterpret_cast<char*>(newScene.samplerHeap.allocation->GetMappedData()) + (i + 1) * stride;
            hostAddressRange.size = properties.dhProperties.samplerDescriptorSize;
            hostAddressRanges.push_back(hostAddressRange);
        }

        dispatchTable.writeSamplerDescriptorsEXT(model.samplers.size() + 1, newScene.samplerCreateInfos.data(), hostAddressRanges.data());
    }

    std::vector<VkImageDescriptorInfoEXT> imageDescriptorInfos;
    std::vector<VkResourceDescriptorInfoEXT> resourceDescriptorInfos;

    //Load the images
    newScene.images.reserve(model.images.size());
    newScene.imageViewCreateInfos.reserve(model.images.size() + ENGINE_IMAGES);
    {
        size_t stride = util::alignUp(properties.dhProperties.imageDescriptorSize, properties.dhProperties.imageDescriptorAlignment);
        std::vector<VkHostAddressRangeEXT> hostAddressRanges;
        hostAddressRanges.reserve(model.images.size() + ENGINE_IMAGES);
        for (int i = 0; i < model.images.size(); ++i) {
            const auto &image = model.images[i];

            VkFormat format = srgbTextures.contains(i) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            VkExtent3D extent = VkExtent3D(image.width, image.height, 1);

            //TODO: Allocate image on the GPU
            VkImageViewCreateInfo info;
            if (image.bits == 16) {
                std::vector<uint8_t> convertedImage;

                const uint16_t *originalImage = reinterpret_cast<const uint16_t *>(image.image.data());

                convertedImage.reserve(image.width * image.height * 4);
                for (int j = 0; j < image.width * image.height * 4; ++j) {
                    convertedImage.push_back(static_cast<uint8_t>(glm::floor((originalImage[j] * 255.f / 65535.f) + 0.5f)));
                }
                newScene.images.push_back(allocator.createImage(convertedImage.data(), extent, format, VK_IMAGE_USAGE_SAMPLED_BIT, false, &info));
            }
            else {
                newScene.images.push_back(allocator.createImage(image.image.data(), extent, format, VK_IMAGE_USAGE_SAMPLED_BIT, false, &info));
            }

            newScene.imageViewCreateInfos.push_back(info);
            VkImageDescriptorInfoEXT imageDescriptorInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT };
            imageDescriptorInfo.pView = &newScene.imageViewCreateInfos.back();
            imageDescriptorInfo.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageDescriptorInfos.push_back(imageDescriptorInfo);

            VkResourceDescriptorInfoEXT resourceDescriptorInfo{ .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
            resourceDescriptorInfo.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            resourceDescriptorInfo.data.pImage = &imageDescriptorInfos.back();
            resourceDescriptorInfos.push_back(resourceDescriptorInfo);

            VkHostAddressRangeEXT hostAddressRange{};
            hostAddressRange.address = reinterpret_cast<char *>(newScene.samplerHeap.allocation->GetMappedData()) + i * stride;
            hostAddressRange.size = (model.images.size() + ENGINE_IMAGES) * stride;
        }
    }
    
    //Load the textures
    newScene.textures.reserve(model.textures.size());
    for (const auto &texture : model.textures) {
        newScene.textures.push_back({texture.source, texture.sampler});
    }
    newScene.textureBuffer = allocator.uploadBuffer(newScene.textures.data(), newScene.textures.size() * sizeof(Texture), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    //TODO: Load instances

    //TODO: Build the tlas for the scene

    newScene.loaded = true;
    return newScene;
}

void loader::destroyScene(VkDevice &device, Allocator &allocator, Scene &scene) {
    for (auto &mesh : scene.meshes) {
        for (auto &subMesh : mesh.subMeshes) {
            allocator.destroyBuffer(subMesh.indexBuffer);
            allocator.destroyBuffer(subMesh.posBuffer);
            allocator.destroyBuffer(subMesh.vertexBuffer);

            //TODO: Destroy blas for the submesh
        }

        mesh.subMeshes.clear();
    }
    scene.meshes.clear();

    allocator.destroyBuffer(scene.materials);
    allocator.destroyBuffer(scene.textureBuffer);

    //Destroy images
    for (auto &image : scene.images) allocator.destroyImage(image);
    scene.images.clear();

    //TODO: Destroy tlas for the scene


}