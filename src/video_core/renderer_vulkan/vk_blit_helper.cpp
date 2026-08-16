// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/hash.h"
#include "common/settings.h"
#include "common/vector_math.h"
#include "video_core/renderer_vulkan/vk_blit_helper.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

#include "video_core/host_shaders/format_reinterpreter/vulkan_d24s8_to_rgba8_comp.h"
#include "video_core/host_shaders/full_screen_triangle_vert.h"
#include "video_core/host_shaders/vulkan_blit_depth_stencil_frag.h"
#include "video_core/host_shaders/vulkan_depth_to_buffer_comp.h"

// Texture filtering shader includes
#include "video_core/host_shaders/texture_filtering/bicubic_frag.h"
#include "video_core/host_shaders/texture_filtering/mmpx_frag.h"
#include "video_core/host_shaders/texture_filtering/refine_frag.h"
#include "video_core/host_shaders/texture_filtering/scale_force_frag.h"
#include "video_core/host_shaders/texture_filtering/x_gradient_frag.h"
#include "video_core/host_shaders/texture_filtering/xbrz_freescale_frag.h"
#include "video_core/host_shaders/texture_filtering/y_gradient_frag.h"
#include "vk_blit_helper.h"

namespace Vulkan {

using Settings::TextureFilter;
using VideoCore::PixelFormat;

namespace {
struct PushConstants {
    std::array<float, 2> tex_scale;
    std::array<float, 2> tex_offset;
};

struct ComputeInfo {
    Common::Vec2i src_offset;
    Common::Vec2i dst_offset;
    Common::Vec2i src_extent;
};

inline constexpr vk::PushConstantRange COMPUTE_PUSH_CONSTANT_RANGE{
    .stageFlags = vk::ShaderStageFlagBits::eCompute,
    .offset = 0,
    .size = sizeof(ComputeInfo),
};

constexpr std::array<vk::DescriptorSetLayoutBinding, 3> COMPUTE_BINDINGS = {{
    {0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
    {1, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
    {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
}};

constexpr std::array<vk::DescriptorSetLayoutBinding, 3> COMPUTE_BUFFER_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
    {2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
}};

constexpr std::array<vk::DescriptorSetLayoutBinding, 2> TWO_TEXTURES_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
}};

// Texture filtering descriptor set bindings
constexpr std::array<vk::DescriptorSetLayoutBinding, 1> SINGLE_TEXTURE_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
}};

constexpr std::array<vk::DescriptorSetLayoutBinding, 3> THREE_TEXTURES_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
}};

// Note: Removed FILTER_UTILITY_BINDINGS as texture filtering doesn't need shadow buffers

// Push constant structure for texture filtering
struct FilterPushConstants {
    std::array<float, 2> tex_scale;
    std::array<float, 2> tex_offset;
    float res_scale; // For xBRZ filter
};

inline constexpr vk::PushConstantRange FILTER_PUSH_CONSTANT_RANGE{
    .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    .offset = 0,
    .size = sizeof(FilterPushConstants),
};
inline constexpr vk::PushConstantRange PUSH_CONSTANT_RANGE{
    .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    .offset = 0,
    .size = sizeof(PushConstants),
};
constexpr vk::PipelineVertexInputStateCreateInfo PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO{
    .vertexBindingDescriptionCount = 0,
    .pVertexBindingDescriptions = nullptr,
    .vertexAttributeDescriptionCount = 0,
    .pVertexAttributeDescriptions = nullptr,
};
constexpr vk::PipelineInputAssemblyStateCreateInfo PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO{
    .topology = vk::PrimitiveTopology::eTriangleList,
    .primitiveRestartEnable = VK_FALSE,
};
constexpr vk::PipelineViewportStateCreateInfo PIPELINE_VIEWPORT_STATE_CREATE_INFO{
    .viewportCount = 1,
    .pViewports = nullptr,
    .scissorCount = 1,
    .pScissors = nullptr,
};
constexpr vk::PipelineRasterizationStateCreateInfo PIPELINE_RASTERIZATION_STATE_CREATE_INFO{
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = vk::PolygonMode::eFill,
    .cullMode = vk::CullModeFlagBits::eBack,
    .frontFace = vk::FrontFace::eClockwise,
    .depthBiasEnable = VK_FALSE,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 0.0f,
    .lineWidth = 1.0f,
};
constexpr vk::PipelineMultisampleStateCreateInfo PIPELINE_MULTISAMPLE_STATE_CREATE_INFO{
    .rasterizationSamples = vk::SampleCountFlagBits::e1,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading = 0.0f,
    .pSampleMask = nullptr,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable = VK_FALSE,
};
constexpr std::array DYNAMIC_STATES{
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor,
};
constexpr vk::PipelineDynamicStateCreateInfo PIPELINE_DYNAMIC_STATE_CREATE_INFO{
    .dynamicStateCount = static_cast<u32>(DYNAMIC_STATES.size()),
    .pDynamicStates = DYNAMIC_STATES.data(),
};

constexpr vk::PipelineColorBlendAttachmentState COLOR_BLEND_ATTACHMENT{
    .blendEnable = VK_FALSE,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
};

constexpr vk::PipelineColorBlendStateCreateInfo PIPELINE_COLOR_BLEND_STATE_CREATE_INFO{
    .logicOpEnable = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments = &COLOR_BLEND_ATTACHMENT,
};
constexpr vk::PipelineDepthStencilStateCreateInfo PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO{
    .depthTestEnable = VK_TRUE,
    .depthWriteEnable = VK_TRUE,
    .depthCompareOp = vk::CompareOp::eAlways,
    .depthBoundsTestEnable = VK_FALSE,
    .stencilTestEnable = VK_FALSE,
    .front = vk::StencilOpState{},
    .back = vk::StencilOpState{},
    .minDepthBounds = 0.0f,
    .maxDepthBounds = 0.0f,
};

