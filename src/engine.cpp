#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <tiny_gltf.h>
#include <vk_mem_alloc.h>
#include <format>

#include "engine.h"
#include "init.h"
#include "loader.h"
#include "pushconstants.h"
#include "aliastable.h"

//Helpers to, uh, help us set up the descriptors for the shaders
static VkDescriptorSetAndBindingMappingEXT heapMapping(uint32_t binding, VkSpirvResourceTypeFlagsEXT mask, 
	VkDeviceSize heapOffset, VkDeviceSize heapArrayStride) {

	VkDescriptorSetAndBindingMappingEXT mapping{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT };
	mapping.descriptorSet = 0;
	mapping.firstBinding = binding;
	mapping.bindingCount = 1;
	mapping.resourceMask = mask;
	mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
	mapping.sourceData.constantOffset.heapOffset = heapOffset;
	mapping.sourceData.constantOffset.heapArrayStride = heapArrayStride; 
	return mapping;
}

static VkDescriptorSetAndBindingMappingEXT pushAddressMapping(uint32_t binding, VkSpirvResourceTypeFlagsEXT mask, uint32_t pushOffset) {
	VkDescriptorSetAndBindingMappingEXT mapping{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT };
	mapping.descriptorSet = 0;
	mapping.firstBinding = binding;
	mapping.bindingCount = 1; 
	mapping.resourceMask = mask;
	mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT;
	mapping.sourceData.pushAddressOffset = pushOffset;
	return mapping;
}

void Engine::initWindow() {
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags windowFlags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	_window = SDL_CreateWindow("Mangetsu Engine", _windowExtent.width, _windowExtent.height, windowFlags);
}

void Engine::initVulkan() {
	//Lambda for validating VkBootstrap calls
	//&& denotes a forwarding/universal reference, which can bind to anything (l or r value)
	const auto vkbCheck = [](auto &&result, const std::string_view op) {
			if (!result) {
				std::string errorMsg = std::format("{} failed: {}", op, result.error().message());
				for (const std::string &reason : result.detailed_failure_reasons()) {
					errorMsg += "\n " + reason;
				}
				throw std::runtime_error(errorMsg);
			}

			return result.value();
		};

	vkb::SystemInfo systemInfo =
		vkbCheck(vkb::SystemInfo::get_system_info(), "Querying Vulkan system info");

	vkb::InstanceBuilder builder = vkb::InstanceBuilder()
		.set_app_name("Mangetsu Engine")
		.set_engine_name("Mangetsu Engine")
		.require_api_version(1, 4, 0)
#if _DEBUG
		.request_validation_layers()
		.set_debug_callback(debugCallback)
#endif
		;

	_vkbData.instance = vkbCheck(builder.build(), "Creating Vulkan instance");
	_instance = _vkbData.instance.instance;
	_instanceDispatchTable = _vkbData.instance.make_table();

	vkb::PhysicalDeviceSelector selector{ _vkbData.instance };

	SDL_Vulkan_CreateSurface(_window, _instance, nullptr, &_surface);

	{
		const std::array requiredExtensions{
			VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
			VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
			VK_KHR_RAY_QUERY_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
			VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
			VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME
		};

		VkPhysicalDeviceVulkan11Features features11{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
		features11.storageBuffer16BitAccess = true; //Allows us to access our half-precision octahedral-projected normals and tangents
		features11.storagePushConstant16 = true;

		VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		features12.bufferDeviceAddress = true;
		features12.descriptorIndexing = true;
		features12.shaderFloat16 = true;
		features12.runtimeDescriptorArray = true; //Allows shaders to use an arrays of resources whose size is not fixed at compile time
		features12.descriptorBindingSampledImageUpdateAfterBind = true;
		features12.descriptorBindingPartiallyBound = true;
		features12.descriptorBindingVariableDescriptorCount = true;
		features12.scalarBlockLayout = true; //So we can have proper striding in shaders

		VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		features13.dynamicRendering = true;
		features13.synchronization2 = true;

		//I was curious as to why features and extensions had to be queried separately.
		//My main takaway was that vendors can support extensions while not supporting all their features

		VkPhysicalDeviceRayTracingPipelineFeaturesKHR featuresRayTracingPipeline = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
		featuresRayTracingPipeline.rayTracingPipeline = true;

		VkPhysicalDeviceAccelerationStructureFeaturesKHR featuresAccelerationStructures = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
		featuresAccelerationStructures.accelerationStructure = true;

		VkPhysicalDeviceRayQueryFeaturesKHR featuresRayQuery = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
		featuresRayQuery.rayQuery = true;

		VkPhysicalDeviceDescriptorHeapFeaturesEXT featuresDescriptorHeap = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT };
		featuresDescriptorHeap.descriptorHeap = true;

		VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT featuresReorder = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT };
		featuresReorder.rayTracingInvocationReorder = true;

		selector.set_surface(_surface)
			.set_minimum_version(1, 4)
			.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
			.add_required_extensions(requiredExtensions.size(), requiredExtensions.data())
			.set_required_features_11(features11)
			.set_required_features_12(features12)
			.set_required_features_13(features13)
			.add_required_extension_features(featuresRayTracingPipeline)
			.add_required_extension_features(featuresAccelerationStructures)
			.add_required_extension_features(featuresDescriptorHeap)
			.add_required_extension_features(featuresRayQuery)
			.add_required_extension_features(featuresReorder);

		const auto physicalDevices = vkbCheck(selector.select_devices(), "Selecting physical device");

		//TODO: Add additional checks to physical device selection

		if (physicalDevices.size() == 0) throw std::runtime_error("No compatible Vulkan devices were found!");

		_vkbData.physicalDevice = physicalDevices[0];
		_physicalDevice = _vkbData.physicalDevice.physical_device;
	}

	{
		_deviceProperties.properties.pNext = &_deviceProperties.rtProperties;
		_deviceProperties.rtProperties.pNext = &_deviceProperties.asProperties;
		_deviceProperties.asProperties.pNext = &_deviceProperties.dhProperties;
		vkGetPhysicalDeviceProperties2(_physicalDevice, &_deviceProperties.properties);
	}

	//Create the logical device and queues
	//TODO: Consider moving compute and present to their own queues
	{
		_vkbData.device = vkbCheck(vkb::DeviceBuilder(_vkbData.physicalDevice).build(), "Creating Vulkan logical device");
		_device = _vkbData.device.device;

		_dispatchTable = _vkbData.device.make_table();

		_queue = vkbCheck(_vkbData.device.get_queue(vkb::QueueType::graphics), "Getting graphics queue");
		_queueFamilyIdx = vkbCheck(_vkbData.device.get_queue_index(vkb::QueueType::graphics), "Getting graphics queue family");


		const uint32_t presentQueueFamilyIdx = vkbCheck(_vkbData.device.get_queue_index(vkb::QueueType::present), "Getting present queue family");

		if (!(_vkbData.device.queue_families[_queueFamilyIdx].queueFlags & VK_QUEUE_COMPUTE_BIT)) throw std::runtime_error("Selected queue family doesn't support compute!");
		if(presentQueueFamilyIdx != _queueFamilyIdx) throw std::runtime_error("Selected queue family doesn't support present!");
	}
}

