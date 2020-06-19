#include "vkApp.h"

class BlurApp : public VkApp
{
public:
    BlurApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height):
        VkApp(instance, wnd, width, height)
    {
        int i = 0;
        for (auto& cmdBuffer : commandBuffers)
        {
            cmdBuffer->begin();
            {
                constexpr magma::ClearColor clearColor(0.35f, 0.53f, 0.7f, 1.0f);
                cmdBuffer->beginRenderPass(renderPass, framebuffers[i],
                    {clearColor}); // Set our clear color
                cmdBuffer->endRenderPass();
            }
            cmdBuffer->end();
            ++i;
        }
    }

    virtual void onRender(uint32_t bufferIndex) override
    {   // Submit commant buffer for execution
        queue->submit(
            commandBuffers[bufferIndex],
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            presentFinished,
            renderFinished,
            waitFences[bufferIndex]);
    }
};

std::unique_ptr<VkApp> createVulkanApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height)
{
    return std::make_unique<BlurApp>(instance, wnd, width, height);
}
