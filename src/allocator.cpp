#include <iostream>
#include <functional>

#include "util.h"
#include "init.h"
#include "allocator.h"

//SETUP AND TEARDOWN
//===================================================================================================================
void Allocator::init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
	_device = device;

	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.device = device;
	allocatorInfo.instance = instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; //Allows us to use device pointers
	vmaCreateAllocator(&allocatorInfo, &_allocator);
}

void Allocator::setCommandInfo(VkQueue queue, VkCommandBuffer commandBuffer, VkFence fence) {
	_queue = queue;
	_immCommandBuffer = commandBuffer;
	_immFence = fence;
}

void Allocator::cleanup() {

	if (_allocator) {
		vmaDestroyAllocator(_allocator);
		_allocator = nullptr;
	}
}

void Allocator::immediateSubmit(std::function<void(VkCommandBuffer commandBuffer)> &&function) {
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer commandBuffer = _immCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = init::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBeginInfo));

	function(commandBuffer);

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	VkCommandBufferSubmitInfo cmdinfo = init::commandBufferSubmitInfo(commandBuffer);
	VkSubmitInfo2 submit = init::submitInfo(&cmdinfo, nullptr, nullptr);

	//_immFence will now block until the graphics commands finish execution
	VK_CHECK(vkQueueSubmit2(_queue, 1, &submit, _immFence));
	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

AllocatedBuffer Allocator::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, bool createMapping /* = false */) {
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = memoryUsage; //The usage flags influences where VMA places our buffer
	vmaAllocInfo.flags = createMapping ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0; //Create memory mapped to the CPU's address space (like vkMapMemory)
	//The mapping isn't automatically kept consistent, for performance reasons
	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));

	VkBufferDeviceAddressInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = newBuffer.buffer };
	newBuffer.address = vkGetBufferDeviceAddress(_device, &info);

	return newBuffer;
}

void Allocator::destroyBuffer(const AllocatedBuffer &buffer) {
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}


AllocatedImage Allocator::createImage(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps /*= false*/ ) {
	AllocatedImage newImage;
	newImage.imageFormat = format;
	newImage.imageExtent = extent;

	VkImageCreateInfo imageInfo = init::imageCreateInfo(format, usage, extent);
	if (mipmaps) {
		imageInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;
	}

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VK_CHECK(vmaCreateImage(_allocator, &imageInfo, &allocInfo, &newImage.image, &newImage.allocation, &newImage.allocationInfo));

	VkImageAspectFlags aspectFlag = (format == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageViewCreateInfo imageViewInfo = init::imageViewCreateInfo(format, newImage.image, aspectFlag);
	imageViewInfo.subresourceRange.levelCount = imageInfo.mipLevels;

	VK_CHECK(vkCreateImageView(_device, &imageViewInfo, nullptr, &newImage.imageView));
	return newImage;
}

AllocatedImage Allocator::createImage(void *data, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps = false) {
	size_t dataSize = extent.width * extent.height * extent.depth * 4; //We're assuming 8-bit RGBA channels

	AllocatedBuffer stagingBuffer = createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	memcpy(stagingBuffer.info.pMappedData, data, dataSize);


	AllocatedImage newImage = createImage(extent, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmaps);
	immediateSubmit([&](VkCommandBuffer commandBuffer) {
		util::transitionImageLayout(commandBuffer, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = extent;

		//Copy the buffer to the image
		vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		//Transition layout for use in the fragment shader
		util::transitionImageLayout(commandBuffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

	destroyBuffer(stagingBuffer);

	return newImage;
}

void Allocator::destroyImage(const AllocatedImage &image) {
	vkDestroyImageView(_device, image.imageView, nullptr);
	vmaDestroyImage(_allocator, image.image, image.allocation);
}