void Engine::createSwapChain(int width, int height) {
	const auto vkbCheck = [](auto &&result, const std::string_view op) {
		if (!result) {
			std::string errorMsg = std::format("{} failed: {}", op, result.error().message());
			for (const std::string &reason : result.detailed_failure_reasons()) {
				errorMsg += "\n " + reason;
			}
			throw std::runtime_error(errorMsg);
		}

		return result.value();
		};

	vkb::SwapchainBuilder swapChainBuilder = vkb::SwapchainBuilder(_vkbData.device)
		.set_desired_extent(width, height)
		.set_desired_format({ VK_FORMAT_R8G8B8A8_UNORM,  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }) //self gamma correction
		.add_image_usage_flags(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) //We want to write to the swap chain images with the compute pass
		.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR);

	_vkbData.swapChain = vkbCheck(swapChainBuilder.build(), "Creating Vulkan swap chain");

	_swapChain = _vkbData.swapChain.swapchain;

	_swapChainImageFormat = _vkbData.swapChain.image_format;
	_swapChainExtent = { 
		.width = _vkbData.swapChain.extent.width, 
		.height = _vkbData.swapChain.extent.height
		};

	_swapChainImages = vkbCheck(_vkbData.swapChain.get_images(), "Getting swap chain images");

	if (_swapChainImages.size() > MAX_SWAPCHAIN_SIZE) throw std::runtime_error("Swapchain maximum size exceeded!");

	for (VkImage &swapChainImage : _swapChainImages) {
		VkImageViewCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.pNext = nullptr;
		info.image = swapChainImage;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.format = _swapChainImageFormat;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.baseMipLevel = 0;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = 0;
		info.subresourceRange.layerCount = 1;

		VkImageView view;
		VK_CHECK(vkCreateImageView(_device, &info, nullptr, &view));
		_swapChainImageViews.push_back(view);
		_swapChainImageViewCreateInfos.push_back(info);
	}

	VkFormatProperties3 props3{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
	VkFormatProperties2 props2{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &props3 };
	vkGetPhysicalDeviceFormatProperties2(_physicalDevice, _swapChainImageFormat, &props2);
	if (_swapChainImageFormat != VK_FORMAT_R8G8B8A8_UNORM || !(props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT))
		throw std::runtime_error("Swapchain format is not R8G8B8A8_UNORM with storage support!");
}

