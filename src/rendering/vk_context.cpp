#define VMA_IMPLEMENTATION
#include "rendering/vk_context.hpp"
#include "core/logger.hpp"
#include <VkBootstrap.h>
#include <SDL2/SDL_vulkan.h>
#include <algorithm>
#include <cstring>

namespace wowee {
namespace rendering {

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    [[maybe_unused]] void* userData)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_ERROR("Vulkan: ", callbackData->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARNING("Vulkan: ", callbackData->pMessage);
    }
    return VK_FALSE;
}

VkContext::~VkContext() {
    shutdown();
}

bool VkContext::initialize(SDL_Window* window) {
    LOG_INFO("Initializing Vulkan context");

    if (!createInstance(window)) return false;
    if (!createSurface(window)) return false;
    if (!selectPhysicalDevice()) return false;
    if (!createLogicalDevice()) return false;
    if (!createAllocator()) return false;

    int w, h;
    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    if (!createSwapchain(w, h)) return false;

    if (!createCommandPools()) return false;
    if (!createSyncObjects()) return false;
    if (!createImGuiResources()) return false;

    LOG_INFO("Vulkan context initialized successfully");
    return true;
}

void VkContext::shutdown() {
    if (device) {
        vkDeviceWaitIdle(device);
    }

    destroyImGuiResources();

    // Destroy sync objects
    for (auto& frame : frames) {
        if (frame.inFlightFence) vkDestroyFence(device, frame.inFlightFence, nullptr);
        if (frame.renderFinishedSemaphore) vkDestroySemaphore(device, frame.renderFinishedSemaphore, nullptr);
        if (frame.imageAvailableSemaphore) vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr);
        if (frame.commandPool) vkDestroyCommandPool(device, frame.commandPool, nullptr);
        frame = {};
    }

    if (immFence) { vkDestroyFence(device, immFence, nullptr); immFence = VK_NULL_HANDLE; }
    if (immCommandPool) { vkDestroyCommandPool(device, immCommandPool, nullptr); immCommandPool = VK_NULL_HANDLE; }

    destroySwapchain();

    if (allocator) { vmaDestroyAllocator(allocator); allocator = VK_NULL_HANDLE; }
    if (device) { vkDestroyDevice(device, nullptr); device = VK_NULL_HANDLE; }
    if (surface) { vkDestroySurfaceKHR(instance, surface, nullptr); surface = VK_NULL_HANDLE; }

    if (debugMessenger) {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func) func(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }

    if (instance) { vkDestroyInstance(instance, nullptr); instance = VK_NULL_HANDLE; }

    LOG_INFO("Vulkan context shutdown");
}

bool VkContext::createInstance(SDL_Window* window) {
    // Get required SDL extensions
    unsigned int sdlExtCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &sdlExtCount, nullptr);
    std::vector<const char*> sdlExts(sdlExtCount);
    SDL_Vulkan_GetInstanceExtensions(window, &sdlExtCount, sdlExts.data());

    vkb::InstanceBuilder builder;
    builder.set_app_name("Wowee")
           .set_app_version(VK_MAKE_VERSION(1, 0, 0))
           .require_api_version(1, 1, 0);

    for (auto ext : sdlExts) {
        builder.enable_extension(ext);
    }

    if (enableValidation) {
        builder.request_validation_layers(true)
               .set_debug_callback(debugCallback);
    }

    auto instRet = builder.build();
    if (!instRet) {
        LOG_ERROR("Failed to create Vulkan instance: ", instRet.error().message());
        return false;
    }

    vkbInstance_ = instRet.value();
    instance = vkbInstance_.instance;
    debugMessenger = vkbInstance_.debug_messenger;

    LOG_INFO("Vulkan instance created");
    return true;
}

bool VkContext::createSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
        LOG_ERROR("Failed to create Vulkan surface: ", SDL_GetError());
        return false;
    }
    return true;
}

