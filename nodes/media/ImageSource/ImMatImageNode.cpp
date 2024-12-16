#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <imgui_json.h>
#if IMGUI_ICONS
#include <icons.h>
#endif
#include <SplitMerge_vulkan.h>
#include <ImVulkanShader.h>
#include <ImGuiFileDialog.h>

#define NODE_VERSION    0x01020000

namespace BluePrint
{
struct MatImageNode final : Node
{
    BP_NODE_WITH_NAME(MatImageNode, "Image Source", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Media")
    MatImageNode(BP* blueprint): Node(blueprint)
    { 
        m_Name = "Image Source"; 
        m_OutputPins.push_back(&m_Exit);
        m_OutputPins.push_back(&m_Reset);
        m_HasCustomLayout = true; 
    }

    ~MatImageNode()
    {
        ImGui::ImDestroyTexture(&m_textureID);
        if (m_split) { delete m_split; m_split = nullptr; }
        m_split_mat.clear();
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
    }

    void OnStop(Context& context) override
    {
    }

    static inline bool ValidateImagePath(const char* path) {
        if (!path || strlen(path)==0) return false;
        FILE* f = ImFileOpen(path,"rb");
        if (f) {fclose(f);f=NULL;return true;}

        return false;
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

    void reset_output_pin()
    {
        for (auto iter = m_OutputPins.begin(); iter < m_OutputPins.end();)
        {
            Pin* pin = (*iter);
            if (pin->GetType() == PinType::Mat)
            {
                if (m_is_split && pin->m_Name == "MatOut")
                {
                    iter = m_OutputPins.erase(iter);
                    for (auto& link_id : pin->m_LinkFrom) { auto link = m_Blueprint->GetPinFromID(link_id); if (link) link->Unlink(); }
                    delete pin;
                    continue;
                }
                if (!m_is_split && pin->m_Name != "MatOut")
                {
                    iter = m_OutputPins.erase(iter);
                    for (auto& link_id : pin->m_LinkFrom) { auto link = m_Blueprint->GetPinFromID(link_id); if (link) link->Unlink(); }
                    delete pin;
                    continue;
                }
                if (m_is_split)
                {
                    int pin_index = atoi(pin->m_Name.c_str());
                    if (pin_index >= m_mat.c)
                    {
                        iter = m_OutputPins.erase(iter);
                        for (auto& link_id : pin->m_LinkFrom) { auto link = m_Blueprint->GetPinFromID(link_id); if (link) link->Unlink(); }
                        delete pin;
                    }
                    else iter++;
                    continue;
                }
                iter++;
            }
            else iter++;
        }
        if (m_is_split)
        {
            // split mat and set
            if (!m_split)
            {
                int gpu = m_mat.device == IM_DD_VULKAN ? m_mat.device_number : ImGui::get_default_gpu_index();
                m_split = new ImGui::SplitMerge_vulkan(gpu);
            }
            if (m_split)
            {
                m_split->split(m_mat, m_split_mat);
                for (auto& mat : m_split_mat) mat.flags &= IM_MAT_FLAGS_IMAGE_FRAME;
            }
            
            for (int i = 0; i < m_split_mat.size(); i++)
            {
                std::string mat_pin_name = std::to_string(i);
                auto pin = InsertOutputPin(BluePrint::PinType::Mat, mat_pin_name);
                if (pin) pin->SetValue(m_split_mat[i]);
            }
        }
        else
        {
            auto pin = InsertOutputPin(BluePrint::PinType::Mat, "MatOut");
            if (pin) pin->SetValue(m_mat);
        }
    }

    bool LoadImage()
    {
        if (!ValidateImagePath(m_path.c_str()))
        {
            return false;
        }
        int width = 0, height = 0, component = 0;
        if (auto data = stbi_load(m_path.c_str(), &width, &height, &component, 4))
        {
            m_mutex.lock();
            m_mat.create(width, height, 4, 1u);
            memcpy(m_mat.data, data, m_mat.total());
            m_mat.flags |= IM_MAT_FLAGS_IMAGE_FRAME;
            stbi_image_free(data);
            m_mutex.unlock();

            reset_output_pin();

            return true;
        }
        return false;
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint, bool threading = false) override
    {
        //m_MatOut.SetValue(m_mat);
        return m_Exit;
    }

    bool DrawSettingLayout(ImGuiContext * ctx) override
    {
        // Draw Setting
        auto changed = Node::DrawSettingLayout(ctx);
        ImGui::Separator();

        // Draw Custom setting
        changed |= ImGui::Checkbox(ICON_IGFD_BOOKMARK " Bookmark", &m_isShowBookmark);
        ImGui::SameLine(0);
        changed |= ImGui::Checkbox(ICON_IGFD_HIDDEN_FILE " ShowHide", &m_isShowHiddenFiles);
        ImGui::SameLine(0);
        // file filter setting
        if (ImGui::InputText("Filters", (char*)m_filters.data(), m_filters.size() + 1, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                auto& stringValue = *static_cast<string*>(data->UserData);
                ImVector<char>* my_str = (ImVector<char>*)data->UserData;
                IM_ASSERT(stringValue.data() == data->Buf);
                stringValue.resize(data->BufSize);
                data->Buf = (char*)stringValue.data();
            }
            else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
            {
                auto& stringValue = *static_cast<string*>(data->UserData);
                stringValue = std::string(data->Buf);
            }
            return 0;
        }, &m_filters))
        {
            m_filters.resize(strlen(m_filters.c_str()));
            changed = true;
        }
        ImGui::Separator();
        // open file dialog
        ImVec2 minSize = ImVec2(400, 300);
		ImVec2 maxSize = ImVec2(FLT_MAX, FLT_MAX);
        auto& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            io.ConfigViewportsNoDecoration = true;
        string file_name;
        auto separator = m_path.find_last_of('/');
        if (separator != std::string::npos)
            file_name = m_path.substr(separator + 1);
        ImGuiFileDialogFlags vflags = ImGuiFileDialogFlags_OpenFile_Default;
        if (!m_isShowBookmark)      vflags &= ~ImGuiFileDialogFlags_ShowBookmark;
        if (m_isShowHiddenFiles)    vflags &= ~ImGuiFileDialogFlags_DontShowHiddenFiles;
        if (m_Blueprint->GetStyleLight())
            ImGuiFileDialog::Instance()->SetLightStyle();
        else
            ImGuiFileDialog::Instance()->SetDarkStyle();
        if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Choose File"))
        {
            IGFD::FileDialogConfig config;
            config.path = m_path.empty() ? "." : m_path;
            config.countSelectionMax = 1;
            config.userDatas = this;
            config.flags = vflags;
            ImGuiFileDialog::Instance()->OpenDialog("##NodeChooseFileDlgKey", "Choose File", 
                                                    m_filters.c_str(), 
                                                    config);
        }
        if (ImGuiFileDialog::Instance()->Display("##NodeChooseFileDlgKey", ImGuiWindowFlags_NoCollapse, minSize, maxSize))
        {
	        // action if OK
            if (ImGuiFileDialog::Instance()->IsOk() == true)
            {
                m_path = ImGuiFileDialog::Instance()->GetFilePathName();
                file_name = ImGuiFileDialog::Instance()->GetCurrentFileName();
                if (LoadImage())
                {
                    ImGui::ImDestroyTexture(&m_textureID);
                }
                changed = true;
            }
            // close
            ImGuiFileDialog::Instance()->Close();
        }
        ImGui::SameLine(0);
        ImGui::TextUnformatted(file_name.c_str());
        ImGui::Separator();
        // Draw custom layout
        changed |= ImGui::InputInt("Preview Width", &m_preview_width);
        changed |= ImGui::InputInt("Preview Height", &m_preview_height);
        bool change_status = ImGui::Checkbox("Split", &m_is_split);
        changed |= change_status;
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            io.ConfigViewportsNoDecoration = false;
        if (change_status) reset_output_pin();
        return changed;
    }

