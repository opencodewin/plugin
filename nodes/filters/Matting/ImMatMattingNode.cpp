#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <UI.h>
#include <ImVulkanShader.h>
#include "Matting_vulkan.h"

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct MattingNode final : Node
{
    BP_NODE_WITH_NAME(MattingNode, "Matting Filter", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Filter#Video#Matting")
    MattingNode(BP* blueprint): Node(blueprint) { m_Name = "Matting Filter"; m_HasCustomLayout = true; m_Skippable = true; }
    ~MattingNode()
    {
        if (m_filter) { delete m_filter; m_filter = nullptr; }
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
        m_mutex.lock();
        m_MatOut.SetValue(ImGui::ImMat());
        m_mutex.unlock();
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint, bool threading = false) override
    {
        auto mat_in = context.GetPinValue<ImGui::ImMat>(m_MatIn);
        auto mat_tri = context.GetPinValue<ImGui::ImMat>(m_MatTri);
        if (!mat_in.empty())
        {
            int gpu = mat_in.device == IM_DD_VULKAN ? mat_in.device_number : ImGui::get_default_gpu_index();
            if (!m_Enabled)
            {
                m_MatOut.SetValue(mat_tri);
                return m_Exit;
            }
            if (!m_filter || gpu != m_device)
            {
                if (m_filter) { delete m_filter; m_filter = nullptr; }
                m_filter = new ImGui::Matting_vulkan(gpu);
            }
            if (!m_filter)
            {
                return {};
            }
            m_device = gpu;
            ImGui::ImMat im_alpha;
            m_NodeTimeMs = m_filter->filter(mat_in, im_alpha, mat_tri, m_expand, m_range);
            m_MatOut.SetValue(im_alpha);
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

    bool DrawCustomLayout(ImGuiContext * ctx, float zoom, ImVec2 origin, ImGui::ImCurveEdit::Curve * key, bool embedded) override
    {
        ImGui::SetCurrentContext(ctx);
        float setting_offset = 320;
        if (!embedded)
        {
            ImVec2 sub_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 sub_window_size = ImGui::GetWindowSize();
            setting_offset = sub_window_size.x - 80;
        }
        bool changed = false;
        int _expand = m_expand;
        int _range = m_range;
        static ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Stick;
        ImGui::PushStyleColor(ImGuiCol_Button, 0);
        ImGui::PushItemWidth(200);
        ImGui::BeginDisabled(!m_Enabled);
        ImGui::SliderInt("Expand##GuidedFilter", &_expand, 4, 32, "%.d", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_expand#GuidedFilter")) { _expand = 16; changed = true; }
        ImGui::ShowTooltipOnHover("Reset Expand");
        ImGui::SliderInt("Range##GuidedFilter", &_range, 1, 512, "%.d", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_range##GuidedFilter")) { _range = 200; changed = true; }
        ImGui::ShowTooltipOnHover("Reset Range");
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        if (_range != m_range) { m_range = _range; changed = true; }
        if (_expand != m_expand) { m_expand = _expand; changed = true; }
        return m_Enabled ? changed : false;
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
        if (value.contains("range"))
        {
            auto& val = value["range"];
            if (val.is_number()) 
                m_range = val.get<imgui_json::number>();
        }
        if (value.contains("expand"))
        {
            auto& val = value["expand"];
            if (val.is_number()) 
                m_expand = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["range"] = imgui_json::number(m_range);
        value["expand"] = imgui_json::number(m_expand);
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
    MatPin    m_MatTri  = { this, "Tri" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[3] = { &m_Enter, &m_MatIn, &m_MatTri };
    Pin* m_OutputPins[2] = { &m_Exit, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    int m_device        {-1};
    int m_expand        {16};
    int m_range         {200};
    ImGui::Matting_vulkan * m_filter   {nullptr};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(MattingNode, "Matting Filter", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Filter#Video#Matting")
