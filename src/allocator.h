#pragma once
#include <vk_mem_alloc.h>
#include <functional>

struct AllocatedBuffer {
	VkBuffer buffer;
	VkDeviceAddress address;
	VmaAllocation allocation;
	VmaAllocationInfo info;
};

struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VkExtent3D imageExtent;
	VkFormat imageFormat;
	VmaAllocation allocation;
	VmaAllocationInfo allocationInfo;
};

class Allocator {
public:
	void init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
	void cleanup();

	//Set the queues, sync objects, and command buffers the allocator will use
	void setCommandInfo(VkQueue queue, VkCommandBuffer commandBuffer, VkFence fence);

	void immediateSubmit(std::function<void(VkCommandBuffer commandBuffer)> &&function);

	//Buffers
	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, bool createMapping = true, size_t minAlignment = 0);
	AllocatedBuffer uploadBuffer(void *data, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, bool createMapping = true, size_t minAlignment = 0);
	void destroyBuffer(const AllocatedBuffer &buffer);

	void flush(const AllocatedBuffer &buffer);

	//Images
	AllocatedImage createImage(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps = false, VkImageViewCreateInfo *imageViewInfoSave = nullptr);
	AllocatedImage createImage(const void *data, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps = false, VkImageViewCreateInfo *imageViewInfoSave = nullptr, size_t bytesPerPixel = 4);
	void destroyImage(const AllocatedImage &image);

private:
	VkDevice _device{ VK_NULL_HANDLE };
	VmaAllocator _allocator{ nullptr };

	//Borrowed from the main engine
	VkQueue _queue{ VK_NULL_HANDLE };
	VkCommandBuffer _immCommandBuffer{ VK_NULL_HANDLE };
	VkFence _immFence{ VK_NULL_HANDLE };
};