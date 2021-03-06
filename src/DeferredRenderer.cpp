#include "DeferredRenderer.h"
#include "Instance.h"
#include "ShaderModule.h"
#include "Vertex.h"
#include "Blades.h"
#include "Camera.h"
#include "Image.h"
#include "BufferUtils.h"

#define PRINT_NUM_BLADES 0

#define LOTSA_NEWLINES "\n\n\n\n\n\n*** "

static constexpr unsigned int WORKGROUP_SIZE = 32;

DeferredRenderer::DeferredRenderer(Device* device, SwapChain* swapChain, Scene* scene, Camera* camera)
    : device(device),
    logicalDevice(device->GetVkDevice()),
    swapChain(swapChain),
    scene(scene),
    camera(camera) {


    CreateCommandPools();
    CreateRenderPass();
    // TODO: call CreateDeferred*() / RecordDeferred*() functions here DTODO
    CreateDeferredRenderPass();
    CreateCameraDescriptorSetLayout();
    CreateModelDescriptorSetLayout();
    CreateTimeDescriptorSetLayout();
    CreateComputeDescriptorSetLayout();
    CreateDescriptorPool();
    CreateCameraDescriptorSet();
    CreateModelDescriptorSets();
    CreateGrassDescriptorSets();
    CreateTimeDescriptorSet();
    CreateComputeDescriptorSets();
    CreateFrameResources();
    CreateGraphicsPipeline();
    CreateGrassPipeline();
    CreateComputePipeline();
    RecordCommandBuffers();
    RecordDeferredCommandBuffer();
    RecordComputeCommandBuffer();
}