void Engine::initSwapChainDescriptors() {
	for (int i = 0; i < _swapChainImageViewCreateInfos.size(); ++i) {
		writeImageDescriptor(ENGINE_IMAGES + i, _swapChainImageViewCreateInfos[i], 
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
	}
	_allocator.flush(_imageHeap);
}

void Engine::createRadianceImage() {
	VkExtent3D extent = VkExtent3D(_swapChainExtent.width, _swapChainExtent.height, 1);

	_radianceImage = _allocator.createImage(extent, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, 
		false, &_radianceImageViewCreateInfo);

	_allocator.immediateSubmit([&](VkCommandBuffer commandBuffer) {
		util::transitionImageLayout(commandBuffer, _radianceImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		});
}

void Engine::initRadianceImageDescriptors() {
	writeImageDescriptor(0, _radianceImageViewCreateInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
	_allocator.flush(_imageHeap);
}

void Engine::initCommandResources() {
	VkCommandPoolCreateInfo commandPoolInfo{};

	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.pNext = nullptr;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolInfo.queueFamilyIndex = _queueFamilyIdx;

	//Initialize command pools and buffers for each frame in flight
	_frameResources.reserve(FRAMES_IN_FLIGHT);
	for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
		_frameResources.push_back({});
		
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frameResources[i]._commandPool));

		VkCommandBufferAllocateInfo commandBufferAllocInfo = init::commandBufferAllocateInfo(_frameResources[i]._commandPool, 1);
		VK_CHECK(vkAllocateCommandBuffers(_device, &commandBufferAllocInfo, &_frameResources[i]._commandBuffer));
	}

	//Initialize the immediate command pool and buffer for the engine
	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

	VkCommandBufferAllocateInfo immCommandBufferAllocInfo = init::commandBufferAllocateInfo(_immCommandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &immCommandBufferAllocInfo, &_immCommandBuffer));

	_deletionQueue.push([this]() { 
		for (auto &frame : _frameResources) vkDestroyCommandPool(_device, frame._commandPool, nullptr);
		vkDestroyCommandPool(_device, _immCommandPool, nullptr);
		});
}

void Engine::initSyncStructures() {
	VkFenceCreateInfo fenceCreateInfo = init::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT); //Pre-signal fence so we don't deadlock immediately
	VkSemaphoreCreateInfo semaphoreCreateInfo = init::semaphoreCreateInfo();

	for (auto &frame : _frameResources) {
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &frame._renderFence));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &frame._swapChainSemaphore));
	}

	//Render complete semaphores are signalled when the rendering operations are done and the image is ready to present
	//Numbered by swap chain image because the semaphore is unsignalled when we get handed the swap chain image
	for (int i = 0; i < MAX_SWAPCHAIN_SIZE; ++i) {
		VkSemaphore semaphore;
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &semaphore));
		_renderCompleteSemaphores.push_back(semaphore);
	}

	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));

	_deletionQueue.push([this]() {
		for (auto &frame : _frameResources) {
			vkDestroyFence(_device, frame._renderFence, nullptr);
			vkDestroySemaphore(_device, frame._swapChainSemaphore, nullptr);
		}

		for (VkSemaphore semaphore : _renderCompleteSemaphores) {
			vkDestroySemaphore(_device, semaphore, nullptr);
		}

		vkDestroyFence(_device, _immFence, nullptr);
		});
}

void Engine::initDescriptorHeaps() {
	const auto &dh = _deviceProperties.dhProperties;

	{
		const size_t slotCount = N_IMAGE_DESCRIPTORS;
		_imageHeapLayout.stride = util::alignUp(dh.imageDescriptorSize, dh.imageDescriptorAlignment);
		_imageHeapLayout.reservedOffset = util::alignUp(slotCount * _imageHeapLayout.stride, std::max(dh.imageDescriptorAlignment, dh.bufferDescriptorAlignment));
		_imageHeapLayout.reservedSize = dh.minResourceHeapReservedRange;
		_imageHeapLayout.size = _imageHeapLayout.reservedOffset + _imageHeapLayout.reservedSize;

		if (_imageHeapLayout.size > dh.maxResourceHeapSize) throw std::runtime_error("Image heap exceeds maxResourceHeapSize!");

		_imageHeap = _allocator.createBuffer(_imageHeapLayout.size, VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT,
			VMA_MEMORY_USAGE_CPU_TO_GPU, true, dh.resourceHeapAlignment);
	}

	{
		const size_t samplerCount = N_SAMPLER_DESCRIPTORS;
		_samplerHeapLayout.stride = util::alignUp(dh.samplerDescriptorSize, dh.samplerDescriptorAlignment);
		_samplerHeapLayout.reservedOffset = samplerCount * _samplerHeapLayout.stride;
		_samplerHeapLayout.reservedSize = dh.minSamplerHeapReservedRange;
		_samplerHeapLayout.size = _samplerHeapLayout.reservedOffset + _samplerHeapLayout.reservedSize;

		if (_samplerHeapLayout.size > dh.maxSamplerHeapSize) throw std::runtime_error("Sampler heap exceeds maxSamplerHeapSize!");

		_samplerHeap = _allocator.createBuffer(_samplerHeapLayout.size, VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT,
			VMA_MEMORY_USAGE_CPU_TO_GPU, true, dh.samplerHeapAlignment);
	}

	//Default sampler, in case a glTF has sampler = -1 for some texture
	VkSamplerCreateInfo defaultSamplerInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	defaultSamplerInfo.magFilter = VK_FILTER_LINEAR;
	defaultSamplerInfo.minFilter = VK_FILTER_LINEAR;
	defaultSamplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	
	VkHostAddressRangeEXT defaultRange{};
	defaultRange.address = _samplerHeap.info.pMappedData;
	defaultRange.size = dh.samplerDescriptorSize;
	VK_CHECK(_dispatchTable.writeSamplerDescriptorsEXT(1, &defaultSamplerInfo, &defaultRange));
	_allocator.flush(_samplerHeap);

	_deletionQueue.push([this]() {
		_allocator.destroyBuffer(_imageHeap);
		_allocator.destroyBuffer(_samplerHeap);
		});
}

