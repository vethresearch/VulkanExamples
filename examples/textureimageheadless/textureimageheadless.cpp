/*
* Vulkan Example - Minimal headless rendering example
*
* Copyright (C) 2017-2022 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <array>
#include <iostream>
#include <algorithm>
#include <memory>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <vulkan/vulkan.h>
#include "VulkanTools.h"
#include "VulkanTexture.h"
#include "camera.hpp"
#include "CommandLineParser.hpp"

#include "textureimageheadless.h"

#define DEBUG (!NDEBUG)

#define BUFFER_ELEMENTS 32

#define LOG(...) printf(__VA_ARGS__)

using std::unique_ptr;
using std::make_unique;
using vra::test::RenderImage;

CommandLineParser command_line_parser;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(
	VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objectType,
	uint64_t object,
	size_t location,
	int32_t messageCode,
	const char* pLayerPrefix,
	const char* pMessage,
	void* pUserData)
{
	LOG("[VALIDATION]: %s - %s\n", pLayerPrefix, pMessage);
	return VK_FALSE;
}

namespace vra::test {
	
RenderImage::RenderImage(int32_t width_, int32_t height_, 
						 std::string output_filename, 
						 bool use_texture_) :
		width(width_),
		height(height_),
		use_texture(use_texture_) {

	printf("Instantiated RenderImage class. %s texture.\n", use_texture ? "USING" : "NOT USING");

	createInstance();
	setupDebugMessenger();
	pickPhysicalDevice();
	createLogicalDevice();
	createCommandPool();

	if (use_texture) {
		Camera camera;
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -2.5f));
		camera.setRotation(glm::vec3(0.0f, 15.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);

		// Set ubo for texture vertex shader
		ubo_scene.projection = camera.matrices.perspective;
		ubo_scene.modelView = camera.matrices.view;
		ubo_scene.viewPos = camera.viewPos;
		ubo_scene.lodBias = 0.0f;

		loadTextureFromFile("textures/statue.jpg");
		prepareTextureVertexAndIndexBuffers();

	} else {
		prepareSimpleVertexAndIndexBuffers();
	}


	createFramebufferAttachments();
	createRenderPass();
	if (use_texture) {
		prepareGraphicsPipelineTexture();
		createCommandBufferTexture();
	} else {
		prepareGraphicsPipelineSimple();
		createCommandBuffer();
	}
	saveFramebufferImage(output_filename);
}

void RenderImage::createInstance() {
	LOG("Running headless texture rendering example\n");

	VkApplicationInfo app_info = {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "Vulkan headless example";
	app_info.pEngineName = "VulkanExample";
	app_info.apiVersion = VK_API_VERSION_1_0;

	/*
		Vulkan instance creation (without surface extensions)
	*/
	VkInstanceCreateInfo instance_create_info = {};
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pApplicationInfo = &app_info;

	uint32_t layer_count = 1;
	const char* validation_layers[] = { "VK_LAYER_KHRONOS_validation" };

	std::vector<const char*> instance_extensions = {};
#if DEBUG
	// Check if layers are available
	uint32_t instance_layer_count;
	vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
	std::vector<VkLayerProperties> instance_layers(instance_layer_count);
	vkEnumerateInstanceLayerProperties(&instance_layer_count, instance_layers.data());

	layers_available = true;
	for (auto layer_name : validation_layers) {
		printf("Searching for validation layer: %s\n", layer_name);
		bool layer_available = false;
		for (auto instance_layer : instance_layers) {
			printf("Found validation layer: %s\n", instance_layer.layerName);
			if (strcmp(instance_layer.layerName, layer_name) == 0) {
				layer_available = true;
				break;
			}
		}
		if (!layer_available) {
			layers_available = false;
			break;
		}
	}

	if (layers_available) {
		instance_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		instance_create_info.ppEnabledLayerNames = validation_layers;
		instance_create_info.enabledLayerCount = layer_count;
	}
#endif

	instance_create_info.enabledExtensionCount = (uint32_t)instance_extensions.size();
	instance_create_info.ppEnabledExtensionNames = instance_extensions.data();
	VK_CHECK_RESULT(vkCreateInstance(&instance_create_info, nullptr, &instance));
}

void RenderImage::setupDebugMessenger() {
#if DEBUG
	if (layers_available) {
		VkDebugReportCallbackCreateInfoEXT debug_report_create_info = {};
		debug_report_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		debug_report_create_info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
		debug_report_create_info.pfnCallback = (PFN_vkDebugReportCallbackEXT)debugMessageCallback;

		// We have to explicitly load this function.
		PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT = 
				reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, 
																						   "vkCreateDebugReportCallbackEXT"));
		assert(vkCreateDebugReportCallbackEXT);
		VK_CHECK_RESULT(vkCreateDebugReportCallbackEXT(instance, &debug_report_create_info, 
													   nullptr, &debug_report_callback));
	}
#endif
}

void RenderImage::createSurface() {
	// Not required for headless rendering
}

void RenderImage::pickPhysicalDevice() {
	uint32_t device_count = 0;
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &device_count, nullptr));
	std::vector<VkPhysicalDevice> physical_devices(device_count);
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data()));
	physical_device = physical_devices[0];

	vkGetPhysicalDeviceProperties(physical_device, &device_properties);
	LOG("GPU: %s\n", device_properties.deviceName);
	vkGetPhysicalDeviceFeatures(physical_device, &device_features);
}

void RenderImage::createLogicalDevice() {
	// Request graphics queue, then create logical device
	const float default_queue_priority(0.0f);
	VkDeviceQueueCreateInfo queue_create_info = {};
	uint32_t queue_family_count;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
	std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, 
											 &queue_family_count, 
											 queue_family_properties.data());
	for (uint32_t i = 0; i < static_cast<uint32_t>(queue_family_properties.size()); i++) {
		if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			queue_family_index = i;
			queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_create_info.queueFamilyIndex = i;
			queue_create_info.queueCount = 1;
			queue_create_info.pQueuePriorities = &default_queue_priority;
			break;
		}
	}

	// Create logical device
	VkDeviceCreateInfo device_create_info = {};
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	std::vector<const char*> device_extensions = {};

	device_create_info.enabledExtensionCount = (uint32_t)device_extensions.size();
	device_create_info.ppEnabledExtensionNames = device_extensions.data();
	VK_CHECK_RESULT(vkCreateDevice(physical_device, &device_create_info, nullptr, &device));

	// Get a graphics queue
	vkGetDeviceQueue(device, queue_family_index, 0, &queue);
}

