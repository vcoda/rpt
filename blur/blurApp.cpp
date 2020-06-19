#include "vkApp.h"
#include "bezierMesh.h"

class BlurApp : public VkApp
{
    std::unique_ptr<BezierPatchMesh> mesh;

    std::shared_ptr<magma::UniformBuffer<rapid::matrix>> uniformTransform;

public:
    BlurApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height):
        VkApp(instance, wnd, width, height)
    {
        createTeapotMesh();
        createUniformBuffers();

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

private:
    void createTeapotMesh()
    {
#       include "teapot.h"
        constexpr uint32_t subdivisionDegree = 16;
        mesh = std::make_unique<BezierPatchMesh>(teapotPatches, kTeapotNumPatches, teapotVertices, subdivisionDegree, cmdBufferCopy);
    }

    void createUniformBuffers()
    {
        uniformTransform = std::make_shared<magma::UniformBuffer<rapid::matrix>>(device);
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