void DeferredRenderer::CreateCommandPools() {
    VkCommandPoolCreateInfo graphicsPoolInfo = {};
    graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    graphicsPoolInfo.queueFamilyIndex = device->GetInstance()->GetQueueFamilyIndices()[QueueFlags::Graphics];
    graphicsPoolInfo.flags = 0;

    if (vkCreateCommandPool(logicalDevice, &graphicsPoolInfo, nullptr, &graphicsCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    VkCommandPoolCreateInfo computePoolInfo = {};
    computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    computePoolInfo.queueFamilyIndex = device->GetInstance()->GetQueueFamilyIndices()[QueueFlags::Compute];
    computePoolInfo.flags = 0;

    if (vkCreateCommandPool(logicalDevice, &computePoolInfo, nullptr, &computeCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
}

void DeferredRenderer::CreateRenderPass() {
    // Color buffer attachment represented by one of the images from the swap chain
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapChain->GetVkImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Create a color attachment reference to be used with subpass
    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Depth buffer attachment
    VkFormat depthFormat = device->GetInstance()->GetSupportedFormat({ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Create a depth attachment reference
    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Create subpass description
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    // Specify subpass dependency
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Create render pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
}

void DeferredRenderer::CreateDeferredRenderPass() {
    // Color buffer attachment represented by one of the images from the swap chain
    VkAttachmentDescription albedoAttachment = {};
    albedoAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    albedoAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    albedoAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    albedoAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    albedoAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    albedoAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    albedoAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    albedoAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // newly added

                                                                             // Create a albedo attachment reference to be used with subpass
    VkAttachmentReference albedoAttachmentRef = {}; // newly added
    albedoAttachmentRef.attachment = 0;
    albedoAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // newly added
    // add position and normal attachments to the render pass (similar to color attachment)
    VkAttachmentDescription positionAttachment = {};
    positionAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    positionAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    positionAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    positionAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    positionAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    positionAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    positionAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    positionAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // newly added

                                                                               // Create a position attachment reference to be used with subpass
    VkAttachmentReference positionAttachmentRef = {};
    positionAttachmentRef.attachment = 1;
    positionAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription normalAttachment = {};
    normalAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    normalAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    normalAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    normalAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    normalAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    normalAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    normalAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    normalAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // newly added

                                                                             // Create a normal attachment reference to be used with subpass
    VkAttachmentReference normalAttachmentRef = {};
    normalAttachmentRef.attachment = 2;
    normalAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Hopefully imageView are created below for all the images we are attaching
    // VkImageCreateInfo ??
    // ----------------

    // Depth buffer attachment
    VkFormat depthFormat = device->GetInstance()->GetSupportedFormat({ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::vector<VkAttachmentReference> colorDeferredReferences;
    colorDeferredReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
    colorDeferredReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
    colorDeferredReferences.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

    // Create a depth attachment reference
    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 3;// 1; newly added
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Create subpass description
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorDeferredReferences.size()); // newly added
    subpass.pColorAttachments = colorDeferredReferences.data();							 // " "
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkAttachmentDescription, 4> attachments = { albedoAttachment, positionAttachment, normalAttachment, depthAttachment, };

    // newly added 
    // Use subpass dependencies for attachment layput transitions
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
    // --------------

    /*
    // Specify subpass dependency
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    */

    // Create render pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2; // 1; newly added
    renderPassInfo.pDependencies = dependencies.data(); // &dependency; newly added

    if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &deferredRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DEFERRED render pass");
    }

    // Create image, image views
    // Create albedo image
    Image::Create(
        device,
        swapChain->GetVkExtent().width,
        swapChain->GetVkExtent().height,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        deferredAlbedoImage,
        deferredAlbedoImageMemory);

    // Create albedo image view
    deferredAlbedoImageView = Image::CreateView(device, deferredAlbedoImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Create position image
    Image::Create(
        device,
        swapChain->GetVkExtent().width,
        swapChain->GetVkExtent().height,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        deferredPositionImage,
        deferredPositionImageMemory);

    // Create position image view
    deferredPositionImageView = Image::CreateView(device, deferredPositionImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Create normal image
    Image::Create(
        device,
        swapChain->GetVkExtent().width,
        swapChain->GetVkExtent().height,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        deferredNormalImage,
        deferredNormalImageMemory);

    // Create normal image view
    deferredNormalImageView = Image::CreateView(device, deferredNormalImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // CREATE DEPTH IMAGE (deferred depth)
    Image::Create(device,
        swapChain->GetVkExtent().width,
        swapChain->GetVkExtent().height,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        deferredDepthImage,
        deferredDepthImageMemory
    );

    // DTODO: may not need 2nd bit at end
    deferredDepthImageView = Image::CreateView(device, deferredDepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    std::array<VkImageView, 4> imageViews;
    imageViews[0] = deferredAlbedoImageView;
    imageViews[1] = deferredPositionImageView;
    imageViews[2] = deferredNormalImageView;
    imageViews[3] = deferredDepthImageView;

    // Create deferred framebuffers

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.pNext = NULL;
    framebufferInfo.renderPass = deferredRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(imageViews.size());
    framebufferInfo.pAttachments = imageViews.data();
    framebufferInfo.width = swapChain->GetVkExtent().width;
    framebufferInfo.height = swapChain->GetVkExtent().height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &deferredFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DEFERRED framebuffer");
    }

    // Create sampler for deferred buffers
    VkSamplerCreateInfo sampler = {};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = sampler.addressModeU;
    sampler.addressModeW = sampler.addressModeU;
    sampler.mipLodBias = 0.0f;
    sampler.maxAnisotropy = 1.0f;
    sampler.minLod = 0.0f;
    sampler.maxLod = 1.0f;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    if (vkCreateSampler(logicalDevice, &sampler, nullptr, &deferredSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create DEFERRED sampler");
    }
}

void DeferredRenderer::RecordDeferredCommandBuffer() {
    // Specify the command pool and number of buffers to allocate
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = graphicsCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(1);

    if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &deferredCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DEFERRED command buffers");
    }

    // Create a semaphore used to synchronize offscreen rendering and usage
    VkSemaphoreCreateInfo semaphoreCreateInfo = {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(logicalDevice, &semaphoreCreateInfo, nullptr, &deferredSemaphore) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate DEFERRED semaphore");
    }

    // Set up command buffer begin
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    // Clear values for all attachments written in the fragment sahder
    std::array<VkClearValue, 4> clearValues;
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    clearValues[3].depthStencil = { 1.0f, 0 };

    // Set up render pass begin
    // This will clear values in G-buffer
    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = deferredRenderPass;
    renderPassBeginInfo.framebuffer = deferredFramebuffer;
    renderPassBeginInfo.renderArea.extent.width = swapChain->GetVkExtent().width;
    renderPassBeginInfo.renderArea.extent.height = swapChain->GetVkExtent().height;
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassBeginInfo.pClearValues = clearValues.data();

    // ~ Start recording ~
    if (vkBeginCommandBuffer(deferredCommandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording DEFERRED command buffer");
    }

    // TODO: change pipeline layout? DTODO
    // Bind the camera descriptor set. This is set 0 in all pipelines so it will be inherited
    vkCmdBindDescriptorSets(deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, grassPipelineLayout, 0, 1, &cameraDescriptorSet, 0, nullptr);

    vkCmdBeginRenderPass(deferredCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind the deferred pipeline
    vkCmdBindPipeline(deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, grassPipeline);

    for (uint32_t j = 0; j < scene->GetBlades().size(); ++j) {
        VkBuffer vertexBuffers[] = { scene->GetBlades()[j]->GetCulledBladesBuffer() };//{ scene->GetBlades()[j]->GetCulledBladesBuffer() };
        VkDeviceSize offsets[] = { 0 };
        // TODO: Uncomment this when the buffers are populated
        vkCmdBindVertexBuffers(deferredCommandBuffer, 0, 1, vertexBuffers, offsets);

        // TODO: Bind the descriptor set for each grass blades model
        vkCmdBindDescriptorSets(deferredCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, grassPipelineLayout, 1, 1, &grassDescriptorSets[j], 0, nullptr);

        // Draw
        // TODO: Uncomment this when the buffers are populated
        // CHECKITOUT: it's getNumBladesBuffer that specifies how many threads are spawned
        // see: https://www.khronos.org/registry/vulkan/specs/1.0/man/html/vkCmdDrawIndirect.html
        // see: https://www.khronos.org/registry/vulkan/specs/1.0/man/html/VkDrawIndirectCommand.html
        vkCmdDrawIndirect(deferredCommandBuffer, scene->GetBlades()[j]->GetNumBladesBuffer(), 0, 1, sizeof(BladeDrawIndirect));
    }

    // End render pass
    vkCmdEndRenderPass(deferredCommandBuffer);

    // ~ End recording ~
    if (vkEndCommandBuffer(deferredCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record DEFERRED command buffer");
    }
}

void DeferredRenderer::CreateCameraDescriptorSetLayout() {
    // Describe the binding of the descriptor set layout
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { uboLayoutBinding };

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &cameraDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void DeferredRenderer::CreateModelDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding albedoImageLayoutBinding = {};
    albedoImageLayoutBinding.binding = 2;
    albedoImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    albedoImageLayoutBinding.descriptorCount = 1;
    albedoImageLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    albedoImageLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding positionImageLayoutBinding = {};
    positionImageLayoutBinding.binding = 3;
    positionImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    positionImageLayoutBinding.descriptorCount = 1;
    positionImageLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    positionImageLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding normalImageLayoutBinding = {};
    normalImageLayoutBinding.binding = 4;
    normalImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalImageLayoutBinding.descriptorCount = 1;
    normalImageLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    normalImageLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { uboLayoutBinding, samplerLayoutBinding, 
        albedoImageLayoutBinding, positionImageLayoutBinding, normalImageLayoutBinding };

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &modelDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void DeferredRenderer::CreateTimeDescriptorSetLayout() {
    // Describe the binding of the descriptor set layout
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { uboLayoutBinding };

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &timeDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void DeferredRenderer::CreateComputeDescriptorSetLayout() {
    // TODOX: Create the descriptor set layout for the compute pipeline
    // Remember this is like a class definition stating why types of information
    // will be stored at each binding
    // Describe the binding of the descriptor set layout
    // TODOX: just copied this from time descriptor
    VkDescriptorSetLayoutBinding inputBladesLayoutBinding = {};
    inputBladesLayoutBinding.binding = 0;
    inputBladesLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    inputBladesLayoutBinding.descriptorCount = 1; // TODO: number of input blades???
    inputBladesLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    inputBladesLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding culledBladesLayoutBinding = {};
    culledBladesLayoutBinding.binding = 1;
    culledBladesLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    culledBladesLayoutBinding.descriptorCount = 1; // TODO: number of input blades???
    culledBladesLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    culledBladesLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding numBladesLayoutBinding = {};
    numBladesLayoutBinding.binding = 2;
    numBladesLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    numBladesLayoutBinding.descriptorCount = 1;
    numBladesLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    numBladesLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { inputBladesLayoutBinding, culledBladesLayoutBinding, numBladesLayoutBinding };

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &grassComputeDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void DeferredRenderer::CreateDescriptorPool() {
    // DTODO: update this with new buffers?
    // Describe which descriptor types that the descriptor sets will contain
    std::vector<VkDescriptorPoolSize> poolSizes = {
        // Camera
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER , 1 },

        // Models + Blades
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER , static_cast<uint32_t>(scene->GetModels().size() * 7 + scene->GetBlades().size()) },

        // Models + Blades
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER , static_cast<uint32_t>(scene->GetModels().size() + scene->GetBlades().size()) },

        // Time (compute)
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER , 1 },

        // TODO: Add any additional types and counts of descriptors you will need to allocate
        // Input blades (compute)
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER , static_cast<uint32_t>(scene->GetBlades().size()) },

        // Culled blades (compute)
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER , static_cast<uint32_t>(scene->GetBlades().size()) },

        // Number of remaining blades (compute)
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER , static_cast<uint32_t>(scene->GetBlades().size()) },
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 5;

    if (vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

void DeferredRenderer::CreateCameraDescriptorSet() {
    // Describe the desciptor set
    VkDescriptorSetLayout layouts[] = { cameraDescriptorSetLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layouts;

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &cameraDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    // Configure the descriptors to refer to buffers
    VkDescriptorBufferInfo cameraBufferInfo = {};
    cameraBufferInfo.buffer = camera->GetBuffer();
    cameraBufferInfo.offset = 0;
    cameraBufferInfo.range = sizeof(CameraBufferObject);

    std::array<VkWriteDescriptorSet, 1> descriptorWrites = {};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = cameraDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &cameraBufferInfo;
    descriptorWrites[0].pImageInfo = nullptr;
    descriptorWrites[0].pTexelBufferView = nullptr;

    // Update descriptor sets
    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void DeferredRenderer::CreateModelDescriptorSets() {
    modelDescriptorSets.resize(scene->GetModels().size());

    // Describe the desciptor set
    VkDescriptorSetLayout layouts[] = { modelDescriptorSetLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(modelDescriptorSets.size());
    allocInfo.pSetLayouts = layouts;

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, modelDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    // Image descriptors for the offscreen color attachments
    VkDescriptorImageInfo texDescriptorAlbedo = {};
    texDescriptorAlbedo.sampler = deferredSampler;
    texDescriptorAlbedo.imageView = deferredAlbedoImageView;
    texDescriptorAlbedo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo texDescriptorPosition = {};
    texDescriptorPosition.sampler = deferredSampler;
    texDescriptorPosition.imageView = deferredPositionImageView;
    texDescriptorPosition.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo texDescriptorNormal = {};
    texDescriptorNormal.sampler = deferredSampler;
    texDescriptorNormal.imageView = deferredNormalImageView;
    texDescriptorNormal.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::vector<VkWriteDescriptorSet> descriptorWrites(5 * modelDescriptorSets.size());

    for (uint32_t i = 0; i < scene->GetModels().size(); ++i) {
        VkDescriptorBufferInfo modelBufferInfo = {};
        modelBufferInfo.buffer = scene->GetModels()[i]->GetModelBuffer();
        modelBufferInfo.offset = 0;
        modelBufferInfo.range = sizeof(ModelBufferObject);

        // Bind image and sampler resources to the descriptor
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = scene->GetModels()[i]->GetTextureView();
        imageInfo.sampler = scene->GetModels()[i]->GetTextureSampler();

        descriptorWrites[5 * i + 0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5 * i + 0].dstSet = modelDescriptorSets[i];
        descriptorWrites[5 * i + 0].dstBinding = 0;
        descriptorWrites[5 * i + 0].dstArrayElement = 0;
        descriptorWrites[5 * i + 0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[5 * i + 0].descriptorCount = 1;
        descriptorWrites[5 * i + 0].pBufferInfo = &modelBufferInfo;
        descriptorWrites[5 * i + 0].pImageInfo = nullptr;
        descriptorWrites[5 * i + 0].pTexelBufferView = nullptr;

        descriptorWrites[5 * i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5 * i + 1].dstSet = modelDescriptorSets[i];
        descriptorWrites[5 * i + 1].dstBinding = 1;
        descriptorWrites[5 * i + 1].dstArrayElement = 0;
        descriptorWrites[5 * i + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5 * i + 1].descriptorCount = 1;
        descriptorWrites[5 * i + 1].pImageInfo = &imageInfo;

        descriptorWrites[5 * i + 2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5 * i + 2].dstSet = modelDescriptorSets[i];
        descriptorWrites[5 * i + 2].dstBinding = 2;
        descriptorWrites[5 * i + 2].dstArrayElement = 0;
        descriptorWrites[5 * i + 2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5 * i + 2].descriptorCount = 1;
        descriptorWrites[5 * i + 2].pImageInfo = &texDescriptorAlbedo;

        descriptorWrites[5 * i + 3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5 * i + 3].dstSet = modelDescriptorSets[i];
        descriptorWrites[5 * i + 3].dstBinding = 3;
        descriptorWrites[5 * i + 3].dstArrayElement = 0;
        descriptorWrites[5 * i + 3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5 * i + 3].descriptorCount = 1;
        descriptorWrites[5 * i + 3].pImageInfo = &texDescriptorPosition;

        descriptorWrites[5 * i + 4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5 * i + 4].dstSet = modelDescriptorSets[i];
        descriptorWrites[5 * i + 4].dstBinding = 4;
        descriptorWrites[5 * i + 4].dstArrayElement = 0;
        descriptorWrites[5 * i + 4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5 * i + 4].descriptorCount = 1;
        descriptorWrites[5 * i + 4].pImageInfo = &texDescriptorNormal;
    }

    // Update descriptor sets
    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void DeferredRenderer::CreateGrassDescriptorSets() {
    // TODO: Create Descriptor sets for the grass.
    // This should involve creating descriptor sets which point to the model matrix of each group of grass blades
    grassDescriptorSets.resize(scene->GetBlades().size());

    // Describe the desciptor set
    VkDescriptorSetLayout layouts[] = { modelDescriptorSetLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(grassDescriptorSets.size());
    allocInfo.pSetLayouts = layouts;

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, grassDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites(1 * grassDescriptorSets.size());

    for (uint32_t i = 0; i < scene->GetBlades().size(); ++i) {
        VkDescriptorBufferInfo grassBufferInfo = {};
        grassBufferInfo.buffer = scene->GetBlades()[i]->GetModelBuffer();
        grassBufferInfo.offset = 0;
        grassBufferInfo.range = sizeof(ModelBufferObject);

        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = grassDescriptorSets[i];
        descriptorWrites[i].dstBinding = 0;
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pBufferInfo = &grassBufferInfo;
        descriptorWrites[i].pImageInfo = nullptr;
        descriptorWrites[i].pTexelBufferView = nullptr;
    }

    // Update descriptor sets
    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void DeferredRenderer::CreateTimeDescriptorSet() {
    // Describe the desciptor set
    VkDescriptorSetLayout layouts[] = { timeDescriptorSetLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = layouts;

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &timeDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    // Configure the descriptors to refer to buffers
    VkDescriptorBufferInfo timeBufferInfo = {};
    timeBufferInfo.buffer = scene->GetTimeBuffer();
    timeBufferInfo.offset = 0;
    timeBufferInfo.range = sizeof(Time);

    std::array<VkWriteDescriptorSet, 1> descriptorWrites = {};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = timeDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &timeBufferInfo;
    descriptorWrites[0].pImageInfo = nullptr;
    descriptorWrites[0].pTexelBufferView = nullptr;

    // Update descriptor sets
    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void DeferredRenderer::CreateComputeDescriptorSets() {
    // TODO: Create Descriptor sets for the compute pipeline
    // The descriptors should point to Storage buffers which will hold the grass blades, the culled grass blades, and the output number of grass blades 
    grassComputeDescriptorSets.resize(scene->GetBlades().size());

    // Describe the desciptor set
    VkDescriptorSetLayout layouts[] = { grassComputeDescriptorSetLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(grassComputeDescriptorSets.size());
    allocInfo.pSetLayouts = layouts;

    // Allocate descriptor sets
    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, grassComputeDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites(3 * grassComputeDescriptorSets.size());

    for (uint32_t i = 0; i < scene->GetBlades().size(); ++i) {
        VkDescriptorBufferInfo inputBladesBufferInfo = {};
        inputBladesBufferInfo.buffer = scene->GetBlades()[i]->GetBladesBuffer();
        inputBladesBufferInfo.offset = 0;
        inputBladesBufferInfo.range = sizeof(Blade) * NUM_BLADES;

        VkDescriptorBufferInfo culledBladesBufferInfo = {};
        culledBladesBufferInfo.buffer = scene->GetBlades()[i]->GetCulledBladesBuffer();
        culledBladesBufferInfo.offset = 0;
        culledBladesBufferInfo.range = sizeof(Blade) * NUM_BLADES;

        VkDescriptorBufferInfo numBladesBufferInfo = {};
        numBladesBufferInfo.buffer = scene->GetBlades()[i]->GetNumBladesBuffer();
        numBladesBufferInfo.offset = 0;
        numBladesBufferInfo.range = sizeof(BladeDrawIndirect);

        descriptorWrites[3 * i + 0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3 * i + 0].dstSet = grassComputeDescriptorSets[i];
        descriptorWrites[3 * i + 0].dstBinding = 0;
        descriptorWrites[3 * i + 0].dstArrayElement = 0;
        descriptorWrites[3 * i + 0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3 * i + 0].descriptorCount = 1; // TODO: should this be 1? same for culledBlades below??? 
                                                         // I think so, because docs say descriptorCount is number of elts in pBufferInfo,
                                                         // and pBufferInfo is array of VkDescriptorBufferInfo, of which we only have one (inputBladesBufferInfo)
                                                         // https://www.khronos.org/registry/vulkan/specs/1.0/man/html/VkWriteDescriptorSet.html
        descriptorWrites[3 * i + 0].pBufferInfo = &inputBladesBufferInfo;
        descriptorWrites[3 * i + 0].pImageInfo = nullptr;
        descriptorWrites[3 * i + 0].pTexelBufferView = nullptr;

        descriptorWrites[3 * i + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3 * i + 1].dstSet = grassComputeDescriptorSets[i];
        descriptorWrites[3 * i + 1].dstBinding = 1;
        descriptorWrites[3 * i + 1].dstArrayElement = 0;
        descriptorWrites[3 * i + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3 * i + 1].descriptorCount = 1;
        descriptorWrites[3 * i + 1].pBufferInfo = &culledBladesBufferInfo;
        descriptorWrites[3 * i + 1].pImageInfo = nullptr;
        descriptorWrites[3 * i + 1].pTexelBufferView = nullptr;

        descriptorWrites[3 * i + 2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3 * i + 2].dstSet = grassComputeDescriptorSets[i];
        descriptorWrites[3 * i + 2].dstBinding = 2;
        descriptorWrites[3 * i + 2].dstArrayElement = 0;
        descriptorWrites[3 * i + 2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3 * i + 2].descriptorCount = 1;
        descriptorWrites[3 * i + 2].pBufferInfo = &numBladesBufferInfo;
        descriptorWrites[3 * i + 2].pImageInfo = nullptr;
        descriptorWrites[3 * i + 2].pTexelBufferView = nullptr;
    }

    // Update descriptor sets
    vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void DeferredRenderer::CreateGraphicsPipeline() {
    VkShaderModule vertShaderModule = ShaderModule::Create("shaders/graphics-DEFERRED.vert.spv", logicalDevice);
    VkShaderModule fragShaderModule = ShaderModule::Create("shaders/graphics-DEFERRED.frag.spv", logicalDevice);

    // Assign each shader module to the appropriate stage in the pipeline
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // --- Set up fixed-function stages ---

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewports and Scissors (rectangles that define in which regions pixels are stored)
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChain->GetVkExtent().width);
    viewport.height = static_cast<float>(swapChain->GetVkExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChain->GetVkExtent();

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    // Multisampling (turned off here)
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Depth testing
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending (turned off here, but showing options for learning)
    // --> Configuration per attached framebuffer
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    // --> Global color blending settings
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { cameraDescriptorSetLayout, modelDescriptorSetLayout };

    // Pipeline layout: used to specify uniform values
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &graphicsPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    // --- Create graphics pipeline ---
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = graphicsPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
}

void DeferredRenderer::CreateGrassPipeline() {
    // --- Set up programmable shaders ---
    VkShaderModule vertShaderModule = ShaderModule::Create("shaders/grass.vert.spv", logicalDevice);
    VkShaderModule tescShaderModule = ShaderModule::Create("shaders/grass.tesc.spv", logicalDevice);
    VkShaderModule teseShaderModule = ShaderModule::Create("shaders/grass-DEFERRED.tese.spv", logicalDevice);
    VkShaderModule fragShaderModule = ShaderModule::Create("shaders/grass-DEFERRED.frag.spv", logicalDevice);

    // Assign each shader module to the appropriate stage in the pipeline
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo tescShaderStageInfo = {};
    tescShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    tescShaderStageInfo.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    tescShaderStageInfo.module = tescShaderModule;
    tescShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo teseShaderStageInfo = {};
    teseShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    teseShaderStageInfo.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    teseShaderStageInfo.module = teseShaderModule;
    teseShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, tescShaderStageInfo, teseShaderStageInfo, fragShaderStageInfo };

    // --- Set up fixed-function stages ---

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescription = Blade::getBindingDescription();
    auto attributeDescriptions = Blade::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewports and Scissors (rectangles that define in which regions pixels are stored)
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChain->GetVkExtent().width);
    viewport.height = static_cast<float>(swapChain->GetVkExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChain->GetVkExtent();

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    // Multisampling (turned off here)
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Depth testing
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending (turned off here, but showing options for learning)
    // --> Configuration per attached framebuffer
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    for (int i = 0; i < 3; i++) {
        VkPipelineColorBlendAttachmentState colorBlendAttachment;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        colorBlendAttachments.push_back(colorBlendAttachment);
    }


    // DTODO: might need to change this? use default settings?
    // --> Global color blending settings
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = colorBlendAttachments.size();
    colorBlending.pAttachments = colorBlendAttachments.data();
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { cameraDescriptorSetLayout, modelDescriptorSetLayout };

    // Pipeline layout: used to specify uniform values
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &grassPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    // Tessellation state
    VkPipelineTessellationStateCreateInfo tessellationInfo = {};
    tessellationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellationInfo.pNext = NULL;
    tessellationInfo.flags = 0;
    tessellationInfo.patchControlPoints = 1;

    // --- Create graphics pipeline ---
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 4;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pTessellationState = &tessellationInfo;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = grassPipelineLayout;
    pipelineInfo.renderPass = deferredRenderPass; // important!!
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &grassPipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    // No need for the shader modules anymore
    vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, tescShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, teseShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
}

void DeferredRenderer::CreateComputePipeline() {
    // Set up programmable shaders
    VkShaderModule computeShaderModule = ShaderModule::Create("shaders/compute.comp.spv", logicalDevice);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo = {};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    // TODO: Add the compute dsecriptor set layout you create to this list
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts = { cameraDescriptorSetLayout, timeDescriptorSetLayout, grassComputeDescriptorSetLayout };

    // Define push constant stuff to hold NUM_BLADES
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(int);
    VkPushConstantRange pushConstantRangeArray[] = { pushConstantRange };

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = pushConstantRangeArray;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = computePipelineLayout;
    pipelineInfo.pNext = nullptr;
    pipelineInfo.flags = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateComputePipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline");
    }

    // No need for shader modules anymore
    vkDestroyShaderModule(logicalDevice, computeShaderModule, nullptr);
}

void DeferredRenderer::CreateFrameResources() {
    imageViews.resize(swapChain->GetCount());

    for (uint32_t i = 0; i < swapChain->GetCount(); i++) {
        // --- Create an image view for each swap chain image ---
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChain->GetVkImage(i);

        // Specify how the image data should be interpreted
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChain->GetVkImageFormat();

        // Specify color channel mappings (can be used for swizzling)
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // Describe the image's purpose and which part of the image should be accessed
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        // Create the image view
        if (vkCreateImageView(logicalDevice, &createInfo, nullptr, &imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image views");
        }
    }

    VkFormat depthFormat = device->GetInstance()->GetSupportedFormat({ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    // CREATE DEPTH IMAGE
    Image::Create(device,
        swapChain->GetVkExtent().width,
        swapChain->GetVkExtent().height,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depthImage,
        depthImageMemory
    );

    depthImageView = Image::CreateView(device, depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Transition the image for use as depth-stencil
    Image::TransitionLayout(device, graphicsCommandPool, depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);


    // CREATE FRAMEBUFFERS
    framebuffers.resize(swapChain->GetCount());
    for (size_t i = 0; i < swapChain->GetCount(); i++) {
        std::vector<VkImageView> attachments = {
            imageViews[i],
            depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChain->GetVkExtent().width;
        framebufferInfo.height = swapChain->GetVkExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }

    }
}

void DeferredRenderer::DestroyFrameResources() {
    for (size_t i = 0; i < imageViews.size(); i++) {
        vkDestroyImageView(logicalDevice, imageViews[i], nullptr);
    }

    vkDestroyImageView(logicalDevice, depthImageView, nullptr);
    vkFreeMemory(logicalDevice, depthImageMemory, nullptr);
    vkDestroyImage(logicalDevice, depthImage, nullptr);

    for (size_t i = 0; i < framebuffers.size(); i++) {
        vkDestroyFramebuffer(logicalDevice, framebuffers[i], nullptr);
    }

    // free deferred pipeline stuff
    vkDestroyImageView(logicalDevice, deferredAlbedoImageView, nullptr);
    vkFreeMemory(logicalDevice, deferredAlbedoImageMemory, nullptr);
    vkDestroyImage(logicalDevice, deferredAlbedoImage, nullptr);

    vkDestroyImageView(logicalDevice, deferredPositionImageView, nullptr);
    vkFreeMemory(logicalDevice, deferredPositionImageMemory, nullptr);
    vkDestroyImage(logicalDevice, deferredPositionImage, nullptr);

    vkDestroyImageView(logicalDevice, deferredNormalImageView, nullptr);
    vkFreeMemory(logicalDevice, deferredNormalImageMemory, nullptr);
    vkDestroyImage(logicalDevice, deferredNormalImage, nullptr);

    vkDestroyImageView(logicalDevice, deferredDepthImageView, nullptr);
    vkFreeMemory(logicalDevice, deferredDepthImageMemory, nullptr);
    vkDestroyImage(logicalDevice, deferredDepthImage, nullptr);

    vkDestroyFramebuffer(logicalDevice, deferredFramebuffer, nullptr);
}

void DeferredRenderer::RecreateFrameResources() {
    vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, grassPipeline, nullptr);
    vkDestroyPipelineLayout(logicalDevice, graphicsPipelineLayout, nullptr);
    vkDestroyPipelineLayout(logicalDevice, grassPipelineLayout, nullptr);
    vkFreeCommandBuffers(logicalDevice, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

    DestroyFrameResources();
    CreateFrameResources();
    CreateGraphicsPipeline();
    CreateGrassPipeline();
    RecordCommandBuffers();
}

void DeferredRenderer::RecordComputeCommandBuffer() {
    // Specify the command pool and number of buffers to allocate
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = computeCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &computeCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    // ~ Start recording ~
    if (vkBeginCommandBuffer(computeCommandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording compute command buffer");
    }

    // Bind to the compute pipeline
    vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);

    // Bind camera descriptor set
    vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &cameraDescriptorSet, 0, nullptr);

    // Bind descriptor set for time uniforms
    vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 1, 1, &timeDescriptorSet, 0, nullptr);

    // Update push constants
    int pushValues[] = { NUM_BLADES };
    vkCmdPushConstants(computeCommandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), pushValues);

    // TODO: For each group of blades bind its descriptor set and dispatch
    for (uint32_t j = 0; j < scene->GetBlades().size(); ++j) {
        vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 2, 1, &grassComputeDescriptorSets[j], 0, nullptr);
        vkCmdDispatch(computeCommandBuffer, (int)ceil((float)NUM_BLADES / WORKGROUP_SIZE), 1, 1);
    }

    // ~ End recording ~
    if (vkEndCommandBuffer(computeCommandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record compute command buffer");
    }
}

void DeferredRenderer::RecordCommandBuffers() {
    commandBuffers.resize(swapChain->GetCount());

    // Specify the command pool and number of buffers to allocate
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = graphicsCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    // Start command buffer recording
    for (size_t i = 0; i < commandBuffers.size(); i++) {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr;

        // ~ Start recording ~
        if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("Failed to begin recording command buffer");
        }

        // Begin the render pass
        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[i];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = swapChain->GetVkExtent();

        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = { 0.768f, 0.8039f, 0.898f, 1.0f };
        clearValues[1].depthStencil = { 1.0f, 0 };
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        std::vector<VkBufferMemoryBarrier> barriers(scene->GetBlades().size());
        for (uint32_t j = 0; j < barriers.size(); ++j) {
            barriers[j].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barriers[j].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[j].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            barriers[j].srcQueueFamilyIndex = device->GetQueueIndex(QueueFlags::Compute);
            barriers[j].dstQueueFamilyIndex = device->GetQueueIndex(QueueFlags::Graphics);
            barriers[j].buffer = scene->GetBlades()[j]->GetNumBladesBuffer();
            barriers[j].offset = 0;
            barriers[j].size = sizeof(BladeDrawIndirect);
        }

        vkCmdPipelineBarrier(commandBuffers[i], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, barriers.size(), barriers.data(), 0, nullptr);

        // Bind the camera descriptor set. This is set 0 in all pipelines so it will be inherited
        vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &cameraDescriptorSet, 0, nullptr);

        vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Bind the graphics pipeline
        vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        // DTODO: this "model" should act as screen-covering quad
        for (uint32_t j = 0; j < scene->GetModels().size(); ++j) {
            // Bind the vertex and index buffers
            VkBuffer vertexBuffers[] = { scene->GetModels()[j]->getVertexBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

            vkCmdBindIndexBuffer(commandBuffers[i], scene->GetModels()[j]->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // Bind the descriptor set for each model
            vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 1, 1, &modelDescriptorSets[j], 0, nullptr);

            // Draw
            std::vector<uint32_t> indices = scene->GetModels()[j]->getIndices();
            vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
        }
#if 0
        // Bind the grass pipeline
        vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, grassPipeline);

        for (uint32_t j = 0; j < scene->GetBlades().size(); ++j) {
            VkBuffer vertexBuffers[] = { scene->GetBlades()[j]->GetCulledBladesBuffer() };//{ scene->GetBlades()[j]->GetCulledBladesBuffer() };
            VkDeviceSize offsets[] = { 0 };
            // TODO: Uncomment this when the buffers are populated
            vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

            // TODO: Bind the descriptor set for each grass blades model
            vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, grassPipelineLayout, 1, 1, &grassDescriptorSets[j], 0, nullptr);

            // Draw
            // TODO: Uncomment this when the buffers are populated
            // CHECKITOUT: it's getNumBladesBuffer that specifies how many threads are spawned
            // see: https://www.khronos.org/registry/vulkan/specs/1.0/man/html/vkCmdDrawIndirect.html
            // see: https://www.khronos.org/registry/vulkan/specs/1.0/man/html/VkDrawIndirectCommand.html
            vkCmdDrawIndirect(commandBuffers[i], scene->GetBlades()[j]->GetNumBladesBuffer(), 0, 1, sizeof(BladeDrawIndirect));
        }
#endif
        // End render pass
        vkCmdEndRenderPass(commandBuffers[i]);

        // ~ End recording ~
        if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to record command buffer");
        }
    }
}

void DeferredRenderer::Frame() {

    VkSubmitInfo computeSubmitInfo = {};
    computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    computeSubmitInfo.commandBufferCount = 1;
    computeSubmitInfo.pCommandBuffers = &computeCommandBuffer;

    if (vkQueueSubmit(device->GetQueue(QueueFlags::Compute), 1, &computeSubmitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    if (!swapChain->Acquire()) {
        RecreateFrameResources();
        return;
    }

    // Submit the grass buffer (make G-buffer)
    VkSubmitInfo deferredSubmitInfo = {};
    deferredSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore deferredWaitSemaphores[] = { swapChain->GetImageAvailableVkSemaphore() };
    VkPipelineStageFlags deferredWaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    deferredSubmitInfo.waitSemaphoreCount = 1;
    deferredSubmitInfo.pWaitSemaphores = deferredWaitSemaphores;
    deferredSubmitInfo.pWaitDstStageMask = deferredWaitStages;
    deferredSubmitInfo.pSignalSemaphores = &deferredSemaphore;

    deferredSubmitInfo.commandBufferCount = 1;
    deferredSubmitInfo.pCommandBuffers = &deferredCommandBuffer;

    //VkSemaphore signalSemaphores[] = { swapChain->GetRenderFinishedVkSemaphore() };
    deferredSubmitInfo.signalSemaphoreCount = 1;
    //submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device->GetQueue(QueueFlags::Graphics), 1, &deferredSubmitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw DEFERRED command buffer");
    }

    // Submit the command buffer
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    //VkSemaphore waitSemaphores[] = { swapChain->GetImageAvailableVkSemaphore() };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &deferredSemaphore;//waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[swapChain->GetIndex()];

    VkSemaphore signalSemaphores[] = { swapChain->GetRenderFinishedVkSemaphore() };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device->GetQueue(QueueFlags::Graphics), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

#if PRINT_NUM_BLADES
    // try to read numBladesBuffer ============================================
    // Create the staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VkDeviceSize bufferSize = sizeof(BladeDrawIndirect);

    VkBufferUsageFlags stagingUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkMemoryPropertyFlags stagingProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    BufferUtils::CreateBuffer(device, bufferSize, stagingUsage, stagingProperties, stagingBuffer, stagingBufferMemory);

    // Fill the staging buffer
    void *data;
    vkMapMemory(device->GetVkDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    // CPU mem "data" should now be mapped to a vkbuffer!!??
    // so copy data into "data"
    BufferUtils::CopyBuffer(device, computeCommandPool, scene->GetBlades()[0]->GetNumBladesBuffer(), stagingBuffer, bufferSize);
    vkUnmapMemory(device->GetVkDevice(), stagingBufferMemory);

    // read "data"
    BladeDrawIndirect* indirectDraw = (BladeDrawIndirect*)data;
    //printf("num blades: %d\n", indirectDraw->vertexCount);

    // No need for the staging buffer anymore
    vkDestroyBuffer(device->GetVkDevice(), stagingBuffer, nullptr);
    vkFreeMemory(device->GetVkDevice(), stagingBufferMemory, nullptr);
    // try to read numBladesBuffer ============================================

    // Try to read all the blades first position info ========================================
    // Create the staging buffer
    VkBuffer stagingBuffer1;
    VkDeviceMemory stagingBuffer1Memory;
    VkDeviceSize buffer1Size = NUM_BLADES * sizeof(Blade);

    VkBufferUsageFlags staging1Usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkMemoryPropertyFlags staging1Properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    BufferUtils::CreateBuffer(device, buffer1Size, staging1Usage, staging1Properties, stagingBuffer1, stagingBuffer1Memory);

    // Fill the staging buffer
    void *data1;
    vkMapMemory(device->GetVkDevice(), stagingBuffer1Memory, 0, buffer1Size, 0, &data1);
    // CPU mem "data" should now be mapped to a vkbuffer!!??
    // so copy data into "data"
    BufferUtils::CopyBuffer(device, computeCommandPool, scene->GetBlades()[0]->GetBladesBuffer(), stagingBuffer1, buffer1Size);
    vkUnmapMemory(device->GetVkDevice(), stagingBuffer1Memory);

    // read "data"
    Blade* blade1 = (Blade*)data1;
    printf("blade[i].vo: %d\n", (*blade1).v0);

    // No need for the staging buffer anymore
    vkDestroyBuffer(device->GetVkDevice(), stagingBuffer1, nullptr);
    // Try to read all the blades first position info ========================================

#endif // PRINT_NUM_BLADES

    if (!swapChain->Present()) {
        RecreateFrameResources();
    }
}

DeferredRenderer::~DeferredRenderer() {
    vkDeviceWaitIdle(logicalDevice);

    // TODO: destroy any resources you created

    vkFreeCommandBuffers(logicalDevice, graphicsCommandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
    vkFreeCommandBuffers(logicalDevice, computeCommandPool, 1, &computeCommandBuffer);
    vkFreeCommandBuffers(logicalDevice, graphicsCommandPool, 1, &deferredCommandBuffer);

    vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, grassPipeline, nullptr);
    vkDestroyPipeline(logicalDevice, computePipeline, nullptr);
    // newly added
    //vkDestroyPipeline(logicalDevice, deferredPipeline, nullptr);

    vkDestroyPipelineLayout(logicalDevice, graphicsPipelineLayout, nullptr);
    vkDestroyPipelineLayout(logicalDevice, grassPipelineLayout, nullptr);
    vkDestroyPipelineLayout(logicalDevice, computePipelineLayout, nullptr);
    // newly added
    //vkDestroyPipelineLayout(logicalDevice, deferredPipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(logicalDevice, cameraDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, modelDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, timeDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, grassComputeDescriptorSetLayout, nullptr);

    vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);

    vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
    vkDestroyRenderPass(logicalDevice, deferredRenderPass, nullptr);
    DestroyFrameResources();
    vkDestroyCommandPool(logicalDevice, computeCommandPool, nullptr);
    vkDestroyCommandPool(logicalDevice, graphicsCommandPool, nullptr);

    vkDestroySemaphore(logicalDevice, deferredSemaphore, nullptr);
    vkDestroySampler(logicalDevice, deferredSampler, nullptr);
}
