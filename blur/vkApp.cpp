#include <sstream>
#include <fstream>
#include <iostream>
#include "vkApp.h"

#ifndef _WIN64
void *VkApp::operator new(size_t size)
{
    void *ptr = _mm_malloc(size, 16);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void VkApp::operator delete(void *ptr) noexcept
{
    _mm_free(ptr);
}
#endif // _WIN64

VkApp::VkApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height):
    width(width),
    height(height)
{
    createInstance();
    createLogicalDevice();
    createSwapchain(instance, wnd, false);
    createRenderPass();
    createFramebuffer();
    createCommandBuffers();
    createSyncPrimitives();
    pipelineCache = std::make_shared<magma::PipelineCache>(device);
}

VkApp::~VkApp()
{
}

void VkApp::render()
{
    const uint32_t bufferIndex = swapchain->acquireNextImage(presentFinished, nullptr);
    waitFences[bufferIndex]->wait();
    waitFences[bufferIndex]->reset();
    {
        onRender(bufferIndex);
    }
    queue->present(swapchain, bufferIndex, renderFinished);
    device->waitIdle(); // Flush
}

void VkApp::onKeyDown(char key, int repeat, uint32_t flags)
{
}

void VkApp::createInstance()
{
    const std::vector<const char *> layerNames = {
#ifdef _DEBUG
        "VK_LAYER_LUNARG_standard_validation"
#endif
    };
    const std::vector<const char *> extensionNames = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#ifdef _DEBUG
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#endif
    };

    instance = std::make_shared<magma::Instance>(
        "vkApp",
        "Magma",
        VK_API_VERSION_1_0,
        layerNames, extensionNames);

    debugReportCallback = std::make_shared<magma::DebugReportCallback>(
        instance,
        VkApp::reportCallback);

    physicalDevice = instance->getPhysicalDevice(0);
    const VkPhysicalDeviceProperties& properties = physicalDevice->getProperties();
    std::cout << "Running on " << properties.deviceName << "\n";

    instanceExtensions = std::make_unique<magma::InstanceExtensions>();
    extensions = std::make_unique<magma::PhysicalDeviceExtensions>(physicalDevice);
}

void VkApp::createLogicalDevice()
{
    const std::vector<float> defaultQueuePriorities = {1.0f};
    const magma::DeviceQueueDescriptor graphicsQueue(physicalDevice, VK_QUEUE_GRAPHICS_BIT, defaultQueuePriorities);
    const magma::DeviceQueueDescriptor transferQueue(physicalDevice, VK_QUEUE_TRANSFER_BIT, defaultQueuePriorities);
    std::vector<magma::DeviceQueueDescriptor> queueDescriptors;
    queueDescriptors.push_back(graphicsQueue);
    if (transferQueue.queueFamilyIndex != graphicsQueue.queueFamilyIndex)
        queueDescriptors.push_back(transferQueue);

    // Enable some widely used features
    VkPhysicalDeviceFeatures features = {0};
    features.fillModeNonSolid = VK_TRUE;
    features.samplerAnisotropy = VK_TRUE;
    features.textureCompressionBC = VK_TRUE;
    features.occlusionQueryPrecise = VK_TRUE;

    std::vector<const char*> enabledExtensions;
    enabledExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (extensions->AMD_negative_viewport_height)
        enabledExtensions.push_back(VK_AMD_NEGATIVE_VIEWPORT_HEIGHT_EXTENSION_NAME);
    else if (extensions->KHR_maintenance1)
        enabledExtensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);

    const std::vector<const char*> noLayers;
    device = physicalDevice->createDevice(queueDescriptors, noLayers, enabledExtensions, features);
}