void Engine::loadHdrImage(const std::string &path) {

	//Load the hdr using stb
	int width, height, numChannels;
	float *data = stbi_loadf(path.c_str(), &width, &height, &numChannels, 4);

	if (!data) {
		std::cout << "Failed to load hdr: " << path << std::endl;
		return;
	}
	if (_hdrLoaded) {
		vkDeviceWaitIdle(_device);
		_allocator.destroyImage(_hdrImage);
	} 

	VkExtent3D extent;
	extent.width = width;
	extent.height = height;
	extent.depth = 1;
	_hdrImage = _allocator.createImage(data, extent, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT, false, &_hdrImageViewCreateInfo, 4 * sizeof(float));
	
	std::vector<double> weights;
	weights.reserve(width * height);
	for (int i = 0; i < width * height; ++i) {
		//The texture gets squished at the poles
		double rowWeight = glm::sin((static_cast<double>(i / width) + .5f) / static_cast<double>(height) * PI);
		weights.push_back(rowWeight * glm::length(glm::vec3(data[4 * i], data[4 * i + 1], data[4 * i + 2])));
	}
	std::vector<float>pdf;
	std::vector<AliasTableEntry> envAliasTable = buildAliasTable(weights, pdf, &_sceneData.totalEnvWeight);

	if (_envAliasTable.loaded) _allocator.destroyBuffer(_envAliasTable);
	_envAliasTable = _allocator.uploadBuffer(envAliasTable.data(), envAliasTable.size() * sizeof(AliasTableEntry), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
	_sceneData.envAliasTable = _envAliasTable.address;
	if (_sceneDataBuffer.loaded) _allocator.destroyBuffer(_sceneDataBuffer);
	_sceneDataBuffer = _allocator.uploadBuffer(&_sceneData, sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	_hdrLoaded = true;
	stbi_image_free(data);
	writeImageDescriptor(ENGINE_IMAGES + MAX_SWAPCHAIN_SIZE, _hdrImageViewCreateInfo, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	_allocator.flush(_imageHeap);
}

void Engine::writeSceneDescriptors() {
	const auto &dh = _deviceProperties.dhProperties;

	if (_scene.images.size() > N_IMAGE_DESCRIPTORS - ENGINE_IMAGES - MAX_SWAPCHAIN_SIZE - HDR_IMAGES) throw std::runtime_error("Number of images in scene exceeds maximum!");
	if (_scene.samplerCreateInfos.size() > N_SAMPLER_DESCRIPTORS - 1) throw std::runtime_error("Number of samplers in scene exceeds maximum!");

	for (size_t i = 0; i < _scene.images.size(); ++i) {
		writeImageDescriptor(ENGINE_IMAGES + HDR_IMAGES + MAX_SWAPCHAIN_SIZE + i, _scene.imageViewCreateInfos[i], VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	_allocator.flush(_imageHeap);

	if (!_scene.samplerCreateInfos.empty()) {
		std::vector<VkHostAddressRangeEXT> hostAddressRanges;
		hostAddressRanges.reserve(_scene.samplerCreateInfos.size());
		for (int i = 0; i < _scene.samplerCreateInfos.size(); ++i) {
			VkHostAddressRangeEXT hostAddressRange{};
			hostAddressRange.address = reinterpret_cast<char *>(_samplerHeap.info.pMappedData) + (1 + i) * _samplerHeapLayout.stride;
			hostAddressRange.size = dh.samplerDescriptorSize;

			hostAddressRanges.push_back(hostAddressRange);
		}
		VK_CHECK(_dispatchTable.writeSamplerDescriptorsEXT(_scene.samplerCreateInfos.size(), _scene.samplerCreateInfos.data(), hostAddressRanges.data()));
		_allocator.flush(_samplerHeap);
	}
}

void Engine::writeImageDescriptor(uint32_t slot, const VkImageViewCreateInfo &viewInfo, VkDescriptorType type, VkImageLayout layout) {
	VkImageDescriptorInfoEXT imageDescriptorInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT };
	imageDescriptorInfo.pView = &viewInfo;
	imageDescriptorInfo.layout = layout;

	VkResourceDescriptorInfoEXT resourceDescriptorInfo{ .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
	resourceDescriptorInfo.type = type;
	resourceDescriptorInfo.data.pImage = &imageDescriptorInfo;

	VkHostAddressRangeEXT hostAddressRange{};
	hostAddressRange.address = reinterpret_cast<char *>(_imageHeap.info.pMappedData) + slot * _imageHeapLayout.stride;
	hostAddressRange.size = _deviceProperties.dhProperties.imageDescriptorSize;

	VK_CHECK(_dispatchTable.writeResourceDescriptorsEXT(1, &resourceDescriptorInfo, &hostAddressRange));
}

void Engine::loadScene(const std::string &file) {
	vkDeviceWaitIdle(_device);

	if (_sceneDataBuffer.loaded) _allocator.destroyBuffer(_sceneDataBuffer);

	if (_scene.loaded) loader::destroyScene(_device, _allocator, _scene, _dispatchTable);

	//TODO: Reset scene descriptors?

	_scene = loader::loadScene(_device, _allocator, file, _deviceProperties, _dispatchTable);

	//Set up scene data struct
	if (_sceneDataBuffer.loaded) _allocator.destroyBuffer(_sceneDataBuffer);
	_sceneData.deviceSubMeshes = _scene.subMeshBuffer.address;
	_sceneData.materials = _scene.materialBuffer.address;
	_sceneData.lightAliasTable = _scene.lightStrengthAliasTable.address;
	_sceneData.lightTriangles = _scene.lightTriangles.address;
	_sceneData.totalLightWeight = _scene.totalLightWeight;
	_sceneData.nLights = _scene.nLights;
	_sceneDataBuffer = _allocator.uploadBuffer(&_sceneData, sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	writeSceneDescriptors();
}

void Engine::initPipelinePathtracer() {
	//Initialize Vulkan raytracing objects

	VkShaderModule modulePathtracer = util::loadShaderModule("./build/shaders/pathtracer.spv", _device);

	VkDeviceSize imageStride = _imageHeapLayout.stride;

	//Create the bindings for the resources
	std::array mappings = {
		heapMapping(0, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, 0 * imageStride, 0), //Radiance Image
		heapMapping(1, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT, (ENGINE_IMAGES + MAX_SWAPCHAIN_SIZE + HDR_IMAGES) * imageStride, imageStride), //Scene images
		heapMapping(2, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT, 0, _samplerHeapLayout.stride), //Samplers
		pushAddressMapping(3, VK_SPIRV_RESOURCE_TYPE_ACCELERATION_STRUCTURE_BIT_EXT, offsetof(PushConstantsPathtracer, tlas)), //Tlas
		heapMapping(4, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT, (ENGINE_IMAGES + MAX_SWAPCHAIN_SIZE) * imageStride, imageStride), //Hdr image
	};

	VkShaderDescriptorSetAndBindingMappingInfoEXT mappingInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT };
	mappingInfo.mappingCount = static_cast<uint32_t>(mappings.size());
	mappingInfo.pMappings = mappings.data();

	//Lambda that defines a shader stage
	auto stageInfo = [&](VkShaderStageFlagBits flag, const char *entry) {
		VkPipelineShaderStageCreateInfo info{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		info.pNext = &mappingInfo;
		info.stage = flag;
		info.module = modulePathtracer;
		info.pName = entry;
		return info;
		};

	std::array stages = {
		stageInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR, "rayGeneration"),
		stageInfo(VK_SHADER_STAGE_MISS_BIT_KHR, "miss"),
		stageInfo(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "closestHit"),
		stageInfo(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "anyHit")
		};

	//A VkRayTracingShaderGroupTypeKHR bundles together related ray tracing shader stages
	auto group = [](VkRayTracingShaderGroupTypeKHR type, uint32_t general, uint32_t closestHit, uint32_t anyHit) {
		VkRayTracingShaderGroupCreateInfoKHR info{ .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
		info.type = type;
		info.generalShader = general;
		info.closestHitShader = closestHit;
		info.anyHitShader = anyHit;
		info.intersectionShader = VK_SHADER_UNUSED_KHR; //Apparently this is for custom procedural geometry/non-triangular primitives...
		return info;
		};

	//General is for standalone shaders that don't directly participate in a geometry hit/miss lookup
	//Triangle hit is pretty true to the name.
	std::array groups = {
		group(VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 0, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR), //Set the ray generation shader
		group(VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 1, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR), //Set the miss shader
		group(VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR, VK_SHADER_UNUSED_KHR, 2, VK_SHADER_UNUSED_KHR), //Set the closest and any hit shaders
	};

	VkPipelineCreateFlags2CreateInfo flags{.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO};
	flags.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

	VkRayTracingPipelineCreateInfoKHR pipelineInfo{ .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
	pipelineInfo.pNext = &flags;
	pipelineInfo.stageCount = stages.size();
	pipelineInfo.pStages = stages.data();
	pipelineInfo.groupCount = groups.size();
	pipelineInfo.pGroups = groups.data();
	pipelineInfo.maxPipelineRayRecursionDepth = 2;
	pipelineInfo.layout = VK_NULL_HANDLE; //We're using descriptor heaps

	VK_CHECK(_dispatchTable.createRayTracingPipelinesKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_pipelinePathtracer));

	vkDestroyShaderModule(_device, modulePathtracer, nullptr);

	_deletionQueue.push([this]() {vkDestroyPipeline(_device, _pipelinePathtracer, nullptr); });

	//Create the shader binding table
	auto &rt = _deviceProperties.rtProperties;
	uint32_t handleSize = rt.shaderGroupHandleSize;
	VkDeviceSize handleStride = util::alignUp(handleSize, rt.shaderGroupHandleAlignment);

	_rayGenerationRegion.stride = util::alignUp(handleStride, rt.shaderGroupBaseAlignment);
	_rayGenerationRegion.size = _rayGenerationRegion.stride;

	_missRegion.stride = handleStride;
	_missRegion.size = util::alignUp(handleStride, rt.shaderGroupBaseAlignment);

	_hitRegion.stride = handleStride;
	_hitRegion.size = util::alignUp(handleStride, rt.shaderGroupBaseAlignment);

	std::vector<uint8_t> handles(groups.size() * handleSize);
	VK_CHECK(_dispatchTable.getRayTracingShaderGroupHandlesKHR(_pipelinePathtracer, 0, groups.size(), handles.size(), handles.data()));

	_sbtBuffer = _allocator.createBuffer(_rayGenerationRegion.size + _missRegion.size + _hitRegion.size + _callRegion.size,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, VMA_MEMORY_USAGE_CPU_TO_GPU, true, rt.shaderGroupBaseAlignment);

	//Write the handles into the shader binding table
	//Shaders hold records (handles and data), where handles reference groups (bundled shaders called together)
	uint8_t *sbt = reinterpret_cast<uint8_t *> (_sbtBuffer.info.pMappedData);
	memcpy(sbt, handles.data() + 0 * handleSize, handleSize);
	memcpy(sbt + _rayGenerationRegion.size, handles.data() + 1 * handleSize, handleSize);
	memcpy(sbt + _rayGenerationRegion.size + _missRegion.size, handles.data() + 2 * handleSize, handleSize);
	_allocator.flush(_sbtBuffer);

	_rayGenerationRegion.deviceAddress = _sbtBuffer.address;
	_missRegion.deviceAddress = _sbtBuffer.address + _rayGenerationRegion.size;
	_hitRegion.deviceAddress = _sbtBuffer.address + _rayGenerationRegion.size + _missRegion.size;
	_deletionQueue.push([this]() { _allocator.destroyBuffer(_sbtBuffer); });
}

void Engine::initPipelinePost() {
	VkShaderModule modulePost = util::loadShaderModule("./build/shaders/post.spv", _device);

	VkDeviceSize imageStride = _imageHeapLayout.stride;

	std::array mappings = {
	heapMapping(0, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, 0 * imageStride, 0),
	heapMapping(1, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT, ENGINE_IMAGES * imageStride, imageStride)
	};

	VkShaderDescriptorSetAndBindingMappingInfoEXT mappingInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT };
	mappingInfo.mappingCount = mappings.size();
	mappingInfo.pMappings = mappings.data();

	VkPipelineCreateFlags2CreateInfo flags{ .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO };
	flags.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

	VkPipelineShaderStageCreateInfo shaderStage{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	shaderStage.pNext = &mappingInfo;
	shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderStage.module = modulePost;
	shaderStage.pName = "main";

	VkComputePipelineCreateInfo pipelineInfo{ .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipelineInfo.pNext = &flags;
	pipelineInfo.stage = shaderStage;
	pipelineInfo.layout = VK_NULL_HANDLE;

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_pipelinePost));

	vkDestroyShaderModule(_device, modulePost, nullptr);

	_deletionQueue.push([this]() {vkDestroyPipeline(_device, _pipelinePost, nullptr); });
}

void Engine::initImgui() {
	ImGui::CreateContext();

	ImGui_ImplSDL3_InitForVulkan(_window);
	ImGui_ImplVulkan_InitInfo imGuiInfo{};
	imGuiInfo.Instance = _instance;
	imGuiInfo.PhysicalDevice = _physicalDevice;
	imGuiInfo.Device = _device;
	imGuiInfo.Queue = _queue;
	imGuiInfo.QueueFamily = _queueFamilyIdx;
	imGuiInfo.DescriptorPool = VK_NULL_HANDLE; //Thank you, Nathan
	imGuiInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
	imGuiInfo.MinImageCount = 3;
	imGuiInfo.ImageCount = _swapChainImages.size();
	imGuiInfo.UseDynamicRendering = true;

	imGuiInfo.PipelineInfoMain.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	imGuiInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	imGuiInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapChainImageFormat;

	imGuiInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&imGuiInfo);

	_io = &ImGui::GetIO();

	_deletionQueue.push([this]() {
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		});
}

void Engine::drawImgui(VkCommandBuffer commandBuffer, VkImageView targetImageView) {
	//mouseOverImGuiWindow = io->WantCaptureMouse;

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	static float f = 0.0f;
	static int counter = 0;

	ImGui::Begin("Mangetsu Engine Analytics", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

	int maxBounces = _pcpt.maxBounces;
	ImGui::SliderInt("Max bounces", &maxBounces, 1, 20);
	if (maxBounces != _pcpt.maxBounces) _frameNum = 0;
	_pcpt.maxBounces = maxBounces;

	float focusDist = _pcpt.focusDist;
	ImGui::SliderFloat("Focus distance", &focusDist, 1.f, 20.f);
	if (focusDist != _pcpt.focusDist) _frameNum = 0;
	_pcpt.focusDist = focusDist;

	float lensRadius = _pcpt.lensRadius;
	ImGui::SliderFloat("Lens radius", &lensRadius, 0.f, 1.f);
	if (lensRadius != _pcpt.lensRadius) _frameNum = 0;
	_pcpt.lensRadius = lensRadius;

	float theta = _camera.getTheta();
	ImGui::InputFloat("Theta", &theta);

	float phi = _camera.getPhi();
	ImGui::InputFloat("Phi", &phi);

	glm::vec3 origin = _camera.getOrigin();
	ImGui::InputFloat3("Origin", reinterpret_cast<float *>(&origin));

	_camera.setCamera(origin, theta, phi);

	ImGui::End();
	ImGui::Render();

	VkRenderingAttachmentInfo colorAttachmentInfo = init::renderingAttachmentInfo(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderingInfo = init::renderingInfo(_swapChainExtent, &colorAttachmentInfo, nullptr);
	
	vkCmdBeginRendering(commandBuffer, &renderingInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
	vkCmdEndRendering(commandBuffer);
}

void Engine::draw() {
	FrameResources &currFrame = getCurrFrame();

	VK_CHECK(vkWaitForFences(_device, 1, &currFrame._renderFence, true, 1000000000)); //Wait for 1 fence (the fence of the current frame) for up to 1 second
	
	//Get image from swapchain
	uint32_t swapChainImageIndex;

	VkResult result = vkAcquireNextImageKHR(_device, _swapChain, 1000000000, currFrame._swapChainSemaphore, nullptr, &swapChainImageIndex); //Signals the swapchain semaphore when complete

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) { //Swapchain is out of date due to needing to be resized
		_resize = true;
		return;
	}

	VK_CHECK(vkResetFences(_device, 1, &currFrame._renderFence));
	
	VkCommandBuffer commandBuffer = currFrame._commandBuffer;

	VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

	//Since we're re-recording every frame
	VkCommandBufferBeginInfo beginInfo = init::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

	//Bind out descriptor heaps
	//TODO: Consider caching?
	VkBindHeapInfoEXT imageHeapBind{ .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT };
	imageHeapBind.heapRange = { _imageHeap.address, _imageHeapLayout.size };
	imageHeapBind.reservedRangeOffset = _imageHeapLayout.reservedOffset;
	imageHeapBind.reservedRangeSize = _imageHeapLayout.reservedSize;
	_dispatchTable.cmdBindResourceHeapEXT(commandBuffer, &imageHeapBind);

	VkBindHeapInfoEXT samplerHeapBind{ .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT };
	samplerHeapBind.heapRange = { _samplerHeap.address, _samplerHeapLayout.size };
	samplerHeapBind.reservedRangeOffset = _samplerHeapLayout.reservedOffset;
	samplerHeapBind.reservedRangeSize = _samplerHeapLayout.reservedSize;
	_dispatchTable.cmdBindSamplerHeapEXT(commandBuffer, &samplerHeapBind);

	//Call the pathtracer
	if (_camera.wasDirty()) _frameNum = 0; //Reset the frame if the camera's view or rotation changed
		
	_camera.toPushConstantsPathtracer(_pcpt);
	_pcpt.frameNum = _frameNum;
	_pcpt.tlas = _scene.tlas.address;
	_pcpt.sceneData = _sceneDataBuffer.address;

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _pipelinePathtracer);

	VkPushDataInfoEXT pushDataPt{ .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT };
	pushDataPt.offset = 0;
	pushDataPt.data = { &_pcpt, sizeof(PushConstantsPathtracer) };
	
	_dispatchTable.cmdPushDataEXT(commandBuffer, &pushDataPt);
	_dispatchTable.cmdTraceRaysKHR(commandBuffer, &_rayGenerationRegion, &_missRegion, &_hitRegion, &_callRegion, _swapChainExtent.width, _swapChainExtent.height, 1);

	//Transition the swap chain image layout for writing from the post process compute
	util::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	//Call the post process
	PushConstantsPost pcp{};
	pcp.outputImageIdx = swapChainImageIndex;
	
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelinePost);
	VkPushDataInfoEXT pushDataPost{ .sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT };
	pushDataPost.offset = 0;
	pushDataPost.data = { &pcp, sizeof(PushConstantsPost) };
	_dispatchTable.cmdPushDataEXT(commandBuffer, &pushDataPost);
	vkCmdDispatch(commandBuffer, (_swapChainExtent.width + 15) / 16, (_swapChainExtent.height + 15) / 16, 1);

	if (_showGui) {
		util::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		drawImgui(commandBuffer, _swapChainImageViews[swapChainImageIndex]);

		//Transition image for presentation
		util::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}
	else {
		util::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}
	

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkCommandBufferSubmitInfo commandBufferSubmitInfo = init::commandBufferSubmitInfo(commandBuffer);

	VkSemaphoreSubmitInfo waitSemaphoreSubmitInfo = init::semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, currFrame._swapChainSemaphore);
	VkSemaphoreSubmitInfo signalSemaphoreSubmitInfo = init::semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, _renderCompleteSemaphores[swapChainImageIndex]);

	//Submit
	VkSubmitInfo2 submit = init::submitInfo(&commandBufferSubmitInfo, &signalSemaphoreSubmitInfo, &waitSemaphoreSubmitInfo, 1, 1);
	VK_CHECK(vkQueueSubmit2(_queue, 1, &submit, currFrame._renderFence));

	//TODO: Present
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &_swapChain;
	presentInfo.pWaitSemaphores = &_renderCompleteSemaphores[swapChainImageIndex];
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapChainImageIndex;

	result = vkQueuePresentKHR(_queue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		_resize = true;
		return;
	}
	++_frameNum;
}

void Engine::resize() {
	int width, height;
	SDL_GetWindowSizeInPixels(_window, &width, &height);

	if (width == 0 || height == 0) return;

	_windowExtent = VkExtent2D(width, height);

	vkDeviceWaitIdle(_device);

	//Destroy and recreate the swap chain
	destroySwapChain();
	createSwapChain(width, height);
	initSwapChainDescriptors();

	//Destroy and recreate the radiance image
	_allocator.destroyImage(_radianceImage);
	createRadianceImage();
	initRadianceImageDescriptors();

	_frameNum = 0;
	_resize = false;
}

void Engine::destroySwapChain() {
	for (auto swapChainImageView : _swapChainImageViews) vkDestroyImageView(_device, swapChainImageView, nullptr);
	_swapChainImages.clear();
	_swapChainImageViews.clear();
	_swapChainImageViewCreateInfos.clear();

	vkDestroySwapchainKHR(_device, _swapChain, nullptr);
}

void Engine::init() {
	initWindow();
	initVulkan();
	createSwapChain(WIDTH, HEIGHT);
	initCommandResources();
	initSyncStructures();

	_allocator.init(_instance, _physicalDevice, _device);
	_allocator.setCommandInfo(_queue, _immCommandBuffer, _immFence);

	createRadianceImage();
	initDescriptorHeaps();

	loadHdrImage("./hdrs/kloofendal_48d_partly_cloudy_puresky_4k.hdr");
	initSwapChainDescriptors();
	initRadianceImageDescriptors();

	_loaded = true;

	//Load default scene
	std::string path = "./models/cornellbox_transmission.glb";
	loadScene(path);

	initPipelinePathtracer();
	initPipelinePost();

	initImgui();
}

void Engine::run() {
	SDL_Event e;

	bool quit = false;

	while (!quit) {

		//Handle events
		while (SDL_PollEvent(&e) != 0) {
			ImGui_ImplSDL3_ProcessEvent(&e);

			if (e.type == SDL_EVENT_QUIT) quit = true;

			if (e.type == SDL_EVENT_WINDOW_MINIMIZED) _minimized = true;

			if (e.type == SDL_EVENT_WINDOW_RESTORED) _minimized = false;

			if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) _resize = true;

			if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_S) _showGui = !_showGui;

			//TODO: Handle camera movement
			if (!_io->WantCaptureMouse) {
				_camera.processEvent(e);
			}
		}

		if (_minimized) {
			SDL_Delay(10);
			continue;
		}

		if (_resize) resize();

		draw();
	}
}

void Engine::cleanup() {
	if (_loaded) {
		vkDeviceWaitIdle(_device);

		if (_sceneDataBuffer.loaded) _allocator.destroyBuffer(_sceneDataBuffer);
		if (_scene.loaded) loader::destroyScene(_device, _allocator, _scene, _dispatchTable);

		if (_envAliasTable.loaded) _allocator.destroyBuffer(_envAliasTable);

		destroySwapChain();
		if (_hdrLoaded) _allocator.destroyImage(_hdrImage);
		_allocator.destroyImage(_radianceImage);

		_deletionQueue.flush();

		_allocator.cleanup();

		SDL_Vulkan_DestroySurface(_instance, _surface, nullptr);
		SDL_DestroyWindow(_window);

		vkb::destroy_device(_vkbData.device);
		vkb::destroy_instance(_vkbData.instance); //Handles debug messenger destruction
	}

	_loaded = false;
}