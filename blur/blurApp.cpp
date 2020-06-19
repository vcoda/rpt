#include "vkApp.h"
#include "bezierMesh.h"

class BlurApp : public VkApp
{
    struct Framebuffer
    {
        std::shared_ptr<magma::ColorAttachment2D> color;
        std::shared_ptr<magma::ImageView> colorView;
        std::shared_ptr<magma::DepthStencilAttachment2D> depth;
        std::shared_ptr<magma::ImageView> depthView;
        std::shared_ptr<magma::RenderPass> renderPass;
        std::shared_ptr<magma::Framebuffer> framebuffer;
    } fb;

    std::unique_ptr<BezierPatchMesh> mesh;
    rapid::matrix viewProj;

    std::shared_ptr<magma::VertexBuffer> quad;

    std::shared_ptr<magma::UniformBuffer<rapid::matrix>> uniformTransform;
    std::shared_ptr<magma::Sampler> textureSampler;

    std::shared_ptr<magma::DescriptorPool> descriptorPool;

    std::shared_ptr<magma::DescriptorSetLayout> teapotDescriptorSetLayout;
    std::shared_ptr<magma::DescriptorSet> teapotDescriptorSet;
    std::shared_ptr<magma::PipelineLayout> teapotPipelineLayout;

    std::shared_ptr<magma::DescriptorSetLayout> blurDescriptorSetLayout;
    std::shared_ptr<magma::DescriptorSet> blurDescriptorSet;
    std::shared_ptr<magma::PipelineLayout> blurPipelineLayout;

    std::shared_ptr<magma::GraphicsPipeline> checkerboardPipeline;
    std::shared_ptr<magma::GraphicsPipeline> teapotPipeline;
    std::shared_ptr<magma::GraphicsPipeline> blurPipeline;