bool VkContext::selectPhysicalDevice() {
    vkb::PhysicalDeviceSelector selector{vkbInstance_};
    selector.set_surface(surface)
            .set_minimum_version(1, 1)
            .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete);

    auto physRet = selector.select();
    if (!physRet) {
        LOG_ERROR("Failed to select Vulkan physical device: ", physRet.error().message());
        return false;
    }

    vkbPhysicalDevice_ = physRet.value();
    physicalDevice = vkbPhysicalDevice_.physical_device;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    LOG_INFO("Vulkan device: ", props.deviceName);
    LOG_INFO("Vulkan API version: ", VK_VERSION_MAJOR(props.apiVersion), ".",
             VK_VERSION_MINOR(props.apiVersion), ".", VK_VERSION_PATCH(props.apiVersion));

    return true;
}

bool VkContext::createLogicalDevice() {
    vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice_};
    auto devRet = deviceBuilder.build();
    if (!devRet) {
        LOG_ERROR("Failed to create Vulkan logical device: ", devRet.error().message());
        return false;
    }

    auto vkbDevice = devRet.value();
    device = vkbDevice.device;

    auto gqRet = vkbDevice.get_queue(vkb::QueueType::graphics);
    if (!gqRet) {
        LOG_ERROR("Failed to get graphics queue");
        return false;
    }
    graphicsQueue = gqRet.value();
    graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    auto pqRet = vkbDevice.get_queue(vkb::QueueType::present);
    if (!pqRet) {
        // Fall back to graphics queue for presentation
        presentQueue = graphicsQueue;
        presentQueueFamily = graphicsQueueFamily;
    } else {
        presentQueue = pqRet.value();
        presentQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::present).value();
    }

    LOG_INFO("Vulkan logical device created");
    return true;
}

bool VkContext::createAllocator() {
    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.instance = instance;
    allocInfo.physicalDevice = physicalDevice;
    allocInfo.device = device;
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_1;

    if (vmaCreateAllocator(&allocInfo, &allocator) != VK_SUCCESS) {
        LOG_ERROR("Failed to create VMA allocator");
        return false;
    }

    LOG_INFO("VMA allocator created");
    return true;
}

bool VkContext::createSwapchain(int width, int height) {
    vkb::SwapchainBuilder swapchainBuilder{physicalDevice, device, surface};

    auto swapRet = swapchainBuilder
        .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // VSync
        .set_desired_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height))
        .set_desired_min_image_count(2)
        .set_old_swapchain(swapchain) // For recreation
        .build();

    if (!swapRet) {
        LOG_ERROR("Failed to create Vulkan swapchain: ", swapRet.error().message());
        return false;
    }

    // Destroy old swapchain if recreating
    if (swapchain != VK_NULL_HANDLE) {
        destroySwapchain();
    }

    auto vkbSwap = swapRet.value();
    swapchain = vkbSwap.swapchain;
    swapchainFormat = vkbSwap.image_format;
    swapchainExtent = vkbSwap.extent;
    swapchainImages = vkbSwap.get_images().value();
    swapchainImageViews = vkbSwap.get_image_views().value();

    // Create framebuffers for ImGui render pass (created after ImGui resources)
    // Will be created in createImGuiResources or recreateSwapchain

    LOG_INFO("Vulkan swapchain created: ", swapchainExtent.width, "x", swapchainExtent.height,
             " (", swapchainImages.size(), " images)");
    swapchainDirty = false;
    return true;
}

void VkContext::destroySwapchain() {
    for (auto fb : swapchainFramebuffers) {
        if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    }
    swapchainFramebuffers.clear();

    for (auto iv : swapchainImageViews) {
        if (iv) vkDestroyImageView(device, iv, nullptr);
    }
    swapchainImageViews.clear();
    swapchainImages.clear();

    if (swapchain) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

bool VkContext::createCommandPools() {
    // Per-frame command pools (resettable)
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsQueueFamily;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &frames[i].commandPool) != VK_SUCCESS) {
            LOG_ERROR("Failed to create command pool for frame ", i);
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frames[i].commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device, &allocInfo, &frames[i].commandBuffer) != VK_SUCCESS) {
            LOG_ERROR("Failed to allocate command buffer for frame ", i);
            return false;
        }
    }

    // Immediate submit pool
    VkCommandPoolCreateInfo immPoolInfo{};
    immPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    immPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    immPoolInfo.queueFamilyIndex = graphicsQueueFamily;

    if (vkCreateCommandPool(device, &immPoolInfo, nullptr, &immCommandPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create immediate command pool");
        return false;
    }

    return true;
}

