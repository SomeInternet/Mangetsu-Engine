#pragma once
#include "util.h"

//Apparently it's good practice to use "inline/constexpr" when defining functions in a header, to avoid
//issues that emerge when copies of the header get placed into the source files (translation units) that reference them.
//They allow for multiple copies to exist across translation units, but only when they're exactly identical
namespace init {
	constexpr VkCommandBufferAllocateInfo commandBufferAllocateInfo(VkCommandPool pool, uint32_t count = 1) {
        VkCommandBufferAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.pNext = nullptr;
        info.commandPool = pool;
        info.commandBufferCount = count;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; //Can't be invoked by other command buffers

        return info;
	}

    constexpr VkFenceCreateInfo fenceCreateInfo(VkFenceCreateFlags flags = 0) {
        VkFenceCreateInfo info{};

        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        info.pNext = nullptr;
        info.flags = flags;

        return info;
    }

    constexpr VkSemaphoreCreateInfo semaphoreCreateInfo(VkSemaphoreCreateFlags flags = 0) {
        VkSemaphoreCreateInfo info{};

        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        info.pNext = nullptr;
        info.flags = flags;

        return info;
    }

    constexpr VkCommandBufferBeginInfo commandBufferBeginInfo(VkCommandBufferUsageFlags flags = 0) {
        VkCommandBufferBeginInfo info{};

        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.pNext = nullptr;
        info.pInheritanceInfo = nullptr;
        info.flags = flags;

        return info;
    }

    constexpr VkCommandBufferSubmitInfo commandBufferSubmitInfo(VkCommandBuffer cmd) {
        VkCommandBufferSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        info.pNext = nullptr;
        info.commandBuffer = cmd;
        info.deviceMask = 0;

        return info;
    }

    constexpr VkSubmitInfo2 submitInfo(VkCommandBufferSubmitInfo *cmd, VkSemaphoreSubmitInfo *signalSemaphoreInfo,
        VkSemaphoreSubmitInfo *waitSemaphoreInfo, uint32_t signalSemaphoreInfoCount = 1, uint32_t waitSemaphoreInfoCount = 1) {
        VkSubmitInfo2 info{};

        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        info.pNext = nullptr;

        //Semaphore that the queue waits on before executing this command buffer
        info.waitSemaphoreInfoCount = (waitSemaphoreInfo == nullptr) ? 0 : waitSemaphoreInfoCount;
        info.pWaitSemaphoreInfos = waitSemaphoreInfo;

        //Semaphore that the Vulkan queue signals signals upon this command buffer executing
        info.signalSemaphoreInfoCount = (signalSemaphoreInfo == nullptr) ? 0 : signalSemaphoreInfoCount;
        info.pSignalSemaphoreInfos = signalSemaphoreInfo;

        info.commandBufferInfoCount = 1;
        info.pCommandBufferInfos = cmd;

        return info;
    }

    constexpr VkImageCreateInfo imageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent) {
        VkImageCreateInfo info{};

        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.pNext = nullptr;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = extent;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usageFlags;
        return info;
    }

    constexpr VkImageSubresourceRange imageSubresourceRange(VkImageAspectFlags aspectMask) {
        VkImageSubresourceRange subImage{};

        subImage.aspectMask = aspectMask;
        subImage.baseMipLevel = 0;
        subImage.levelCount = VK_REMAINING_MIP_LEVELS;
        subImage.baseArrayLayer = 0;
        subImage.layerCount = VK_REMAINING_ARRAY_LAYERS;

        return subImage;
    }

    constexpr VkImageViewCreateInfo imageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags) {
        VkImageViewCreateInfo info{};

        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.pNext = nullptr;
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange.aspectMask = aspectFlags;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;
        return info;
    }

    constexpr VkSemaphoreSubmitInfo semaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore) {
        VkSemaphoreSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        info.pNext = nullptr;
        info.semaphore = semaphore;
        info.stageMask = stageMask;
        info.value = 0;
        return info;
    }

    constexpr VkRenderingAttachmentInfo renderingAttachmentInfo(VkImageView imageView, VkClearValue *clearValue, VkImageLayout imageLayout) {
        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.pNext = nullptr;

        colorAttachment.imageView = imageView;
        colorAttachment.imageLayout = imageLayout;
        colorAttachment.loadOp = clearValue ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD; //If we have a clear value, then clear the image, else load it, keeping the existing data
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (clearValue) {
            colorAttachment.clearValue = *clearValue;
        }

        return colorAttachment;
    }

    constexpr VkRenderingInfo renderingInfo(VkExtent2D renderExtent, VkRenderingAttachmentInfo *colorAttachment,
        VkRenderingAttachmentInfo *depthAttachment) {
        VkRenderingInfo renderInfo{};

        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.pNext = nullptr;

        //RenderArea bounds both the viewport and the scissors
        renderInfo.renderArea = VkRect2D{ VkOffset2D { 0, 0 }, renderExtent };
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = colorAttachment;
        renderInfo.pDepthAttachment = depthAttachment;
        renderInfo.pStencilAttachment = nullptr;

        return renderInfo;
    }
};