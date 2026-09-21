#pragma once
#include <vector>

#include <vulkan/vk_enum_string_helper.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "geometry.h"
#include "util.h"
#include "allocator.h"

struct FrameResources {
	VkCommandPool _commandPool{ VK_NULL_HANDLE };
	VkCommandBuffer _commandBuffer{ VK_NULL_HANDLE };

	VkSemaphore _swapChainSemaphore{ VK_NULL_HANDLE }; //Wait to perform rendering operations on the GPU until the swap chain semaphore is signalled
	VkFence _renderFence{ VK_NULL_HANDLE }; //Wait to record the command buffer until the fence is signalled
};

struct vkbData {
	vkb::Instance instance;
	vkb::Device device;
	vkb::PhysicalDevice physicalDevice;
	vkb::Swapchain swapChain;
};

constexpr int WIDTH = 1920;
constexpr int HEIGHT = 1080;

constexpr int FRAMES_IN_FLIGHT = 3;

class Engine {
private:
	bool _loaded{ false };

	VkExtent2D _windowExtent;

	//Vulkan handles
	VkInstance _instance;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;
	VkSurfaceKHR _surface;
	VkDebugUtilsMessengerEXT _debugMessenger;

	VkSwapchainKHR _swapChain;
	VkExtent2D _swapChainExtent;
	VkFormat _swapChainImageFormat;
	std::vector<VkImage> _swapChainImages;
	std::vector<VkImageView> _swapChainImageViews;

	VkQueue _queue;
	uint32_t _queueFamilyIdx;

	//TODO: Consider adding more command pools
	VkCommandPool _immCommandPool;
	VkCommandBuffer _immCommandBuffer;

	VkPipeline _pipelinePathtraced;

	//TODO: Cache physical device properties

	vkbData _vkbData;

	//VMA Allocator
	Allocator _allocator;

	//Synchronization resources
	std::vector<FrameResources> _frameResources;
	std::vector<VkSemaphore> _renderCompleteSemaphores;
	VkFence _immFence; //Fence to avoid overwriting the immediate command buffer

	//Other handles
	SDL_Window *_window{ nullptr };

	//Vulkan raytracing objects

	//Deletion queue to handle object destruction
	//TODO: VkGuide, from which I got this implementation, mentioned a better way to do it, so that's something I might want to look into
	DeletionQueue _deletionQueue;

	//Scene and geometry data
	std::vector<Mesh> meshes;

	void initWindow();
	void initVulkan();
	void createSwapChain(int width, int height);
	void initCommandResources();
	void initSyncStructures();

	void initPipelinePathtraced();

public:
	void init();
	void run();
	void cleanup();
};

inline Engine *loadedEngine{ nullptr };