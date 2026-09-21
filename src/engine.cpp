
#include <VkBootstrap.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <tiny_gltf.h>
#include <vk_mem_alloc.h>
#include <format>

#include "engine.h"
#include "init.h"

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
		.require_api_version(1, 4, 0);

	_vkbData.instance = vkbCheck(builder.build(), "Creating Vulkan instance");
	_instance = _vkbData.instance.instance;

	vkb::PhysicalDeviceSelector selector{ _vkbData.instance };

	SDL_Vulkan_CreateSurface(_window, _instance, nullptr, &_surface);

	{
		const std::array requiredExtensions {
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

		assert(physicalDevices.size() > 0, "No compatible Vulkan devices were found!");

		_vkbData.physicalDevice = physicalDevices[0];
		_physicalDevice = _vkbData.physicalDevice.physical_device;
	}

	//TODO: Cache physical device properties to avoid querying repeatedly
	{
	}

	//Create the logical device and queues
	//TODO: Consider moving compute and present to their own queues
	{
		_vkbData.device = vkbCheck(vkb::DeviceBuilder(_vkbData.physicalDevice).build(), "Creating Vulkan logical device");
		_device = _vkbData.device.device;

		_queue = vkbCheck(_vkbData.device.get_queue(vkb::QueueType::graphics), "Getting graphics queue");
		_queueFamilyIdx = vkbCheck(_vkbData.device.get_queue_index(vkb::QueueType::graphics), "Getting graphics queue family");


		const uint32_t presentQueueFamilyIdx = vkbCheck(_vkbData.device.get_queue_index(vkb::QueueType::present), "Getting present queue family");

		assert(_vkbData.device.queue_families[_queueFamilyIdx].queueFlags &VK_QUEUE_COMPUTE_BIT, "Selected queue family doesn't support compute!");
		assert(presentQueueFamilyIdx == _queueFamilyIdx, "Selected queue family doesn't support present!");
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

	_deletionQueue.push([=]() { vkDestroyCommandPool(_device, _immCommandPool, nullptr); });
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

	_deletionQueue.push([=]() {
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

void Engine::initPipelinePathtraced() {
	//Initialize Vulkan raytracing objects

	
}

void Engine::init() {
	initWindow();
	initVulkan();
	createSwapChain(WIDTH, HEIGHT);
	initCommandResources();
	initSyncStructures();

	_loaded = true;
}

void Engine::run() {
	SDL_Event e;

	bool quit = false;

	while (!quit) {
		while (SDL_PollEvent(&e) != 0) {
			if (e.type == SDL_EVENT_QUIT) quit = true;
		}

	}
}

void Engine::cleanup() {
	if (_loaded) {
		vkDeviceWaitIdle(_device);

		_deletionQueue.flush();

		SDL_Vulkan_DestroySurface(_instance, _surface, nullptr);
		SDL_DestroyWindow(_window);
	}

	_loaded = false;
}