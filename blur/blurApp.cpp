#include <fstream>
#include <chrono>
#include "vkApp.h"
#include "bezierMesh.h"
#include "../gliml/gliml.h"

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

    struct Texture
    {
        std::shared_ptr<magma::Image2D> image;
        std::shared_ptr<magma::ImageView> imageView;
    } texture;

    struct alignas(16) Transforms
    {
        rapid::matrix normal;
        rapid::matrix view;
        rapid::matrix worldView;
        rapid::matrix worldViewProj;
    };

    struct alignas(16) Material
    {
        rapid::float4 ambient;
        rapid::float4 diffuse;
        rapid::float3 specular;
        float shininess;
    };

    std::unique_ptr<BezierPatchMesh> mesh;
    rapid::matrix view;
    rapid::matrix viewProj;
    std::chrono::high_resolution_clock::time_point oldTime;

    std::shared_ptr<magma::VertexBuffer> quad;
    std::shared_ptr<magma::UniformBuffer<Transforms>> uniformTransform;
    std::shared_ptr<magma::UniformBuffer<Material>> uniformMaterials;
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
    std::shared_ptr<magma::GraphicsPipeline> blitPipeline;
    std::shared_ptr<magma::GraphicsPipeline> blurPipeline;

    std::shared_ptr<magma::CommandBuffer> offscreenCommandBuffer;
    std::shared_ptr<magma::Semaphore> offscreenSemaphore;

