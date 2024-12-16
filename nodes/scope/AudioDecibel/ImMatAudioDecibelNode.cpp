#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <imgui_extra_widget.h>
#include <imgui_audio_widget.h>
#include <imgui_json.h>
#include <imgui_helper.h>
#include <imgui_fft.h>

#define NODE_VERSION    0x01030000

namespace BluePrint
{
struct decibel_channel_data
{
    ImGui::ImMat m_fft;
    ImGui::ImMat m_db;
    ImGui::ImMat m_DBShort;
    ImGui::ImMat m_DBLong;
    float m_decibel {0};
    int m_DBMaxIndex {-1};
    int m_stack = 0;
    int m_count = 0;
};

struct AudioDecibelNode final : Node
{
    BP_NODE_WITH_NAME(AudioDecibelNode, "Audio Decibel", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Scope#Audio")
    AudioDecibelNode(BP* blueprint): Node(blueprint) { m_Name = "Audio Decibel"; m_HasCustomLayout = true; m_Skippable = true; }

    ~AudioDecibelNode()
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
                m_channel_data[i].m_db.create_type((mat_in.w >> 1) + 1, IM_DT_FLOAT32); m_channel_data[i].m_db.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
                m_channel_data[i].m_DBMaxIndex = ImGui::ImReComposeDB((float*)m_channel_data[i].m_fft.data, (float *)m_channel_data[i].m_db.data, mat_in.w, false);
                m_channel_data[i].m_DBShort.create_type(20, IM_DT_FLOAT32); m_channel_data[i].m_DBShort.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
                ImGui::ImReComposeDBShort((float*)m_channel_data[i].m_fft.data, (float*)m_channel_data[i].m_DBShort.data, mat_in.w);
                m_channel_data[i].m_DBLong.create_type(76, IM_DT_FLOAT32); m_channel_data[i].m_DBLong.flags |= IM_MAT_FLAGS_CUSTOM_UPDATED;
                ImGui::ImReComposeDBLong((float*)m_channel_data[i].m_fft.data, (float*)m_channel_data[i].m_DBLong.data, mat_in.w);
                m_channel_data[i].m_decibel = ImGui::ImDoDecibel((float*)m_channel_data[i].m_fft.data, mat_in.w);
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
        changed |= ImGui::RadioButton("dB", (int *)&m_show_type, 0); ImGui::SameLine();
        changed |= ImGui::RadioButton("20", (int *)&m_show_type, 1); ImGui::SameLine();
        changed |= ImGui::RadioButton("76", (int *)&m_show_type, 2); ImGui::SameLine();
        changed |= ImGui::RadioButton("Level", (int *)&m_show_type, 3);

        ImGui::Dummy(ImVec2(0, 4));
        auto cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 size(512, 512);
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, cursorPos + size, IM_COL32_BLACK);
        if (!m_channel_data.empty())
        {
            std::vector<ImGui::ImMat> db_mats;
            if(m_show_type == 0)
            {
                for (int i = 0; i < m_channel_data.size(); i++)
                {
                    db_mats.push_back(m_channel_data[i].m_db);
                }
                changed = ImGui::ScopeViewDB(
                    "##audio_db_view", cursorPos, size,
                    db_mats, m_texture,
                    m_scale
                );
            }
            else if (m_show_type == 1)
            {
                const int is_short = 1;
                for (int i = 0; i < m_channel_data.size(); i++)
                {
                    db_mats.push_back(m_channel_data[i].m_DBShort);
                }
                changed = ImGui::ScopeViewDBLevel(
                    "##audio_db_level_short_view", cursorPos, size,
                    db_mats, db_mats,
                    is_short
                );
            }
            else if (m_show_type == 2)
            {
                const int is_short = 0;
                for (int i = 0; i < m_channel_data.size(); i++)
                {
                    db_mats.push_back(m_channel_data[i].m_DBLong);
                }
                changed = ImGui::ScopeViewDBLevel(
                    "##audio_db_level_short_view", cursorPos, size,
                    db_mats, db_mats,
                    is_short
                );
            }
            else
            {
                ImVec2 scope_view_size = ImVec2(size.x, size.y / m_channel_data.size());
                for (int i = 0; i < m_channel_data.size(); i++)
                {
                    ImGui::PushID(i);
                    int idb = m_channel_data[i].m_decibel;
                    ImGui::UvMeter("##level", scope_view_size, &idb, 0, 100, scope_view_size.x * 2, &m_channel_data[i].m_stack, &m_channel_data[i].m_count);
                    ImGui::PopID();
                }
            }
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
    std::vector<decibel_channel_data> m_channel_data;
    int m_show_type {0};
    float m_scale   {1.0};
    ImTextureID m_texture {0};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(AudioDecibelNode, "Audio Decibel", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Scope#Audio")
