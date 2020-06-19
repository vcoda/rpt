#pragma once
#include "../magma/magma.h"
#include "../rapid/rapid.h"

class VkApp
{
public:
#ifndef _WIN64
    // For 16-byte alignment in heap
    void *operator new(size_t size);
    void operator delete(void *ptr) noexcept;
#endif

    VkApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height);
    virtual ~VkApp();
    void render();
    virtual void onKeyDown(char key, int repeat, uint32_t flags);

protected:
    virtual void onRender(uint32_t bufferIndex) = 0;
    bool submitCommandBuffer(uint32_t bufferIndex);
    VkFormat getSupportedDepthFormat(std::shared_ptr<magma::PhysicalDevice> physicalDevice,
        bool hasStencil, bool optimalTiling);

private:
    void createInstance();
    void createLogicalDevice();
    void createSwapchain(HINSTANCE instance, HWND wnd, bool vSync);
    void createRenderPass();
    void createFramebuffer();
    void createCommandBuffers();
    void createSyncPrimitives();

    static VkBool32 VKAPI_PTR reportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
        uint64_t object, size_t location, int32_t messageCode, const char *pLayerPrefix, const char *pMessage, void *pUserData);

protected:
    uint32_t width;
    uint32_t height;

    std::shared_ptr<magma::Instance> instance;
    std::shared_ptr<magma::DebugReportCallback> debugReportCallback;
    std::shared_ptr<magma::Surface> surface;
    std::shared_ptr<magma::PhysicalDevice> physicalDevice;
    std::shared_ptr<magma::Device> device;
    std::shared_ptr<magma::Swapchain> swapchain;
    std::unique_ptr<magma::InstanceExtensions> instanceExtensions;
    std::unique_ptr<magma::PhysicalDeviceExtensions> extensions;

    std::shared_ptr<magma::CommandPool> commandPools[2];
    std::vector<std::shared_ptr<magma::CommandBuffer>> commandBuffers;
    std::shared_ptr<magma::CommandBuffer> cmdImageCopy;
    std::shared_ptr<magma::CommandBuffer> cmdBufferCopy;

    std::shared_ptr<magma::DepthStencilAttachment2D> depthStencil;
    std::shared_ptr<magma::ImageView> depthStencilView;
    std::shared_ptr<magma::RenderPass> renderPass;
    std::vector<std::shared_ptr<magma::Framebuffer>> framebuffers;
    std::shared_ptr<magma::Queue> queue;
    std::shared_ptr<magma::Semaphore> presentFinished;
    std::shared_ptr<magma::Semaphore> renderFinished;
    std::vector<std::shared_ptr<magma::Fence>> waitFences;

    std::shared_ptr<magma::PipelineCache> pipelineCache;
};
