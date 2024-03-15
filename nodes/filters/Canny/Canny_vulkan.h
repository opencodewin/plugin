#pragma once
#include "imvk_gpu.h"
#include "imvk_pipeline.h"
#include "immat.h"

namespace ImGui
{
class VKSHADER_API Canny_vulkan
{
public:
    Canny_vulkan(int gpu = 0);
    ~Canny_vulkan();
    
    double filter(const ImMat& src, ImMat& dst, int _blurRadius, float minThreshold, float maxThreshold);

private:
    VulkanDevice* vkdev      {nullptr};
    Option opt;
    Pipeline* pipe           {nullptr};
    Pipeline* pipe_dsobel    {nullptr};
    Pipeline* pipe_nms       {nullptr};
    Pipeline * pipe_column   {nullptr};
    Pipeline * pipe_row      {nullptr};
    VkCompute * cmd          {nullptr};

private:
    ImMat kernel;
    VkMat vk_kernel;
    int blurRadius  {3};
    int xksize;
    int yksize;
    int xanchor;
    int yanchor;
    float sigma     {0};

private:
    void upload_param(const VkMat& src, VkMat& dst, int _blurRadius, float minThreshold, float maxThreshold);
    void prepare_kernel();
};
} // namespace ImGui

