#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include <Waveform_vulkan.h>

#define NODE_VERSION    0x01030000

namespace BluePrint
{
struct MatWaveformNode final : Node
{
    BP_NODE_WITH_NAME(MatWaveformNode, "Image Waveform", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Scope#Video")
    MatWaveformNode(BP* blueprint): Node(blueprint) { m_Name = "Image Waveform"; m_HasCustomLayout = true; }

    ~MatWaveformNode()
    {
        if (m_scope) { delete m_scope; m_scope = nullptr; }
        ImGui::ImDestroyTexture(&m_texture);
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
        if (m_scope) { delete m_scope; m_scope = nullptr; }
        ImGui::ImDestroyTexture(&m_texture);
        m_RGB.release();
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
        auto mat_in = context.GetPinValue<ImGui::ImMat>(m_MatIn);
        if (!mat_in.empty())
        {
            m_MatOut.SetValue(mat_in);
            if (!m_scope)
            {
                int gpu = mat_in.device == IM_DD_VULKAN ? mat_in.device_number : ImGui::get_default_gpu_index();
                if (m_scope) { delete m_scope; m_scope = nullptr; }
                ImGui::ImDestroyTexture(&m_texture);
                m_scope = new ImGui::Waveform_vulkan(gpu);
            }
            if (!m_scope)
            {
                return {};
            }
            m_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_scope->scope(mat_in, m_RGB, 256, m_fintensity, m_component, m_showY);
            m_RGB.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
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
        auto drawList  = ImGui::GetWindowDrawList();
        bool changed = false;
        bool _mirror = m_mirror;
        bool _component = m_component;
        bool _showY = m_showY;
        float _fintensity = m_fintensity;
        ImGui::Dummy(ImVec2(300, 8));
        ImGui::PushItemWidth(300);
        static ImGuiSliderFlags flags = ImGuiSliderFlags_NoInput;
        ImGui::TextUnformatted("Mirror");ImGui::SameLine();
        ImGui::ToggleButton("##mirror",&_mirror);
        ImGui::TextUnformatted("Component");ImGui::SameLine();
        ImGui::ToggleButton("##component",&_component);
        ImGui::TextUnformatted("Show Y");ImGui::SameLine();
        ImGui::ToggleButton("##showY",&_showY);
        ImGui::SliderFloat("Intensity", &_fintensity, 0.01f, 20.0f, "%.2f", flags);
        ImGui::PopItemWidth();
        if (_mirror != m_mirror) { m_mirror = _mirror; changed = true; }
        if (_component != m_component) { m_component = _component; changed = true; }
        if (_fintensity != m_fintensity) { m_fintensity = _fintensity; changed = true;}
        if (_showY != m_showY) { m_showY = _showY; changed = true;}
        ImGui::Dummy(ImVec2(0, 4));
        auto cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 size(512, 512);
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, cursorPos + size, IM_COL32_BLACK);
        changed = ImGui::ScopeViewWaveform(
                "##waveform_view", cursorPos, size,
                m_RGB,
                m_texture, 
                m_fintensity,
                m_component,
                m_mirror,
                m_showY
            );
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
        if (value.contains("mirror"))
        {
            auto& val = value["mirror"];
            if (val.is_boolean()) 
                m_mirror = val.get<imgui_json::boolean>();
        }
        if (value.contains("component"))
        {
            auto& val = value["component"];
            if (val.is_boolean()) 
                m_component = val.get<imgui_json::boolean>();
        }
        if (value.contains("showY"))
        {
            auto& val = value["showY"];
            if (val.is_boolean()) 
                m_showY = val.get<imgui_json::boolean>();
        }
        if (value.contains("fintensity"))
        {
            auto& val = value["fintensity"];
            if (val.is_number()) 
                m_fintensity = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["mirror"] = m_mirror;
        value["component"] = m_component;
        value["showY"] = m_showY;
        value["fintensity"] = imgui_json::number(m_fintensity);
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }
    Pin* GetAutoLinkInputFlowPin() override { return &m_Enter; }
    Pin* GetAutoLinkOutputFlowPin() override { return &m_Exit; }
    vector<Pin*> GetAutoLinkInputDataPin() override { return {&m_MatIn}; }
    vector<Pin*> GetAutoLinkOutputDataPin() override { return {&m_MatOut}; }

    FlowPin   m_Enter   = { this, "Enter" };
    FlowPin   m_Exit    = { this, "Exit" };
    FlowPin   m_OReset  = { this, "Reset Out" };
    MatPin    m_MatIn   = { this, "In" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[2] = { &m_Enter, &m_MatIn };
    Pin* m_OutputPins[3] = { &m_Exit, &m_OReset, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    bool m_mirror           {true};
    bool m_component        {false};
    bool m_showY            {false};
    float m_fintensity      {10.0};
    ImGui::Waveform_vulkan * m_scope      {nullptr};
    ImGui::ImMat            m_RGB;
    ImTextureID             m_texture   {0};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(MatWaveformNode, "Image Waveform", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Scope#Video")