void RenderImage::createCommandPool() {
	VkCommandPoolCreateInfo cmd_pool_info = {};
	cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmd_pool_info.queueFamilyIndex = queue_family_index;
	cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VK_CHECK_RESULT(vkCreateCommandPool(device, &cmd_pool_info, nullptr, &command_pool));
}

void RenderImage::createCommandBuffer() {
	VkCommandBuffer command_buffer;
	VkCommandBufferAllocateInfo cmd_buf_allocate_info =
		vks::initializers::commandBufferAllocateInfo(command_pool, 
													 VK_COMMAND_BUFFER_LEVEL_PRIMARY, 
													 1);
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmd_buf_allocate_info, &command_buffer));

	VkCommandBufferBeginInfo cmd_buf_info =
		vks::initializers::commandBufferBeginInfo();

	VK_CHECK_RESULT(vkBeginCommandBuffer(command_buffer, &cmd_buf_info));

	VkClearValue clear_values[2];
	clear_values[0].color = { { 0.0f, 0.0f, 0.2f, 1.0f } };	// Very light blue
	clear_values[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo render_pass_begin_info = {};
	render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_begin_info.renderArea.extent.width = width;
	render_pass_begin_info.renderArea.extent.height = height;
	render_pass_begin_info.clearValueCount = 2;
	render_pass_begin_info.pClearValues = clear_values;
	render_pass_begin_info.renderPass = render_pass;
	render_pass_begin_info.framebuffer = framebuffer;

	vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport = {};
	viewport.height = (float)height;
	viewport.width = (float)width;
	viewport.minDepth = (float)0.0f;
	viewport.maxDepth = (float)1.0f;
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);

	// Update dynamic scissor state
	VkRect2D scissor = {};
	scissor.extent.width = width;
	scissor.extent.height = height;
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Render scene
	VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, offsets);
	vkCmdBindIndexBuffer(command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

	std::vector<glm::vec3> pos = {
		glm::vec3(-1.5f, 0.0f, -4.0f),
		glm::vec3( 0.0f, 0.0f, -2.5f),
		glm::vec3( 1.5f, 0.0f, -4.0f),
	};

	for (auto v : pos) {
		glm::mat4 mvpMatrix = glm::perspective(glm::radians(60.0f), 
											   (float)width / (float)height, 
											   0.1f, 256.0f) * glm::translate(glm::mat4(1.0f), v);
		vkCmdPushConstants(command_buffer, 
						   pipeline_layout, 
						   VK_SHADER_STAGE_VERTEX_BIT, 
						   0, 
						   sizeof(mvpMatrix), 
						   &mvpMatrix);
		vkCmdDrawIndexed(command_buffer, 3, 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(command_buffer);

	VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer));

	submitWork(command_buffer, queue);

	vkDeviceWaitIdle(device);
}

void RenderImage::createCommandBufferTexture() {
	// buildCommandBuffers()
	VkCommandBuffer command_buffer;
	VkCommandBufferAllocateInfo cmd_buf_allocate_info =
		vks::initializers::commandBufferAllocateInfo(command_pool, 
													 VK_COMMAND_BUFFER_LEVEL_PRIMARY, 
													 1);
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmd_buf_allocate_info, &command_buffer));

	VkCommandBufferBeginInfo cmd_buf_info =
		vks::initializers::commandBufferBeginInfo();

	VK_CHECK_RESULT(vkBeginCommandBuffer(command_buffer, &cmd_buf_info));

	VkClearValue clear_values[2];
	clear_values[0].color = { { 0.0f, 0.0f, 0.2f, 1.0f } };	// Very light blue
	clear_values[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo render_pass_begin_info = {};
	render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_begin_info.renderArea.extent.width = width;
	render_pass_begin_info.renderArea.extent.height = height;
	render_pass_begin_info.clearValueCount = 2;
	render_pass_begin_info.pClearValues = clear_values;
	render_pass_begin_info.renderPass = render_pass;
	render_pass_begin_info.framebuffer = framebuffer;

	vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport = {};
	viewport.height = (float)height;
	viewport.width = (float)width;
	viewport.minDepth = (float)0.0f;
	viewport.maxDepth = (float)1.0f;
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);

	// Update dynamic scissor state
	VkRect2D scissor = {};
	scissor.extent.width = width;
	scissor.extent.height = height;
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);
	vkCmdBindDescriptorSets(command_buffer, 
							VK_PIPELINE_BIND_POINT_GRAPHICS, 
							pipeline_layout, 
							0, 
							1, 
							&descriptor_set, 
							0, 
							NULL);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Render scene
	VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, offsets);
	vkCmdBindIndexBuffer(command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(command_buffer, index_buffer_count, 1, 0, 0, 0);
	
	vkCmdEndRenderPass(command_buffer);

	VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer));

	submitWork(command_buffer, queue);

	vkDeviceWaitIdle(device);
}

