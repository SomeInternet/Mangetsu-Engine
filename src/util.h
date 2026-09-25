#pragma once
#include <iostream>
#include <deque>
#include <functional>
#include <vulkan/vk_enum_string_helper.h>

#include <vulkan/vulkan.h>

//The do-while helps w/ expansion, and scopes the err
#define VK_CHECK(x)																																\
	do {																																		\
		VkResult err = x;																														\
		if (err) {																																\
			std::cerr << "Vulkan error at file: " << __FILE__ << ", line: " << __LINE__ << ", error: " << string_VkResult(err) << std::endl;	\
			abort();																															\
		}																																		\
	} while (0)

inline VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT * pCallbackData,
	void *pUserData) {

	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		//The pCallbackData parameter refers to a VkDebugUtilsMessengerCallbackDataEXT
		//struct containing the details of the message itself, with the most important members being:
		//pMessage: The debug message as a null - terminated string
		//pObjects : Array of Vulkan object handles related to the message
		//objectCount : Number of objects in array

		std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl; //TODO: Replace w/ FMT (?)
	}

	return VK_FALSE;
}

//Regarding the note in init.h, functions declared in a class body are implicitly inline
class DeletionQueue {
public:
	void push(std::function<void()> &&function) { deletors.push_back(std::move(function)); }

	void flush() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); ++it) (*it)();
		deletors.clear();
	}

private:
	std::deque<std::function<void()>> deletors;
};

struct HeapLayout {
	VkDeviceSize stride{ 0 };
	VkDeviceSize reservedOffset{ 0 };
	VkDeviceSize reservedSize{ 0 };
	VkDeviceSize size{ 0 };
	uint32_t firstEngineSlot{ 0 };
};

namespace util {
	void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currLayout, VkImageLayout newLayout);

	constexpr VkDeviceSize alignUp(VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) & ~(a - 1); }
};