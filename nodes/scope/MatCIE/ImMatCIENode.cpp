#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include <CIE_vulkan.h>

#define NODE_VERSION    0x01030000
enum CIE_QUALITY : int32_t
{
    CIEQ_LOW = 0,
    CIEQ_MID = 1,
    CIEQ_HIGH = 2,
    CIEQ_ULTRA = 3,
};

static const char* color_system_items[] = { "NTSC System", "EBU System", "SMPTE System", "SMPTE 240M System", "APPLE System", "wRGB System", "CIE1931 System", "Rec709 System", "Rec2020 System", "DCIP3" };
static const char* cie_system_items[] = { "XYY", "UCS", "LUV" };
static const char* quality_items[] = { "Low(256x256)", "Middle(512x512)", "High(1024x1024)", "Ultra(2048x2048)"};
namespace BluePrint
{
struct MatCIENode final : Node
{
    BP_NODE_WITH_NAME(MatCIENode, "Image CIE", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Scope#Video")
    MatCIENode(BP* blueprint): Node(blueprint) { m_Name = "Image CIE Scope"; m_HasCustomLayout = true; }

    ~MatCIENode()
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
                m_scope = new ImGui::CIE_vulkan(gpu);
            }
            if (!m_scope)
            {
                return {};
            }
            m_scope->SetParam(m_color_system, m_cie_system, (256 << m_quality), m_gamuts, 0.75f, m_correct_gamma);
            m_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_scope->scope(mat_in, m_RGB, m_intensity, m_show_color);
            m_RGB.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
        }
        return m_Exit;
    }

    bool DrawCustomLayout(ImGuiContext * ctx, float zoom, ImVec2 origin, ImGui::ImCurveEdit::Curve * key, bool embedded) override
    {
        ImGui::SetCurrentContext(ctx);
        auto drawList  = ImGui::GetWindowDrawList();
        bool changed = false;
        bool _show_color = m_show_color;
        bool _correct_gamma = m_correct_gamma;
        float _intensity = m_intensity;
        ImGui::TextUnformatted("Show Color");ImGui::SameLine();
        ImGui::ToggleButton("##ShowColor",&_show_color);
        ImGui::TextUnformatted("Show Correct Gamma");ImGui::SameLine();
        ImGui::ToggleButton("##ShowCorrectGamma",&_correct_gamma);
        ImGui::Dummy(ImVec2(300, 8));
        ImGui::PushItemWidth(300);
        static ImGuiSliderFlags flags = ImGuiSliderFlags_NoInput;
        ImGui::SliderFloat("Intensity", &_intensity, 0.01f, 1.0f, "%.2f", flags);
        ImGui::PopItemWidth();
        if (_show_color != m_show_color) { m_show_color = _show_color; changed = true; }
        if (_correct_gamma != m_correct_gamma) { m_correct_gamma = _correct_gamma; changed = true; }
        if (_intensity != m_intensity) { m_intensity = _intensity; changed = true; }
        ImGui::Dummy(ImVec2(0, 4));
        auto cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 size(512, 512);
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, cursorPos + size, IM_COL32_BLACK);
        ImVec2 white_point, green_point_system, green_point_gamuts;
        if (m_scope)
        {
            m_scope->GetWhitePoint(m_color_system, size.x, size.y, &white_point.x, &white_point.y);
            m_scope->GetGreenPoint(m_color_system, size.x, size.y, &green_point_system.x, &green_point_system.y);
            m_scope->GetGreenPoint(m_gamuts, size.x, size.y, &green_point_gamuts.x, &green_point_gamuts.y);
        }
        
        int cie_system = m_cie_system;
        changed = ImGui::ScopeViewCIE(
                "##cie_view", cursorPos, size,
                m_RGB, m_texture,
                m_intensity,
                m_correct_gamma,
                m_show_color,
                cie_system,
                white_point,
                green_point_system, color_system_items[m_color_system],
                green_point_gamuts, color_system_items[m_gamuts]
            );
        return changed;
    }

    bool DrawSettingLayout(ImGuiContext * ctx) override
    {
        // Draw Set Node Name
        auto changed = Node::DrawSettingLayout(ctx);
        ImGui::Separator();
        changed |= Node::DrawDataTypeSetting("Mat Type:", m_mat_data_type);
        ImGui::Separator();
        changed |= ImGui::Combo("Color System", (int *)&m_color_system, color_system_items, IM_ARRAYSIZE(color_system_items));
        changed |= ImGui::Combo("Show Gamut", (int *)&m_gamuts, color_system_items, IM_ARRAYSIZE(color_system_items));
        changed |= ImGui::Combo("Cie System", (int *)&m_cie_system, cie_system_items, IM_ARRAYSIZE(cie_system_items));
        changed |= ImGui::Combo("Quality", (int *)&m_quality, quality_items, IM_ARRAYSIZE(quality_items));
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
        if (value.contains("color_system"))
        {
            auto& val = value["color_system"];
            if (val.is_number()) 
                m_color_system = (ImGui::ColorsSystems)val.get<imgui_json::number>();
        }
        if (value.contains("cie_system"))
        {
            auto& val = value["cie_system"];
            if (val.is_number()) 
                m_cie_system = (ImGui::CieSystem)val.get<imgui_json::number>();
        }
        if (value.contains("gamut"))
        {
            auto& val = value["gamut"];
            if (val.is_number()) 
                m_gamuts = (ImGui::ColorsSystems)val.get<imgui_json::number>();
        }
        if (value.contains("quality"))
        {
            auto& val = value["quality"];
            if (val.is_number()) 
                m_quality = (CIE_QUALITY)val.get<imgui_json::number>();
        }
        if (value.contains("show_color"))
        {
            auto& val = value["show_color"];
            if (val.is_boolean()) 
                m_show_color = val.get<imgui_json::boolean>();
        }
        if (value.contains("correct_gamma"))
        {
            auto& val = value["correct_gamma"];
            if (val.is_boolean()) 
                m_correct_gamma = val.get<imgui_json::boolean>();
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
        value["show_color"] = m_show_color;
        value["correct_gamma"] = m_correct_gamma;
        value["quality"] = imgui_json::number(m_quality);
        value["color_system"] = imgui_json::number(m_color_system);
        value["gamut"] = imgui_json::number(m_gamuts);
        value["cie_system"] = imgui_json::number(m_cie_system);
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
    ImGui::CIE_vulkan *     m_scope   {nullptr};
    ImGui::ColorsSystems    m_color_system {ImGui::Rec709system};
    ImGui::ColorsSystems    m_gamuts {ImGui::Rec2020system};
    ImGui::CieSystem        m_cie_system {ImGui::XYY};
    ImGui::ImMat    m_RGB;
    bool            m_show_color {false};
    bool            m_correct_gamma {false};
    float           m_intensity {0.5};
    CIE_QUALITY     m_quality {CIEQ_HIGH};
    ImTextureID     m_texture {0};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(MatCIENode, "Image CIE", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Scope#Video")