bool VkContext::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so first frame doesn't block

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device, &semInfo, nullptr, &frames[i].imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semInfo, nullptr, &frames[i].renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &frames[i].inFlightFence) != VK_SUCCESS) {
            LOG_ERROR("Failed to create sync objects for frame ", i);
            return false;
        }
    }

    // Immediate submit fence (not signaled initially)
    VkFenceCreateInfo immFenceInfo{};
    immFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &immFenceInfo, nullptr, &immFence) != VK_SUCCESS) {
        LOG_ERROR("Failed to create immediate submit fence");
        return false;
    }

    return true;
}

bool VkContext::createDepthBuffer() {
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = depthFormat;
    imgInfo.extent = {swapchainExtent.width, swapchainExtent.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = msaaSamples_;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imgInfo, &allocInfo, &depthImage, &depthAllocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth image");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth image view");
        return false;
    }

    return true;
}

void VkContext::destroyDepthBuffer() {
    if (depthImageView) { vkDestroyImageView(device, depthImageView, nullptr); depthImageView = VK_NULL_HANDLE; }
    if (depthImage) { vmaDestroyImage(allocator, depthImage, depthAllocation); depthImage = VK_NULL_HANDLE; depthAllocation = VK_NULL_HANDLE; }
}

bool VkContext::createMsaaColorImage() {
    if (msaaSamples_ == VK_SAMPLE_COUNT_1_BIT) return true; // No MSAA image needed

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = swapchainFormat;
    imgInfo.extent = {swapchainExtent.width, swapchainExtent.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = msaaSamples_;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;

    if (vmaCreateImage(allocator, &imgInfo, &allocInfo, &msaaColorImage_, &msaaColorAllocation_, nullptr) != VK_SUCCESS) {
        // Retry without TRANSIENT (some drivers reject it at high sample counts)
        imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        allocInfo.preferredFlags = 0;
        if (vmaCreateImage(allocator, &imgInfo, &allocInfo, &msaaColorImage_, &msaaColorAllocation_, nullptr) != VK_SUCCESS) {
            LOG_ERROR("Failed to create MSAA color image");
            return false;
        }
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = msaaColorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = swapchainFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &msaaColorView_) != VK_SUCCESS) {
        LOG_ERROR("Failed to create MSAA color image view");
        return false;
    }

    return true;
}

void VkContext::destroyMsaaColorImage() {
    if (msaaColorView_) { vkDestroyImageView(device, msaaColorView_, nullptr); msaaColorView_ = VK_NULL_HANDLE; }
    if (msaaColorImage_) { vmaDestroyImage(allocator, msaaColorImage_, msaaColorAllocation_); msaaColorImage_ = VK_NULL_HANDLE; msaaColorAllocation_ = VK_NULL_HANDLE; }
}

