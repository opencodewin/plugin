#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <UI.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include <SeamlessCloning_vulkan.h>

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct BlendNode final : Node
{
    BP_NODE_WITH_NAME(BlendNode, "Poisson Image Blend", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Media")
    BlendNode(BP* blueprint): Node(blueprint) { m_Name = "Poisson Blend"; m_HasCustomLayout = true; }

    ~BlendNode()
    {
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
        if (entryPoint.m_ID == m_IReset.m_ID)
        {
            Reset(context);
            return m_OReset;
        }
        auto b_mat_in = context.GetPinValue<ImGui::ImMat>(m_MatBIn);
        auto f_mat_in = context.GetPinValue<ImGui::ImMat>(m_MatFIn);
        auto m_mat_in = context.GetPinValue<ImGui::ImMat>(m_MatMIn);
        if (!b_mat_in.empty())
        {
            if (!m_Enabled)
            {
                m_MatOut.SetValue(b_mat_in);
                return m_Exit;
            }
            int gpu = b_mat_in.device == IM_DD_VULKAN ? b_mat_in.device_number : ImGui::get_default_gpu_index();
            ImGui::ImMat im_RGB;
            m_NodeTimeMs = ImGui::SeamlessClone(f_mat_in, b_mat_in, m_mat_in, im_RGB, ImPoint(m_offset_x * b_mat_in.w, m_offset_y * b_mat_in.h), m_clone_type, gpu);
            im_RGB.flags |= IM_MAT_FLAGS_VIDEO_FRAME;
            m_MatOut.SetValue(im_RGB);
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
        float setting_offset = 348;
        if (!embedded)
        {
            ImVec2 sub_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 sub_window_size = ImGui::GetWindowSize();
            setting_offset = sub_window_size.x - 80;
        }
        bool changed = false;
        float _offset_x = m_offset_x;
        float _offset_y = m_offset_y;
        int _clone_type = m_clone_type;

        static ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Stick;
        ImGui::PushStyleColor(ImGuiCol_Button, 0);
        ImGui::PushItemWidth(300);
        ImGui::BeginDisabled(!m_Enabled);
        ImGui::SliderFloat("Offset X##Blend", &_offset_x, 0.f, 1.f, "%.2f", flags); 
        ImGui::SameLine(setting_offset); if (ImGui::Button(ICON_RESET "##reset_offset_x##Blend")) { _offset_x = 0; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::SliderFloat("Offset Y##Blend", &_offset_y, 0.f, 1.f, "%.2f", flags);
        ImGui::SameLine(setting_offset); if (ImGui::Button(ICON_RESET "##reset_offset_y##Blend")) { _offset_y = 0; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::Combo("Type", &_clone_type, "Normal seamless\0Mixed seamless\0RMonochrome transfer\0\0");
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        if (_offset_x != m_offset_x) { m_offset_x = _offset_x; changed = true; }
        if (_offset_y != m_offset_y) { m_offset_y = _offset_y; changed = true; }
        if (_clone_type != m_clone_type) { m_clone_type = _clone_type;  changed = true; }
        ImGui::EndDisabled();
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
        if (value.contains("offset_x"))
        {
            auto& val = value["offset_x"];
            if (val.is_number()) 
                m_offset_x = val.get<imgui_json::number>();
        }
        if (value.contains("offset_y"))
        {
            auto& val = value["offset_y"];
            if (val.is_number()) 
                m_offset_y = val.get<imgui_json::number>();
        }
        if (value.contains("type"))
        {
            auto& val = value["type"];
            if (val.is_number()) 
                m_clone_type = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["offset_x"] = imgui_json::number(m_offset_x);
        value["offset_y"] = imgui_json::number(m_offset_y);
        value["type"] = imgui_json::number(m_clone_type);
    }

    void DrawNodeLogo(ImGuiContext * ctx, ImVec2 size, std::string logo) const override
    {
        Node::DrawNodeLogo(ctx, size, std::string(u8"\ue434"));
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }
    Pin* GetAutoLinkInputFlowPin() override { return &m_Enter; }
    Pin* GetAutoLinkOutputFlowPin() override { return &m_Exit; }
    vector<Pin*> GetAutoLinkInputDataPin() override { return {&m_MatBIn, &m_MatFIn, &m_MatMIn}; }
    vector<Pin*> GetAutoLinkOutputDataPin() override { return {&m_MatOut}; }

    FlowPin   m_Enter   = { this, "Enter" };
    FlowPin   m_IReset  = { this, "Reset In" };
    FlowPin   m_Exit    = { this, "Exit" };
    FlowPin   m_OReset  = { this, "Reset Out" };
    MatPin    m_MatBIn  = { this, "InBack" };
    MatPin    m_MatFIn  = { this, "InFront" };
    MatPin    m_MatMIn  = { this, "InMask" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[5] = { &m_Enter, &m_IReset, &m_MatBIn, &m_MatFIn, &m_MatMIn };
    Pin* m_OutputPins[3] = { &m_Exit, &m_OReset, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    float m_offset_x {0.f};
    float m_offset_y {0.f};
    int m_clone_type {0};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(BlendNode, "Poisson Image Blend", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Media")