template <vk::Filter filter>
inline constexpr vk::SamplerCreateInfo SAMPLER_CREATE_INFO{
    .magFilter = filter,
    .minFilter = filter,
    .mipmapMode = vk::SamplerMipmapMode::eNearest,
    .addressModeU = vk::SamplerAddressMode::eClampToEdge,
    .addressModeV = vk::SamplerAddressMode::eClampToEdge,
    .addressModeW = vk::SamplerAddressMode::eClampToEdge,
    .mipLodBias = 0.0f,
    .anisotropyEnable = VK_FALSE,
    .maxAnisotropy = 0.0f,
    .compareEnable = VK_FALSE,
    .compareOp = vk::CompareOp::eNever,
    .minLod = 0.0f,
    .maxLod = 0.0f,
    .borderColor = vk::BorderColor::eFloatOpaqueWhite,
    .unnormalizedCoordinates = VK_FALSE,
};

constexpr vk::PipelineLayoutCreateInfo PipelineLayoutCreateInfo(
    const vk::DescriptorSetLayout* set_layout, bool compute = false, bool filter = false) {
    return vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            (compute ? &COMPUTE_PUSH_CONSTANT_RANGE
                     : (filter ? &FILTER_PUSH_CONSTANT_RANGE : &PUSH_CONSTANT_RANGE)),
    };
}

constexpr std::array<vk::PipelineShaderStageCreateInfo, 2> MakeStages(
    vk::ShaderModule vertex_shader, vk::ShaderModule fragment_shader) {
    return std::array{
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = vertex_shader,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = fragment_shader,
            .pName = "main",
        },
    };
}

constexpr vk::PipelineShaderStageCreateInfo MakeStages(vk::ShaderModule compute_shader) {
    return vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = compute_shader,
        .pName = "main",
    };
}

vk::RenderPass CreateFilterRenderPass(vk::Device device, vk::Format format) {
    const vk::AttachmentDescription attachment = {
        .format = format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eGeneral,
        .finalLayout = vk::ImageLayout::eGeneral,
    };
    const vk::AttachmentReference color_attachment = {
        .attachment = 0,
        .layout = vk::ImageLayout::eGeneral,
    };
    const vk::SubpassDescription subpass = {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
    };
    const vk::RenderPassCreateInfo renderpass_info = {
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    return device.createRenderPass(renderpass_info);
}

} // Anonymous namespace

struct Anime4KResourceKey {
    u32 width;
    u32 height;
    vk::Format source_format;

    bool operator==(const Anime4KResourceKey&) const noexcept = default;
};

struct Anime4KResourceKeyHash {
    std::size_t operator()(const Anime4KResourceKey& key) const noexcept {
        return Common::HashCombine(Common::HashCombine(key.width, key.height),
                                   static_cast<u32>(key.source_format));
    }
};

struct Anime4KResourceSet {
    Anime4KResourceSet(const Instance& instance, vk::RenderPass xy_renderpass,
                       vk::RenderPass luma_renderpass, const Anime4KResourceKey& key)
        : device{instance.GetDevice()}, source{instance}, xy{instance}, luma{instance} {
        constexpr auto color_aspect = vk::ImageAspectFlagBits::eColor;
        constexpr auto source_usage =
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        constexpr auto intermediate_usage =
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment;

        source.Create(key.width, key.height, 1, VideoCore::TextureType::Texture2D,
                      key.source_format, source_usage, {}, color_aspect, false,
                      "Anime4K source copy");
        xy.Create(key.width * 2, key.height * 2, 1, VideoCore::TextureType::Texture2D,
                  vk::Format::eR16G16Sfloat, intermediate_usage, {}, color_aspect, false,
                  "Anime4K XY gradient");
        luma.Create(key.width * 2, key.height * 2, 1, VideoCore::TextureType::Texture2D,
                    vk::Format::eR16Sfloat, intermediate_usage, {}, color_aspect, false,
                    "Anime4K luma gradient");

        const auto create_framebuffer = [this](Handle& handle, vk::RenderPass renderpass) {
            const vk::ImageView image_view = handle.image_views[ViewType::Sample];
            const vk::FramebufferCreateInfo framebuffer_info = {
                .renderPass = renderpass,
                .attachmentCount = 1,
                .pAttachments = &image_view,
                .width = handle.width,
                .height = handle.height,
                .layers = 1,
            };
            handle.framebuffer = device.createFramebuffer(framebuffer_info);
        };
        create_framebuffer(xy, xy_renderpass);
        create_framebuffer(luma, luma_renderpass);
    }

    ~Anime4KResourceSet() {
        // Handle::Destroy destroys image views before its framebuffer, so detach and destroy the
        // framebuffers here while their attachment views are still alive.
        if (xy.framebuffer) {
            device.destroyFramebuffer(xy.framebuffer);
            xy.framebuffer = VK_NULL_HANDLE;
        }
        if (luma.framebuffer) {
            device.destroyFramebuffer(luma.framebuffer);
            luma.framebuffer = VK_NULL_HANDLE;
        }
    }

    vk::Device device;
    Handle source;
    Handle xy;
    Handle luma;
};

struct Anime4KResources {
    std::unordered_map<Anime4KResourceKey, std::unique_ptr<Anime4KResourceSet>,
                       Anime4KResourceKeyHash>
        sets;
};