VkSampleCountFlagBits VkContext::getMaxUsableSampleCount() const {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts
                               & props.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

void VkContext::setMsaaSamples(VkSampleCountFlagBits samples) {
    // Clamp to max supported
    VkSampleCountFlagBits maxSamples = getMaxUsableSampleCount();
    if (samples > maxSamples) samples = maxSamples;
    msaaSamples_ = samples;
    swapchainDirty = true;
}

bool VkContext::createImGuiResources() {
    // Create depth buffer first
    if (!createDepthBuffer()) return false;

    // Create MSAA color image if needed
    if (!createMsaaColorImage()) return false;

    bool useMsaa = (msaaSamples_ > VK_SAMPLE_COUNT_1_BIT);

    if (useMsaa) {
        // MSAA render pass: 3 attachments (MSAA color, depth, resolve/swapchain)
        VkAttachmentDescription attachments[3] = {};

        // Attachment 0: MSAA color target
        attachments[0].format = swapchainFormat;
        attachments[0].samples = msaaSamples_;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // Attachment 1: Depth (multisampled)
        attachments[1].format = depthFormat;
        attachments[1].samples = msaaSamples_;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // Attachment 2: Resolve target (swapchain image)
        attachments[2].format = swapchainFormat;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference resolveRef{};
        resolveRef.attachment = 2;
        resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;
        subpass.pResolveAttachments = &resolveRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 3;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &rpInfo, nullptr, &imguiRenderPass) != VK_SUCCESS) {
            LOG_ERROR("Failed to create MSAA render pass");
            return false;
        }

        // Framebuffers: [msaaColorView, depthView, swapchainView]
        swapchainFramebuffers.resize(swapchainImageViews.size());
        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            VkImageView fbAttachments[3] = {msaaColorView_, depthImageView, swapchainImageViews[i]};

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = imguiRenderPass;
            fbInfo.attachmentCount = 3;
            fbInfo.pAttachments = fbAttachments;
            fbInfo.width = swapchainExtent.width;
            fbInfo.height = swapchainExtent.height;
            fbInfo.layers = 1;

            if (vkCreateFramebuffer(device, &fbInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
                LOG_ERROR("Failed to create MSAA swapchain framebuffer ", i);
                return false;
            }
        }
    } else {
        // Non-MSAA render pass: 2 attachments (color + depth) — original path
        VkAttachmentDescription attachments[2] = {};

        // Color attachment (swapchain image)
        attachments[0].format = swapchainFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // Depth attachment
        attachments[1].format = depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 2;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &rpInfo, nullptr, &imguiRenderPass) != VK_SUCCESS) {
            LOG_ERROR("Failed to create render pass");
            return false;
        }

        // Framebuffers: [swapchainView, depthView]
        swapchainFramebuffers.resize(swapchainImageViews.size());
        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            VkImageView fbAttachments[2] = {swapchainImageViews[i], depthImageView};

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = imguiRenderPass;
            fbInfo.attachmentCount = 2;
            fbInfo.pAttachments = fbAttachments;
            fbInfo.width = swapchainExtent.width;
            fbInfo.height = swapchainExtent.height;
            fbInfo.layers = 1;

            if (vkCreateFramebuffer(device, &fbInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
                LOG_ERROR("Failed to create swapchain framebuffer ", i);
                return false;
            }
        }
    }

    // Create descriptor pool for ImGui
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
    };

    VkDescriptorPoolCreateInfo dpInfo{};
    dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpInfo.maxSets = 100;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device, &dpInfo, nullptr, &imguiDescriptorPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create ImGui descriptor pool");
        return false;
    }

    return true;
}

void VkContext::destroyImGuiResources() {
    if (imguiDescriptorPool) {
        vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr);
        imguiDescriptorPool = VK_NULL_HANDLE;
    }
    destroyMsaaColorImage();
    destroyDepthBuffer();
    // Framebuffers are destroyed in destroySwapchain()
    if (imguiRenderPass) {
        vkDestroyRenderPass(device, imguiRenderPass, nullptr);
        imguiRenderPass = VK_NULL_HANDLE;
    }
}

