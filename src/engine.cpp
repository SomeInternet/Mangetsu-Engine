#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <tiny_gltf.h>
#include <vk_mem_alloc.h>
#include <format>

#include "engine.h"
#include "init.h"
#include "loader.h"

void Engine::initWindow() {
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags windowFlags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN);

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
			VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME
		};

		VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		features12.bufferDeviceAddress = true;
		features12.descriptorIndexing = true;
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

		selector.set_surface(_surface)
			.set_minimum_version(1, 4)
			.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
			.add_required_extensions(requiredExtensions.size(), requiredExtensions.data())
			.set_required_features_12(features12)
			.set_required_features_13(features13)
			.add_required_extension_features(featuresRayTracingPipeline)
			.add_required_extension_features(featuresAccelerationStructures)
			.add_required_extension_features(featuresDescriptorHeap)
			.add_required_extension_features(featuresRayQuery);

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

	_windowExtent = VkExtent2D(width, height);

	vkb::SwapchainBuilder swapChainBuilder = vkb::SwapchainBuilder(_vkbData.device)
		.set_desired_extent(width, height)
		.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
		.add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT); //TODO: Consider removing the transfer bit

	_vkbData.swapChain = vkbCheck(swapChainBuilder.build(), "Creating Vulkan swap chain");

	_swapChain = _vkbData.swapChain.swapchain;

	_swapChainImageFormat = _vkbData.swapChain.image_format;
	_swapChainExtent = { 
		.width = _vkbData.swapChain.extent.width, 
		.height = _vkbData.swapChain.extent.height
		};

	_swapChainImages = vkbCheck(_vkbData.swapChain.get_images(), "Getting swap chain images");

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
	}
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
	for (int i = 0; i < _swapChainImages.size(); ++i) {
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

	_deletionQueue.push([this]() {
		_allocator.destroyBuffer(_imageHeap);
		_allocator.destroyBuffer(_samplerHeap);
		});
}

void Engine::writeSceneDescriptors() {
	const auto &dh = _deviceProperties.dhProperties;

	if (_scene.images.size() > N_IMAGE_DESCRIPTORS - ENGINE_IMAGES) throw std::runtime_error("Number of images in scene exceeds maximum!");
	if (_scene.samplerCreateInfos.size() > N_SAMPLER_DESCRIPTORS - 1) throw std::runtime_error("Number of samplers in scene exceeds maximum!");

	for (size_t i = 0; i < _scene.images.size(); ++i) {
		writeImageDescriptor(ENGINE_IMAGES + i, _scene.imageViewCreateInfos[i], VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	_allocator.flush(_imageHeap);

	std::vector<VkHostAddressRangeEXT> hostAddressRanges;
	hostAddressRanges.reserve(1 + _scene.samplerCreateInfos.size());
	for (int i = 0; i < _scene.samplerCreateInfos.size(); ++i) {
		VkHostAddressRangeEXT hostAddressRange{};
		hostAddressRange.address = reinterpret_cast<char *>(_samplerHeap.info.pMappedData) + i * _samplerHeapLayout.stride, dh.samplerDescriptorSize;
		hostAddressRange.size = dh.samplerDescriptorSize;

		hostAddressRanges.push_back(hostAddressRange);
	}
	VK_CHECK(_dispatchTable.writeSamplerDescriptorsEXT(_scene.samplerCreateInfos.size(), _scene.samplerCreateInfos.data(), hostAddressRanges.data()));
	_allocator.flush(_samplerHeap);
}

void Engine::writeImageDescriptor(uint32_t slot, const VkImageViewCreateInfo &viewInfo, VkDescriptorType type, VkImageLayout layout) {
	VkImageDescriptorInfoEXT imageDescriptorInfo{ .sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT };
	imageDescriptorInfo.pView = &viewInfo;
	imageDescriptorInfo.layout = layout;

	VkResourceDescriptorInfoEXT resourceDescriptorInfo{ .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
	resourceDescriptorInfo.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	resourceDescriptorInfo.data.pImage = &imageDescriptorInfo;

	VkHostAddressRangeEXT hostAddressRange{};
	hostAddressRange.address = reinterpret_cast<char *>(_imageHeap.info.pMappedData) + slot * _imageHeapLayout.stride;
	hostAddressRange.size = _deviceProperties.dhProperties.imageDescriptorSize;

	VK_CHECK(_dispatchTable.writeResourceDescriptorsEXT(1, &resourceDescriptorInfo, &hostAddressRange));
}

void Engine::loadScene(std::string &file) {
	if (_scene.loaded) loader::destroyScene(_device, _allocator, _scene);

	_scene = loader::loadScene();

	writeSceneDescriptors();
}

void Engine::initPipelinePathtraced() {
	//Initialize Vulkan raytracing objects

	
}

void Engine::draw() {
	FrameResources &currFrame = getCurrFrame();

	VK_CHECK(vkWaitForFences(_device, 1, &currFrame._renderFence, true, 1000000000)); //Wait for 1 fence (the fence of the current frame) for up to 1 second
	
	//Get image from swapchain
	uint32_t swapChainImageIndex;

	VkResult result = vkAcquireNextImageKHR(_device, _swapChain, 1000000000, currFrame._swapChainSemaphore, nullptr, &swapChainImageIndex); //Signals the swapchain semaphore when complete
	if (result == VK_ERROR_OUT_OF_DATE_KHR) { //Swapchain is out of date due to needing to be resized
		_resize = true;
		return;
	}

	//TODO: Fill in the frame's loop here

	VkSubmitInfo2 submit{};

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
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		_resize = true;
		return;
	}
	++_frameNum;
}

void Engine::destroySwapChain() {
	for (auto swapChainImageView : _swapChainImageViews) vkDestroyImageView(_device, swapChainImageView, nullptr);
	_swapChainImages.clear();
	_swapChainImageViews.clear();

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

	_loaded = true;
}

void Engine::run() {
	SDL_Event e;

	bool quit = false;

	while (!quit) {
		while (SDL_PollEvent(&e) != 0) {
			if (e.type == SDL_EVENT_QUIT) quit = true;
		}

		//Draw the frame
	}
}

void Engine::cleanup() {
	if (_loaded) {
		vkDeviceWaitIdle(_device);
		destroySwapChain();

		_deletionQueue.flush();

		_allocator.cleanup();

		SDL_Vulkan_DestroySurface(_instance, _surface, nullptr);
		SDL_DestroyWindow(_window);

		vkb::destroy_device(_vkbData.device);
		vkb::destroy_instance(_vkbData.instance); //Handles debug messenger destruction
	}

	_loaded = false;
}