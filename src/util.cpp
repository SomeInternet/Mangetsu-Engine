#include <fstream>

#include "util.h"
#include "init.h"

void util::transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 imageBarrier{};

    //All commands recorded to this command buffer must complete for this barrier to be passed, in order for any commands after it in the command buffer to execute
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imageBarrier.pNext = nullptr;
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; //Inefficient, but okay for our purposes. TODO: Make more lightweight {?)
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; //Inefficient, but okay for our purposes
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    imageBarrier.oldLayout = currLayout;
    imageBarrier.newLayout = newLayout;

    //Access the depth compononent of the image if it's supposed to be a depth attachment, else access the color
    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange = init::imageSubresourceRange(aspectMask); //Target the part of the image
    imageBarrier.image = image;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.pNext = nullptr;

    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

VkShaderModule util::loadShaderModule(const char *filePath, VkDevice device) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary); //Open file at the end, in binary mode

    if (!file.is_open()) throw std::runtime_error("Failed to open file");

    size_t fileSize = static_cast<size_t>(file.tellg()); //Get file size via cursor position

    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));

    return shaderModule;
}