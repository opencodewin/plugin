#pragma once
#include "imvk_gpu.h"
#include "imvk_pipeline.h"
#include "immat.h"
#include <string>

namespace ImGui
{
class VKSHADER_API Lighting_vulkan
{
public:
    Lighting_vulkan(int gpu = 0);
    ~Lighting_vulkan();

    double effect(const ImMat& src, ImMat& dst, float progress, int count, float saturation, float light);

private:
    const VulkanDevice* vkdev   {nullptr};
    Option opt;
    Pipeline* pipe              {nullptr};
    VkCompute * cmd             {nullptr};

private:
    void upload_param(const VkMat& src, VkMat& dst, float progress, int count, float saturation, float light);
};
} // namespace ImGui
