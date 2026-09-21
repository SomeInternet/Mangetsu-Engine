#pragma once
#include <iostream>
#include <deque>
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

namespace util {
	void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currLayout, VkImageLayout newLayout);
};