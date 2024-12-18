#include <UI.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <imgui_fft.h>
#include <immat.h>
#include <ImGuiFileDialog.h>
#include "MediaPlayer.h"

#define CheckPlayerError(funccall) \
    if (!(funccall)) \
    { \
        std::cerr << m_player->GetError() << std::endl; \
    }

static inline std::string PrintTimeStamp(double time_stamp)
{
    char buffer[1024] = {0};
    int hours = 0, mins = 0, secs = 0, ms = 0;
    if (!isnan(time_stamp))
    {
        time_stamp *= 1000.f;
        hours = time_stamp / 1000 / 60 / 60; time_stamp -= hours * 60 * 60 * 1000;
        mins = time_stamp / 1000 / 60; time_stamp -= mins * 60 * 1000;
        secs = time_stamp / 1000; time_stamp -= secs * 1000;
        ms = time_stamp;
    }
    snprintf(buffer, 1024, "%02d:%02d:%02d.%03d", hours, mins, secs, ms);
    return std::string(buffer);
}

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct MediaSourceNode final : Node
{
    BP_NODE_WITH_NAME(MediaSourceNode, "Media Source", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Media")
    MediaSourceNode(BP* blueprint): Node(blueprint)
    {
        m_Name = "Media Source";
        m_HasCustomLayout = true;
        m_OutputPins.push_back(&m_Exit);
        m_OutputPins.push_back(&m_OReset);
    }

    ~MediaSourceNode()
    {
        ReleaseMediaPlayer(&m_player);
        ReleaseAudioRender(&m_audrnd);
    }

    Pin* InsertOutputPin(PinType type, const std::string name) override
    {
        auto pin = FindPin(name);
        if (pin) return pin;
        pin = NewPin(type, name);
        if (pin)
        {
            pin->m_Flags |= PIN_FLAG_FORCESHOW;
            m_OutputPins.push_back(pin);
        }
        return pin;
    }

    void CloseMedia()
    {
        if (m_player)
        {
            m_player->Close();
        }
        // rebuild output pins
        for (auto iter = m_OutputPins.begin(); iter != m_OutputPins.end();)
        {
            if ((*iter)->m_Name != m_Exit.m_Name && (*iter)->m_Name != m_OReset.m_Name)
            {
                (*iter)->Unlink();
                if ((*iter)->m_LinkFrom.size() > 0)
                {
                    for (auto from_pin : (*iter)->m_LinkFrom)
                    {
                        auto link = m_Blueprint->GetPinFromID(from_pin);
                        if (link)
                        {
                            link->Unlink();
                        }
                    }
                }
                iter = m_OutputPins.erase(iter);
            }
            else
                ++iter;
        }
        if (m_player) m_player->Close();
        m_info = nullptr;
        m_flow = nullptr;
        m_vmat = nullptr;
        m_amat.clear();
        m_total_time = 0;
    }

    void OpenMedia()
    {
        if (!m_player)
        {
            m_player = CreateMediaPlayer();
            m_audrnd = CreateAudioRender();
            CheckPlayerError(m_player->SetAudioRender(m_audrnd));
        }
        if (m_player)
        {
            if (m_path.empty()) m_player->Open("Camera");
            else m_player->Open(m_path);
            m_info = m_player->GetMediaInfo();
            if (m_info)
            {
                m_total_time = m_info->duration;
                m_paused = false;
                for (auto stream : m_info->streams)
                {
                    if (stream->type == MediaInfo::VIDEO)
                    {
                        auto vstr = dynamic_cast<MediaInfo::VideoStream*>(stream.get());
                        m_flow = (FlowPin*)InsertOutputPin(BluePrint::PinType::Flow, "Out");
                        m_vmat = InsertOutputPin(BluePrint::PinType::Mat, "VMat");
                    }
                    if (stream->type == MediaInfo::AUDIO)
                    {
                        auto astr = dynamic_cast<MediaInfo::AudioStream*>(stream.get());
                        if (!m_flow) m_flow = (FlowPin*)InsertOutputPin(BluePrint::PinType::Flow, "Out");
                        auto channels = astr->channels;
                        for (int i = 0; i < channels; i++)
                        {
                            std::string mat_pin_name = "AC:" + std::to_string(i);
                            auto pin = InsertOutputPin(BluePrint::PinType::Mat, mat_pin_name);
                            m_amat.push_back(pin);
                        }
                    }
                }
            }
        }
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
    }

    void OnStop(Context& context) override
    {
        Reset(context);
        if (m_player) m_player->Reset();
    }

    void OnPause(Context& context) override
    {
        if (m_player) m_player->Pause();
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint, bool threading = false) override
    {
        if (entryPoint.m_ID == m_Reset.m_ID)
        {
            Reset(context);
            return {};
        }
        if (!m_player)
        {
            return m_Exit;
        }
        else if (m_player->IsSeeking())
        {
            if (threading)
                ImGui::sleep((int)(40));
            else
                ImGui::sleep(0);
            context.PushReturnPoint(entryPoint);
        }
        else if (m_player->IsOpened() && !m_player->IsEof())
        {
            if (!m_player->IsPlaying()) m_player->Play();
            ImGui::ImMat vmat;
            m_player->GetVideo(vmat);
            if (!vmat.empty())
            {
                if (m_vmat) m_vmat->SetValue(vmat);
                context.PushReturnPoint(entryPoint);
                return *m_flow;
            }
            else
            {
                if (threading)
                    ImGui::sleep((int)(40));
                else
                    ImGui::sleep(0);
                context.PushReturnPoint(entryPoint);
            }
        }
        else if (m_player->IsEof())
        {
            return m_Exit;
        }

        return {};
    }
    
    bool DrawSettingLayout(ImGuiContext * ctx) override
    {
        const char *filters = "Media Files(*.mp4 *.mov *.mkv *.webm *.mxf *.avi){.mp4,.mov,.mkv,.webm,.mxf,.avi,.MP4,.MOV,.MKV,.MXF,.WEBM,.AVI},.*";
        // Draw Setting
        auto changed = Node::DrawSettingLayout(ctx);
        ImGui::Separator();
        ImVec2 minSize = ImVec2(400, 300);
		ImVec2 maxSize = ImVec2(FLT_MAX, FLT_MAX);
        auto& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            io.ConfigViewportsNoDecoration = true;
        if (ImGui::Checkbox("Camera", &m_camera))
        {
            if (m_camera)
            {
                CloseMedia();
                m_path.clear();
                m_file_name.clear();
                OpenMedia();
            }
            changed = true;
        }
        ImGui::BeginDisabled(m_camera);
        if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Choose File"))
        {
            IGFD::FileDialogConfig config;
            config.path = m_path.empty() ? "." : m_path;
            config.countSelectionMax = 1;
            config.userDatas = this;
            config.flags = ImGuiFileDialogFlags_OpenFile_Default;
            ImGuiFileDialog::Instance()->OpenDialog("##NodeMediaSourceDlgKey", "Choose File", 
                                                    filters, 
                                                    config);
        }
        ImGui::SameLine(0);
        ImGui::TextUnformatted(m_file_name.c_str());
        ImGui::EndDisabled();

        ImGui::Separator();
        changed |= Node::DrawDataTypeSetting("Mat Type:", m_mat_data_type);
        ImGui::Separator();
        changed |= ImGui::RadioButton("GPU",  (int *)&m_device, 0); ImGui::SameLine();
        changed |= ImGui::RadioButton("CPU",   (int *)&m_device, -1);
        if (ImGuiFileDialog::Instance()->Display("##NodeMediaSourceDlgKey", ImGuiWindowFlags_NoCollapse, minSize, maxSize))
        {
	        // action if OK
            if (ImGuiFileDialog::Instance()->IsOk() == true)
            {
                m_path = ImGuiFileDialog::Instance()->GetFilePathName();
                m_file_name = ImGuiFileDialog::Instance()->GetCurrentFileName();
                CloseMedia();
                OpenMedia();
                changed = true;
            }
            // close
            ImGuiFileDialog::Instance()->Close();
        }
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            io.ConfigViewportsNoDecoration = false;
        return changed;
    }

    bool DrawCustomLayout(ImGuiContext * ctx, float zoom, ImVec2 origin, ImGui::ImCurveEdit::Curve * key, bool embedded) override
    {
        ImGui::SetCurrentContext(ctx);
        ImGui::Text("%s", m_file_name.c_str());
        static ImGuiSliderFlags flags = ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Stick;
        ImGui::Dummy(ImVec2(320, 8));
        ImGui::PushItemWidth(300);
        if (!m_camera)
        {
            float time = m_player && m_player->IsOpened() ? m_player->GetPlayPos() / 1000.f : 0;
            float total_time = m_total_time;
            if (ImGui::SliderFloat("##time", &time, 0, total_time, "%.2f", flags))
            {
                if (m_player && m_player->IsOpened() && !m_player->IsSeeking())
                    m_player->Seek(time * 1000);
            }
            string str_current_time = PrintTimeStamp(time);
            string str_total_time = PrintTimeStamp(m_total_time);
            ImGui::Text("%s / %s", str_current_time.c_str(), str_total_time.c_str());
        }

        if (m_player && m_player->IsOpened())
        {
            if (m_info)
            {
                for (auto stream : m_info->streams)
                {
                    if (stream->type == MediaInfo::VIDEO)
                    {
                        auto vstr = dynamic_cast<MediaInfo::VideoStream*>(stream.get());
                        ImGui::Text("     Video: %d x %d @ %.2f", vstr->width, vstr->height, 
                                                    (float)vstr->realFrameRate.num / (float)vstr->realFrameRate.den);
                        ImGui::Text("            %d bit depth", vstr->bitDepth);
                    }
                    if (stream->type == MediaInfo::AUDIO)
                    {
                        auto astr = dynamic_cast<MediaInfo::AudioStream*>(stream.get());
                        ImGui::Text("     Audio: %d @ %d", astr->channels, astr->sampleRate);
                        ImGui::Text("            %d bit depth", astr->bitDepth);
                    }
                    if (stream->type == MediaInfo::SUBTITLE)
                    {
                        ImGui::Text("     Subtitle");
                    }
                }
            }
            if (m_player->HasAudio())
            {
                auto channels = m_player->GetAudioChannels();
                for (int i = 0; i < channels; i++)
                {
                    ImGui::ImMat audio_wave;
                    double current_pts = m_player->GetPlayPos() / 1000.f;
                    auto stack = m_player->GetAudioMeterStack(i);
                    auto count = m_player->GetAudioMeterCount(i);
                    int value = m_player->GetAudioMeterValue(i, current_pts, audio_wave);
                    ImGui::PushID(i);
                    ImGui::UvMeter("##uv", ImVec2(300, 10), &value, 0, 96, 600, &stack, &count);
                    ImGui::PopID();
                    m_player->SetAudioMeterStack(i, stack);
                    m_player->SetAudioMeterCount(i, count);
                    if (i < m_amat.size())
                    {
                        m_amat[i]->SetValue(audio_wave);
                    }
                }
            }
        }

        ImGui::PopItemWidth();
        return false;
    }

    int LoadPins(const imgui_json::array* PinValueArray, std::vector<Pin *>& pinArray)
    {
        for (auto& pinValue : *PinValueArray)
        {
            string pinType;
            PinType type = PinType::Any;
            if (!imgui_json::GetTo<imgui_json::string>(pinValue, "type", pinType)) // check pin type
                continue;
            PinTypeFromString(pinType, type);

            std::string name;
            if (pinValue.contains("name"))
                imgui_json::GetTo<imgui_json::string>(pinValue, "name", name);

            auto iter = std::find_if(m_OutputPins.begin(), m_OutputPins.end(), [name](const Pin * pin)
            {
                return pin->m_Name == name;
            });
            if (iter != m_OutputPins.end())
            {
                if (!(*iter)->Load(pinValue))
                {
                    return BP_ERR_GENERAL;
                }
            }
            else
            {
                Pin *new_pin = NewPin(type, name);
                if (new_pin)
                {
                    if (!new_pin->Load(pinValue))
                    {
                        delete new_pin;
                        return BP_ERR_GENERAL;
                    }
                    pinArray.push_back(new_pin);
                }
            }
        }
        return BP_ERR_NONE;
    }

    int Load(const imgui_json::value& value) override
    {
        int ret = BP_ERR_NONE;
        if (!value.is_object())
            return BP_ERR_NODE_LOAD;

        if (!imgui_json::GetTo<imgui_json::number>(value, "id", m_ID)) // required
            return BP_ERR_NODE_LOAD;

        if (!imgui_json::GetTo<imgui_json::string>(value, "name", m_Name)) // required
            return BP_ERR_NODE_LOAD;

        imgui_json::GetTo<imgui_json::boolean>(value, "break_point", m_BreakPoint); // optional

        imgui_json::GetTo<imgui_json::number>(value, "group_id", m_GroupID); // optional

        if (value.contains("mat_type"))
        {
            auto& val = value["mat_type"];
            if (val.is_number()) 
                m_mat_data_type = (ImDataType)val.get<imgui_json::number>();
        }
        if (value.contains("device_type"))
        {
            auto& val = value["device_type"];
            if (val.is_number()) 
                m_device = (ImDataType)val.get<imgui_json::number>();
        }
        if (value.contains("media_path"))
        {
            auto& val = value["media_path"];
            if (val.is_string()) 
                m_path = val.get<imgui_json::string>();
        }
        if (value.contains("file_name"))
        {
            auto& val = value["file_name"];
            if (val.is_string())
            {
                m_file_name = val.get<imgui_json::string>();
            }
        }
        if (value.contains("camera"))
        {
            auto& val = value["camera"];
            if (val.is_boolean())
            {
                m_camera = val.get<imgui_json::boolean>();
            }
        }

        const imgui_json::array* inputPinsArray = nullptr;
        if (imgui_json::GetPtrTo(value, "input_pins", inputPinsArray)) // optional
        {
            auto pins = GetInputPins();

            if (pins.size() != inputPinsArray->size())
                return BP_ERR_PIN_NUMPER;

            auto pin = pins.data();
            for (auto& pinValue : *inputPinsArray)
            {
                if (!(*pin)->Load(pinValue))
                {
                    return BP_ERR_INPIN_LOAD;
                }
                ++pin;
            }
        }

        const imgui_json::array* outputPinsArray = nullptr;
        if (imgui_json::GetPtrTo(value, "output_pins", outputPinsArray)) // optional
        {
            if (LoadPins(outputPinsArray, m_OutputPins) != BP_ERR_NONE)
                return BP_ERR_INPIN_LOAD;
        }

        if (!m_path.empty() || m_camera)
            OpenMedia();
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["device_type"] = imgui_json::number(m_device);
        value["camera"] = imgui_json::boolean(m_camera);
        value["media_path"] = m_path;
        value["file_name"] = m_file_name;
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }
    Pin* GetAutoLinkInputFlowPin() override { return &m_Enter; }
    Pin* GetAutoLinkOutputFlowPin() override { return &m_Exit; }

    FlowPin   m_Enter           = { this, "Enter" };
    FlowPin   m_Reset           = { this, "Reset" };
    FlowPin   m_Exit            = { this, "Exit" };
    FlowPin   m_OReset          = { this, "Reset Out"};

    Pin*      m_InputPins[2] = { &m_Enter, &m_Reset };
    std::vector<Pin *>          m_OutputPins;
private:
    int                 m_device  {0};          // 0 = GPU -1 = CPU
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    bool                m_camera {false};
    std::string         m_path;
    std::string         m_file_name;
    MediaPlayer*        m_player = nullptr;
    AudioRender*        m_audrnd = nullptr;
    MediaInfo::InfoHolder m_info = nullptr;

    double              m_total_time    {0};
    bool                m_paused        {false};
    bool                m_need_update   {false};
    FlowPin *           m_flow {nullptr};
    Pin *               m_vmat {nullptr};
    std::vector<Pin *>  m_amat;
};
}

BP_NODE_DYNAMIC_WITH_NAME(MediaSourceNode, "Media Source", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Media")