void RenderImage::saveFramebufferImage(std::string fname) {

	// Create the linear tiled destination image to copy to and to read the memory from
	VkImageCreateInfo img_create_info(vks::initializers::imageCreateInfo());
	img_create_info.imageType = VK_IMAGE_TYPE_2D;
	img_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	img_create_info.extent.width = width;
	img_create_info.extent.height = height;
	img_create_info.extent.depth = 1;
	img_create_info.arrayLayers = 1;
	img_create_info.mipLevels = 1;
	img_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	img_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	img_create_info.tiling = VK_IMAGE_TILING_LINEAR;
	img_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	// Create the image
	VkImage dst_image;
	VK_CHECK_RESULT(vkCreateImage(device, &img_create_info, nullptr, &dst_image));

	// Create memory to back up the image
	VkMemoryRequirements mem_requirements;
	VkMemoryAllocateInfo mem_alloc_info(vks::initializers::memoryAllocateInfo());
	VkDeviceMemory dst_image_memory;
	vkGetImageMemoryRequirements(device, dst_image, &mem_requirements);
	mem_alloc_info.allocationSize = mem_requirements.size;
	// Memory must be host visible to copy from
	mem_alloc_info.memoryTypeIndex = getMemoryTypeIndex(mem_requirements.memoryTypeBits, 
														VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
														VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &mem_alloc_info, nullptr, &dst_image_memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, dst_image, dst_image_memory, 0));

	// Do the actual blit from the offscreen image to our host visible destination image
	VkCommandBufferAllocateInfo cmd_buf_allocate_info = 
			vks::initializers::commandBufferAllocateInfo(command_pool, 
														 VK_COMMAND_BUFFER_LEVEL_PRIMARY, 
														 1);
	VkCommandBuffer copy_cmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmd_buf_allocate_info, &copy_cmd));
	VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(copy_cmd, &cmd_buf_info));

	// Transition destination image to transfer destination layout
	vks::tools::insertImageMemoryBarrier(
		copy_cmd,
		dst_image,
		0,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

	// colorAttachment.image is already in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, and does not need to be transitioned

	VkImageCopy image_copy_region{};
	image_copy_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_copy_region.srcSubresource.layerCount = 1;
	image_copy_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_copy_region.dstSubresource.layerCount = 1;
	image_copy_region.extent.width = width;
	image_copy_region.extent.height = height;
	image_copy_region.extent.depth = 1;

	vkCmdCopyImage(
		copy_cmd,
		color_attachment.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&image_copy_region);

	// Transition destination image to general layout, which is the required layout for mapping the image memory later on
	vks::tools::insertImageMemoryBarrier(
		copy_cmd,
		dst_image,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_MEMORY_READ_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

	VK_CHECK_RESULT(vkEndCommandBuffer(copy_cmd));

	submitWork(copy_cmd, queue);

	// Get layout of the image (including row pitch)
	VkImageSubresource sub_resource{};
	sub_resource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	VkSubresourceLayout sub_resource_layout;
	vkGetImageSubresourceLayout(device, dst_image, &sub_resource, &sub_resource_layout);

	// Map image memory so we can start copying from it
	vkMapMemory(device, dst_image_memory, 0, VK_WHOLE_SIZE, 0, (void**)&imagedata);
	imagedata += sub_resource_layout.offset;	// Point to start of first row of image data

	/*
		Save host visible framebuffer image to disk (ppm format)
	*/
	std::ofstream file(fname, std::ios::out | std::ios::binary);

	// ppm header
	file << "P6\n" << width << "\n" << height << "\n" << 255 << "\n";

	// If source is BGR (destination is always RGB) and we can't use blit (which does automatic conversion), we'll have to manually swizzle color components
	// Check if source is BGR and needs swizzle
	std::vector<VkFormat> formatsBGR = { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM };
	const bool color_swizzle = (std::find(formatsBGR.begin(), 
										  formatsBGR.end(), 
										  VK_FORMAT_R8G8B8A8_UNORM) != formatsBGR.end());

	// ppm binary pixel data
	for (int32_t y = 0; y < height; y++) {
		unsigned int *row = (unsigned int*)imagedata;
		for (int32_t x = 0; x < width; x++) {
			if (color_swizzle) {
				file.write((char*)row + 2, 1);
				file.write((char*)row + 1, 1);
				file.write((char*)row, 1);
			}
			else {
				file.write((char*)row, 3);
			}
			row++;
		}
		imagedata += sub_resource_layout.rowPitch;
	}
	file.close();

	LOG("Framebuffer image saved to %s\n", fname.c_str());

	// Clean up resources
	vkUnmapMemory(device, dst_image_memory);
	vkFreeMemory(device, dst_image_memory, nullptr);
	vkDestroyImage(device, dst_image, nullptr);

	vkQueueWaitIdle(queue);
}

void RenderImage::prepareSimpleVertexAndIndexBuffers() {
	std::vector<SimpleVertex> vertices = {
		{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ {  0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
	};
	std::vector<uint32_t> indices = { 0, 1, 2 };

	const VkDeviceSize vertex_buffer_size = vertices.size() * sizeof(SimpleVertex);
	const VkDeviceSize index_buffer_size = indices.size() * sizeof(uint32_t);

	VkBuffer staging_buffer;
	VkDeviceMemory staging_memory;

	// Command buffer for copy commands (reused)
	VkCommandBufferAllocateInfo cmd_buf_allocate_info = 
			vks::initializers::commandBufferAllocateInfo(command_pool, 
														 VK_COMMAND_BUFFER_LEVEL_PRIMARY, 
														 1);
	VkCommandBuffer copy_cmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmd_buf_allocate_info, &copy_cmd));
	VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();

	// Copy input data to VRAM using a staging buffer
	{
		// Vertices
		createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&staging_buffer,
			&staging_memory,
			vertex_buffer_size,
			vertices.data());

		createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vertex_buffer,
			&vertex_memory,
			vertex_buffer_size);

		VK_CHECK_RESULT(vkBeginCommandBuffer(copy_cmd, &cmd_buf_info));
		VkBufferCopy copy_region = {};
		copy_region.size = vertex_buffer_size;
		vkCmdCopyBuffer(copy_cmd, staging_buffer, vertex_buffer, 1, &copy_region);
		VK_CHECK_RESULT(vkEndCommandBuffer(copy_cmd));

		submitWork(copy_cmd, queue);

		vkDestroyBuffer(device, staging_buffer, nullptr);
		vkFreeMemory(device, staging_memory, nullptr);

		// Indices
		createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&staging_buffer,
			&staging_memory,
			index_buffer_size,
			indices.data());

		createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&index_buffer,
			&index_memory,
			index_buffer_size);

		VK_CHECK_RESULT(vkBeginCommandBuffer(copy_cmd, &cmd_buf_info));
		copy_region.size = index_buffer_size;
		vkCmdCopyBuffer(copy_cmd, staging_buffer, index_buffer, 1, &copy_region);
		VK_CHECK_RESULT(vkEndCommandBuffer(copy_cmd));

		submitWork(copy_cmd, queue);

		vkDestroyBuffer(device, staging_buffer, nullptr);
		vkFreeMemory(device, staging_memory, nullptr);
	}
}