BlitHelper::BlitHelper(const Instance& instance_, Scheduler& scheduler_,
                       RenderManager& renderpass_cache_, DescriptorUpdateQueue& update_queue_)
    : instance{instance_}, scheduler{scheduler_}, renderpass_cache{renderpass_cache_},
      update_queue{update_queue_}, device{instance.GetDevice()},
      anime4k_xy_renderpass{CreateFilterRenderPass(device, vk::Format::eR16G16Sfloat)},
      anime4k_luma_renderpass{CreateFilterRenderPass(device, vk::Format::eR16Sfloat)},
      compute_provider{instance, scheduler.GetMasterSemaphore(), COMPUTE_BINDINGS},
      compute_buffer_provider{instance, scheduler.GetMasterSemaphore(), COMPUTE_BUFFER_BINDINGS},
      two_textures_provider{instance, scheduler.GetMasterSemaphore(), TWO_TEXTURES_BINDINGS, 16},
      single_texture_provider{instance, scheduler.GetMasterSemaphore(), SINGLE_TEXTURE_BINDINGS,
                              16},
      three_textures_provider{instance, scheduler.GetMasterSemaphore(), THREE_TEXTURES_BINDINGS,
                              16},
      compute_pipeline_layout{
          device.createPipelineLayout(PipelineLayoutCreateInfo(&compute_provider.Layout(), true))},
      compute_buffer_pipeline_layout{device.createPipelineLayout(
          PipelineLayoutCreateInfo(&compute_buffer_provider.Layout(), true))},
      two_textures_pipeline_layout{
          device.createPipelineLayout(PipelineLayoutCreateInfo(&two_textures_provider.Layout()))},
      single_texture_pipeline_layout{device.createPipelineLayout(
          PipelineLayoutCreateInfo(&single_texture_provider.Layout(), false, true))},
      three_textures_pipeline_layout{device.createPipelineLayout(
          PipelineLayoutCreateInfo(&three_textures_provider.Layout(), false, true))},
      full_screen_vert{Compile(HostShaders::FULL_SCREEN_TRIANGLE_VERT,
                               vk::ShaderStageFlagBits::eVertex, device)},
      d24s8_to_rgba8_comp{Compile(HostShaders::VULKAN_D24S8_TO_RGBA8_COMP,
                                  vk::ShaderStageFlagBits::eCompute, device)},
      depth_to_buffer_comp{Compile(HostShaders::VULKAN_DEPTH_TO_BUFFER_COMP,
                                   vk::ShaderStageFlagBits::eCompute, device)},
      blit_depth_stencil_frag{VK_NULL_HANDLE},
      // Texture filtering shader modules
      bicubic_frag{Compile(HostShaders::BICUBIC_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      scale_force_frag{
          Compile(HostShaders::SCALE_FORCE_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      xbrz_frag{
          Compile(HostShaders::XBRZ_FREESCALE_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      mmpx_frag{Compile(HostShaders::MMPX_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      x_gradient_frag{
          Compile(HostShaders::X_GRADIENT_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      y_gradient_frag{
          Compile(HostShaders::Y_GRADIENT_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      refine_frag{Compile(HostShaders::REFINE_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      d24s8_to_rgba8_pipeline{MakeComputePipeline(d24s8_to_rgba8_comp, compute_pipeline_layout)},
      depth_to_buffer_pipeline{
          MakeComputePipeline(depth_to_buffer_comp, compute_buffer_pipeline_layout)},
      depth_blit_pipeline{VK_NULL_HANDLE},
      linear_sampler{device.createSampler(SAMPLER_CREATE_INFO<vk::Filter::eLinear>)},
      nearest_sampler{device.createSampler(SAMPLER_CREATE_INFO<vk::Filter::eNearest>)},
      anime4k_resources{std::make_unique<Anime4KResources>()} {

    if (instance.IsShaderStencilExportSupported()) {
        blit_depth_stencil_frag = Compile(HostShaders::VULKAN_BLIT_DEPTH_STENCIL_FRAG,
                                          vk::ShaderStageFlagBits::eFragment, device);
        depth_blit_pipeline = MakeDepthStencilBlitPipeline();
    }

    if (instance.HasDebuggingToolAttached()) {
        SetObjectName(device, compute_pipeline_layout, "BlitHelper: compute_pipeline_layout");
        SetObjectName(device, compute_buffer_pipeline_layout,
                      "BlitHelper: compute_buffer_pipeline_layout");
        SetObjectName(device, two_textures_pipeline_layout,
                      "BlitHelper: two_textures_pipeline_layout");
        SetObjectName(device, single_texture_pipeline_layout,
                      "BlitHelper: single_texture_pipeline_layout");
        SetObjectName(device, three_textures_pipeline_layout,
                      "BlitHelper: three_textures_pipeline_layout");
        SetObjectName(device, full_screen_vert, "BlitHelper: full_screen_vert");
        SetObjectName(device, d24s8_to_rgba8_comp, "BlitHelper: d24s8_to_rgba8_comp");
        SetObjectName(device, depth_to_buffer_comp, "BlitHelper: depth_to_buffer_comp");
        if (blit_depth_stencil_frag) {
            SetObjectName(device, blit_depth_stencil_frag, "BlitHelper: blit_depth_stencil_frag");
        }
        SetObjectName(device, d24s8_to_rgba8_pipeline, "BlitHelper: d24s8_to_rgba8_pipeline");
        SetObjectName(device, depth_to_buffer_pipeline, "BlitHelper: depth_to_buffer_pipeline");
        if (depth_blit_pipeline) {
            SetObjectName(device, depth_blit_pipeline, "BlitHelper: depth_blit_pipeline");
        }
        SetObjectName(device, linear_sampler, "BlitHelper: linear_sampler");
        SetObjectName(device, nearest_sampler, "BlitHelper: nearest_sampler");
    }
}

BlitHelper::~BlitHelper() {
    for (const auto& [_, pipeline] : filter_pipeline_cache) {
        device.destroyPipeline(pipeline);
    }
    filter_pipeline_cache.clear();
    anime4k_resources.reset();
    device.destroyPipelineLayout(compute_pipeline_layout);
    device.destroyPipelineLayout(compute_buffer_pipeline_layout);
    device.destroyPipelineLayout(two_textures_pipeline_layout);
    device.destroyPipelineLayout(single_texture_pipeline_layout);
    device.destroyPipelineLayout(three_textures_pipeline_layout);
    device.destroyShaderModule(full_screen_vert);
    device.destroyShaderModule(d24s8_to_rgba8_comp);
    device.destroyShaderModule(depth_to_buffer_comp);
    if (blit_depth_stencil_frag) {
        device.destroyShaderModule(blit_depth_stencil_frag);
    }
    // Destroy texture filtering shader modules
    device.destroyShaderModule(bicubic_frag);
    device.destroyShaderModule(scale_force_frag);
    device.destroyShaderModule(xbrz_frag);
    device.destroyShaderModule(mmpx_frag);
    device.destroyShaderModule(x_gradient_frag);
    device.destroyShaderModule(y_gradient_frag);
    device.destroyShaderModule(refine_frag);
    device.destroyPipeline(depth_to_buffer_pipeline);
    device.destroyPipeline(d24s8_to_rgba8_pipeline);
    device.destroyPipeline(depth_blit_pipeline);
    device.destroySampler(linear_sampler);
    device.destroySampler(nearest_sampler);
    device.destroyRenderPass(anime4k_xy_renderpass);
    device.destroyRenderPass(anime4k_luma_renderpass);
}

void BindBlitState(vk::CommandBuffer cmdbuf, vk::PipelineLayout layout,
                   const VideoCore::TextureBlit& blit, const Surface& dest) {
    const vk::Offset2D offset{
        .x = std::min<s32>(blit.dst_rect.left, blit.dst_rect.right),
        .y = std::min<s32>(blit.dst_rect.bottom, blit.dst_rect.top),
    };
    const vk::Extent2D extent{
        .width = blit.dst_rect.GetWidth(),
        .height = blit.dst_rect.GetHeight(),
    };
    const vk::Viewport viewport{
        .x = static_cast<float>(offset.x),
        .y = static_cast<float>(offset.y),
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const vk::Rect2D scissor{
        .offset = offset,
        .extent = extent,
    };
    const float scale_x = static_cast<float>(blit.src_rect.GetWidth());
    const float scale_y = static_cast<float>(blit.src_rect.GetHeight());
    const PushConstants push_constants{
        .tex_scale = {scale_x, scale_y},
        .tex_offset = {static_cast<float>(blit.src_rect.left),
                       static_cast<float>(blit.src_rect.bottom)},
    };
    cmdbuf.setViewport(0, viewport);
    cmdbuf.setScissor(0, scissor);
    cmdbuf.pushConstants(layout,
                         vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                         sizeof(push_constants), &push_constants);
}

bool BlitHelper::BlitDepthStencil(Surface& source, Surface& dest,
                                  const VideoCore::TextureBlit& blit) {
    if (!instance.IsShaderStencilExportSupported()) {
        LOG_ERROR(Render_Vulkan, "Unable to emulate depth stencil images");
        return false;
    }

    const vk::Rect2D dst_render_area = {
        .offset = {0, 0},
        .extent = {dest.GetScaledWidth(), dest.GetScaledHeight()},
    };

    const auto descriptor_set = two_textures_provider.Commit();
    update_queue.AddImageSampler(descriptor_set, 0, 0, source.DepthView(), nearest_sampler);
    update_queue.AddImageSampler(descriptor_set, 1, 0, source.StencilView(), nearest_sampler);

    const RenderPass depth_pass = {
        .framebuffer = dest.Framebuffer(),
        .render_pass =
            renderpass_cache.GetRenderpass(PixelFormat::Invalid, dest.pixel_format, false),
        .render_area = dst_render_area,
    };
    renderpass_cache.BeginRendering(depth_pass);

    scheduler.Record([blit, descriptor_set, &dest, this](vk::CommandBuffer cmdbuf) {
        const vk::PipelineLayout layout = two_textures_pipeline_layout;

        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, depth_blit_pipeline);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, descriptor_set, {});
        BindBlitState(cmdbuf, layout, blit, dest);
        cmdbuf.draw(3, 1, 0, 0);
    });
    scheduler.MakeDirty(StateFlags::Pipeline);
    return true;
}

bool BlitHelper::ConvertDS24S8ToRGBA8(Surface& source, Surface& dest,
                                      const VideoCore::TextureCopy& copy) {
    const auto descriptor_set = compute_provider.Commit();
    update_queue.AddImageSampler(descriptor_set, 0, 0, source.DepthView(), VK_NULL_HANDLE,
                                 vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    update_queue.AddImageSampler(descriptor_set, 1, 0, source.StencilView(), VK_NULL_HANDLE,
                                 vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    update_queue.AddStorageImage(descriptor_set, 2, dest.ImageView());

    renderpass_cache.EndRendering();
    scheduler.Record([this, descriptor_set, copy, src_image = source.Image(),
                      dst_image = dest.Image()](vk::CommandBuffer cmdbuf) {
        const std::array pre_barriers = {
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = src_image,
                .subresourceRange{
                    .aspectMask =
                        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eNone,
                .dstAccessMask = vk::AccessFlagBits::eShaderWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst_image,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            },
        };
        const std::array post_barriers = {
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderRead,
                .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                                 vk::AccessFlagBits::eDepthStencilAttachmentRead,
                .oldLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = src_image,
                .subresourceRange{
                    .aspectMask =
                        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst_image,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            }};
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests,
                               vk::PipelineStageFlagBits::eComputeShader,
                               vk::DependencyFlagBits::eByRegion, {}, {}, pre_barriers);

        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute_pipeline_layout, 0,
                                  descriptor_set, {});
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, d24s8_to_rgba8_pipeline);

        const ComputeInfo info = {
            .src_offset = Common::Vec2i{static_cast<int>(copy.src_offset.x),
                                        static_cast<int>(copy.src_offset.y)},
            .dst_offset = Common::Vec2i{static_cast<int>(copy.dst_offset.x),
                                        static_cast<int>(copy.dst_offset.y)},
        };
        cmdbuf.pushConstants(compute_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                             sizeof(info), &info);

        cmdbuf.dispatch(copy.extent.width / 8, copy.extent.height / 8, 1);

        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                               vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests |
                                   vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, {}, {}, post_barriers);
    });
    return true;
}

bool BlitHelper::DepthToBuffer(Surface& source, vk::Buffer buffer,
                               const VideoCore::BufferTextureCopy& copy) {
    const auto descriptor_set = compute_buffer_provider.Commit();
    update_queue.AddImageSampler(descriptor_set, 0, 0, source.DepthView(), nearest_sampler,
                                 vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    update_queue.AddImageSampler(descriptor_set, 1, 0, source.StencilView(), nearest_sampler,
                                 vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    update_queue.AddBuffer(descriptor_set, 2, buffer, copy.buffer_offset, copy.buffer_size,
                           vk::DescriptorType::eStorageBuffer);

    renderpass_cache.EndRendering();
    scheduler.Record([this, descriptor_set, copy, src_image = source.Image(),
                      extent = source.RealExtent(false)](vk::CommandBuffer cmdbuf) {
        const vk::ImageMemoryBarrier pre_barrier = {
            .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = src_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        };
        const vk::ImageMemoryBarrier post_barrier = {
            .srcAccessMask = vk::AccessFlagBits::eShaderRead,
            .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                             vk::AccessFlagBits::eDepthStencilAttachmentRead,
            .oldLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = src_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests,
                               vk::PipelineStageFlagBits::eComputeShader,
                               vk::DependencyFlagBits::eByRegion, {}, {}, pre_barrier);

        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute_buffer_pipeline_layout,
                                  0, descriptor_set, {});
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, depth_to_buffer_pipeline);

        const ComputeInfo info = {
            .src_offset = Common::Vec2i{static_cast<int>(copy.texture_rect.left),
                                        static_cast<int>(copy.texture_rect.bottom)},
            .src_extent =
                Common::Vec2i{static_cast<int>(extent.width), static_cast<int>(extent.height)},
        };
        cmdbuf.pushConstants(compute_buffer_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                             sizeof(ComputeInfo), &info);

        cmdbuf.dispatch(copy.texture_rect.GetWidth() / 8, copy.texture_rect.GetHeight() / 8, 1);

        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                               vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests |
                                   vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, {}, {}, post_barrier);
    });
    return true;
}

vk::Pipeline BlitHelper::MakeComputePipeline(vk::ShaderModule shader, vk::PipelineLayout layout) {
    const vk::ComputePipelineCreateInfo compute_info = {
        .stage = MakeStages(shader),
        .layout = layout,
    };

    if (const auto result = device.createComputePipeline({}, compute_info);
        result.result == vk::Result::eSuccess) {
        return result.value;
    } else {
        LOG_CRITICAL(Render_Vulkan, "Compute pipeline creation failed!");
        UNREACHABLE();
    }
}

vk::Pipeline BlitHelper::MakeDepthStencilBlitPipeline() {
    const std::array stages = MakeStages(full_screen_vert, blit_depth_stencil_frag);
    const auto renderpass = renderpass_cache.GetRenderpass(VideoCore::PixelFormat::Invalid,
                                                           VideoCore::PixelFormat::D24S8, false);
    vk::GraphicsPipelineCreateInfo depth_stencil_info = {
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pInputAssemblyState = &PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pTessellationState = nullptr,
        .pViewportState = &PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pRasterizationState = &PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pMultisampleState = &PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pDepthStencilState = &PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pColorBlendState = &PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pDynamicState = &PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .layout = two_textures_pipeline_layout,
        .renderPass = renderpass,
    };

    if (const auto result = device.createGraphicsPipeline({}, depth_stencil_info);
        result.result == vk::Result::eSuccess) {
        return result.value;
    } else {
        LOG_CRITICAL(Render_Vulkan, "Depth stencil blit pipeline creation failed!");
        UNREACHABLE();
    }
    return VK_NULL_HANDLE;
}

bool BlitHelper::Filter(Surface& surface, const VideoCore::TextureBlit& blit) {
    const auto filter = Settings::values.texture_filter.GetValue();
    if (filter == Settings::TextureFilter::NoFilter) {
        return false;
    }
    if (blit.src_level != 0) {
        return true;
    }

    switch (filter) {
    case TextureFilter::Anime4K:
        FilterAnime4K(surface, blit);
        break;
    case TextureFilter::Bicubic:
        FilterBicubic(surface, blit);
        break;
    case TextureFilter::ScaleForce:
        FilterScaleForce(surface, blit);
        break;
    case TextureFilter::xBRZ:
        FilterXbrz(surface, blit);
        break;
    case TextureFilter::MMPX:
        FilterMMPX(surface, blit);
        break;
    default:
        LOG_ERROR(Render_Vulkan, "Unknown texture filter {}", filter);
        return false;
    }
    return true;
}

void BlitHelper::FilterAnime4K(Surface& surface, const VideoCore::TextureBlit& blit) {
    constexpr u32 internal_scale_factor = 2;
    const u32 source_width = blit.src_rect.GetWidth();
    const u32 source_height = blit.src_rect.GetHeight();
    if (source_width == 0 || source_height == 0) {
        return;
    }

    const Anime4KResourceKey resource_key{
        .width = source_width,
        .height = source_height,
        .source_format = surface.traits.native,
    };
    auto [resource_it, inserted] = anime4k_resources->sets.try_emplace(resource_key);
    if (inserted) {
        resource_it->second = std::make_unique<Anime4KResourceSet>(
            instance, anime4k_xy_renderpass, anime4k_luma_renderpass, resource_key);
    }
    Anime4KResourceSet& resources = *resource_it->second;

    renderpass_cache.EndRendering();

    if (inserted) {
        const std::array images{resources.source.image, resources.xy.image, resources.luma.image};
        scheduler.Record([images](vk::CommandBuffer cmdbuf) {
            std::array<vk::ImageMemoryBarrier, images.size()> barriers;
            for (std::size_t index = 0; index < images.size(); ++index) {
                barriers[index] = vk::ImageMemoryBarrier{
                    .srcAccessMask = vk::AccessFlagBits::eNone,
                    .dstAccessMask = vk::AccessFlagBits::eTransferWrite |
                                     vk::AccessFlagBits::eColorAttachmentWrite,
                    .oldLayout = vk::ImageLayout::eUndefined,
                    .newLayout = vk::ImageLayout::eGeneral,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = images[index],
                    .subresourceRange{
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                };
            }
            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                   vk::PipelineStageFlagBits::eTransfer |
                                       vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                   vk::DependencyFlagBits::eByRegion, {}, {}, barriers);
        });
    }

    const vk::Image source_image = resources.source.image;
    const vk::Image surface_image = surface.Image(Type::Base);
    const vk::ImageCopy source_copy = {
        .srcSubresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcOffset = {static_cast<s32>(blit.src_rect.left), static_cast<s32>(blit.src_rect.bottom),
                      0},
        .dstSubresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstOffset = {0, 0, 0},
        .extent = {source_width, source_height, 1},
    };
    scheduler.Record([source_image, surface_image, source_copy](vk::CommandBuffer cmdbuf) {
        const vk::ImageMemoryBarrier prepare_copy = {
            .srcAccessMask = vk::AccessFlagBits::eShaderRead,
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = source_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                               vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, {}, {}, prepare_copy);
        cmdbuf.copyImage(surface_image, vk::ImageLayout::eGeneral, source_image,
                         vk::ImageLayout::eGeneral, source_copy);
        const vk::ImageMemoryBarrier finish_copy = {
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = source_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eFragmentShader,
                               vk::DependencyFlagBits::eByRegion, {}, {}, finish_copy);
    });

    const vk::DescriptorSet texture_descriptor_set = three_textures_provider.Commit();
    update_queue.AddImageSampler(texture_descriptor_set, 0, 0,
                                 resources.source.image_views[ViewType::Sample], linear_sampler,
                                 vk::ImageLayout::eGeneral);
    update_queue.AddImageSampler(texture_descriptor_set, 1, 0,
                                 resources.luma.image_views[ViewType::Sample], linear_sampler,
                                 vk::ImageLayout::eGeneral);
    update_queue.AddImageSampler(texture_descriptor_set, 2, 0,
                                 resources.xy.image_views[ViewType::Sample], nearest_sampler,
                                 vk::ImageLayout::eGeneral);

    const auto transition_image =
        [this](vk::Image image, vk::AccessFlags src_access, vk::AccessFlags dst_access,
               vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage) {
            scheduler.Record(
                [image, src_access, dst_access, src_stage, dst_stage](vk::CommandBuffer cmdbuf) {
                    const vk::ImageMemoryBarrier barrier = {
                        .srcAccessMask = src_access,
                        .dstAccessMask = dst_access,
                        .oldLayout = vk::ImageLayout::eGeneral,
                        .newLayout = vk::ImageLayout::eGeneral,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = image,
                        .subresourceRange{
                            .aspectMask = vk::ImageAspectFlagBits::eColor,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                    };
                    cmdbuf.pipelineBarrier(src_stage, dst_stage, vk::DependencyFlagBits::eByRegion,
                                           {}, {}, barrier);
                });
        };

    const auto draw_pass =
        [this, texture_descriptor_set](vk::Pipeline pipeline, vk::PipelineLayout layout,
                                       vk::Framebuffer framebuffer, vk::RenderPass renderpass,
                                       vk::Rect2D output_rect, FilterPushConstants push_constants) {
            const RenderPass render_pass = {
                .framebuffer = framebuffer,
                .render_pass = renderpass,
                .render_area = output_rect,
            };
            renderpass_cache.BeginRendering(render_pass);
            scheduler.Record([pipeline, layout, texture_descriptor_set, output_rect,
                              push_constants](vk::CommandBuffer cmdbuf) {
                const vk::Viewport viewport = {
                    .x = static_cast<float>(output_rect.offset.x),
                    .y = static_cast<float>(output_rect.offset.y),
                    .width = static_cast<float>(output_rect.extent.width),
                    .height = static_cast<float>(output_rect.extent.height),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                };
                cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
                cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0,
                                          texture_descriptor_set, {});
                cmdbuf.pushConstants(layout, FILTER_PUSH_CONSTANT_RANGE.stageFlags,
                                     FILTER_PUSH_CONSTANT_RANGE.offset,
                                     FILTER_PUSH_CONSTANT_RANGE.size, &push_constants);
                cmdbuf.setViewport(0, viewport);
                cmdbuf.setScissor(0, output_rect);
                cmdbuf.draw(3, 1, 0, 0);
            });
            renderpass_cache.EndRendering();
        };

    const vk::Extent2D intermediate_extent{
        .width = source_width * internal_scale_factor,
        .height = source_height * internal_scale_factor,
    };
    const vk::Rect2D intermediate_rect{
        .offset = {0, 0},
        .extent = intermediate_extent,
    };
    const FilterPushConstants full_texture_constants{
        .tex_scale = {1.0f, 1.0f},
        .tex_offset = {0.0f, 0.0f},
        .res_scale = static_cast<float>(surface.GetResScale()),
    };

    transition_image(resources.xy.image, vk::AccessFlagBits::eShaderRead,
                     vk::AccessFlagBits::eColorAttachmentWrite,
                     vk::PipelineStageFlagBits::eFragmentShader,
                     vk::PipelineStageFlagBits::eColorAttachmentOutput);
    draw_pass(
        MakeFilterPipeline(x_gradient_frag, three_textures_pipeline_layout, anime4k_xy_renderpass),
        three_textures_pipeline_layout, resources.xy.framebuffer, anime4k_xy_renderpass,
        intermediate_rect, full_texture_constants);
    transition_image(resources.xy.image, vk::AccessFlagBits::eColorAttachmentWrite,
                     vk::AccessFlagBits::eShaderRead,
                     vk::PipelineStageFlagBits::eColorAttachmentOutput,
                     vk::PipelineStageFlagBits::eFragmentShader);

    transition_image(resources.luma.image, vk::AccessFlagBits::eShaderRead,
                     vk::AccessFlagBits::eColorAttachmentWrite,
                     vk::PipelineStageFlagBits::eFragmentShader,
                     vk::PipelineStageFlagBits::eColorAttachmentOutput);
    draw_pass(MakeFilterPipeline(y_gradient_frag, three_textures_pipeline_layout,
                                 anime4k_luma_renderpass),
              three_textures_pipeline_layout, resources.luma.framebuffer, anime4k_luma_renderpass,
              intermediate_rect, full_texture_constants);
    transition_image(resources.luma.image, vk::AccessFlagBits::eColorAttachmentWrite,
                     vk::AccessFlagBits::eShaderRead,
                     vk::PipelineStageFlagBits::eColorAttachmentOutput,
                     vk::PipelineStageFlagBits::eFragmentShader);

    const vk::Offset2D output_offset{
        .x = std::min<s32>(blit.dst_rect.left, blit.dst_rect.right),
        .y = std::min<s32>(blit.dst_rect.bottom, blit.dst_rect.top),
    };
    const vk::Rect2D output_rect{
        .offset = output_offset,
        .extent = {blit.dst_rect.GetWidth(), blit.dst_rect.GetHeight()},
    };
    const vk::RenderPass output_renderpass = renderpass_cache.GetRenderpass(
        surface.pixel_format, VideoCore::PixelFormat::Invalid, false);
    draw_pass(MakeFilterPipeline(refine_frag, three_textures_pipeline_layout, surface.pixel_format),
              three_textures_pipeline_layout, surface.Framebuffer(), output_renderpass, output_rect,
              full_texture_constants);
    transition_image(surface.Image(), vk::AccessFlagBits::eColorAttachmentWrite,
                     vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead,
                     vk::PipelineStageFlagBits::eColorAttachmentOutput,
                     vk::PipelineStageFlagBits::eFragmentShader |
                         vk::PipelineStageFlagBits::eTransfer);
    scheduler.MakeDirty(StateFlags::Pipeline);
}

void BlitHelper::FilterBicubic(Surface& surface, const VideoCore::TextureBlit& blit) {
    auto pipeline =
        MakeFilterPipeline(bicubic_frag, single_texture_pipeline_layout, surface.pixel_format);
    FilterPass(surface, pipeline, single_texture_pipeline_layout, blit);
}

void BlitHelper::FilterScaleForce(Surface& surface, const VideoCore::TextureBlit& blit) {
    auto pipeline =
        MakeFilterPipeline(scale_force_frag, single_texture_pipeline_layout, surface.pixel_format);
    FilterPass(surface, pipeline, single_texture_pipeline_layout, blit);
}

void BlitHelper::FilterXbrz(Surface& surface, const VideoCore::TextureBlit& blit) {
    auto pipeline =
        MakeFilterPipeline(xbrz_frag, single_texture_pipeline_layout, surface.pixel_format);
    FilterPass(surface, pipeline, single_texture_pipeline_layout, blit);
}

void BlitHelper::FilterMMPX(Surface& surface, const VideoCore::TextureBlit& blit) {
    auto pipeline =
        MakeFilterPipeline(mmpx_frag, single_texture_pipeline_layout, surface.pixel_format);
    FilterPass(surface, pipeline, single_texture_pipeline_layout, blit);
}

vk::Pipeline BlitHelper::MakeFilterPipeline(vk::ShaderModule fragment_shader,
                                            vk::PipelineLayout layout,
                                            VideoCore::PixelFormat color_format) {
    const vk::RenderPass renderpass =
        renderpass_cache.GetRenderpass(color_format, VideoCore::PixelFormat::Invalid, false);
    return MakeFilterPipeline(fragment_shader, layout, renderpass);
}

vk::Pipeline BlitHelper::MakeFilterPipeline(vk::ShaderModule fragment_shader,
                                            vk::PipelineLayout layout, vk::RenderPass renderpass) {

    const VkShaderModule c_shader = static_cast<VkShaderModule>(fragment_shader);
    const VkPipelineLayout c_layout = static_cast<VkPipelineLayout>(layout);
    const VkRenderPass c_renderpass = static_cast<VkRenderPass>(renderpass);
    const u64 cache_key = Common::HashCombine(
        Common::HashCombine(static_cast<u64>(reinterpret_cast<uintptr_t>(c_shader)),
                            static_cast<u64>(reinterpret_cast<uintptr_t>(c_layout))),
        static_cast<u64>(reinterpret_cast<uintptr_t>(c_renderpass)));

    if (const auto it = filter_pipeline_cache.find(cache_key); it != filter_pipeline_cache.end()) {
        return it->second;
    }

    const std::array stages = MakeStages(full_screen_vert, fragment_shader);

    vk::GraphicsPipelineCreateInfo pipeline_info = {
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pInputAssemblyState = &PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pTessellationState = nullptr,
        .pViewportState = &PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pRasterizationState = &PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pMultisampleState = &PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pDynamicState = &PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .layout = layout,
        .renderPass = renderpass,
    };

    if (const auto result = device.createGraphicsPipeline({}, pipeline_info);
        result.result == vk::Result::eSuccess) {
        const vk::Pipeline pipeline = result.value;
        filter_pipeline_cache.emplace(cache_key, pipeline);
        return pipeline;
    } else {
        LOG_CRITICAL(Render_Vulkan, "Filter pipeline creation failed!");
        UNREACHABLE();
    }
}

void BlitHelper::FilterPass(Surface& surface, vk::Pipeline pipeline, vk::PipelineLayout layout,
                            const VideoCore::TextureBlit& blit) {
    const auto texture_descriptor_set = single_texture_provider.Commit();
    update_queue.AddImageSampler(texture_descriptor_set, 0, 0,
                                 surface.ImageView(ViewType::Sample, Type::Base), linear_sampler,
                                 vk::ImageLayout::eGeneral);

    const auto renderpass = renderpass_cache.GetRenderpass(surface.pixel_format,
                                                           VideoCore::PixelFormat::Invalid, false);

    const RenderPass render_pass = {
        .framebuffer = surface.Framebuffer(),
        .render_pass = renderpass,
        .render_area =
            {
                .offset = {0, 0},
                .extent = {surface.GetScaledWidth(), surface.GetScaledHeight()},
            },
    };
    renderpass_cache.BeginRendering(render_pass);
    const float src_scale = static_cast<float>(surface.GetResScale());
    // Calculate normalized texture coordinates like OpenGL does
    const auto src_extent = surface.RealExtent(false); // Get unscaled texture extent
    const float tex_scale_x =
        static_cast<float>(blit.src_rect.GetWidth()) / static_cast<float>(src_extent.width);
    const float tex_scale_y =
        static_cast<float>(blit.src_rect.GetHeight()) / static_cast<float>(src_extent.height);
    const float tex_offset_x =
        static_cast<float>(blit.src_rect.left) / static_cast<float>(src_extent.width);
    const float tex_offset_y =
        static_cast<float>(blit.src_rect.bottom) / static_cast<float>(src_extent.height);

    scheduler.Record([pipeline, layout, texture_descriptor_set, blit, tex_scale_x, tex_scale_y,
                      tex_offset_x, tex_offset_y, src_scale](vk::CommandBuffer cmdbuf) {
        const FilterPushConstants push_constants{.tex_scale = {tex_scale_x, tex_scale_y},
                                                 .tex_offset = {tex_offset_x, tex_offset_y},
                                                 .res_scale = src_scale};

        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        // Bind single texture descriptor set
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0,
                                  texture_descriptor_set, {});

        cmdbuf.pushConstants(layout, FILTER_PUSH_CONSTANT_RANGE.stageFlags,
                             FILTER_PUSH_CONSTANT_RANGE.offset, FILTER_PUSH_CONSTANT_RANGE.size,
                             &push_constants);

        // Set up viewport and scissor for filtering (don't use BindBlitState as it overwrites push
        // constants)
        const vk::Offset2D offset{
            .x = std::min<s32>(blit.dst_rect.left, blit.dst_rect.right),
            .y = std::min<s32>(blit.dst_rect.bottom, blit.dst_rect.top),
        };
        const vk::Extent2D extent{
            .width = blit.dst_rect.GetWidth(),
            .height = blit.dst_rect.GetHeight(),
        };
        const vk::Viewport viewport{
            .x = static_cast<float>(offset.x),
            .y = static_cast<float>(offset.y),
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const vk::Rect2D scissor{
            .offset = offset,
            .extent = extent,
        };
        cmdbuf.setViewport(0, viewport);
        cmdbuf.setScissor(0, scissor);
        cmdbuf.draw(3, 1, 0, 0);
    });
    scheduler.MakeDirty(StateFlags::Pipeline);
}

} // namespace Vulkan
