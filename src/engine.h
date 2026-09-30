#pragma once
#include <vector>

#include <vulkan/vk_enum_string_helper.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"

#include "geometry.h"
#include "util.h"
#include "allocator.h"
#include "camera.h"

struct FrameResources {
	VkCommandPool _commandPool{ VK_NULL_HANDLE };
	VkCommandBuffer _commandBuffer{ VK_NULL_HANDLE };

	VkSemaphore _swapChainSemaphore{ VK_NULL_HANDLE }; //Wait to perform rendering operations on the GPU until the swap chain semaphore is signalled
	VkFence _renderFence{ VK_NULL_HANDLE }; //Wait to record the command buffer until the fence is signalled
};

struct VkbData {
	vkb::Instance instance;
	vkb::Device device;
	vkb::PhysicalDevice physicalDevice;
	vkb::Swapchain swapChain;
};

struct DeviceProperties {
	VkPhysicalDeviceProperties2 properties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
	VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
	VkPhysicalDeviceDescriptorHeapPropertiesEXT dhProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT };
};

constexpr int WIDTH = 1920;
constexpr int HEIGHT = 1080;

constexpr int MAX_SWAPCHAIN_SIZE = 4;
constexpr int FRAMES_IN_FLIGHT = 3;

constexpr int ENGINE_IMAGES = 1;
constexpr int N_IMAGE_DESCRIPTORS = 4096;
constexpr int N_SAMPLER_DESCRIPTORS = 256;

constexpr int MAX_BOUNCES = 10;

class Engine {
private:
	bool _loaded{ false };

	VkExtent2D _windowExtent{ WIDTH, HEIGHT };

	//Vulkan handles
	VkInstance _instance;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapChain;
	VkExtent2D _swapChainExtent;
	VkFormat _swapChainImageFormat;
	std::vector<VkImage> _swapChainImages;
	std::vector<VkImageView> _swapChainImageViews;
	std::vector<VkImageViewCreateInfo> _swapChainImageViewCreateInfos;

	VkQueue _queue;
	uint32_t _queueFamilyIdx;

	//TODO: Consider adding more command pools
	VkCommandPool _immCommandPool;
	VkCommandBuffer _immCommandBuffer;

	VkPipeline _pipelinePathtracer;
	VkPipeline _pipelinePost;

	VkbData _vkbData;

	//VMA Allocator
	Allocator _allocator;

	//Synchronization resources
	std::vector<FrameResources> _frameResources;
	std::vector<VkSemaphore> _renderCompleteSemaphores;
	VkFence _immFence; //Fence to avoid overwriting the immediate command buffer

	//Other handles
	SDL_Window *_window{ nullptr };

	//Tables that manage and store Vulkan function pointers
	vkb::DispatchTable _dispatchTable;
	vkb::InstanceDispatchTable _instanceDispatchTable;

	//Cached Vulkan physical device properties
	DeviceProperties _deviceProperties;

	//Descriptor heap information (only 1 resource heap and 1 sampler heap can be bound at once)
	HeapLayout _samplerHeapLayout;
	AllocatedBuffer _samplerHeap;

	HeapLayout _imageHeapLayout;
	AllocatedBuffer _imageHeap;

	ImGuiIO *_io{ nullptr };
	Camera _camera{};

	PushConstantsPathtracer _pcpt{};

	//Vulkan raytracing objects
	Scene _scene{};

	//For the shader binding table
	VkStridedDeviceAddressRegionKHR _rayGenerationRegion{};
	VkStridedDeviceAddressRegionKHR _missRegion{};
	VkStridedDeviceAddressRegionKHR _hitRegion{};
	VkStridedDeviceAddressRegionKHR _callRegion{};

	AllocatedBuffer _sbtBuffer;

	AllocatedImage _radianceImage;
	VkImageViewCreateInfo _radianceImageViewCreateInfo;

	//Deletion queue to handle object destruction
	//TODO: VkGuide, from which I got this implementation, mentioned a better way to do it, so that's something I might want to look into
	DeletionQueue _deletionQueue;

	void initWindow();
	void initVulkan();

	void createSwapChain(int width, int height);
	void destroySwapChain();
	void initSwapChainDescriptors();

	void createRadianceImage();
	void initRadianceImageDescriptors();
	void destroyRadianceImage();

	void initCommandResources();
	void initSyncStructures();

	void initDescriptorHeaps();
	void writeSceneDescriptors();
	void writeImageDescriptor(uint32_t slot, const VkImageViewCreateInfo &viewInfo, VkDescriptorType type, VkImageLayout layout);

	void initImgui();

	void loadScene(const std::string &file);

	void initPipelinePathtracer();
	void initPipelinePost();

	void drawImgui(VkCommandBuffer commandBuffer, VkImageView targetImageView);
	void draw();

	void resize();

	int _frameNum{ 0 };
	bool _resize{ false };
	bool _minimized{ false };

	//Helpers
	inline FrameResources &getCurrFrame() { return _frameResources[_frameNum % FRAMES_IN_FLIGHT]; }

public:
	void init();
	void run();
	void cleanup();

	Engine() = default;

	//Prevent copy-construction and assignment
	Engine(const Engine &) = delete;
	Engine & operator=(const Engine &) = delete;
};

inline Engine *loadedEngine{ nullptr };