void RenderImage::prepareTextureVertexAndIndexBuffers() {
	std::vector<TextureVertex> vertices = {
		{ {  1.0f,  1.0f, 0.0f }, {1.0f, 1.0f}, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f,  1.0f, 0.0f }, {0.0f, 1.0f}, { 0.0f, 0.0f, 1.0f } },
		{ { -1.0f, -1.0f, 0.0f }, {0.5f, 0.0f}, { 0.0f, 0.0f, 1.0f } }
	};
	std::vector<uint32_t> indices = { 0, 1, 2 };

	const VkDeviceSize vertex_buffer_size = vertices.size() * sizeof(TextureVertex);
	const VkDeviceSize index_buffer_size = indices.size() * sizeof(uint32_t);
	index_buffer_count = indices.size();

	VkBuffer staging_buffer;
	VkDeviceMemory staging_memory;

	// Command buffer for copy commands (reused)
	VkCommandBufferAllocateInfo cmd_buf_allocate_info = 
			vks::initializers::commandBufferAllocateInfo(command_pool, 
														 VK_COMMAND_BUFFER_LEVEL_PRIMARY, 
														 1);
	VkCommandBuffer copy_cmd;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmd_buf_allocate_info, &copy_cmd));
	VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();

	// Copy input data to VRAM using a staging buffer
	{
		// Vertices
		createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&staging_buffer,
			&staging_memory,
			vertex_buffer_size,
			vertices.data());

		createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vertex_buffer,
			&vertex_memory,
			vertex_buffer_size);

		VK_CHECK_RESULT(vkBeginCommandBuffer(copy_cmd, &cmd_buf_info));
		VkBufferCopy copy_region = {};
		copy_region.size = vertex_buffer_size;
		vkCmdCopyBuffer(copy_cmd, staging_buffer, vertex_buffer, 1, &copy_region);
		VK_CHECK_RESULT(vkEndCommandBuffer(copy_cmd));

		submitWork(copy_cmd, queue);

		vkDestroyBuffer(device, staging_buffer, nullptr);
		vkFreeMemory(device, staging_memory, nullptr);

		// Indices
		createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&staging_buffer,
			&staging_memory,
			index_buffer_size,
			indices.data());

		createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&index_buffer,
			&index_memory,
			index_buffer_size);

		VK_CHECK_RESULT(vkBeginCommandBuffer(copy_cmd, &cmd_buf_info));
		copy_region.size = index_buffer_size;
		vkCmdCopyBuffer(copy_cmd, staging_buffer, index_buffer, 1, &copy_region);
		VK_CHECK_RESULT(vkEndCommandBuffer(copy_cmd));

		submitWork(copy_cmd, queue);

		vkDestroyBuffer(device, staging_buffer, nullptr);
		vkFreeMemory(device, staging_memory, nullptr);
	}
}

void RenderImage::createFramebufferAttachments() {
	color_format = VK_FORMAT_R8G8B8A8_UNORM;
	
	// Color attachment
	VkImageCreateInfo image = vks::initializers::imageCreateInfo();
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = color_format;
	image.extent.width = width;
	image.extent.height = height;
	image.extent.depth = 1;
	image.mipLevels = 1;
	image.arrayLayers = 1;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	VkMemoryAllocateInfo mem_alloc = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements mem_reqs;

	VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &color_attachment.image));
	vkGetImageMemoryRequirements(device, color_attachment.image, &mem_reqs);
	mem_alloc.allocationSize = mem_reqs.size;
	mem_alloc.memoryTypeIndex = getMemoryTypeIndex(mem_reqs.memoryTypeBits, 
												   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, 
									 &mem_alloc, 
									 nullptr, 
									 &color_attachment.memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, 
									  color_attachment.image, 
									  color_attachment.memory, 
									  0))    

	VkImageViewCreateInfo color_image_view = vks::initializers::imageViewCreateInfo();
	color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	color_image_view.format = color_format;
	color_image_view.subresourceRange = {};
	color_image_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	color_image_view.subresourceRange.baseMipLevel = 0;
	color_image_view.subresourceRange.levelCount = 1;
	color_image_view.subresourceRange.baseArrayLayer = 0;
	color_image_view.subresourceRange.layerCount = 1;
	color_image_view.image = color_attachment.image;
	VK_CHECK_RESULT(vkCreateImageView(device, 
									  &color_image_view, 
									  nullptr, 
									  &color_attachment.view));

	// Depth stencil attachment
	vks::tools::getSupportedDepthFormat(physical_device, &depth_format);
	image.format = depth_format;
	image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &depth_attachment.image));
	vkGetImageMemoryRequirements(device, depth_attachment.image, &mem_reqs);
	mem_alloc.allocationSize = mem_reqs.size;
	mem_alloc.memoryTypeIndex = getMemoryTypeIndex(mem_reqs.memoryTypeBits, 
													VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &mem_alloc, nullptr, &depth_attachment.memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, 
									  depth_attachment.image, 
									  depth_attachment.memory, 
									  0));

	VkImageViewCreateInfo depth_stencil_view = vks::initializers::imageViewCreateInfo();
	depth_stencil_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depth_stencil_view.format = depth_format;
	depth_stencil_view.flags = 0;
	depth_stencil_view.subresourceRange = {};
	depth_stencil_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	if (depth_format >= VK_FORMAT_D16_UNORM_S8_UINT)
		depth_stencil_view.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	depth_stencil_view.subresourceRange.baseMipLevel = 0;
	depth_stencil_view.subresourceRange.levelCount = 1;
	depth_stencil_view.subresourceRange.baseArrayLayer = 0;
	depth_stencil_view.subresourceRange.layerCount = 1;
	depth_stencil_view.image = depth_attachment.image;
	VK_CHECK_RESULT(vkCreateImageView(device, 
									  &depth_stencil_view, 
									  nullptr, 
									  &depth_attachment.view));
	
}

