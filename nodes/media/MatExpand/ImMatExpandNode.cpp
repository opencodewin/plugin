#include <UI.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include <Expand_vulkan.h>

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct ExpandNode final : Node
{
    BP_NODE_WITH_NAME(ExpandNode, "Mat Expand", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Media")
    ExpandNode(BP* blueprint): Node(blueprint) { m_Name = "Mat Expand"; m_HasCustomLayout = true; m_Skippable = true; }

    ~ExpandNode()
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
        if (!mat_in.empty())
        {
            m_in_size = ImVec2(mat_in.w, mat_in.h);
            int gpu = mat_in.device == IM_DD_VULKAN ? mat_in.device_number : ImGui::get_default_gpu_index();
            if (!m_Enabled)
            {
                m_MatOut.SetValue(mat_in);
                return m_Exit;
            }
            if (!m_filter || gpu != m_device)
            {
                if (m_filter) { delete m_filter; m_filter = nullptr; }
                m_filter = new ImGui::Expand_vulkan(gpu);
            }
            if (!m_filter)
            {
                return {};
            }
            if (m_top_expand == 0.f && m_bottom_expand == 0.f && m_left_expand == 0.f && m_right_expand == 0.f)
            {
                m_MatOut.SetValue(mat_in);
                return m_Exit;
            }
            m_device = gpu;
            ImGui::VkMat im_RGB; im_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_filter->expand(mat_in, im_RGB, mat_in.h * m_top_expand, mat_in.h * m_bottom_expand, mat_in.w * m_left_expand, mat_in.w * m_right_expand);
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
        bool changed = false;
        float _top = m_top_expand;
        float _bottom = m_bottom_expand;
        float _left = m_left_expand;
        float _right = m_right_expand;
        auto slider_tooltips = [&]()
        {
            if (m_in_size.x > 0 && m_in_size.y > 0)
            {
                if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    ed::Suspend();
                    if (ImGui::BeginTooltip())
                    {
                        ImGui::Text("Source: %dx%d", (int)m_in_size.x, (int)m_in_size.y);
                        ImGui::Text("   Top: %d", (int)(m_in_size.y * _top));
                        ImGui::Text("Bottom: %d", (int)(m_in_size.y * _bottom));
                        ImGui::Text("  Left: %d", (int)(m_in_size.x * _left));
                        ImGui::Text(" Right: %d", (int)(m_in_size.x * _right));
                        ImGui::Text("Target: %dx%d", (int)(m_in_size.x + m_in_size.x * (_left + _right)), (int)(m_in_size.y + m_in_size.y * (_top + _bottom)));
                        ImGui::EndTooltip();
                    }
                    ed::Resume();
                }
            }
        };
        static ImGuiSliderFlags flags = ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Stick;
        ImGui::PushItemWidth(300);
        ImGui::BeginDisabled(!m_Enabled);
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SliderFloat("top", &_top, 0.0, 1.f, "%.3f", flags); slider_tooltips();
        ImGui::SliderFloat("bottom", &_bottom, 0.0, 1.f, "%.3f", flags); slider_tooltips();
        ImGui::SliderFloat("left", &_left, 0.0, 1.f, "%.3f", flags); slider_tooltips();
        ImGui::SliderFloat("right", &_right, 0.0, 1.f, "%.3f", flags); slider_tooltips();
        ImGui::PopItemWidth();
        if (_top != m_top_expand) { m_top_expand = _top; changed = true; }
        if (_bottom != m_bottom_expand) { m_bottom_expand = _bottom; changed = true; }
        if (_left != m_left_expand) { m_left_expand = _left; changed = true; }
        if (_right != m_right_expand) { m_right_expand = _right; changed = true; }
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
        if (value.contains("top"))
        {
            auto& val = value["top"];
            if (val.is_number()) 
                m_top_expand = val.get<imgui_json::number>();
        }
        if (value.contains("bottom"))
        {
            auto& val = value["bottom"];
            if (val.is_number()) 
                m_bottom_expand = val.get<imgui_json::number>();
        }
        if (value.contains("left"))
        {
            auto& val = value["left"];
            if (val.is_number()) 
                m_left_expand = val.get<imgui_json::number>();
        }
        if (value.contains("right"))
        {
            auto& val = value["right"];
            if (val.is_number()) 
                m_right_expand = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["top"] = imgui_json::number(m_top_expand);
        value["bottom"] = imgui_json::number(m_bottom_expand);
        value["left"] = imgui_json::number(m_left_expand);
        value["right"] = imgui_json::number(m_right_expand);
    }

    void DrawNodeLogo(ImGuiContext * ctx, ImVec2 size, std::string logo) const override
    {
        Node::DrawNodeLogo(ctx, size, std::string(u8"\uea37"));
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
    ImGui::Expand_vulkan * m_filter {nullptr};
    float m_top_expand {0.f};
    float m_bottom_expand {0.f};
    float m_left_expand {0.f};
    float m_right_expand {0.f};
    ImVec2 m_in_size {0.f, 0.f};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(ExpandNode, "Mat Expand", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Media")
