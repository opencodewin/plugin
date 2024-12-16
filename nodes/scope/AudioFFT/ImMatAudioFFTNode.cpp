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
struct fft_channel_data
{
    ImGui::ImMat m_fft;
    ImGui::ImMat m_amplitude;
    ImGui::ImMat m_phase;
};

struct AudioFFTNode final : Node
{
    BP_NODE_WITH_NAME(AudioFFTNode, "Audio FFT", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Scope#Audio")
    AudioFFTNode(BP* blueprint): Node(blueprint) { m_Name = "Audio FFT"; m_HasCustomLayout = true; m_Skippable = true; }

    ~AudioFFTNode()
    {
        m_channel_data.clear();
        ImGui::ImDestroyTexture(&m_texture);
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
        ImGui::ImDestroyTexture(&m_texture);
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
                m_channel_data[i].m_fft.clone_from(mat_in.channel(i)); m_channel_data[i].m_fft.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
                ImGui::ImRFFT((float *)m_channel_data[i].m_fft.data, m_channel_data[i].m_fft.w, true);
                m_channel_data[i].m_amplitude.create_type((mat_in.w >> 1) + 1, IM_DT_FLOAT32); m_channel_data[i].m_amplitude.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
                ImGui::ImReComposeAmplitude((float*)m_channel_data[i].m_fft.data, (float *)m_channel_data[i].m_amplitude.data, mat_in.w);
                m_channel_data[i].m_phase.create_type((mat_in.w >> 1) + 1, IM_DT_FLOAT32); m_channel_data[i].m_phase.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
                ImGui::ImReComposePhase((float*)m_channel_data[i].m_fft.data, (float *)m_channel_data[i].m_phase.data, mat_in.w);
            }
        }
        return m_Exit;
    }

    bool DrawCustomLayout(ImGuiContext * ctx, float zoom, ImVec2 origin, ImGui::ImCurveEdit::Curve * key, bool embedded) override
    {
        ImGui::SetCurrentContext(ctx);
        bool changed = false;
        ImGui::PushItemWidth(300);
        changed |= ImGui::DragFloat("Scale##AudioWaveScale", &m_scale, 0.05f, 0.1f, 4.f, "%.1f");
        ImGui::PopItemWidth();
        ImGui::TextUnformatted("Show Type:"); ImGui::SameLine();
        changed |= ImGui::RadioButton("FFT", (int *)&m_show_type, 0); ImGui::SameLine();
        changed |= ImGui::RadioButton("AMP", (int *)&m_show_type, 1); ImGui::SameLine();
        changed |= ImGui::RadioButton("PHA", (int *)&m_show_type, 2); 

        ImGui::Dummy(ImVec2(0, 4));
        auto cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 size(512, 512);
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, cursorPos + size, IM_COL32_BLACK);
        std::vector<ImGui::ImMat> fft_mats;
        if (!m_channel_data.empty())
        {
            for (int i = 0; i < m_channel_data.size(); i++)
            {
                if (m_show_type == 0)
                    fft_mats.push_back(m_channel_data[i].m_fft);
                else if (m_show_type == 1)
                    fft_mats.push_back(m_channel_data[i].m_amplitude);
                else if (m_show_type == 2)
                    fft_mats.push_back(m_channel_data[i].m_phase);
            }
            changed = ImGui::ScopeViewFFT(
                "##audio_fft_view", cursorPos, size,
                fft_mats, m_texture,
                m_scale
            );
        }
        else
            ImGui::Dummy(size);
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
        if (value.contains("show_type"))
        {
            auto& val = value["show_type"];
            if (val.is_number()) 
                m_show_type = val.get<imgui_json::number>();
        }
        if (value.contains("show_scale"))
        {
            auto& val = value["show_scale"];
            if (val.is_number()) 
                m_scale = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["show_type"] = imgui_json::number(m_show_type);
        value["show_scale"] = imgui_json::number(m_scale);
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
    std::vector<fft_channel_data> m_channel_data;
    int m_show_type {0};
    float m_scale   {1.0};
    ImTextureID m_texture {0};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(AudioFFTNode, "Audio FFT", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Scope#Audio")
