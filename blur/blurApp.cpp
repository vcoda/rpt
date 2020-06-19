#include <fstream>
#include "vkApp.h"
#include "bezierMesh.h"

class BlurApp : public VkApp
{
    std::unique_ptr<BezierPatchMesh> mesh;

    std::shared_ptr<magma::UniformBuffer<rapid::matrix>> uniformTransform;
    std::shared_ptr<magma::DescriptorPool> descriptorPool;
    std::shared_ptr<magma::DescriptorSetLayout> teapotDescriptorSetLayout;
    std::shared_ptr<magma::DescriptorSet> teapotDescriptorSet;

public:
    BlurApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height):
        VkApp(instance, wnd, width, height)
    {
        createTeapotMesh();
        createUniformBuffers();
        createDescriptorSets();

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

    void createDescriptorSets()
    {
        constexpr magma::Descriptor oneUniformBuffer = magma::descriptors::UniformBuffer(1);

        constexpr uint32_t maxDescriptorSets = 1;
        descriptorPool = std::make_shared<magma::DescriptorPool>(device, maxDescriptorSets, oneUniformBuffer);

        teapotDescriptorSetLayout = std::make_shared<magma::DescriptorSetLayout>(device,
            magma::bindings::VertexStageBinding(0, oneUniformBuffer));

        teapotDescriptorSet = descriptorPool->allocateDescriptorSet(teapotDescriptorSetLayout);
        teapotDescriptorSet->update(0, uniformTransform);
    }

    magma::PipelineShaderStage loadShader(const char *fileName) const
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
