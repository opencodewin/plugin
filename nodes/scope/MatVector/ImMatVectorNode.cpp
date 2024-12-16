#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include <Vector_vulkan.h>

#define NODE_VERSION    0x01030000
namespace BluePrint
{
struct MatVectorNode final : Node
{
    BP_NODE_WITH_NAME(MatVectorNode, "Image Vector", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Scope#Video")
    MatVectorNode(BP* blueprint): Node(blueprint) { m_Name = "Image Vector"; m_HasCustomLayout = true; }

    ~MatVectorNode()
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
                m_scope = new ImGui::Vector_vulkan(gpu);
            }
            if (!m_scope)
            {
                return {};
            }
            //m_scope->SetParam(m_color_system, m_cie_system, (256 << m_quality), m_gamuts, 0.75f, m_correct_gamma);
            m_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_scope->scope(mat_in, m_RGB, m_intensity);
            m_RGB.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
        }
        return m_Exit;
    }

    bool DrawCustomLayout(ImGuiContext * ctx, float zoom, ImVec2 origin, ImGui::ImCurveEdit::Curve * key, bool embedded) override
    {
        ImGui::SetCurrentContext(ctx);
        auto drawList  = ImGui::GetWindowDrawList();
        bool changed = false;
        float _intensity = m_intensity;
        ImGui::Dummy(ImVec2(300, 8));
        ImGui::PushItemWidth(300);
        static ImGuiSliderFlags flags = ImGuiSliderFlags_NoInput;
        ImGui::SliderFloat("Intensity", &_intensity, 0.01f, 1.0f, "%.2f", flags);
        ImGui::PopItemWidth();
        if (_intensity != m_intensity) { m_intensity = _intensity; changed = true; }
        ImGui::Dummy(ImVec2(0, 4));
        auto cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 size(512, 512);
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, cursorPos + size, IM_COL32_BLACK);
        changed = ImGui::ScopeViewVideoVector(
                "##waveform_view", cursorPos, size,
                m_RGB, m_texture,
                m_intensity
            );
        return changed;
    }

    bool DrawSettingLayout(ImGuiContext * ctx) override
    {
        // Draw Set Node Name
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
        if (value.contains("fintensity"))
        {
            auto& val = value["fintensity"];
            if (val.is_number()) 
                m_intensity = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["fintensity"] = imgui_json::number(m_intensity);
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
    ImDataType              m_mat_data_type {IM_DT_UNDEFINED};
    ImGui::Vector_vulkan *  m_scope   {nullptr};
    ImGui::ImMat    m_RGB;
    float           m_intensity {1.00};
    ImTextureID     m_texture {0};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(MatVectorNode, "Image Vector", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Scope#Video")