void RenderImage::createRenderPass() {

	std::array<VkAttachmentDescription, 2> attchment_descriptions = {};
	// Color attachment
	attchment_descriptions[0].format = color_format;
	attchment_descriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attchment_descriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attchment_descriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attchment_descriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attchment_descriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attchment_descriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attchment_descriptions[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	// Depth attachment
	attchment_descriptions[1].format = depth_format;
	attchment_descriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attchment_descriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attchment_descriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attchment_descriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attchment_descriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attchment_descriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attchment_descriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference color_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depth_reference = { 1, 
											 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpass_description = {};
	subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass_description.colorAttachmentCount = 1;
	subpass_description.pColorAttachments = &color_reference;
	subpass_description.pDepthStencilAttachment = &depth_reference;

	// Use subpass dependencies for layout transitions
	std::array<VkSubpassDependency, 2> dependencies;

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Create the actual renderpass
	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = static_cast<uint32_t>(attchment_descriptions.size());
	render_pass_info.pAttachments = attchment_descriptions.data();
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass_description;
	render_pass_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
	render_pass_info.pDependencies = dependencies.data();
	VK_CHECK_RESULT(vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass));

	VkImageView attachments[2];
	attachments[0] = color_attachment.view;
	attachments[1] = depth_attachment.view;

	// Create the framebuffer
	VkFramebufferCreateInfo framebuffer_create_info = 
			vks::initializers::framebufferCreateInfo();
	framebuffer_create_info.renderPass = render_pass;
	framebuffer_create_info.attachmentCount = 2;
	framebuffer_create_info.pAttachments = attachments;
	framebuffer_create_info.width = width;
	framebuffer_create_info.height = height;
	framebuffer_create_info.layers = 1;
	VK_CHECK_RESULT(vkCreateFramebuffer(device, 
										&framebuffer_create_info, 
										nullptr, 
										&framebuffer));
	
}

void RenderImage::createSwapChain() {
	// Not necessary for headless
} 

void RenderImage::createImageViews() {
	// Not necessary for headless
} 

void RenderImage::prepareGraphicsPipelineSimple() {

	std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings = {};
	VkDescriptorSetLayoutCreateInfo descriptor_layout =
			vks::initializers::descriptorSetLayoutCreateInfo(set_layout_bindings);
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, 
												&descriptor_layout, 
												nullptr, 
												&descriptor_set_layout));

	VkPipelineLayoutCreateInfo pipeline_layout_create_info =
			vks::initializers::pipelineLayoutCreateInfo(nullptr, 0);

	// MVP via push constant block
	VkPushConstantRange pushConstantRange = 
			vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 
												 sizeof(glm::mat4), 
												 0);
	pipeline_layout_create_info.pushConstantRangeCount = 1;
	pipeline_layout_create_info.pPushConstantRanges = &pushConstantRange;

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, 
										   &pipeline_layout_create_info, 
										   nullptr, 
										   &pipeline_layout));

	VkPipelineCacheCreateInfo pipeline_cache_create_info = {};
	pipeline_cache_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipeline_cache_create_info, nullptr, &pipeline_cache));

	// Create pipeline
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 
																0, 
																VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterization_state =
		vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, 
															    VK_CULL_MODE_BACK_BIT, 
																VK_FRONT_FACE_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blend_attachment_state =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo color_blend_state =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blend_attachment_state);

	VkPipelineDepthStencilStateCreateInfo depth_stencil_state =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, 
															   VK_TRUE, 
															   VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipelineViewportStateCreateInfo viewport_state =
		vks::initializers::pipelineViewportStateCreateInfo(1, 1);

	VkPipelineMultisampleStateCreateInfo multisample_state =
		vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

	std::vector<VkDynamicState> dynamic_state_enables = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamic_state =
		vks::initializers::pipelineDynamicStateCreateInfo(dynamic_state_enables);

	VkGraphicsPipelineCreateInfo pipeline_create_info =
		vks::initializers::pipelineCreateInfo(pipeline_layout, render_pass);

	std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{};

	pipeline_create_info.pInputAssemblyState = &input_assembly_state;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState = &color_blend_state;
	pipeline_create_info.pMultisampleState = &multisample_state;
	pipeline_create_info.pViewportState = &viewport_state;
	pipeline_create_info.pDepthStencilState = &depth_stencil_state;
	pipeline_create_info.pDynamicState = &dynamic_state;
	pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
	pipeline_create_info.pStages = shader_stages.data();

	// Vertex bindings an attributes
	// Binding description
	std::vector<VkVertexInputBindingDescription> vertex_input_bindings = {
			vks::initializers::vertexInputBindingDescription(0, 
															 sizeof(SimpleVertex), 
															 VK_VERTEX_INPUT_RATE_VERTEX),
	};

	// Attribute descriptions
	std::vector<VkVertexInputAttributeDescription> vertex_input_attributes = {
		vks::initializers::vertexInputAttributeDescription(0, 
														   0, 
														   VK_FORMAT_R32G32B32_SFLOAT, 
														   0),					// Position
		vks::initializers::vertexInputAttributeDescription(0, 
														   1, 
														   VK_FORMAT_R32G32B32_SFLOAT, 
														   sizeof(float) * 3),	// Color
	};

	VkPipelineVertexInputStateCreateInfo vertex_input_state = 
			vks::initializers::pipelineVertexInputStateCreateInfo();
	vertex_input_state.vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state.pVertexBindingDescriptions = vertex_input_bindings.data();
	vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state.pVertexAttributeDescriptions = vertex_input_attributes.data();

	pipeline_create_info.pVertexInputState = &vertex_input_state;

	std::string shader_dir = "glsl";
	if (command_line_parser.isSet("shaders")) {
		shader_dir = command_line_parser.getValueAsString("shaders", "glsl");
	}
	const std::string shaders_path = getShaderBasePath() + shader_dir + "/renderheadless/";

	shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stages[0].pName = "main";
	shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_stages[1].pName = "main";
	shader_stages[0].module = vks::tools::loadShader((shaders_path + "triangle.vert.spv").c_str(), device);
	shader_stages[1].module = vks::tools::loadShader((shaders_path + "triangle.frag.spv").c_str(), device);
	shader_modules = { shader_stages[0].module, shader_stages[1].module };
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipeline_cache, 
											  1, &pipeline_create_info, 
											  nullptr, &pipeline));
}