    bool DrawCustomLayout(ImGuiContext * ctx, float zoom, ImVec2 origin, ImGui::ImCurveEdit::Curve * key, bool embedded) override
    {
        ImGui::SetCurrentContext(ctx);
        m_mutex.lock();
        if (!m_textureID && !m_mat.empty())
        {
            ImGui::ImMatToTexture(m_mat, m_textureID);
        }
        if (m_textureID)
        {
            ImGuiIO& io = ImGui::GetIO();
            ImGui::Image(m_textureID,ImVec2(m_preview_width,m_preview_height));
            if (ImGui::IsItemHovered())
            {
                ImVec2 scale_range = ImVec2(2.0 , 8.0);
                float zoom_size = 384;
                auto image_width = ImGui::ImGetTextureWidth(m_textureID);
                auto image_height = ImGui::ImGetTextureHeight(m_textureID);
                float scale_w =  (float)image_width / (float)m_preview_width;
                float scale_h =  (float)image_height / (float)m_preview_height;
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);   // No tint
                ImVec4 border_col = ImVec4(1.0f, 1.0f, 1.0f, 0.5f); // 50% opaque white
                static float texture_zoom = scale_range.x;
                float region_sz = zoom_size / texture_zoom;
                float pos_x = (io.MousePos.x - pos.x) * scale_w;
                float pos_y = (io.MousePos.y - pos.y + m_preview_height) * scale_h;
                float region_x = pos_x - region_sz * 0.5f;
                float region_y = pos_y - region_sz * 0.5f;
                if (region_x < 0.0f) { region_x = 0.0f; }
                else if (region_x > image_width - region_sz) { region_x = image_width - region_sz; }
                if (region_y < 0.0f) { region_y = 0.0f; }
                else if (region_y > image_height - region_sz) { region_y = image_height - region_sz; }
                ed::Suspend();
                if (ImGui::BeginTooltip())
                {
                    ImGui::SameLine();
                    std::string child_title = "##Texture" + std::to_string((intptr_t)m_textureID);
                    ImGui::BeginChild(child_title.c_str(), ImVec2(0, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
                    {
                        ImGui::Text("%s(zoom:%.2fx)", m_Name.c_str(), texture_zoom);
                        ImGui::Text(" Pos:(%d, %d)", (int)pos_x, (int)pos_y);
                        ImGui::Text("Rect:(%d, %d, %d, %d)", (int)region_x, (int)region_y, (int)(region_x + region_sz), (int)(region_y + region_sz));
                        ImVec2 uv0 = ImVec2((region_x) / image_width, (region_y) / image_height);
                        ImVec2 uv1 = ImVec2((region_x + region_sz) / image_width, (region_y + region_sz) / image_height);
                        ImGui::Image(m_textureID, ImVec2(region_sz * texture_zoom, region_sz * texture_zoom), uv0, uv1, tint_col, border_col);
                    }
                    ImGui::EndChild();
                    ImGui::EndTooltip();
                    if (io.MouseWheel < -FLT_EPSILON)
                    {
                        texture_zoom *= 0.9;
                        if (texture_zoom < scale_range.x) texture_zoom = scale_range.x;
                    }
                    else if (io.MouseWheel > FLT_EPSILON)
                    {
                        texture_zoom *= 1.1;
                        if (texture_zoom > scale_range.y) texture_zoom = scale_range.y;
                    }
                }
                ed::Resume();
            }
        }
        else
        {
            ImGui::Dummy(ImVec2(m_preview_width,m_preview_height));
        }
        m_mutex.unlock();
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

        if (value.contains("preview_width"))
        {
            auto& val = value["preview_width"];
            if (val.is_number()) 
                m_preview_width = val.get<imgui_json::number>();
        }
        if (value.contains("preview_height"))
        {
            auto& val = value["preview_height"];
            if (val.is_number()) 
                m_preview_height = val.get<imgui_json::number>();
        }
        if (value.contains("file_path"))
        {
            auto& val = value["file_path"];
            if (val.is_string())
            {
                m_path = val.get<imgui_json::string>();
            }
        }
        if (value.contains("filter"))
        {
            auto& val = value["filter"];
            if (val.is_string())
            {
                m_filters = val.get<imgui_json::string>();
            }
        }
        if (value.contains("show_bookmark"))
        {
            auto& val = value["show_bookmark"];
            if (val.is_boolean()) m_isShowBookmark = val.get<imgui_json::boolean>();
        }
        if (value.contains("show_hidden"))
        {
            auto& val = value["show_hidden"];
            if (val.is_boolean()) m_isShowHiddenFiles = val.get<imgui_json::boolean>();
        }
        if (value.contains("split"))
        {
            auto& val = value["split"];
            if (val.is_boolean()) m_is_split = val.get<imgui_json::boolean>();
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

        LoadImage();
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["preview_width"] = imgui_json::number(m_preview_width);
        value["preview_height"] = imgui_json::number(m_preview_height);
        value["file_path"] = m_path;
        value["show_bookmark"] = m_isShowBookmark;
        value["show_hidden"] = m_isShowHiddenFiles;
        value["filter"] = m_filters;
        value["split"] = m_is_split;
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }
    Pin* GetAutoLinkInputFlowPin() override { return &m_Enter; }
    Pin* GetAutoLinkOutputFlowPin() override { return &m_Exit; }

    FlowPin   m_Enter  = { this, "Enter" };
    FlowPin   m_Reset  = { this, "Reset" };
    FlowPin   m_Exit   = { this, "Exit" };
    
    Pin* m_InputPins[1] = { &m_Enter };
    std::vector<Pin *>          m_OutputPins;

    ImTextureID m_textureID {0};
    bool m_is_split {false};
    std::string  m_path;
    std::string m_filters {".*"};
    bool m_isShowBookmark {false};
    bool m_isShowHiddenFiles {false};
    int32_t m_preview_width {240};
    int32_t m_preview_height {160};
private:
    ImGui::ImMat                m_mat;
    std::vector<ImGui::ImMat>   m_split_mat;
    std::mutex                  m_mutex;
private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    ImGui::SplitMerge_vulkan * m_split {nullptr};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(MatImageNode, "Image Source", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Media")
