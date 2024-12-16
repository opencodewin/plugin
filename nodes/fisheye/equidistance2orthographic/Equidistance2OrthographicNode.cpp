#include <UI.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include "equidistance2orthographic_vulkan.h"

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct Equidistance2OrthographicNode final : Node
{
    BP_NODE_WITH_NAME(Equidistance2OrthographicNode, "Equidistance2Orthographic", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Filter#Video#Fisheye")
    Equidistance2OrthographicNode(BP* blueprint): Node(blueprint) { m_Name = "Equidistance to Orthographic"; m_HasCustomLayout = true; m_Skippable = true; }
    ~Equidistance2OrthographicNode()
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
        if (m_FOVIn.IsLinked()) m_fov = context.GetPinValue<float>(m_FOVIn);
        if (m_CXIn.IsLinked()) m_cx = context.GetPinValue<float>(m_CXIn);
        if (m_CYIn.IsLinked()) m_cy = context.GetPinValue<float>(m_CYIn);
        
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
                m_filter = new ImGui::Equidistance2Orthographic_vulkan(gpu);
            }
            if (!m_filter)
            {
                return {};
            }
            m_device = gpu;
            ImGui::VkMat im_RGB; im_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_filter->filter(mat_in, im_RGB, m_fov, m_cx, m_cy, m_interpolate);
            m_MatOut.SetValue(im_RGB);
        }
        return m_Exit;
    }

    void WasUnlinked(const Pin& receiver, const Pin& provider) override
    {
        if (receiver.m_ID == m_FOVIn.m_ID) m_FOVIn.SetValue(m_fov);
        if (receiver.m_ID == m_CXIn.m_ID) m_CXIn.SetValue(m_cx);
        if (receiver.m_ID == m_CYIn.m_ID) m_CYIn.SetValue(m_cy);
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
        float _fov = m_fov;
        float _cx = m_cx;
        float _cy = m_cy;
        ImInterpolateMode _interpolate = m_interpolate;
        static ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Stick;
        ImGui::PushStyleColor(ImGuiCol_Button, 0);
        ImGui::PushItemWidth(200);
        ImGui::BeginDisabled(!m_Enabled);
        ImGui::SliderFloat("FOV", &_fov, 0.f, 360.f, "%.3f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_fov##Equidistance2Orthographic")) { _fov = 180.f; changed = true; }
        if (!embedded) ImGui::ShowTooltipOnHover("Reset");
        ImGui::SliderFloat("Center X", &_cx, 0.f, 1.f, "%.3f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_cx##Equidistance2Orthographic")) { _cx = .5f; changed = true; }
        if (!embedded) ImGui::ShowTooltipOnHover("Reset");
        ImGui::SliderFloat("Center Y", &_cy, 0.f, 1.f, "%.3f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_cy##Equidistance2Orthographic")) { _cy = .5f; changed = true; }
        if (!embedded) ImGui::ShowTooltipOnHover("Reset");
        ImGui::RadioButton("Bilinear",  (int *)&_interpolate, IM_INTERPOLATE_BILINEAR); ImGui::SameLine();
        ImGui::RadioButton("Bicubic",   (int *)&_interpolate, IM_INTERPOLATE_BICUBIC);
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        if (_fov != m_fov) { m_fov = _fov; changed = true; }
        if (_cx != m_cx) { m_cx = _cx; changed = true; }
        if (_cy != m_cy) { m_cy = _cy; changed = true; }
        if (_interpolate != m_interpolate) { m_interpolate = _interpolate; changed = true; }
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
        if (value.contains("fov"))
        {
            auto& val = value["fov"];
            if (val.is_number()) 
                m_fov = val.get<imgui_json::number>();
        }
        if (value.contains("cx"))
        {
            auto& val = value["cx"];
            if (val.is_number()) 
                m_cx = val.get<imgui_json::number>();
        }
        if (value.contains("cy"))
        {
            auto& val = value["cy"];
            if (val.is_number()) 
                m_cy = val.get<imgui_json::number>();
        }
        if (value.contains("interpolate"))
        {
            auto& val = value["interpolate"];
            if (val.is_number()) 
                m_interpolate = (ImInterpolateMode)val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["fov"] = imgui_json::number(m_fov);
        value["cx"] = imgui_json::number(m_cx);
        value["cy"] = imgui_json::number(m_cy);
        value["interpolate"] = imgui_json::number(m_interpolate);
    }

    void DrawNodeLogo(ImGuiContext * ctx, ImVec2 size, std::string logo) const override
    {
        Node::DrawNodeLogo(ctx, size, std::string(u8"\ue3e9"));
        //if (ctx) ImGui::SetCurrentContext(ctx); // External Node must set context
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
    FloatPin  m_FOVIn   = { this, "FOV" };
    FloatPin  m_CXIn    = { this, "CX" };
    FloatPin  m_CYIn    = { this, "CY" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[5] = { &m_Enter, &m_MatIn, &m_FOVIn, &m_CXIn, &m_CYIn };
    Pin* m_OutputPins[2] = { &m_Exit, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    int m_device            {-1};
    float m_fov             {180};
    float m_cx              {0.5};
    float m_cy              {0.5};
    ImInterpolateMode m_interpolate {IM_INTERPOLATE_BICUBIC}; // IM_INTERPOLATE_BILINEAR/IM_INTERPOLATE_BICUBIC
    ImGui::Equidistance2Orthographic_vulkan * m_filter {nullptr};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(Equidistance2OrthographicNode, "Equidistance to Orthographic", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Filter#Video#Fisheye")