bool VkContext::recreateSwapchain(int width, int height) {
    vkDeviceWaitIdle(device);

    // Destroy old framebuffers
    for (auto fb : swapchainFramebuffers) {
        if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    }
    swapchainFramebuffers.clear();

    // Destroy old image views
    for (auto iv : swapchainImageViews) {
        if (iv) vkDestroyImageView(device, iv, nullptr);
    }
    swapchainImageViews.clear();

    VkSwapchainKHR oldSwapchain = swapchain;

    vkb::SwapchainBuilder swapchainBuilder{physicalDevice, device, surface};
    auto swapRet = swapchainBuilder
        .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height))
        .set_desired_min_image_count(2)
        .set_old_swapchain(oldSwapchain)
        .build();

    if (oldSwapchain) {
        vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
    }

    if (!swapRet) {
        LOG_ERROR("Failed to recreate swapchain: ", swapRet.error().message());
        swapchain = VK_NULL_HANDLE;
        return false;
    }

    auto vkbSwap = swapRet.value();
    swapchain = vkbSwap.swapchain;
    swapchainFormat = vkbSwap.image_format;
    swapchainExtent = vkbSwap.extent;
    swapchainImages = vkbSwap.get_images().value();
    swapchainImageViews = vkbSwap.get_image_views().value();

    // Recreate depth buffer + MSAA color image
    destroyMsaaColorImage();
    destroyDepthBuffer();

    // Destroy old render pass (needs recreation if MSAA changed)
    if (imguiRenderPass) {
        vkDestroyRenderPass(device, imguiRenderPass, nullptr);
        imguiRenderPass = VK_NULL_HANDLE;
    }

    if (!createDepthBuffer()) return false;
    if (!createMsaaColorImage()) return false;

    bool useMsaa = (msaaSamples_ > VK_SAMPLE_COUNT_1_BIT);

    if (useMsaa) {
        // MSAA render pass: 3 attachments
        VkAttachmentDescription attachments[3] = {};
        attachments[0].format = swapchainFormat;
        attachments[0].samples = msaaSamples_;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachments[1].format = depthFormat;
        attachments[1].samples = msaaSamples_;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachments[2].format = swapchainFormat;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;
        subpass.pResolveAttachments = &resolveRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 3;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &rpInfo, nullptr, &imguiRenderPass) != VK_SUCCESS) {
            LOG_ERROR("Failed to recreate MSAA render pass");
            return false;
        }

        swapchainFramebuffers.resize(swapchainImageViews.size());
        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            VkImageView fbAttachments[3] = {msaaColorView_, depthImageView, swapchainImageViews[i]};
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = imguiRenderPass;
            fbInfo.attachmentCount = 3;
            fbInfo.pAttachments = fbAttachments;
            fbInfo.width = swapchainExtent.width;
            fbInfo.height = swapchainExtent.height;
            fbInfo.layers = 1;
            if (vkCreateFramebuffer(device, &fbInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
                LOG_ERROR("Failed to recreate MSAA swapchain framebuffer ", i);
                return false;
            }
        }
    } else {
        // Non-MSAA render pass: 2 attachments
        VkAttachmentDescription attachments[2] = {};
        attachments[0].format = swapchainFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        attachments[1].format = depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 2;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &rpInfo, nullptr, &imguiRenderPass) != VK_SUCCESS) {
            LOG_ERROR("Failed to recreate render pass");
            return false;
        }

        swapchainFramebuffers.resize(swapchainImageViews.size());
        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            VkImageView fbAttachments[2] = {swapchainImageViews[i], depthImageView};
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = imguiRenderPass;
            fbInfo.attachmentCount = 2;
            fbInfo.pAttachments = fbAttachments;
            fbInfo.width = swapchainExtent.width;
            fbInfo.height = swapchainExtent.height;
            fbInfo.layers = 1;
            if (vkCreateFramebuffer(device, &fbInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
                LOG_ERROR("Failed to recreate swapchain framebuffer ", i);
                return false;
            }
        }
    }

    swapchainDirty = false;
    LOG_INFO("Swapchain recreated: ", swapchainExtent.width, "x", swapchainExtent.height);
    return true;
}

VkCommandBuffer VkContext::beginFrame(uint32_t& imageIndex) {
    auto& frame = frames[currentFrame];

    // Wait for this frame's fence
    vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    // Acquire next swapchain image
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
        frame.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainDirty = true;
        return VK_NULL_HANDLE;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("Failed to acquire swapchain image");
        return VK_NULL_HANDLE;
    }

    vkResetFences(device, 1, &frame.inFlightFence);
    vkResetCommandBuffer(frame.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);

    return frame.commandBuffer;
}

void VkContext::endFrame(VkCommandBuffer cmd, uint32_t imageIndex) {
    vkEndCommandBuffer(cmd);

    auto& frame = frames[currentFrame];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &frame.renderFinishedSemaphore;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS) {
        LOG_ERROR("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchainDirty = true;
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkCommandBuffer VkContext::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = immCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    return cmd;
}

void VkContext::endSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, immFence);
    vkWaitForFences(device, 1, &immFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &immFence);

    vkFreeCommandBuffers(device, immCommandPool, 1, &cmd);
}

void VkContext::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function) {
    VkCommandBuffer cmd = beginSingleTimeCommands();
    function(cmd);
    endSingleTimeCommands(cmd);
}

} // namespace rendering
} // namespace wowee