void RenderImage::prepareGraphicsPipelineTexture() {

	// prepareUniformBuffers()
	// Create uniform buffer objects to support vertex shader and fragment shader bindings
	VkDeviceSize ubo_size = sizeof(UniformBufferObject);
	VK_CHECK_RESULT(createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
								 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
								 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
								 &uniform_buffer_modelview,
								 &uniform_buffer_memory,
								 sizeof(UniformBufferObject),
								 &uniform_buffer_modelview));


	// setupDescriptorSetLayout()
	// Add uniform buffer objects to bindings
	std::vector<VkDescriptorSetLayoutBinding> set_layout_bindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0),
			// Binding 1 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1)
		};

	VkDescriptorSetLayoutCreateInfo descriptor_layout =
		vks::initializers::descriptorSetLayoutCreateInfo(
			set_layout_bindings.data(),
			static_cast<uint32_t>(set_layout_bindings.size()));

	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, 
												&descriptor_layout, 
												nullptr, 
												&descriptor_set_layout));

	VkPipelineLayoutCreateInfo pipeline_layout_create_info =
		vks::initializers::pipelineLayoutCreateInfo(
			&descriptor_set_layout,
			1);

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, 
										   &pipeline_layout_create_info, 
										   nullptr, 
										   &pipeline_layout));



	// preparePipelines()
	VkPipelineCacheCreateInfo pipeline_cache_create_info = {};
	pipeline_cache_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipeline_cache_create_info, nullptr, &pipeline_cache));





	// Create pipeline (from preparePipelines())
	VkPipelineInputAssemblyStateCreateInfo input_assembly_state =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 
																0, 
																VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterization_state =
		vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, 
															    VK_CULL_MODE_NONE, 
																VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineColorBlendAttachmentState blend_attachment_state =
		vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

	VkPipelineColorBlendStateCreateInfo color_blend_state =
		vks::initializers::pipelineColorBlendStateCreateInfo(1, &blend_attachment_state);

	VkPipelineDepthStencilStateCreateInfo depth_stencil_state =
		vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, 
															   VK_TRUE, 
															   VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipelineViewportStateCreateInfo viewport_state =
		vks::initializers::pipelineViewportStateCreateInfo(1, 1);

	VkPipelineMultisampleStateCreateInfo multisample_state =
		vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

	std::vector<VkDynamicState> dynamic_state_enables = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamic_state =
		vks::initializers::pipelineDynamicStateCreateInfo(dynamic_state_enables);




	// Load shaders
	std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{};

	std::string shader_dir = "glsl";
	if (command_line_parser.isSet("shaders")) {
		shader_dir = command_line_parser.getValueAsString("shaders", "glsl");
	}
	const std::string shaders_path = getShaderBasePath() + shader_dir + "/texture/";

	shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_stages[0].pName = "main";
	shader_stages[0].module = vks::tools::loadShader((shaders_path + "texture_basic.vert.spv").c_str(), device);

	shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shader_stages[1].pName = "main";
	shader_stages[1].module = vks::tools::loadShader((shaders_path + "texture_basic.frag.spv").c_str(), device);
	
	shader_modules = { shader_stages[0].module, shader_stages[1].module };






	// Create the pipeline
	VkGraphicsPipelineCreateInfo pipeline_create_info =
		vks::initializers::pipelineCreateInfo(pipeline_layout, render_pass);

	pipeline_create_info.pInputAssemblyState = &input_assembly_state;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState = &color_blend_state;
	pipeline_create_info.pMultisampleState = &multisample_state;
	pipeline_create_info.pViewportState = &viewport_state;
	pipeline_create_info.pDepthStencilState = &depth_stencil_state;
	pipeline_create_info.pDynamicState = &dynamic_state;
	pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
	pipeline_create_info.pStages = shader_stages.data();









	// setupVertexDescriptions()
	// Vertex bindings and attributes

	// Binding description - all of the vertex information is combined in one
	// array so only one binding is required.
	std::vector<VkVertexInputBindingDescription> vertex_input_bindings = {
			vks::initializers::vertexInputBindingDescription(0, 
															 sizeof(TextureVertex), 
															 VK_VERTEX_INPUT_RATE_VERTEX),
	};

	// Attribute descriptions
	// The "texture.vert" vertex shader has three attributes: 
	// 		vertex position
	// 		texture uv coord
	//		vertex normal
	std::vector<VkVertexInputAttributeDescription> vertex_input_attributes = {
		vks::initializers::vertexInputAttributeDescription(0, 
														   0, 
														   VK_FORMAT_R32G32B32_SFLOAT, 
														   0),					// Position
		vks::initializers::vertexInputAttributeDescription(0, 
														   1, 
														   VK_FORMAT_R32G32_SFLOAT, 
														   sizeof(float) * 3),	// Texture coord
		vks::initializers::vertexInputAttributeDescription(0, 
														   2, 
														   VK_FORMAT_R32G32B32_SFLOAT, 
														   sizeof(float) * 5),	// Normal
	};


	VkPipelineVertexInputStateCreateInfo vertex_input_state = 
			vks::initializers::pipelineVertexInputStateCreateInfo();
	vertex_input_state.vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state.pVertexBindingDescriptions = vertex_input_bindings.data();
	vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state.pVertexAttributeDescriptions = vertex_input_attributes.data();

	pipeline_create_info.pVertexInputState = &vertex_input_state;





	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipeline_cache, 
											  1, &pipeline_create_info, 
											  nullptr, &pipeline));

	// setupDescriptorPool()
 
	// Use one ubo and one image sampler
	std::vector<VkDescriptorPoolSize> pool_sizes =
	{
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 
											  1),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
											  1)
	};

	VkDescriptorPoolCreateInfo descriptor_pool_info =
		vks::initializers::descriptorPoolCreateInfo(
			static_cast<uint32_t>(pool_sizes.size()),
			pool_sizes.data(),
			2);

	VK_CHECK_RESULT(vkCreateDescriptorPool(device, 
											&descriptor_pool_info, 
											nullptr, 
											&descriptor_pool));



	// setupDescriptorSet()
	VkDescriptorSetAllocateInfo alloc_info =
		vks::initializers::descriptorSetAllocateInfo(
			descriptor_pool,
			&descriptor_set_layout,
			1);

	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &alloc_info, &descriptor_set));

	VkDescriptorBufferInfo ubo_descriptor{};
	ubo_descriptor.buffer = uniform_buffer_modelview;
	ubo_descriptor.offset = 0;
	ubo_descriptor.range = sizeof(UniformBufferObject);

	// Setup a descriptor image info for the current texture to be used 
	// as a combined image sampler
	VkDescriptorImageInfo texture_descriptor;
	texture_descriptor.imageView = texture.view;			// The image's view (images are never directly accessed by the shader, but rather through views defining subresources)
	texture_descriptor.sampler = texture.sampler;			// The sampler (Telling the pipeline how to sample the texture, including repeat, border, etc.)
	texture_descriptor.imageLayout = texture.imageLayout;	// The current layout of the image (Note: Should always fit the actual use, e.g. shader read)

	std::vector<VkWriteDescriptorSet> writeDescriptorSets =
	{
		// Binding 0 : Vertex shader uniform buffer
		vks::initializers::writeDescriptorSet(
			descriptor_set,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			0,
			&ubo_descriptor),

		// Binding 1 : Fragment shader texture sampler
		//	Fragment shader: layout (binding = 1) uniform sampler2D samplerColor;
		vks::initializers::writeDescriptorSet(
			descriptor_set,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,		// The descriptor set will use a combined image sampler (sampler and image could be split)
			1,												// Shader binding point 1
			&texture_descriptor)							// Pointer to the descriptor image for our texture
	};

	vkUpdateDescriptorSets(device, 
						   static_cast<uint32_t>(writeDescriptorSets.size()), 
						   writeDescriptorSets.data(), 
						   0, 
						   NULL);





}

