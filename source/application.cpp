#include "application.hpp"

#include <imgui.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include "torus.h"
}

namespace application {

    namespace {

        struct Mat4 {
            float m[16];
        };

        struct PushConstants {
            float mvp[16];
            float tint[4];
        };

        constexpr float deg_to_rad = 0.0174532925f;

        uint32_t index_count = 0;

        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VmaAllocation vertex_allocation = nullptr;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        VmaAllocation index_allocation = nullptr;

        VkShaderModule vertex_shader = VK_NULL_HANDLE;
        VkShaderModule fragment_shader = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;

        bool perspective_projection = true;
        float fov = 60.0f;
        float ortho_size = 8.0f;

        float position[3] = { 0.0f, 0.0f, -14.0f };
        float rotation[3] = { 60.0f, 20.0f, 0.0f };
        float scale = 1.0f;

        float color[3] = { 1.0f, 1.0f, 1.0f };
        bool vertex_colors = true;

        bool playing = true;
        float speed = 1.0f;
        float radius = 2.0f;
        float spin = 45.0f;
        float animation_time = 0.0f;

        double previous_time = 0.0;

        Mat4 identity() {
            Mat4 r{};
            r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
            return r;
        }

        Mat4 translation(float x, float y, float z) {
            Mat4 r = identity();
            r.m[12] = x;
            r.m[13] = y;
            r.m[14] = z;
            return r;
        }

        Mat4 scaling(float s) {
            Mat4 r = identity();
            r.m[0] = r.m[5] = r.m[10] = s;
            return r;
        }

        Mat4 rotation_matrix(int axis, float angle) {
            Mat4 r = identity();
            const int i = (axis + 1) % 3;
            const int j = (axis + 2) % 3;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            r.m[i * 4 + i] = c;
            r.m[i * 4 + j] = s;
            r.m[j * 4 + i] = -s;
            r.m[j * 4 + j] = c;
            return r;
        }

