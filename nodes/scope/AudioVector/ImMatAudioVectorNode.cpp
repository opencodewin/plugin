#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>

enum AudioVectorScopeMode  : int
{
    LISSAJOUS,
    LISSAJOUS_XY,
    POLAR,
    MODE_NB,
};

const char* audio_vector_mode_items[] = { "Lissajous", "Lissajous XY", "Polar"};

#define NODE_VERSION    0x01030000
namespace BluePrint
{
struct MatAudioVectorNode final : Node
{
    BP_NODE_WITH_NAME(MatAudioVectorNode, "Audio Vector", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Scope#Audio")
    MatAudioVectorNode(BP* blueprint): Node(blueprint) { m_Name = "Audio Vector"; m_HasCustomLayout = true; }

    ~MatAudioVectorNode()
    {
        ImGui::ImDestroyTexture(&m_texture);
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
        ImGui::ImDestroyTexture(&m_texture);
        m_audio_vector.release();
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
            if (m_audio_vector.empty())
            {
                m_audio_vector.create_type(256, 256, 4, IM_DT_INT8);
                m_audio_vector.fill((int8_t)0);
                m_audio_vector.elempack = 4;
            }
            if (!m_audio_vector.empty())
            {
                float zoom = m_scale;
                float hw = m_audio_vector.w / 2;
                float hh = m_audio_vector.h / 2;
                int samples = mat_in.w;
                m_audio_vector -= 64;
                for (int n = 0; n < samples; n++)
                {
                    float s1 = mat_in.at<float>(n, 0, 0);
                    float s2 = mat_in.c >= 2 ? mat_in.at<float>(n, 0, 1) : s1;
                    int x = hw;
                    int y = hh;

                    if (m_mode == LISSAJOUS)
                    {
                        x = ((s2 - s1) * zoom / 2 + 1) * hw;
                        y = (1.0 - (s1 + s2) * zoom / 2) * hh;
                    }
                    else if (m_mode == LISSAJOUS_XY)
                    {
                        x = (s2 * zoom + 1) * hw;
                        y = (s1 * zoom + 1) * hh;
                    }
                    else
                    {
                        float sx, sy, cx, cy;
                        sx = s2 * zoom;
                        sy = s1 * zoom;
                        cx = sx * sqrtf(1 - 0.5 * sy * sy);
                        cy = sy * sqrtf(1 - 0.5 * sx * sx);
                        x = hw + hw * ImSign(cx + cy) * (cx - cy) * .7;
                        y = m_audio_vector.h - m_audio_vector.h * fabsf(cx + cy) * .7;
                    }
                    x = ImClamp(x, 0, m_audio_vector.w - 1);
                    y = ImClamp(y, 0, m_audio_vector.h - 1);
                    uint8_t r = ImClamp(m_audio_vector.at<uint8_t>(x, y, 0) + 30, 0, 255);
                    uint8_t g = ImClamp(m_audio_vector.at<uint8_t>(x, y, 1) + 50, 0, 255);
                    uint8_t b = ImClamp(m_audio_vector.at<uint8_t>(x, y, 2) + 30, 0, 255);
                    m_audio_vector.set_pixel(x, y, ImPixel(r / 255.0, g / 255.0, b / 255.0, 1.f));
                }
                m_audio_vector.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
            }
        }
        return m_Exit;
    }

    bool DrawCustomLayout(ImGuiContext * ctx, float zoom, ImVec2 origin, ImGui::ImCurveEdit::Curve * key, bool embedded) override
    {
        ImGui::SetCurrentContext(ctx);
        auto drawList  = ImGui::GetWindowDrawList();
        bool changed = false;
        float _scale = m_scale;
        ImGui::Dummy(ImVec2(300, 8));
        ImGui::PushItemWidth(300);
        static ImGuiSliderFlags flags = ImGuiSliderFlags_NoInput;
        ImGui::SliderFloat("Scale", &_scale, 0.5f, 4.0f, "%.2f", flags);
        ImGui::PopItemWidth();
        if (_scale != m_scale) { m_scale = _scale; changed = true; }
        ImGui::Dummy(ImVec2(0, 4));
        auto cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 size(512, 512);
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, cursorPos + size, IM_COL32_BLACK);
        changed = ScopeViewAudioVector(
                "##audio_vector_view", cursorPos, size,
                m_audio_vector,
                m_texture,
                m_scale
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
        changed |= ImGui::Combo("Mode", (int *)&m_mode, audio_vector_mode_items, IM_ARRAYSIZE(audio_vector_mode_items));
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
        value["scale"] = imgui_json::number(m_scale);
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
    ImDataType          m_mat_data_type {IM_DT_UNDEFINED};
    ImGui::ImMat        m_audio_vector;
    AudioVectorScopeMode m_mode {LISSAJOUS};
    float               m_scale {1.0};
    ImTextureID         m_texture {0};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(MatAudioVectorNode, "Audio Vector", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Scope#Audio")
