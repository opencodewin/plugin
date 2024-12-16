#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include <Histogram_vulkan.h>

#define NODE_VERSION    0x01030000

namespace BluePrint
{
struct MatHistogramNode final : Node
{
    BP_NODE_WITH_NAME(MatHistogramNode, "Image Histogram", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Scope#Video")
    MatHistogramNode(BP* blueprint): Node(blueprint) { m_Name = "Image Histogram"; m_HasCustomLayout = true; }

    ~MatHistogramNode()
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
        auto mat_in = context.GetPinValue<ImGui::ImMat>(m_MatIn);
        if (!mat_in.empty())
        {
            m_MatOut.SetValue(mat_in);
            if (!m_scope)
            {
                int gpu = mat_in.device == IM_DD_VULKAN ? mat_in.device_number : ImGui::get_default_gpu_index();
                if (m_scope) { delete m_scope; m_scope = nullptr; }
                m_scope = new ImGui::Histogram_vulkan(gpu);
            }
            if (!m_scope)
            {
                return {};
            }
            m_histogram.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_scope->scope(mat_in, m_histogram, 256, m_scale, m_draw_log);
            m_histogram.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
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
        bool _draw_log = m_draw_log;
        bool _splited = m_splited;
        bool _YRGB = m_YRGB;
        float _scale = m_scale;
        ImGui::Dummy(ImVec2(300, 8));
        ImGui::PushItemWidth(300);
        ImGui::TextUnformatted("Log");ImGui::SameLine();
        ImGui::ToggleButton("##draw_log",&_draw_log); ImGui::SameLine();
        ImGui::TextUnformatted("Split");ImGui::SameLine();
        ImGui::ToggleButton("##draw_splited",&_splited); ImGui::SameLine();
        ImGui::TextUnformatted("YRGB");ImGui::SameLine();
        ImGui::ToggleButton("##draw_YRGB",&_YRGB);
        ImGui::SliderFloat("Scale", &_scale, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_NoInput);
        if (_draw_log != m_draw_log) { m_draw_log = _draw_log; changed = true; }
        if (_splited != m_splited) { m_splited = _splited; changed = true; }
        if (_YRGB != m_YRGB) { m_YRGB = _YRGB; changed = true; }
        if (_scale != m_scale) { m_scale = _scale; changed = true; }
        ImGui::PopItemWidth();
        ImGui::Dummy(ImVec2(0, 4));
        auto cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 size(512, 512);
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, cursorPos + size, IM_COL32_BLACK);
        changed = ImGui::ScopeViewHistogram(
                "##histogram_view", cursorPos, size,
                m_histogram,
                m_texture,
                m_scale,
                m_splited,
                m_YRGB,
                m_draw_log
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
        if (value.contains("draw_log"))
        {
            auto& val = value["draw_log"];
            if (val.is_boolean()) 
                m_draw_log = val.get<imgui_json::boolean>();
        }
        if (value.contains("splited"))
        {
            auto& val = value["splited"];
            if (val.is_boolean()) 
                m_splited = val.get<imgui_json::boolean>();
        }
        if (value.contains("YRGB"))
        {
            auto& val = value["YRGB"];
            if (val.is_boolean()) 
                m_YRGB = val.get<imgui_json::boolean>();
        }
        if (value.contains("scale"))
        {
            auto& val = value["scale"];
            if (val.is_number()) 
                m_scale = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["draw_log"] = m_draw_log;
        value["scale"] = imgui_json::number(m_scale);
        value["splited"] = m_splited;
        value["YRGB"] = m_YRGB;
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
    ImGui::Histogram_vulkan * m_scope   {nullptr};
    bool m_draw_log {false};
    bool m_splited {false};
    bool m_YRGB  {false};
    float m_scale {0.5f};
    ImGui::ImMat    m_histogram;
    ImGui::ImMat    m_histogram_mat;
    ImTextureID     m_texture {0};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(MatHistogramNode, "Image Histogram", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Scope#Video")
