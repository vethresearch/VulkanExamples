#pragma once

#include <stb_image.h>

#include <vulkan/vulkan.h>
#include "VulkanTools.h"
#include "VulkanTexture.h"

#include "CommandLineParser.hpp"

namespace vra::test {

class RenderImage {
public:
    RenderImage(int32_t width, int32_t height, 
                std::string output_filename, 
                bool use_texture);
    ~RenderImage();

    VkInstance instance;
	VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties device_properties;
    VkPhysicalDeviceFeatures device_features;
	VkDevice device;                                // Logical device
	uint32_t queue_family_index;
	VkPipelineCache pipeline_cache;
	VkQueue queue;
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorSet descriptor_set;
    VkDescriptorPool descriptor_pool;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	std::vector<VkShaderModule> shader_modules;
	VkBuffer vertex_buffer, index_buffer;
    size_t index_buffer_count;
	VkDeviceMemory vertex_memory, index_memory;
    bool layers_available;

    VkFormat color_format;
	VkFormat depth_format;

    VkBuffer uniform_buffer_modelview;
    VkDeviceMemory  uniform_buffer_memory;

    struct UniformBufferObject {
        glm::mat4 projection;
		glm::mat4 modelView;
		glm::vec4 viewPos;
		float lodBias = 0.0f;
    };

    UniformBufferObject ubo_scene;

	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory memory;
		VkImageView view;
	};

	int32_t width, height;
    bool use_texture;

	VkFramebuffer framebuffer;
    
	FrameBufferAttachment color_attachment, depth_attachment;
	VkRenderPass render_pass;

    struct SimpleVertex {
        float position[3];
        float color[3];
    };
    struct TextureVertex {
        float position[3];
        float uv[2];
        // float normal[3];
    };
    vks::Texture2D  texture;

	VkDebugReportCallbackEXT debug_report_callback{};

    const char* imagedata;  // Pointer to imagedata buffer

    void createInstance();
    void setupDebugMessenger();
    void createSurface();   // Not necessary for headless
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    
    void prepareSimpleVertexAndIndexBuffers();
    void prepareTextureVertexAndIndexBuffers();

    void createFramebufferAttachments();
    void createRenderPass();
    void prepareGraphicsPipelineSimple();
    void prepareGraphicsPipelineTexture();
    void createCommandBuffer();
    void createCommandBufferTexture();
    void saveFramebufferImage(std::string fname);

    void createSwapChain(); // Not necessary for headless
    void createImageViews();    // Not necessary for headless

    void loadTextureFromFile(std::string fname = "textures/statue.jpg");

    /**
	* Allocate a command buffer from the command pool
	*
	* @param level Level of the new command buffer (primary or secondary)
	* @param pool Command pool from which the command buffer will be allocated
	* @param (Optional) begin If true, recording on the new command buffer will be started (vkBeginCommandBuffer) (Defaults to false)
	*
	* @return A handle to the allocated command buffer
	*/
	VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool, bool begin = false);

    /**
	* Finish command buffer recording and submit it to a queue
	*
	* @param commandBuffer Command buffer to flush
	* @param queue Queue to submit the command buffer to
	* @param pool Command pool on which the command buffer has been created
	* @param free (Optional) Free the command buffer once it has been submitted (Defaults to true)
	*
	* @note The queue that the command buffer is submitted to must be from the same family index as the pool it was allocated from
	* @note Uses a fence to ensure command buffer has finished executing
	*/
	void flushCommandBuffer(VkCommandBuffer command_buffer, VkQueue queue, VkCommandPool pool, bool free = true);

    uint32_t getMemoryTypeIndex(uint32_t type_bits, VkMemoryPropertyFlags properties);

    VkResult createBuffer(VkBufferUsageFlags usage_flags, 
                          VkMemoryPropertyFlags memory_property_flags, 
                          VkBuffer *buffer, 
                          VkDeviceMemory *memory, 
                          VkDeviceSize size, 
                          void *data = nullptr);

    /*
		Submit command buffer to a queue and wait for fence until queue operations have been finished
	*/
	void submitWork(VkCommandBuffer cmd_buffer, VkQueue queue);
};

}