        Mat4 multiply(const Mat4& a, const Mat4& b) {
            Mat4 r{};
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    for (int k = 0; k < 4; ++k) {
                        r.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
                    }
                }
            }
            return r;
        }

        Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane) {
            Mat4 r{};
            const float f = 1.0f / std::tan(fov_y * 0.5f);
            r.m[0] = f / aspect;
            r.m[5] = -f;
            r.m[10] = far_plane / (near_plane - far_plane);
            r.m[11] = -1.0f;
            r.m[14] = far_plane * near_plane / (near_plane - far_plane);
            return r;
        }

        Mat4 orthographic(float half_width, float half_height, float near_plane, float far_plane) {
            Mat4 r{};
            r.m[0] = 1.0f / half_width;
            r.m[5] = -1.0f / half_height;
            r.m[10] = 1.0f / (near_plane - far_plane);
            r.m[14] = near_plane / (near_plane - far_plane);
            r.m[15] = 1.0f;
            return r;
        }

        VkShaderModule createShader(const char* name) {
            std::ifstream file(std::string(SHADER_DIR) + "/" + name, std::ios::ate | std::ios::binary);
            if (!file) {
                std::cerr << "Failed to open shader " << name << '\n';
                return VK_NULL_HANDLE;
            }

            std::vector<char> code(static_cast<size_t>(file.tellg()));
            file.seekg(0);
            file.read(code.data(), static_cast<std::streamsize>(code.size()));

            const VkShaderModuleCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size(),
                .pCode = reinterpret_cast<const uint32_t*>(code.data()),
            };

            VkShaderModule module = VK_NULL_HANDLE;
            vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &module);
            return module;
        }

        bool createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
            VkBuffer& buffer, VmaAllocation& allocation) {
            const VkBufferCreateInfo buffer_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size,
                .usage = usage,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            };

            const VmaAllocationCreateInfo allocation_info = {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            };

            VmaAllocationInfo mapped{};
            if (vmaCreateBuffer(graphics::internal::context.allocator, &buffer_info, &allocation_info,
                &buffer, &allocation, &mapped) != VK_SUCCESS) {
                std::cerr << "Failed to create buffer\n";
                return false;
            }

            std::memcpy(mapped.pMappedData, data, static_cast<size_t>(size));
            vmaFlushAllocation(graphics::internal::context.allocator, allocation, 0, size);
            return true;
        }

        bool createPipeline() {
            auto& context = graphics::internal::context;

            const VkPushConstantRange push_range = {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = sizeof(PushConstants),
            };

            const VkPipelineLayoutCreateInfo layout_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &push_range,
            };

            if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
                std::cerr << "Failed to create pipeline layout\n";
                return false;
            }

            const VkPipelineShaderStageCreateInfo stages[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = vertex_shader,
                    .pName = "main",
                },
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = fragment_shader,
                    .pName = "main",
                },
            };

            const VkVertexInputBindingDescription binding = {
                .binding = 0,
                .stride = sizeof(TorusVertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            };

            const VkVertexInputAttributeDescription attribute = {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = static_cast<uint32_t>(offsetof(TorusVertex, x)),
            };

            const VkPipelineVertexInputStateCreateInfo vertex_input = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = &binding,
                .vertexAttributeDescriptionCount = 1,
                .pVertexAttributeDescriptions = &attribute,
            };

            const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            };

            const VkPipelineViewportStateCreateInfo viewport = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1,
            };

            const VkPipelineRasterizationStateCreateInfo rasterization = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f,
            };

            const VkPipelineMultisampleStateCreateInfo multisample = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            };

            const VkPipelineDepthStencilStateCreateInfo depth = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = VK_TRUE,
                .depthCompareOp = VK_COMPARE_OP_LESS,
            };

            const VkPipelineColorBlendAttachmentState blend_attachment = {
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            };

            const VkPipelineColorBlendStateCreateInfo blend = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1,
                .pAttachments = &blend_attachment,
            };

            const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

            const VkPipelineDynamicStateCreateInfo dynamic = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2,
                .pDynamicStates = dynamic_states,
            };

            const VkGraphicsPipelineCreateInfo pipeline_info = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2,
                .pStages = stages,
                .pVertexInputState = &vertex_input,
                .pInputAssemblyState = &input_assembly,
                .pViewportState = &viewport,
                .pRasterizationState = &rasterization,
                .pMultisampleState = &multisample,
                .pDepthStencilState = &depth,
                .pColorBlendState = &blend,
                .pDynamicState = &dynamic,
                .layout = pipeline_layout,
                .renderPass = context.render_pass,
                .subpass = 0,
            };

            if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
                nullptr, &pipeline) != VK_SUCCESS) {
                std::cerr << "Failed to create pipeline\n";
                return false;
            }
            return true;
        }

        void destroyResources() {
            auto& context = graphics::internal::context;

            vkDestroyPipeline(context.device, pipeline, nullptr);
            vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
            vkDestroyShaderModule(context.device, vertex_shader, nullptr);
            vkDestroyShaderModule(context.device, fragment_shader, nullptr);

            if (vertex_buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_allocation);
            }
            if (index_buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(context.allocator, index_buffer, index_allocation);
            }

            pipeline = VK_NULL_HANDLE;
            pipeline_layout = VK_NULL_HANDLE;
            vertex_shader = VK_NULL_HANDLE;
            fragment_shader = VK_NULL_HANDLE;
            vertex_buffer = VK_NULL_HANDLE;
            index_buffer = VK_NULL_HANDLE;
        }

    }

    bool initialize([[maybe_unused]] GLFWwindow* window) {
        TorusMesh torus = torus_create(3.0f, 1.0f, 64, 32);
        if (torus.vertices == nullptr || torus.indices == nullptr) {
            std::cerr << "Failed to create torus\n";
            return false;
        }

        index_count = torus.index_count;

        const bool buffers_ok =
            createBuffer(torus.vertices, torus.vertex_count * sizeof(TorusVertex),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer, vertex_allocation) &&
            createBuffer(torus.indices, torus.index_count * sizeof(unsigned int),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer, index_allocation);

        torus_destroy(&torus);

        vertex_shader = createShader("torus.vert.spv");
        fragment_shader = createShader("torus.frag.spv");

        if (!buffers_ok || vertex_shader == VK_NULL_HANDLE ||
            fragment_shader == VK_NULL_HANDLE || !createPipeline()) {
            destroyResources();
            return false;
        }
        return true;
    }

    void shutdown() {
        vkQueueWaitIdle(graphics::internal::context.graphics_queue);
        destroyResources();
    }

    void update(double time) {
        float dt = static_cast<float>(time - previous_time);
        previous_time = time;
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        if (playing) {
            animation_time += dt * speed;
        }

        ImGui::Begin("Torus");

        int mode = perspective_projection ? 0 : 1;
        ImGui::RadioButton("Perspective", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Orthographic", &mode, 1);
        perspective_projection = mode == 0;

        if (perspective_projection) {
            ImGui::SliderFloat("FOV", &fov, 20.0f, 120.0f);
        }
        else {
            ImGui::SliderFloat("Ortho size", &ortho_size, 1.0f, 20.0f);
        }

        ImGui::Separator();
        ImGui::DragFloat3("Position", position, 0.05f);
        ImGui::DragFloat3("Rotation", rotation, 0.5f);
        ImGui::SliderFloat("Scale", &scale, 0.1f, 5.0f);

        ImGui::Separator();
        ImGui::ColorEdit3("Color", color);
        ImGui::Checkbox("Vertex colors", &vertex_colors);

        ImGui::Separator();
        if (ImGui::Button(playing ? "Pause" : "Play")) {
            playing = !playing;
        }
        ImGui::SliderFloat("Speed", &speed, 0.0f, 5.0f);
        ImGui::SliderFloat("Path radius", &radius, 0.0f, 8.0f);
        ImGui::SliderFloat("Spin", &spin, -360.0f, 360.0f);

        ImGui::End();
    }

    void render(const graphics::internal::FrameData& fd) {
        auto& context = graphics::internal::context;
        VkCommandBuffer cmd = fd.command_buffer;

        vkResetCommandBuffer(cmd, 0);

        const VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(cmd, &begin_info);

        const VkClearValue clear_values[] = {
            {.color = {.float32 = { 0.03f, 0.04f, 0.07f, 1.0f } } },
            {.depthStencil = { 1.0f, 0 } },
        };

        const VkRenderPassBeginInfo pass_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = context.render_pass,
            .framebuffer = fd.framebuffer,
            .renderArea = {.offset = { 0, 0 }, .extent = context.swapchain_extent },
            .clearValueCount = 2,
            .pClearValues = clear_values,
        };
        vkCmdBeginRenderPass(cmd, &pass_info, VK_SUBPASS_CONTENTS_INLINE);

        const float width = static_cast<float>(context.swapchain_extent.width);
        const float height = static_cast<float>(context.swapchain_extent.height);

        const VkViewport viewport = { 0.0f, 0.0f, width, height, 0.0f, 1.0f };
        const VkRect2D scissor = { { 0, 0 }, context.swapchain_extent };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const float t = animation_time;
        const float x = position[0] + radius * std::sin(t);
        const float y = position[1] + 0.5f * radius * std::sin(2.0f * t);

        Mat4 model = scaling(scale);
        model = multiply(rotation_matrix(0, rotation[0] * deg_to_rad), model);
        model = multiply(rotation_matrix(1, (rotation[1] + spin * t) * deg_to_rad), model);
        model = multiply(rotation_matrix(2, rotation[2] * deg_to_rad), model);
        model = multiply(translation(x, y, position[2]), model);

        const float aspect = width / height;
        const Mat4 projection = perspective_projection
            ? perspective(fov * deg_to_rad, aspect, 0.1f, 100.0f)
            : orthographic(ortho_size * aspect, ortho_size, 0.1f, 100.0f);

        const Mat4 mvp = multiply(projection, model);

        PushConstants push{};
        std::memcpy(push.mvp, mvp.m, sizeof(push.mvp));
        push.tint[0] = color[0];
        push.tint[1] = color[1];
        push.tint[2] = color[2];
        push.tint[3] = vertex_colors ? 1.0f : 0.0f;

        const VkDeviceSize offset = 0;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
        vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }

}