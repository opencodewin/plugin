#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <imgui_extra_widget.h>
#include <imgui_json.h>
#include <imgui_helper.h>
#include <imgui_fft.h>

#define NODE_VERSION    0x01030000

namespace BluePrint
{
struct spectrogram_channel_data
{
    ImGui::ImMat m_fft;
    ImGui::ImMat m_db;
    ImGui::ImMat m_Spectrogram;
    ImTextureID texture_spectrogram {0};
    ~spectrogram_channel_data() { ImGui::ImDestroyTexture(&texture_spectrogram); }
};

struct AudioSpectorgramNode final : Node
{
    BP_NODE_WITH_NAME(AudioSpectorgramNode, "Audio Spectorgram", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Scope#Audio")
    AudioSpectorgramNode(BP* blueprint): Node(blueprint) { m_Name = "Audio Spectorgram"; m_HasCustomLayout = true; m_Skippable = true; }

    ~AudioSpectorgramNode()
    {
        m_channel_data.clear();
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
        m_channel_data.clear();
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
            if (!m_Enabled)
            {
                return m_Exit;
            }
            if (m_channel_data.size() != mat_in.c)
            {
                m_channel_data.clear();
                m_channel_data.resize(mat_in.c);
            }
            for (int i = 0; i < mat_in.c; i++)
            {
                m_channel_data[i].m_fft.clone_from(mat_in.channel(i));
                ImGui::ImRFFT((float *)m_channel_data[i].m_fft.data, m_channel_data[i].m_fft.w, true);
                m_channel_data[i].m_db.create_type((mat_in.w >> 1) + 1, IM_DT_FLOAT32);
                ImGui::ImReComposeDB((float*)m_channel_data[i].m_fft.data, (float *)m_channel_data[i].m_db.data, mat_in.w, false);
                if (m_channel_data[i].m_Spectrogram.w != (mat_in.w >> 1) + 1)
                {
                    m_channel_data[i].m_Spectrogram.create_type((mat_in.w >> 1) + 1, 256, 4, IM_DT_INT8);
                }
                if (!m_channel_data[i].m_Spectrogram.empty())
                {
                    auto w = m_channel_data[i].m_Spectrogram.w;
                    auto c = m_channel_data[i].m_Spectrogram.c;
                    memmove(m_channel_data[i].m_Spectrogram.data, (char *)m_channel_data[i].m_Spectrogram.data + w * c, m_channel_data[i].m_Spectrogram.total() - w * c);
                    uint32_t * last_line = (uint32_t *)m_channel_data[i].m_Spectrogram.row_c<uint8_t>(255);
                    for (int n = 0; n < w; n++)
                    {
                        float value = m_channel_data[i].m_db.at<float>(n) * M_SQRT2 + 64 + m_show_offset;
                        value = ImClamp(value, -64.f, 63.f);
                        float light = (value + 64) / 127.f;
                        value = (int)((value + 64) + 170) % 255; 
                        auto hue = value / 255.f;
                        auto color = ImColor::HSV(hue, 1.0, light * m_show_light);
                        last_line[n] = color;
                    }
                    m_channel_data[i].m_Spectrogram.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
                }
            }
        }
        return m_Exit;
    }

    bool DrawCustomLayout(ImGuiContext * ctx, float zoom, ImVec2 origin, ImGui::ImCurveEdit::Curve * key, bool embedded) override
    {
        ImGui::SetCurrentContext(ctx);
        auto drawList  = ImGui::GetWindowDrawList();
        bool changed = false;
        float _show_light = m_show_light;
        float _show_offset = m_show_offset;
        ImGui::Dummy(ImVec2(300, 8));
        ImGui::PushItemWidth(300);
        static ImGuiSliderFlags flags = ImGuiSliderFlags_NoInput;
        ImGui::SliderFloat("Chroma Light", &_show_light, 0.1f, 1.0f, "%.2f", flags);
        ImGui::SliderFloat("Chroma Offset", &_show_offset, -96.f, 96.f, "%.2f", flags);
        ImGui::PopItemWidth();
        if (_show_light != m_show_light) { m_show_light = _show_light; changed = true; }
        if (_show_offset != m_show_offset) { m_show_offset = _show_offset; changed = true; }
        
        ImGui::Dummy(ImVec2(0, 4));
        auto cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 size(512, 512);
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, cursorPos + size, IM_COL32_BLACK);
        
        std::vector<ImGui::ImMat> spectrogram_mats;
        std::vector<ImTextureID> spectrogram_texs;
        for (int i = 0; i < m_channel_data.size(); i++)
        {
            spectrogram_mats.push_back(m_channel_data[i].m_Spectrogram);
            spectrogram_texs.push_back(m_channel_data[i].texture_spectrogram);
        }
        changed = ImGui::ScopeViewSpectrogram(
                "##audio_spectrogram_view", cursorPos, size,
                spectrogram_mats, spectrogram_texs,
                m_show_light,
                m_show_offset
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
        if (value.contains("show_light"))
        {
            auto& val = value["show_light"];
            if (val.is_number()) 
                m_show_light = val.get<imgui_json::number>();
        }
        if (value.contains("show_offset"))
        {
            auto& val = value["show_offset"];
            if (val.is_number()) 
                m_show_offset = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["show_light"] = imgui_json::number(m_show_light);
        value["show_offset"] = imgui_json::number(m_show_offset);
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
    std::vector<spectrogram_channel_data> m_channel_data;
    float m_show_light {1.0};
    float m_show_offset {0.0};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(AudioSpectorgramNode, "Audio Spectorgram", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Scope#Audio")
