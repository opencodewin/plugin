#include <UI.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include <Harris_vulkan.h>

#define NODE_VERSION    0x01010000
namespace BluePrint
{
struct HarrisNode final : Node
{
    BP_NODE_WITH_NAME(HarrisNode, "Image Harris Corner", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Scope#Video")
    HarrisNode(BP* blueprint): Node(blueprint) { m_Name = "Image Harris Corner"; m_HasCustomLayout = true; m_Skippable = true; }
    ~HarrisNode()
    {
        if (m_scope) { delete m_scope; m_scope = nullptr; }
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
        if (m_scope) { delete m_scope; m_scope = nullptr; }
    }

    void OnStop(Context& context) override
    {
        // keep last Mat
        //m_mutex.lock();
        //m_MatOut.SetValue(ImGui::ImMat());
        //m_mutex.unlock();
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint, bool threading = false) override
    {
        if (entryPoint.m_ID == m_IReset.m_ID)
        {
            Reset(context);
            return m_OReset;
        }
        auto mat_in = context.GetPinValue<ImGui::ImMat>(m_MatIn);
        if (!mat_in.empty())
        {
            if (!m_Enabled)
            {
                m_MatOut.SetValue(mat_in);
                return m_Exit;
            }
            if (!m_scope)
            {
                int gpu = mat_in.device == IM_DD_VULKAN ? mat_in.device_number : ImGui::get_default_gpu_index();
                if (m_scope) { delete m_scope; m_scope = nullptr; }
                m_scope = new ImGui::Harris_vulkan(gpu);
            }
            if (!m_scope)
            {
                return {};
            }
            ImGui::VkMat im_RGB; im_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_scope->scope(mat_in, im_RGB, m_blurRadius, m_edgeStrength, m_Threshold, m_harris, m_sensitivity, m_noble);
            im_RGB.time_stamp = mat_in.time_stamp;
            im_RGB.rate = mat_in.rate;
            im_RGB.flags = mat_in.flags;
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
        float setting_offset = 320;
        bool changed = false;
        static ImGuiSliderFlags flags = ImGuiSliderFlags_NoInput;
        int _blurRadius = m_blurRadius;
        float _Threshold = m_Threshold;
        float _edgeStrength = m_edgeStrength;
        float _harris = m_harris;
        float _sensitivity = m_sensitivity;
        bool _noble = m_noble;
        ImGui::Dummy(ImVec2(200, 8));
        ImGui::PushItemWidth(200);
        ImGui::BeginDisabled(!m_Enabled);
        ImGui::SliderInt("Blur Radius##HarrisCorner", &_blurRadius, 0, 10, "%d", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_radius##HarrisCorner")) { _blurRadius = 3; changed = true; }
        ImGui::SliderFloat("Threshold##HarrisCorner", &_Threshold, 0.1f, 1.f, "%.2f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_threshold##HarrisCorner")) { _Threshold = 0.1; changed = true; }
        ImGui::SliderFloat("Edge Strength##HarrisCorner", &_edgeStrength, 0.1, 5.f, "%.1f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_strength##HarrisCorner")) { _edgeStrength = 2.0; changed = true; }
        ImGui::SliderFloat("Harris##HarrisCorner", &_harris, 0.01, 0.5f, "%.2f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_harris##HarrisCorner")) { _harris = 0.04; changed = true; }
        ImGui::SliderFloat("Sensitivity##HarrisCorner", &_sensitivity, 1.0, 10.0f, "%.1f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_sensitivity##HarrisCorner")) { _sensitivity = 5.0; changed = true; }
        if (ImGui::Checkbox("Noble", &_noble)) { changed = true; }
        ImGui::PopItemWidth();
        if (m_blurRadius != _blurRadius) { m_blurRadius = _blurRadius; changed = true; }
        if (m_Threshold != _Threshold) { m_Threshold = _Threshold; changed = true; }
        if (m_edgeStrength != _edgeStrength) { m_edgeStrength = _edgeStrength; changed = true; }
        if (m_harris != _harris) { m_harris = _harris; changed = true; }
        if (m_sensitivity != _sensitivity) { m_sensitivity = _sensitivity; changed = true; }
        if (m_noble != _noble) { m_noble = _noble; changed = true; }
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
        if (value.contains("Radius"))
        {
            auto& val = value["Radius"];
            if (val.is_number()) 
                m_blurRadius = val.get<imgui_json::number>();
        }
        if (value.contains("edgeStrength"))
        {
            auto& val = value["edgeStrength"];
            if (val.is_number()) 
                m_edgeStrength = val.get<imgui_json::number>();
        }
        if (value.contains("Threshold"))
        {
            auto& val = value["Threshold"];
            if (val.is_number()) 
                m_Threshold = val.get<imgui_json::number>();
        }
        if (value.contains("harris"))
        {
            auto& val = value["harris"];
            if (val.is_number()) 
                m_harris = val.get<imgui_json::number>();
        }
        if (value.contains("sensitivity"))
        {
            auto& val = value["sensitivity"];
            if (val.is_number()) 
                m_sensitivity = val.get<imgui_json::number>();
        }
        if (value.contains("noble"))
        {
            auto& val = value["noble"];
            if (val.is_boolean()) 
                m_noble = val.get<imgui_json::boolean>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["Radius"] = imgui_json::number(m_blurRadius);
        value["edgeStrength"] = imgui_json::number(m_edgeStrength);
        value["Threshold"] = imgui_json::number(m_Threshold);
        value["harris"] = imgui_json::number(m_harris);
        value["sensitivity"] = imgui_json::number(m_sensitivity);
        value["noble"] = imgui_json::boolean(m_noble);
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }
    Pin* GetAutoLinkInputFlowPin() override { return &m_Enter; }
    Pin* GetAutoLinkOutputFlowPin() override { return &m_Exit; }
    vector<Pin*> GetAutoLinkInputDataPin() override { return {&m_MatIn}; }
    vector<Pin*> GetAutoLinkOutputDataPin() override { return {&m_MatOut}; }

    FlowPin   m_Enter   = { this, "Enter" };
    FlowPin   m_IReset  = { this, "Reset In" };
    FlowPin   m_Exit    = { this, "Exit" };
    FlowPin   m_OReset  = { this, "Reset Out" };
    MatPin    m_MatIn   = { this, "In" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[3] = { &m_Enter, &m_IReset, &m_MatIn };
    Pin* m_OutputPins[3] = { &m_Exit, &m_OReset, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    float m_Threshold       {0.1};
    float m_edgeStrength    {2.0};
    int m_blurRadius        {3};
    float m_harris          {0.04};
    float m_sensitivity     {5.0};
    bool m_noble            {false};
    ImGui::Harris_vulkan * m_scope {nullptr};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(HarrisNode, "Harris Corner", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Scope#Video")