    std::shared_ptr<magma::CommandBuffer> offscreenCommandBuffer;
    std::shared_ptr<magma::Semaphore> offscreenSemaphore;

public:
    BlurApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height):
        VkApp(instance, wnd, width, height)
    {
        createFramebuffer();
        createQuadMesh();
        createTeapotMesh();
        createUniformBuffers();
        createTextureSampler();
        createDescriptorSets();
        createCheckerboardPipeline();
        createTeapotPipeline();
        createBlurPipeline();
        recordOffscreenCommandBuffer();
        recordCommandBuffer(0);
        recordCommandBuffer(1);
        setupView();
    }

    void onRender(uint32_t bufferIndex) override
    {
        updatePerspectiveTransform();
        queue->submit(offscreenCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            presentFinished, // Wait for swapchain
            offscreenSemaphore,
            nullptr);
        queue->submit(commandBuffers[bufferIndex], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            offscreenSemaphore, // Wait for offscreen pass
            renderFinished,
            waitFences[bufferIndex]);
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

    void createFramebuffer()
    {
        const VkExtent2D extent{width, height};
        // Create color attachment
        fb.color = std::make_shared<magma::ColorAttachment2D>(device, VK_FORMAT_R8G8B8A8_UNORM, extent, 1, 1);
        fb.colorView = std::make_shared<magma::ImageView>(fb.color);
        // Create depth attachment
        const VkFormat depthFormat = getSupportedDepthFormat(physicalDevice, false, true);
        fb.depth = std::make_shared<magma::DepthStencilAttachment2D>(device, depthFormat, extent, 1, 1);
        fb.depthView = std::make_shared<magma::ImageView>(fb.depth);

        // Define that color attachment don't care about clear, can store shader output and should be read-only image
        const magma::AttachmentDescription colorAttachment(fb.color->getFormat(), 1, magma::attachments::colorDontCareStoreShaderReadOnly);
        // Define that depth attachment can be cleared and can store shader output
        const magma::AttachmentDescription depthAttachment(fb.depth->getFormat(), 1, magma::attachments::depthClearStoreAttachment);

        // Render pass defines attachment formats, load/store operations and final layouts
        fb.renderPass = std::shared_ptr<magma::RenderPass>(new magma::RenderPass(
            device, {colorAttachment, depthAttachment}));
        // Framebuffer defines render pass, color/depth/stencil image views and dimensions
        fb.framebuffer = std::shared_ptr<magma::Framebuffer>(new magma::Framebuffer(
            fb.renderPass, {fb.colorView, fb.depthView}));
    }

    void createQuadMesh()
    {
        const std::vector<rapid::float2> vertices = {
            {-1.f, -1.f}, {-1.f, 1.f}, {1.f, -1.f}, {1.f, 1.f}
        };
        quad = std::make_shared<magma::VertexBuffer>(cmdBufferCopy, vertices);
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

    void createTextureSampler()
    {
        textureSampler = std::make_shared<magma::Sampler>(device, magma::samplers::magMinMipNearestClampToEdge);
    }

    void createDescriptorSets()
    {
        constexpr magma::Descriptor oneUniformBuffer = magma::descriptors::UniformBuffer(1);
        constexpr magma::Descriptor oneImageSampler = magma::descriptors::CombinedImageSampler(1);

        constexpr uint32_t maxDescriptorSets = 2;
        descriptorPool = std::shared_ptr<magma::DescriptorPool>(new magma::DescriptorPool(device, maxDescriptorSets,
            {
                oneUniformBuffer,
                oneImageSampler
            }));

        teapotDescriptorSetLayout = std::make_shared<magma::DescriptorSetLayout>(device,
            magma::bindings::VertexStageBinding(0, oneUniformBuffer));
        teapotDescriptorSet = descriptorPool->allocateDescriptorSet(teapotDescriptorSetLayout);
        teapotDescriptorSet->update(0, uniformTransform);
        teapotPipelineLayout = std::make_shared<magma::PipelineLayout>(teapotDescriptorSetLayout);

        blurDescriptorSetLayout = std::make_shared<magma::DescriptorSetLayout>(device,
            magma::bindings::FragmentStageBinding(0, oneImageSampler));
        blurDescriptorSet = descriptorPool->allocateDescriptorSet(blurDescriptorSetLayout);
        blurDescriptorSet->update(0, fb.colorView, textureSampler);
        blurPipelineLayout = std::make_shared<magma::PipelineLayout>(blurDescriptorSetLayout);
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
            fb.renderPass, 0,
            pipelineCache,
            nullptr, nullptr, 0);
    }

    void createTeapotPipeline()
    {
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
            fb.renderPass, 0,
            pipelineCache,
            nullptr, nullptr, 0);
    }

    void createBlurPipeline()
    {

        blurPipeline = std::make_shared<magma::GraphicsPipeline>(device,
            std::vector<magma::PipelineShaderStage>{
                loadShader("shaders/passthrough.o"),
                loadShader("shaders/blur.o")
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
            blurPipelineLayout,
            renderPass, 0,
            pipelineCache,
            nullptr, nullptr, 0);
    }

    void recordOffscreenCommandBuffer()
    {
        offscreenSemaphore = std::make_shared<magma::Semaphore>(device);
        offscreenCommandBuffer = std::make_shared<magma::PrimaryCommandBuffer>(commandPools[0]);
        offscreenCommandBuffer->begin();
        {
            offscreenCommandBuffer->beginRenderPass(fb.renderPass, fb.framebuffer,
                {
                    // Only elements corresponding to cleared attachments are used. Other elements of pClearValues are ignored.
                    magma::clears::blackColor,
                    magma::clears::depthOne
                });
            {
                offscreenCommandBuffer->setViewport(0, 0, width, height);
                offscreenCommandBuffer->setScissor(0, 0, width, height);
                // Draw checkerboard
                offscreenCommandBuffer->bindPipeline(checkerboardPipeline);
                offscreenCommandBuffer->bindVertexBuffer(0, quad);
                offscreenCommandBuffer->draw(4, 0);
                // Draw teapot mesh
                offscreenCommandBuffer->bindDescriptorSet(teapotPipeline, teapotDescriptorSet);
                offscreenCommandBuffer->bindPipeline(teapotPipeline);
                mesh->draw(offscreenCommandBuffer);
            }
            offscreenCommandBuffer->endRenderPass();
        }
        offscreenCommandBuffer->end();
    }

    void recordCommandBuffer(uint32_t index)
    {
        std::shared_ptr<magma::CommandBuffer> cmdBuffer = commandBuffers[index];
        cmdBuffer->begin();
        {
            cmdBuffer->beginRenderPass(renderPass, framebuffers[index], {/* don't clear */});
            {
                cmdBuffer->setViewport(0, 0, width, height);
                cmdBuffer->setScissor(0, 0, width, height);
                // Blur offscreen texture
                cmdBuffer->bindDescriptorSet(blurPipeline, blurDescriptorSet);
                cmdBuffer->bindPipeline(blurPipeline);
                cmdBuffer->bindVertexBuffer(0, quad);
                cmdBuffer->draw(4, 0);
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
