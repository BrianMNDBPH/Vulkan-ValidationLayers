#include <type_traits>

#include "cast_utils.h"
#include "layer_validation_tests.h"

TEST_F(VkLayerTest, SyncCmdDispatchDrawHazards) {
    // TODO: Add code to enable sync validation
    SetTargetApiVersion(VK_API_VERSION_1_2);

    // Enable VK_KHR_draw_indirect_count for KHR variants
    ASSERT_NO_FATAL_FAILURE(InitFramework(m_errorMonitor));
    VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, nullptr};
    if (DeviceExtensionSupported(gpu(), nullptr, VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME)) {
        m_device_extension_names.push_back(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
        if (DeviceValidationVersion() >= VK_API_VERSION_1_2) {
            features12.drawIndirectCount = VK_TRUE;
        }
    }
    ASSERT_NO_FATAL_FAILURE(InitState(nullptr, &features12, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
    //ASSERT_NO_FATAL_FAILURE(Init());
    //bool has_khr_indirect = DeviceExtensionEnabled(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
    ASSERT_NO_FATAL_FAILURE(InitRenderTarget());

    const VkFormat format_texel_case = VK_FORMAT_R32_SFLOAT;
    const char *format_texel_case_string = "VK_FORMAT_R32_SFLOAT";
    VkFormatProperties format_properties;
    vk::GetPhysicalDeviceFormatProperties(gpu(), format_texel_case, &format_properties);
    if (!(format_properties.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT)) {
        printf("%s Test requires %s to support VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT\n", kSkipPrefix, format_texel_case_string);
        return;
    }

    VkImageUsageFlags image_usage_combine = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageObj image_c_a(m_device), image_c_b(m_device);
    const auto image_c_ci = VkImageObj::ImageCreateInfo2D(16, 16, 1, 1, format, image_usage_combine, VK_IMAGE_TILING_OPTIMAL);
    image_c_a.Init(image_c_ci);
    image_c_b.Init(image_c_ci);

    VkImageView imageview_c = image_c_a.targetView(format);

    VkImageUsageFlags image_usage_storage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageObj image_s_a(m_device), image_s_b(m_device);
    const auto image_s_ci = VkImageObj::ImageCreateInfo2D(16, 16, 1, 1, format, image_usage_storage, VK_IMAGE_TILING_OPTIMAL);
    image_s_a.Init(image_s_ci);
    image_s_b.Init(image_s_ci);

    VkImageView imageview_s = image_s_a.targetView(format);

    VkSampler sampler_s, sampler_c;
    VkSamplerCreateInfo sampler_ci = SafeSaneSamplerCreateInfo();
    VkResult err = vk::CreateSampler(m_device->device(), &sampler_ci, nullptr, &sampler_s);
    ASSERT_VK_SUCCESS(err);
    err = vk::CreateSampler(m_device->device(), &sampler_ci, nullptr, &sampler_c);
    ASSERT_VK_SUCCESS(err);

    VkBufferObj buffer_a, buffer_b;
    VkMemoryPropertyFlags mem_prop = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | //VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_a.init(*m_device, buffer_a.create_info(2048, buffer_usage, nullptr), mem_prop);
    buffer_b.init(*m_device, buffer_b.create_info(2048, buffer_usage, nullptr), mem_prop);

    VkBufferView bufferview;
    auto bvci = lvl_init_struct<VkBufferViewCreateInfo>();
    bvci.buffer = buffer_a.handle();
    bvci.format = format_texel_case;
    bvci.offset = 0;
    bvci.range = VK_WHOLE_SIZE;

    err = vk::CreateBufferView(m_device->device(), &bvci, NULL, &bufferview);
    ASSERT_VK_SUCCESS(err);
    
    OneOffDescriptorSet descriptor_set(m_device,
                                       {
                                           {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                           {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                           {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
                                           {3, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
                                       });

    descriptor_set.WriteDescriptorBufferInfo(0, buffer_a.handle(), 2048);
    descriptor_set.WriteDescriptorImageInfo(1, imageview_c, sampler_c, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                            VK_IMAGE_LAYOUT_GENERAL);
    descriptor_set.WriteDescriptorImageInfo(2, imageview_s, sampler_s, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    //descriptor_set.WriteDescriptorBufferView(3, bufferview);
    descriptor_set.UpdateDescriptorSets();

    // Dispatch
    std::string csSource =
        "#version 450\n"
        "layout(set=0, binding=0) uniform foo { int x; } ub0;\n"
        "layout(set=0, binding=1) uniform sampler2D cis1;\n"
        "layout(set=0, binding=2, rgba8) uniform image2D si2;\n"
        "layout(set=0, binding=3, rgba8) uniform imageBuffer stb3;\n"
        "void main(){\n"
        "    int value = 0;\n"
        "    vec4 vColor4;\n"
        "    value = ub0.x;\n"
        "    vColor4 = texture(cis1, vec2(0));\n"
        "    imageStore(si2, ivec2(0), vec4(0));\n"
        "    vColor4 = imageLoad(stb3, 0);\n"
        "}\n";

    // Draw
    m_errorMonitor->ExpectSuccess();
    const float vbo_data[3] = {1.f, 0.f, 1.f};
    VkVertexInputAttributeDescription VertexInputAttributeDescription = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(vbo_data)};
    VkVertexInputBindingDescription VertexInputBindingDescription = {0, sizeof(vbo_data), VK_VERTEX_INPUT_RATE_VERTEX};
    VkBufferObj vbo, vbo2;
    buffer_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vbo.init(*m_device, vbo.create_info(sizeof(vbo_data), buffer_usage, nullptr), mem_prop);
    vbo2.init(*m_device, vbo2.create_info(sizeof(vbo_data), buffer_usage, nullptr), mem_prop);

    VkShaderObj vs(m_device, bindStateVertShaderText, VK_SHADER_STAGE_VERTEX_BIT, this);
    VkShaderObj fs(m_device, csSource.c_str(), VK_SHADER_STAGE_FRAGMENT_BIT, this);

    CreatePipelineHelper g_pipe(*this);
    g_pipe.InitInfo();
    g_pipe.InitState();
    g_pipe.vi_ci_.pVertexBindingDescriptions = &VertexInputBindingDescription;
    g_pipe.vi_ci_.vertexBindingDescriptionCount = 1;
    g_pipe.vi_ci_.pVertexAttributeDescriptions = &VertexInputAttributeDescription;
    g_pipe.vi_ci_.vertexAttributeDescriptionCount = 1;
    //g_pipe.shader_stages_ = {vs.GetStageCreateInfo(), fs.GetStageCreateInfo()};
    g_pipe.pipeline_layout_ = VkPipelineLayoutObj(m_device, {&descriptor_set.layout_});
    ASSERT_VK_SUCCESS(g_pipe.CreateGraphicsPipeline());
}