public:
    explicit BlurApp(HINSTANCE instance, HWND wnd, uint32_t width, uint32_t height):
        VkApp(instance, wnd, width, height)
    {
        createFramebuffer();
        loadTexture("textures/stonewall.dds");
        createQuadMesh();
        createTeapotMesh();
        createUniformBuffers();
        createTextureSampler();
        createDescriptorSets();
        createCheckerboardPipeline();
        createTeapotPipeline();
        createBlitPipeline();
        createBlurPipeline();
        recordOffscreenCommandBuffer();
        recordCommandBuffer(0);
        recordCommandBuffer(1);
        setupMaterials();
        setupView();
        oldTime = std::chrono::high_resolution_clock::now();
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
    void setupMaterials()
    {
        magma::helpers::mapScoped<Material>(uniformMaterials, true, [this](auto *materials)
        {
            constexpr int light = 0;
            constexpr int surface = 1;

            materials[light].ambient = rapid::float4(0.25f, 0.25f, 0.25f, 0.f);
            materials[light].diffuse = rapid::float4(1.0f, 0.9f, 0.8f, 0.f);
            materials[light].specular = rapid::float3(1.0f, 0.8f, 0.5f);

            materials[surface].ambient = rapid::float4(0.25f, 0.25f, 0.25f, 0.f);
            materials[surface].diffuse = rapid::float4(1.f, 1.f, 1.f, 0.f);
            materials[surface].specular = rapid::float3(1.f, 1.f, 1.f);
            materials[surface].shininess = 4.0f; // Metallic
        });
    }

    void setupView()
    {
        const rapid::vector3 eye(0.f, 1.f, 7.f);
        const rapid::vector3 center(0.f, 0.f, 0.f);
        const rapid::vector3 up(0.f, 1.f, 0.f);
        constexpr float fov = rapid::radians(60.f);
        const float aspect = width/(float)height;
        constexpr float zn = 1.f, zf = 100.f;
        const rapid::matrix proj = rapid::perspectiveFovRH(fov, aspect, zn, zf);
        view = rapid::lookAtRH(eye, center, up);
        viewProj = view * proj;
    }

    void updatePerspectiveTransform()
    {
        static float angle = 0.f;

        // Compute elapsed milliseconds
        const auto curTime = std::chrono::high_resolution_clock::now();
        const auto mcs = std::chrono::duration_cast<std::chrono::microseconds>(curTime - oldTime);
        const float ms = static_cast<float>(mcs.count()) * 0.001f;
        oldTime = curTime;

        angle += rapid::radians(ms * 0.03f);
        const rapid::matrix pitch = rapid::rotationX(angle);
        const rapid::matrix yaw = rapid::rotationY(angle);
        const rapid::matrix roll = rapid::rotationZ(angle);
        const rapid::matrix offset = rapid::translation(0.f, -1.5f, 0.f);
        const rapid::matrix world = offset * pitch * yaw * roll;
        const rapid::matrix worldView = world * view;
        const rapid::matrix worldViewInv = rapid::inverse(worldView);
        const rapid::matrix normal = rapid::transpose(worldViewInv);

        magma::helpers::mapScoped<Transforms>(uniformTransform, true, [this, &normal, &world, &worldView](auto *transforms)
        {
            transforms->normal = normal; // Normal matrix used to transform objects-space normal in view space
            transforms->view = this->view;
            transforms->worldView = worldView;
            transforms->worldViewProj = world * this->viewProj;
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

    VkFormat bcFormat(const gliml::context& ctx)
    {
        const int internalFormat = ctx.image_internal_format();
        switch (internalFormat)
        {
        case GLIML_GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case GLIML_GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case GLIML_GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        default:
            throw std::invalid_argument("unknown block compressed format");
            return VK_FORMAT_UNDEFINED;
        }
    }

    void loadTexture(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::in | std::ios::binary | std::ios::ate);
        if (!file.is_open())
            throw std::runtime_error("failed to open file \"" + filename + "\"");
        const std::streamoff size = file.tellg();
        file.seekg(0, std::ios::beg);
        gliml::context ctx;
        VkDeviceSize baseMipOffset = 0;
        std::shared_ptr<magma::SrcTransferBuffer> buffer = std::make_shared<magma::SrcTransferBuffer>(device, size);
        magma::helpers::mapScoped<uint8_t>(buffer, [&](uint8_t *data)
        {   // Read data to buffer
            file.read(reinterpret_cast<char *>(data), size);
            file.close();
            ctx.enable_dxt(true);
            if (!ctx.load(data, static_cast<unsigned>(size)))
                throw std::runtime_error("failed to load DDS texture");
            // Skip DDS header
            baseMipOffset = reinterpret_cast<const uint8_t *>(ctx.image_data(0, 0)) - data;
        });
        // Setup texture data description
        const VkFormat format = bcFormat(ctx);
        const VkExtent2D extent = {static_cast<uint32_t>(ctx.image_width(0, 0)), static_cast<uint32_t>(ctx.image_height(0, 0))};
        magma::Image::MipmapLayout mipOffsets(1, 0);
        for (int level = 1; level < ctx.num_mipmaps(0); ++level)
        {   // Compute relative offset
            const intptr_t mipOffset = (const uint8_t *)ctx.image_data(0, level) - (const uint8_t *)ctx.image_data(0, level - 1);
            mipOffsets.push_back(mipOffset);
        }
        // Upload texture data from buffer
        magma::Image::CopyLayout bufferLayout{baseMipOffset, 0, 0};
        texture.image = std::make_shared<magma::Image2D>(cmdImageCopy, format, extent, buffer, mipOffsets, bufferLayout);
        // Create image view for shader
        texture.imageView = std::make_shared<magma::ImageView>(texture.image);
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
        uniformTransform = std::make_shared<magma::UniformBuffer<Transforms>>(device);
        uniformMaterials = std::make_shared<magma::UniformBuffer<Material>>(device, 2); // Allocate two materials
    }

    void createTextureSampler()
    {
        // Create trilinear sampler
        textureSampler = std::make_shared<magma::Sampler>(device, magma::samplers::magMinMipLinearClampToEdge);
    }

    void createDescriptorSets()
    {
        constexpr magma::Descriptor oneUniformBuffer = magma::descriptors::UniformBuffer(1);
        constexpr magma::Descriptor oneImageSampler = magma::descriptors::CombinedImageSampler(1);

        constexpr uint32_t maxDescriptorSets = 2;
        descriptorPool = std::shared_ptr<magma::DescriptorPool>(new magma::DescriptorPool(device, maxDescriptorSets,
            {
                magma::descriptors::UniformBuffer(2),
                magma::descriptors::CombinedImageSampler(2)
            }));

        // Create pipeline layout for teapot drawing
        teapotDescriptorSetLayout = std::make_shared<magma::DescriptorSetLayout>(device,
            std::initializer_list<magma::DescriptorSetLayout::Binding>{
                magma::bindings::VertexFragmentStageBinding(0, oneUniformBuffer),
                magma::bindings::FragmentStageBinding(1, oneUniformBuffer),
                magma::bindings::FragmentStageBinding(2, oneImageSampler)
            });
        teapotDescriptorSet = descriptorPool->allocateDescriptorSet(teapotDescriptorSetLayout);
        teapotDescriptorSet->update(0, uniformTransform);
        teapotDescriptorSet->update(1, uniformMaterials);
        teapotDescriptorSet->update(2, texture.imageView, textureSampler);
        teapotPipelineLayout = std::make_shared<magma::PipelineLayout>(teapotDescriptorSetLayout);

        // Create pipeline layout for blur post-effect
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

    void createBlitPipeline()
    {
        blitPipeline = std::make_shared<magma::GraphicsPipeline>(device,
            std::vector<magma::PipelineShaderStage>{
                loadShader("shaders/passthrough.o"),
                loadShader("shaders/blit.o")
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
        const uint32_t halfWidth = width >> 1;

        std::shared_ptr<magma::CommandBuffer> cmdBuffer = commandBuffers[index];
        cmdBuffer->begin();
        {
            cmdBuffer->beginRenderPass(renderPass, framebuffers[index], {/* don't clear */});
            {
                cmdBuffer->setViewport(0, 0, width, height);
                cmdBuffer->bindVertexBuffer(0, quad);

                // Blit left half of the screen
                cmdBuffer->setScissor(0, 0, halfWidth, height);
                cmdBuffer->bindDescriptorSet(blitPipeline, blurDescriptorSet);
                cmdBuffer->bindPipeline(blitPipeline);
                cmdBuffer->draw(4);

                // Blur right half of the screen
                cmdBuffer->setScissor(halfWidth, 0, width, height);
                cmdBuffer->bindDescriptorSet(blurPipeline, blurDescriptorSet);
                cmdBuffer->bindPipeline(blurPipeline);
                cmdBuffer->draw(4);
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
