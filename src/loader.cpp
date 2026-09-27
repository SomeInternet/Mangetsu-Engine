#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <filesystem>

#include <tiny_gltf.h>
#include <unordered_set>
#include <glm/gtc/type_ptr.hpp>

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

//Helper to create the logical outline of the acceleration structure and the scratch buffer
//Color me surprised at how readable this is
static AccelerationStructure createAccelerationStructure(vkb::DispatchTable &dispatchTable, Allocator &allocator, DeviceProperties &deviceProperties,
    VkAccelerationStructureBuildGeometryInfoKHR &buildInfo, uint32_t *maxPrimitiveCounts, AllocatedBuffer &scratchBuffer) {

    VkAccelerationStructureBuildSizesInfoKHR sizes{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    dispatchTable.getAccelerationStructureBuildSizesKHR(VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, maxPrimitiveCounts, &sizes);

    AccelerationStructure newAS{};
    newAS.buffer = allocator.createBuffer(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_GPU_ONLY, false);
    
    VkAccelerationStructureCreateInfoKHR createInfo{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    createInfo.buffer = newAS.buffer.buffer;
    createInfo.size = sizes.accelerationStructureSize;
    createInfo.type = buildInfo.type;

    VK_CHECK(dispatchTable.createAccelerationStructureKHR(&createInfo, nullptr, &newAS.as));
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    addressInfo.accelerationStructure = newAS.as;
    newAS.address = dispatchTable.getAccelerationStructureDeviceAddressKHR(&addressInfo);

    scratchBuffer = allocator.createBuffer(sizes.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, false,
        deviceProperties.asProperties.minAccelerationStructureScratchOffsetAlignment);

    buildInfo.dstAccelerationStructure = newAS.as;
    buildInfo.scratchData.deviceAddress = scratchBuffer.address;
    return newAS;
}

//Helper to parse the local transform matrix of a node in the scene graph
static glm::mat4 parseLocalTransform(const tinygltf::Node &node) {
    if (node.matrix.size() == 16) return glm::mat4(glm::make_mat4(node.matrix.data()));

    //Transformations specified T-R-S
    glm::mat4 transform(1.f);
    if (node.translation.size() == 3) { //X, Y, Z displacement
        transform = glm::translate(transform, glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    }

    if (node.rotation.size() == 4) { //Axis-angle rotation
        glm::quat rot = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
        transform *= glm::toMat4(rot);
    }

    if (node.scale.size() == 3) { //X, Y, Z scale
        transform = glm::scale(transform, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    }

    return transform;
}

static void traverseSceneGraph(const tinygltf::Model &model, int nodeIdx, const glm::mat4 &parentMatrix, std::vector<Instance> &instances) {
    const tinygltf::Node &node = model.nodes[nodeIdx];
    glm::mat4 worldMatrix = parentMatrix * parseLocalTransform(node);

    if (node.mesh >= 0) {
        Instance instance{};
        instance.transform = worldMatrix;
        instance.mesh = node.mesh;
        instances.push_back(instance);
    }

    //Recurse on children
    for (int childIdx : node.children) traverseSceneGraph(model, childIdx, worldMatrix, instances);
}

//Adapted from the Vulkan docs tutorial
Scene loader::loadScene(VkDevice &device, Allocator &allocator, const std::string &file, 
    DeviceProperties &deviceProperties, vkb::DispatchTable &dispatchTable) {
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
            SubMesh newSubMesh{};

            if (subMesh.mode != TINYGLTF_MODE_TRIANGLES) {
                newMesh.subMeshes.push_back(newSubMesh);
                continue;
            }

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
                    if (uvAccessor->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) throw std::runtime_error("UV's must be floats!");
                    const float *uv = reinterpret_cast<const float *>(&uvBuffer->data[uvBufferView->byteOffset + uvAccessor->byteOffset + i * uvAccessor->ByteStride(*uvBufferView)]);
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
                newSubMesh.indexCount = posAccessor.count;
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
    newScene.materialBuffer = allocator.uploadBuffer(materials.data(), materials.size() * sizeof(Material), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    //Load the samplers
    {
        //TODO: Support more sampler types

        for (int i = 0; i < model.samplers.size(); ++i) {
            const auto &sampler = model.samplers[i];
            VkSamplerCreateInfo samplerInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
            samplerInfo.minLod = 0;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            newScene.samplerCreateInfos.push_back(samplerInfo);
        }
    }

    //Load the images
    newScene.images.reserve(model.images.size());
    newScene.imageViewCreateInfos.reserve(model.images.size() + ENGINE_IMAGES);
    {
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
        }
    }
    
    //Load the textures
    newScene.textures.reserve(model.textures.size());
    for (const auto &texture : model.textures) {
        newScene.textures.push_back({texture.source, texture.sampler});
    }
    newScene.textureBuffer = allocator.uploadBuffer(newScene.textures.data(), newScene.textures.size() * sizeof(Texture), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    //Build the acceleration structures
    std::vector<std::vector<VkAccelerationStructureGeometryKHR>> blasGeometries;
    std::vector<std::vector<VkAccelerationStructureBuildRangeInfoKHR>> blasRanges;
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> blasBuildInfos;

    std::vector<AllocatedBuffer> scratchBuffers;
    scratchBuffers.reserve(newScene.meshes.size() + 1); //1 extra for the tlas

    for (int i = 0; i < newScene.meshes.size(); ++i) {
        Mesh &mesh = newScene.meshes[i];
        if (mesh.subMeshes.empty()) continue; //Skip acceleration structure build for meshes with no triangles

        blasGeometries.push_back({});
        blasRanges.push_back({});

        std::vector<uint32_t> triCounts;
        triCounts.reserve(mesh.subMeshes.size());

        for (const SubMesh &subMesh : mesh.subMeshes) {
            VkAccelerationStructureGeometryKHR geometry{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; //TODO: Modify for transmissive materials
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            geometry.geometry.triangles.vertexData.deviceAddress = subMesh.posBuffer.address;
            geometry.geometry.triangles.vertexStride = sizeof(glm::vec3);
            geometry.geometry.triangles.maxVertex = subMesh.vertexCount - 1;
            geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
            geometry.geometry.triangles.indexData.deviceAddress = subMesh.indexBuffer.address;

            blasGeometries.back().push_back(geometry);
            blasRanges.back().push_back({.primitiveCount = subMesh.indexCount / 3});
            triCounts.push_back(subMesh.indexCount / 3);
        }

        //The submeshes are geometries used to build the blas for the mesh which contains them
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = static_cast<uint32_t>(blasGeometries.back().size());
        buildInfo.pGeometries = blasGeometries.back().data();

        scratchBuffers.push_back({});

        //Create the logical structure for the acceleration structure
        mesh.blas = createAccelerationStructure(dispatchTable, allocator, deviceProperties, buildInfo, triCounts.data(), scratchBuffers.back());

        blasBuildInfos.push_back(buildInfo);
    }

    std::vector<uint32_t> firstSubMesh(newScene.meshes.size());
    std::vector<DeviceSubMesh> subMeshTable; //Flat array of DeviceSubMeshes
    for (size_t i = 0; i < newScene.meshes.size(); ++i) {
        firstSubMesh[i] = subMeshTable.size(); //Get the next free index

        for (const auto &subMesh : newScene.meshes[i].subMeshes) {
            DeviceSubMesh deviceSubMesh{};
            deviceSubMesh.indexBuffer = subMesh.indexBuffer.address;
            deviceSubMesh.posBuffer = subMesh.posBuffer.address;
            deviceSubMesh.vertexBuffer = subMesh.vertexBuffer.address;
            deviceSubMesh.materialIdx = subMesh.materialIdx;
            
            subMeshTable.push_back(deviceSubMesh);
        }
    }
    newScene.subMeshBuffer = allocator.uploadBuffer(subMeshTable.data(), subMeshTable.size() * sizeof(DeviceSubMesh), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, false);

    //Traverse the scene graph to load instances
    std::vector<Instance> instances;
    if (!model.scenes.empty()) {
        const tinygltf::Scene &modelScene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];

        for (int root : modelScene.nodes) traverseSceneGraph(model, root, glm::mat4(1.f), instances);
    }
    else {
        //Push the meshes with default transforms
        for (int i = 0; i < newScene.meshes.size(); ++i) {
            Instance instance{};
            instance.mesh = i;
            instance.transform = glm::mat4{ 1.f };

            instances.push_back(instance);
        }
    }

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    vkInstances.reserve(instances.size());

    //Convert our instances into instances for Vulkan to use
    for (auto &instance : instances) {
        if (newScene.meshes[instance.mesh].subMeshes.empty()) continue; //Don't create an instance of a mesh that doesn't have triangles

        VkAccelerationStructureInstanceKHR vkInstance{};

        //Vulkan instances have a row-major transform matrix that is 3 by 4
        glm::mat4 transformRowMajor = glm::transpose(instance.transform);
        memcpy(&vkInstance.transform, glm::value_ptr(transformRowMajor), sizeof(VkTransformMatrixKHR));
        vkInstance.instanceCustomIndex = firstSubMesh[instance.mesh]; //Sets the builtin index of our instance to our custom value

        vkInstance.mask = 0xFF; //TODO: Look more into this. Apparently this combined with a ray's cull mask allows you to have certain rays interact only with certain instances
        vkInstance.instanceShaderBindingTableRecordOffset = 0;
        vkInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        vkInstance.accelerationStructureReference = newScene.meshes[instance.mesh].blas.address;

        vkInstances.push_back(vkInstance);
    }

    newScene.instanceBuffer = allocator.uploadBuffer(vkInstances.data(), vkInstances.size() * sizeof(VkAccelerationStructureInstanceKHR), 
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_GPU_ONLY, false);

    //TODO: Build the tlas for the scene
    VkAccelerationStructureGeometryKHR tlasGeometry{};
    tlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tlasGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    tlasGeometry.geometry.instances.data.deviceAddress = newScene.instanceBuffer.address;

    VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{};
    tlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlasBuildInfo.geometryCount = 1;
    tlasBuildInfo.pGeometries = &tlasGeometry;

    uint32_t instanceCount = vkInstances.size();
    scratchBuffers.push_back({});
    newScene.tlas = createAccelerationStructure(dispatchTable, allocator, deviceProperties, tlasBuildInfo, &instanceCount, scratchBuffers.back());
    
    VkAccelerationStructureBuildRangeInfoKHR tlasRange{};
    tlasRange.primitiveCount = instanceCount;

    //Build the blases
    allocator.immediateSubmit([&](VkCommandBuffer commandBuffer) {
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR *> pRanges;

        pRanges.reserve(blasRanges.size());
        for (auto &ranges : blasRanges) pRanges.push_back(ranges.data());

        //Batch build the blases
        dispatchTable.cmdBuildAccelerationStructuresKHR(commandBuffer, blasBuildInfos.size(), 
            blasBuildInfos.data(), pRanges.data());

        //Force the tlas build reads to wait for the blas build writes
        //Side note, I think I finally (kind of) understand what this pipeline business is all about.
        //Each command goes through a hardware pipeline. The barrier enforces synchronization between command A's src stage mask and src access mask
        //and command B's dst stage mask and dst access mask, where A and B are determined by their positioning in the command buffer's recording
        VkMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(commandBuffer, &dependency);

        //Build the tlas
        const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRange;
        dispatchTable.cmdBuildAccelerationStructuresKHR(commandBuffer, 1, &tlasBuildInfo, &pTlasRange);
        });

    //Destroy the scratch buffers
    for (auto &scratchBuffer : scratchBuffers) allocator.destroyBuffer(scratchBuffer);

    newScene.loaded = true;
    return newScene;
}

void loader::destroyScene(VkDevice &device, Allocator &allocator, Scene &scene, vkb::DispatchTable &dispatchTable) {
    for (auto &mesh : scene.meshes) {
        for (auto &subMesh : mesh.subMeshes) {
            allocator.destroyBuffer(subMesh.indexBuffer);
            allocator.destroyBuffer(subMesh.posBuffer);
            allocator.destroyBuffer(subMesh.vertexBuffer);
        }

        //TODO: Destroy blas for mesh
        if (mesh.blas.as != VK_NULL_HANDLE) {
            dispatchTable.destroyAccelerationStructureKHR(mesh.blas.as, nullptr);
            allocator.destroyBuffer(mesh.blas.buffer);
        }

        mesh.subMeshes.clear();
    }
    scene.meshes.clear();

    allocator.destroyBuffer(scene.materialBuffer);
    allocator.destroyBuffer(scene.textureBuffer);

    //Destroy images
    for (auto &image : scene.images) allocator.destroyImage(image);
    scene.images.clear();

    allocator.destroyBuffer(scene.instanceBuffer);
    allocator.destroyBuffer(scene.subMeshBuffer);

    //TODO: Destroy tlas for the scene
    dispatchTable.destroyAccelerationStructureKHR(scene.tlas.as, nullptr);
    allocator.destroyBuffer(scene.tlas.buffer);
    scene.loaded = false;

}