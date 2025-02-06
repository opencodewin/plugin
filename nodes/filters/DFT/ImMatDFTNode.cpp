#include <UI.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include "DFT_vulkan.h"
#include <SplitMerge_vulkan.h>

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct DFTNode final : Node
{
    BP_NODE_WITH_NAME(DFTNode, "DFT", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Filter#Video#Color")
    DFTNode(BP* blueprint): Node(blueprint) { m_Name = "DFT"; m_Skippable = true; }
    ~DFTNode()
    {
        if (m_filter) { delete m_filter; m_filter = nullptr; }
        if (m_splitter) { delete m_splitter; m_splitter = nullptr; }
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint, bool threading = false) override
    {
        auto mat_in = context.GetPinValue<ImGui::ImMat>(m_MatIn);
        if (!mat_in.empty())
        {
            int gpu = mat_in.device == IM_DD_VULKAN ? mat_in.device_number : ImGui::get_default_gpu_index();
            if (!m_Enabled)
            {
                m_MatOut.SetValue(mat_in);
                return m_Exit;
            }
            if (!m_filter || gpu != m_device)
            {
                if (m_filter) { delete m_filter; m_filter = nullptr; }
                m_filter = new ImGui::DFT_vulkan(gpu);
            }
            if (!m_filter)
            {
                return {};
            }
            if (!m_splitter || gpu != m_device)
            {
                if (m_splitter) { delete m_splitter; m_splitter = nullptr; }
                m_splitter = new ImGui::SplitMerge_vulkan(gpu);
            }
            if (!m_splitter)
                return {};
            m_device = gpu;
            ImGui::ImMat im_dft;
            if (mat_in.c == 1)
                m_NodeTimeMs = m_filter->dft(mat_in, im_dft);
            else
            {
                std::vector<ImGui::ImMat> split_mats;
                m_splitter->split(mat_in, split_mats);
                if (!split_mats.empty())
                {
                    m_NodeTimeMs = m_filter->dft(split_mats[0], im_dft);
                }
            }
            im_dft.flags |= IM_MAT_FLAGS_IMAGE_FRAME;
            m_MatOut.SetValue(im_dft);
        }
        return m_Exit;
    }

    bool DrawSettingLayout(ImGuiContext * ctx) override
    {
        // Draw Setting
        auto changed = Node::DrawSettingLayout(ctx);
        ImGui::Separator();
        changed |= Node::DrawDataTypeSetting("Mat Type:", m_mat_data_type);
        return changed;
    }

    int Load(const imgui_json::value& value) override
    {
        int ret = BP_ERR_NONE;
        if ((ret = Node::Load(value)) != BP_ERR_NONE)
            return ret;

        if (value.contains("mat_type"))
        {
            auto& val = value["mat_type"];
            if (val.is_number()) 
                m_mat_data_type = (ImDataType)val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }
    Pin* GetAutoLinkInputFlowPin() override { return &m_Enter; }
    Pin* GetAutoLinkOutputFlowPin() override { return &m_Exit; }
    vector<Pin*> GetAutoLinkInputDataPin() override { return {&m_MatIn}; }
    vector<Pin*> GetAutoLinkOutputDataPin() override { return {&m_MatOut}; }

    FlowPin   m_Enter   = { this, "Enter" };
    FlowPin   m_Exit    = { this, "Exit" };
    MatPin    m_MatIn   = { this, "In" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[2] = { &m_Enter, &m_MatIn };
    Pin* m_OutputPins[2] = { &m_Exit, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    int m_device            {-1};
    ImGui::DFT_vulkan * m_filter {nullptr};
    ImGui::SplitMerge_vulkan * m_splitter {nullptr};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(DFTNode, "DFT", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Filter#Video#Color")