void RenderImage::loadTextureFromFile(std::string fname) {
	std::string texture_filename = getAssetPath() + fname;

	int tex_width, tex_height, tex_channels;
	stbi_uc* pixels = stbi_load(texture_filename.c_str(), 
								&tex_width, 
								&tex_height, 
								&tex_channels, 
								STBI_rgb_alpha);
	VkDeviceSize image_size = tex_width * tex_height * 4;

	if (!pixels) {
		throw std::runtime_error("failed to load texture image!");
	}

	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;		// Use RGBA since we forced this via STBI_rgb_alpha in stbi_load()

	texture.width = tex_width;
	texture.height = tex_height;
	texture.mipLevels = 1;

	// We prefer using staging to copy the texture data to a device local optimal image
	VkBool32 use_staging = true;

	// Only use linear tiling if forced
	bool force_linear_tiling = false;
	if (force_linear_tiling) {
		// Don't use linear if format is not supported for (linear) shader sampling
		// Get device properties for the requested texture format
		VkFormatProperties format_properties;
		vkGetPhysicalDeviceFormatProperties(physical_device, format, &format_properties);
		use_staging = !(format_properties.linearTilingFeatures & 
						VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
	}

	VkMemoryAllocateInfo mem_alloc_info = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements mem_reqs = {};

	if (use_staging) {
		std::cout << "Using staging.\n";

		// Copy data to an optimal tiled image
		// This loads the texture data into a host local buffer that is copied to the optimal tiled image on the device

		// Create a host-visible staging buffer that contains the raw image data
		// This buffer will be the data source for copying texture data to the optimal tiled image on the device
		VkBuffer staging_buffer;
		VkDeviceMemory staging_memory;

		VkDeviceSize image_size = tex_width * tex_height * 4;

		VkBufferCreateInfo buffer_create_info = vks::initializers::bufferCreateInfo();
		buffer_create_info.size = image_size;

		// This buffer is used as a transfer source for the buffer copy
		buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(device, 
									   &buffer_create_info, 
									   nullptr, 
									   &staging_buffer));

		// Get memory requirements for the staging buffer (alignment, memory type bits)
		vkGetBufferMemoryRequirements(device, staging_buffer, &mem_reqs);
		mem_alloc_info.allocationSize = mem_reqs.size;

		// Get memory type index for a host visible buffer
		// memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, 
		//                                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
		//                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);   // Replace with following
		
		// BEGIN REPLACE
		mem_alloc_info.memoryTypeIndex = getMemoryTypeIndex(mem_reqs.memoryTypeBits, 
															VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
															VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		// END REPLACE
		
		VK_CHECK_RESULT(vkAllocateMemory(device, &mem_alloc_info, nullptr, &staging_memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, staging_buffer, staging_memory, 0));

		// Copy texture data into host local staging buffer
		uint8_t *data;
		VK_CHECK_RESULT(vkMapMemory(device, 
									staging_memory, 
									0, 
									mem_reqs.size, 
									0, 
									(void **)&data));
		memcpy(data, pixels, static_cast<size_t>(image_size));
		vkUnmapMemory(device, staging_memory);

		// Setup buffer copy regions for each mip level
		std::vector<VkBufferImageCopy> buffer_copy_regions;
		uint32_t offset = 0;

		// Setup a buffer image copy structure for the current mip level
		VkBufferImageCopy buffer_copy_region = {};
		buffer_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		buffer_copy_region.imageSubresource.mipLevel = 0;
		buffer_copy_region.imageSubresource.baseArrayLayer = 0;
		buffer_copy_region.imageSubresource.layerCount = 1;
		buffer_copy_region.imageExtent.width = tex_width;
		buffer_copy_region.imageExtent.height = tex_height;
		buffer_copy_region.imageExtent.depth = 1;
		buffer_copy_region.bufferOffset = offset;
		buffer_copy_regions.push_back(buffer_copy_region);
		

		// Create optimal tiled target image on the device
		VkImageCreateInfo image_create_info = vks::initializers::imageCreateInfo();
		image_create_info.imageType = VK_IMAGE_TYPE_2D;
		image_create_info.format = format;
		image_create_info.mipLevels = texture.mipLevels;
		image_create_info.arrayLayers = 1;
		image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		// Set initial layout of the image to undefined
		image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_create_info.extent.width = texture.width;
		image_create_info.extent.height = texture.height;
		image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
								  VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device, &image_create_info, nullptr, &texture.image));

		vkGetImageMemoryRequirements(device, texture.image, &mem_reqs);
		mem_alloc_info.allocationSize = mem_reqs.size;
		// memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		// BEGIN REPLACE
		mem_alloc_info.memoryTypeIndex = getMemoryTypeIndex(mem_reqs.memoryTypeBits, 
															VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		// END REPLACE

		VK_CHECK_RESULT(vkAllocateMemory(device, 
										 &mem_alloc_info, 
										 nullptr, 
										 &texture.deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device, texture.image, texture.deviceMemory, 0));

		// VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkCommandBuffer copy_cmd = createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, 
													   command_pool, 
													   true);

		// Image memory barriers for the texture image

		// The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
		VkImageSubresourceRange subresource_range = {};
		// Image only contains color data
		subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		// Start at first mip level
		subresource_range.baseMipLevel = 0;
		// We will transition on all mip levels
		subresource_range.levelCount = texture.mipLevels;
		// The 2D texture only has one layer
		subresource_range.layerCount = 1;

		// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
		VkImageMemoryBarrier image_memory_barrier = vks::initializers::imageMemoryBarrier();;
		image_memory_barrier.image = texture.image;
		image_memory_barrier.subresourceRange = subresource_range;
		image_memory_barrier.srcAccessMask = 0;
		image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
		// Source pipeline stage is host write/read execution (VK_PIPELINE_STAGE_HOST_BIT)
		// Destination pipeline stage is copy command execution (VK_PIPELINE_STAGE_TRANSFER_BIT)
		vkCmdPipelineBarrier(
			copy_cmd,
			VK_PIPELINE_STAGE_HOST_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &image_memory_barrier);

		// Copy mip levels from staging buffer
		vkCmdCopyBufferToImage(
			copy_cmd,
			staging_buffer,
			texture.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>(buffer_copy_regions.size()),
			buffer_copy_regions.data());

		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
		// Source pipeline stage is copy command execution (VK_PIPELINE_STAGE_TRANSFER_BIT)
		// Destination pipeline stage fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
		vkCmdPipelineBarrier(
			copy_cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &image_memory_barrier);

		// Store current layout for later reuse
		texture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// vulkanDevice->flushCommandBuffer(copyCmd, queue, true);
		flushCommandBuffer(copy_cmd, queue, command_pool, true);

		// Clean up staging resources
		vkFreeMemory(device, staging_memory, nullptr);
		vkDestroyBuffer(device, staging_buffer, nullptr);

	} else {
		std::cout << "Not using staging.\n";
	}

	stbi_image_free(pixels);	// Destroy raw STBI pixel buffer

	// Create a texture sampler
	// In Vulkan textures are accessed by samplers
	// This separates all the sampling information from the texture data. This 
	// means you could have multiple sampler 
	// objects for the same texture with different settings
	// Note: Similar to the samplers available with OpenGL 3.3
	VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.mipLodBias = 0.0f;
	sampler.compareOp = VK_COMPARE_OP_NEVER;
	sampler.minLod = 0.0f;
	// Set max level-of-detail to mip level count of the texture
	sampler.maxLod = (use_staging) ? (float)texture.mipLevels : 0.0f;
	// Enable anisotropic filtering
	// This feature is optional, so we must check if it's supported on the device
	if (device_features.samplerAnisotropy) {
		// Use max. level of anisotropy for this example
		sampler.maxAnisotropy = device_properties.limits.maxSamplerAnisotropy;
		sampler.anisotropyEnable = VK_TRUE;
	} else {
		// The device does not support anisotropic filtering
		sampler.maxAnisotropy = 1.0;
		sampler.anisotropyEnable = VK_FALSE;
	}
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &texture.sampler));

	// Create image view
	// Textures are not directly accessed by the shaders and
	// are abstracted by image views containing additional
	// information and sub resource ranges
	VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;

	// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
	// It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount = 1;
	// Linear tiling usually won't support mip maps
	// Only set mip map count if optimal tiling is used
	view.subresourceRange.levelCount = (use_staging) ? texture.mipLevels : 1;
	// The view will be based on the texture's image
	view.image = texture.image;
	VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &texture.view));

	printf("Created texture.  [%d x %d x %d]\n", texture.width, 
												 texture.height, 
												 texture.mipLevels);

}