void VkApp::createSwapchain(HINSTANCE hInstance, HWND wnd, bool vSync)
{
    surface = std::make_shared<magma::Win32Surface>(instance, hInstance, wnd);
    if (!physicalDevice->getSurfaceSupport(surface))
        throw std::runtime_error("surface not supported");
    // Get surface caps
    VkSurfaceCapabilitiesKHR surfaceCaps;
    surfaceCaps = physicalDevice->getSurfaceCapabilities(surface);
    assert(surfaceCaps.currentExtent.width == width);
    assert(surfaceCaps.currentExtent.height == height);
    // Find supported transform flags
    VkSurfaceTransformFlagBitsKHR preTransform;
    if (surfaceCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    else
        preTransform = surfaceCaps.currentTransform;
    // Find supported alpha composite
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[] =
    {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (auto alphaFlag : compositeAlphaFlags)
    {
        if (surfaceCaps.supportedCompositeAlpha & alphaFlag)
        {
            compositeAlpha = alphaFlag;
            break;
        }
    }
    const std::vector<VkSurfaceFormatKHR> surfaceFormats = physicalDevice->getSurfaceFormats(surface);
    // Choose available present mode
    const std::vector<VkPresentModeKHR> presentModes = physicalDevice->getSurfacePresentModes(surface);
    VkPresentModeKHR presentMode;
    if (vSync)
        presentMode = VK_PRESENT_MODE_FIFO_KHR;
    else
    {   // Search for first appropriate present mode
        const VkPresentModeKHR modes[] =
        {
            VK_PRESENT_MODE_IMMEDIATE_KHR,  // AMD
            VK_PRESENT_MODE_MAILBOX_KHR,    // NVidia, Intel
            VK_PRESENT_MODE_FIFO_RELAXED_KHR
        };
        bool found = false;
        for (auto mode : modes)
        {
            if (std::find(presentModes.begin(), presentModes.end(), mode)
                != presentModes.end())
            {
                presentMode = mode;
                found = true;
                break;
            }
        }
        if (!found)
        {   // Must always be present
            presentMode = VK_PRESENT_MODE_FIFO_KHR;
        }
    }
    swapchain = std::make_shared<magma::Swapchain>(device, surface,
        std::min(2U, surfaceCaps.maxImageCount),
        surfaceFormats[0], surfaceCaps.currentExtent,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // Allow screenshots
        preTransform, compositeAlpha, presentMode, 0,
        nullptr, debugReportCallback);
}

void VkApp::createRenderPass()
{
    const std::vector<VkSurfaceFormatKHR> surfaceFormats = physicalDevice->getSurfaceFormats(surface);
    const magma::AttachmentDescription colorAttachment(surfaceFormats[0].format, 1,
        magma::op::clearStore, // Clear color, store
        magma::op::dontCare, // Stencil don't care
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    const VkFormat depthFormat = getSupportedDepthFormat(physicalDevice, false, true);
    const magma::AttachmentDescription depthStencilAttachment(depthFormat, 1,
        magma::op::clearStore, // Clear depth, store
        magma::op::clearDontCare, // Stencil don't care
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    const std::initializer_list<magma::AttachmentDescription> attachments = {colorAttachment, depthStencilAttachment};
    renderPass = std::make_shared<magma::RenderPass>(device, attachments);
}

void VkApp::createFramebuffer()
{
    const VkSurfaceCapabilitiesKHR surfaceCaps = physicalDevice->getSurfaceCapabilities(surface);
    const VkFormat depthFormat = getSupportedDepthFormat(physicalDevice, false, true);
    depthStencil = std::make_shared<magma::DepthStencilAttachment2D>(device, depthFormat, surfaceCaps.currentExtent, 1, 1);
    depthStencilView = std::make_shared<magma::ImageView>(depthStencil);

    for (const auto& image : swapchain->getImages())
    {
        std::vector<std::shared_ptr<magma::ImageView>> attachments;
        std::shared_ptr<magma::ImageView> colorView(std::make_shared<magma::ImageView>(image));
        attachments.push_back(colorView);
        attachments.push_back(depthStencilView);
        std::shared_ptr<magma::Framebuffer> framebuffer(std::make_shared<magma::Framebuffer>(renderPass, attachments));
        framebuffers.push_back(framebuffer);
    }
}

void VkApp::createCommandBuffers()
{
    queue = device->getQueue(VK_QUEUE_GRAPHICS_BIT, 0);
    commandPools[0] = std::make_shared<magma::CommandPool>(device, queue->getFamilyIndex());
    // Create draw command buffers
    commandBuffers = commandPools[0]->allocateCommandBuffers(static_cast<uint32_t>(framebuffers.size()), true);
    // Create image copy command buffer
    cmdImageCopy = std::make_shared<magma::PrimaryCommandBuffer>(commandPools[0]);
    try
    {
        std::shared_ptr<magma::Queue> transferQueue = device->getQueue(VK_QUEUE_TRANSFER_BIT, 0);
        commandPools[1] = std::make_shared<magma::CommandPool>(device, transferQueue->getFamilyIndex());
        // Create buffer copy command buffer
        cmdBufferCopy = std::make_shared<magma::PrimaryCommandBuffer>(commandPools[1]);
    }
    catch (...)
    {
        std::cout << "Transfer queue not present\n";
    }
}

void VkApp::createSyncPrimitives()
{
    presentFinished = std::make_shared<magma::Semaphore>(device);
    renderFinished = std::make_shared<magma::Semaphore>(device);
    for (int i = 0; i < (int)commandBuffers.size(); ++i)
    {
        constexpr bool signaled = true; // Don't wait on first render of each command buffer
        waitFences.push_back(std::make_shared<magma::Fence>(device, signaled));
    }
}

VkFormat VkApp::getSupportedDepthFormat(std::shared_ptr<magma::PhysicalDevice> physicalDevice, bool hasStencil, bool optimalTiling)
{
    for (VkFormat format : {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM})
    {
        const magma::Format fmt(format);
        if (hasStencil && !fmt.depthStencil())
            continue;
        if (!hasStencil && !fmt.depth())
            continue;
        const VkFormatProperties properties = physicalDevice->getFormatProperties(format);
        if (optimalTiling && (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
            return format;
        else if (!optimalTiling && (properties.linearTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
            return format;
    }
    return VK_FORMAT_UNDEFINED;
}

magma::PipelineShaderStage VkApp::loadShader(const char *fileName) const
{
    std::ifstream file(fileName, std::ios::in | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("file \"" + std::string(fileName) + "\" not found");

    std::vector<char> bytecode((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytecode.size() % sizeof(magma::SpirvWord))
        throw std::runtime_error("size of \"" + std::string(fileName) + "\" bytecode must be a multiple of SPIR-V word");

    std::shared_ptr<magma::ShaderModule> module = std::make_shared<magma::ShaderModule>(device,
        reinterpret_cast<const magma::SpirvWord *>(bytecode.data()), bytecode.size(),
        0, 0, true, device->getAllocator());

    const VkShaderStageFlagBits stage = module->getReflection()->getShaderStage();
    const char *const entrypoint = module->getReflection()->getEntryPointName(0);
    return magma::PipelineShaderStage(stage, std::move(module), entrypoint);
}

bool VkApp::submitCommandBuffer(uint32_t bufferIndex)
{
    return queue->submit(commandBuffers[bufferIndex],
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        presentFinished,
        renderFinished,
        waitFences[bufferIndex]);
}

VkBool32 VKAPI_PTR VkApp::reportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
    uint64_t object, size_t location, int32_t messageCode,
    const char *pLayerPrefix, const char *pMessage, void *pUserData)
{
    if (strstr(pMessage, "Extension"))
        return VK_FALSE;
    std::stringstream msg;
    msg << "[" << pLayerPrefix << "] " << pMessage << "\n";
    if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
        std::cerr << msg.str();
    else
        std::cout << msg.str();
    return VK_FALSE;
}
