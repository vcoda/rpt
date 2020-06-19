#include <fstream>
#include "vkApp.h"
#include "bezierMesh.h"

class BlurApp : public VkApp
{
    std::unique_ptr<BezierPatchMesh> mesh;
    rapid::matrix viewProj;

    std::shared_ptr<magma::UniformBuffer<rapid::matrix>> uniformTransform;
    std::shared_ptr<magma::DescriptorPool> descriptorPool;
    std::shared_ptr<magma::DescriptorSetLayout> teapotDescriptorSetLayout;
    std::shared_ptr<magma::DescriptorSet> teapotDescriptorSet;

    std::shared_ptr<magma::PipelineLayout> teapotPipelineLayout;
    std::shared_ptr<magma::GraphicsPipeline> checkerboardPipeline;
    std::shared_ptr<magma::GraphicsPipeline> teapotPipeline;

public:
    BlurApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height):
        VkApp(instance, wnd, width, height)
    {
        setupView();
        createTeapotMesh();
        createUniformBuffers();
        createDescriptorSets();
        createCheckerboardPipeline();
        createTeapotPipeline();
        recordCommandBuffer(0);
        recordCommandBuffer(1);
    }

    void onRender(uint32_t bufferIndex) override
    {
        updatePerspectiveTransform();
        submitCommandBuffer(bufferIndex);
    }

private:
    void setupView()
    {
        const rapid::vector3 eye(0.f, 3.f, 8.f);
        const rapid::vector3 center(0.f, 2.f, 0.f);
        const rapid::vector3 up(0.f, 1.f, 0.f);
        constexpr float fov = rapid::radians(60.f);
        const float aspect = width/(float)height;
        constexpr float zn = 1.f, zf = 100.f;
        const rapid::matrix view = rapid::lookAtRH(eye, center, up);
        const rapid::matrix proj = rapid::perspectiveFovRH(fov, aspect, zn, zf);
        viewProj = view * proj;
    }

    void updatePerspectiveTransform()
    {
        const rapid::matrix world = rapid::identity();
        magma::helpers::mapScoped<rapid::matrix>(uniformTransform, true, [this, &world](auto *worldViewProj)
        {
            *worldViewProj = world * viewProj;
        });
    }

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

    void createCheckerboardPipeline()
    {
        checkerboardPipeline = std::make_shared<magma::GraphicsPipeline>(device,
            std::vector<magma::PipelineShaderStage>{
                loadShader("shaders/passthrough.o"),
                loadShader("shaders/checkerboard.o")
            },
            magma::renderstates::pos2f,
            magma::renderstates::triangleStrip,
            magma::renderstates::fillCullNoneCW,
            magma::renderstates::dontMultisample,
            magma::renderstates::depthAlwaysDontWrite,
            magma::renderstates::dontBlendRgb,
            std::initializer_list<VkDynamicState>{
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            },
            nullptr,
            renderPass, 0,
            pipelineCache,
            nullptr, nullptr, 0);
    }

    void createTeapotPipeline()
    {
        teapotPipelineLayout = std::make_shared<magma::PipelineLayout>(teapotDescriptorSetLayout);
        teapotPipeline = std::make_shared<magma::GraphicsPipeline>(device,
            std::vector<magma::PipelineShaderStage>{
                loadShader("shaders/transform.o"),
                loadShader("shaders/teapot.o")
            },
            mesh->getVertexInput(),
            magma::renderstates::triangleList,
            magma::renderstates::fillCullBackCW,
            magma::renderstates::dontMultisample,
            magma::renderstates::depthLessOrEqual,
            magma::renderstates::dontBlendRgb,
            std::initializer_list<VkDynamicState>{
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            },
            teapotPipelineLayout,
            renderPass, 0,
            pipelineCache,
            nullptr, nullptr, 0);
    }

    void recordCommandBuffer(uint32_t index)
    {
        std::shared_ptr<magma::CommandBuffer> cmdBuffer = commandBuffers[index];
        cmdBuffer->begin();
        {
            cmdBuffer->beginRenderPass(renderPass, framebuffers[index],
                {
                    magma::clears::grayColor,
                    magma::clears::depthOne
                });
            {
                cmdBuffer->setViewport(0, 0, width, height);
                cmdBuffer->setScissor(0, 0, width, height);
                cmdBuffer->bindDescriptorSet(teapotPipeline, teapotDescriptorSet);
                cmdBuffer->bindPipeline(teapotPipeline);
                mesh->draw(cmdBuffer);
            }
            cmdBuffer->endRenderPass();
        }
        cmdBuffer->end();
    }
};

std::unique_ptr<VkApp> createVulkanApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height)
{
    return std::make_unique<BlurApp>(instance, wnd, width, height);
}