RenderImage::~RenderImage()	{
	vkDestroyBuffer(device, vertex_buffer, nullptr);
	vkFreeMemory(device, vertex_memory, nullptr);
	vkDestroyBuffer(device, index_buffer, nullptr);
	vkFreeMemory(device, index_memory, nullptr);
	vkDestroyImageView(device, color_attachment.view, nullptr);
	vkDestroyImage(device, color_attachment.image, nullptr);
	vkFreeMemory(device, color_attachment.memory, nullptr);
	vkDestroyImageView(device, depth_attachment.view, nullptr);
	vkDestroyImage(device, depth_attachment.image, nullptr);
	vkFreeMemory(device, depth_attachment.memory, nullptr);
	vkDestroyRenderPass(device, render_pass, nullptr);
	vkDestroyFramebuffer(device, framebuffer, nullptr);
	vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyPipelineCache(device, pipeline_cache, nullptr);
	vkDestroyCommandPool(device, command_pool, nullptr);
	for (auto shadermodule : shader_modules) {
		vkDestroyShaderModule(device, shadermodule, nullptr);
	}
	vkDestroyDevice(device, nullptr);
#if DEBUG
	if (debug_report_callback) {
		PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
		assert(vkDestroyDebugReportCallback);
		vkDestroyDebugReportCallback(instance, debug_report_callback, nullptr);
	}
#endif
	vkDestroyInstance(instance, nullptr);
}

VkCommandBuffer RenderImage::createCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool, bool begin) {
	VkCommandBufferAllocateInfo cmd_buf_allocate_info = vks::initializers::commandBufferAllocateInfo(pool, level, 1);
	VkCommandBuffer cmd_buffer;
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmd_buf_allocate_info, &cmd_buffer));
	// If requested, also start recording for the new command buffer
	if (begin)
	{
		VkCommandBufferBeginInfo cmd_buf_info = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmd_buffer, &cmd_buf_info));
	}
	return cmd_buffer;
}

void RenderImage::flushCommandBuffer(VkCommandBuffer command_buffer, VkQueue queue, 
									 VkCommandPool pool, bool free) {
	if (command_buffer == VK_NULL_HANDLE)
	{
		return;
	}

	VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer));

	VkSubmitInfo submit_info = vks::initializers::submitInfo();
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	// Create fence to ensure that the command buffer has finished executing
	VkFenceCreateInfo fence_info = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fence_info, nullptr, &fence));
	// Submit to the queue
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, fence));
	// Wait for the fence to signal that command buffer has finished executing
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
	vkDestroyFence(device, fence, nullptr);
	if (free)
	{
		vkFreeCommandBuffers(device, pool, 1, &command_buffer);
	}
}

uint32_t RenderImage::getMemoryTypeIndex(uint32_t type_bits, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties device_memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &device_memory_properties);
	for (uint32_t i = 0; i < device_memory_properties.memoryTypeCount; i++) {
		if ((type_bits & 1) == 1) {
			if ((device_memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
				return i;
			}
		}
		type_bits >>= 1;
	}
	return 0;
}

VkResult RenderImage::createBuffer(VkBufferUsageFlags usage_flags, 
								   VkMemoryPropertyFlags memory_property_flags, 
								   VkBuffer *buffer, 
								   VkDeviceMemory *memory, 
								   VkDeviceSize size, 
								   void *data) {
	// Create the buffer handle
	VkBufferCreateInfo buffer_create_info = vks::initializers::bufferCreateInfo(usage_flags, size);
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK_RESULT(vkCreateBuffer(device, &buffer_create_info, nullptr, buffer));

	// Create the memory backing up the buffer handle
	VkMemoryRequirements mem_reqs;
	VkMemoryAllocateInfo mem_alloc = vks::initializers::memoryAllocateInfo();
	vkGetBufferMemoryRequirements(device, *buffer, &mem_reqs);
	mem_alloc.allocationSize = mem_reqs.size;
	mem_alloc.memoryTypeIndex = getMemoryTypeIndex(mem_reqs.memoryTypeBits, memory_property_flags);
	VK_CHECK_RESULT(vkAllocateMemory(device, &mem_alloc, nullptr, memory));

	if (data != nullptr) {
		void *mapped;
		VK_CHECK_RESULT(vkMapMemory(device, *memory, 0, size, 0, &mapped));
		memcpy(mapped, data, size);
		vkUnmapMemory(device, *memory);
	}

	VK_CHECK_RESULT(vkBindBufferMemory(device, *buffer, *memory, 0));

	return VK_SUCCESS;
}

/*
	Submit command buffer to a queue and wait for fence until queue operations have been finished
*/
void RenderImage::submitWork(VkCommandBuffer cmd_buffer, VkQueue queue) {
	VkSubmitInfo submit_info = vks::initializers::submitInfo();
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd_buffer;
	VkFenceCreateInfo fence_info = vks::initializers::fenceCreateInfo();
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fence_info, nullptr, &fence));
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submit_info, fence));
	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	vkDestroyFence(device, fence, nullptr);
}

}

int main(int argc, char* argv[]) {
	command_line_parser.add("help", { "--help" }, 0, "Show help");
	command_line_parser.add("shaders", { "-s", "--shaders" }, 1, "Select shader type to use (glsl or hlsl)");
	command_line_parser.add("use_vertex", { "-v", "--use_vertex" }, 0, "Select to use vertex rendering.");
	command_line_parser.parse(argc, argv);
	if (command_line_parser.isSet("help")) {
		command_line_parser.printHelp();
		std::cin.get();
		return 0;
	}	

	unique_ptr<RenderImage> render_tool = make_unique<RenderImage>(640, 512, 
																   "headless.png", 
																   !command_line_parser.isSet("use_vertex"));

	std::cout << "Finished.  Have a great day ...\n";
	return 0;
}
