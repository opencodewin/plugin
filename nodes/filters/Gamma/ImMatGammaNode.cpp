#include <UI.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include <Gamma_vulkan.h>

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct GammaNode final : Node
{
    BP_NODE_WITH_NAME(GammaNode, "Gamma", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Filter#Video#Color")
    GammaNode(BP* blueprint): Node(blueprint) { m_Name = "Gamma"; m_HasCustomLayout = true; m_Skippable = true; }

    ~GammaNode()
    {
        if (m_filter) { delete m_filter; m_filter = nullptr; }
        if (m_logo) { ImGui::ImDestroyTexture(m_logo); m_logo = nullptr; }
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
        m_mutex.lock();
        m_MatOut.SetValue(ImGui::ImMat());
        m_mutex.unlock();
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint, bool threading = false) override
    {
        auto mat_in = context.GetPinValue<ImGui::ImMat>(m_MatIn);
        if (m_GammaIn.IsLinked())
        {
            m_gamma = context.GetPinValue<float>(m_GammaIn);
        }
        if (!mat_in.empty())
        {
            int gpu = mat_in.device == IM_DD_VULKAN ? mat_in.device_number : ImGui::get_default_gpu_index();
            if (!m_Enabled)
            {
                m_MatOut.SetValue(mat_in);
                return m_Exit;
            }
            if (!m_filter || gpu != m_device)
            {
                if (m_filter) { delete m_filter; m_filter = nullptr; }
                m_filter = new ImGui::Gamma_vulkan(gpu);
            }
            if (!m_filter)
            {
                return {};
            }
            m_device = gpu;
            ImGui::VkMat im_RGB; im_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_filter->filter(mat_in, im_RGB, m_gamma);
            m_MatOut.SetValue(im_RGB);
        }
        return m_Exit;
    }

    void WasUnlinked(const Pin& receiver, const Pin& provider) override
    {
        if (receiver.m_ID == m_GammaIn.m_ID)
        {
            m_GammaIn.SetValue(m_gamma);
        }
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
        float setting_offset = 320;
        if (!embedded)
        {
            ImVec2 sub_window_pos = ImGui::GetCursorScreenPos();
            ImVec2 sub_window_size = ImGui::GetWindowSize();
            setting_offset = sub_window_size.x - 80;
        }
        bool changed = false;
        float val = m_gamma;
        ImGui::PushStyleColor(ImGuiCol_Button, 0);
        ImGui::PushItemWidth(200);
        ImGui::BeginDisabled(!m_Enabled || m_GammaIn.IsLinked());
        ImGui::GammaSelector("##slider_gamma##Gamma", ImVec2(200, 20), &val, 1.0f, 0.f, 4.f, zoom);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_gamma##Gamma")) { val = 1.0; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_gamma##Gamma", key, ImGui::ImCurveEdit::DIM_X, m_GammaIn.IsLinked(), "gamma##Gamma@" + std::to_string(m_ID), 0.f, 4.f, 1.f, m_GammaIn.m_ID);
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        if (val != m_gamma) { m_gamma = val; changed = true; }
        return m_Enabled ? changed : false;
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
        if (value.contains("gamma"))
        {
            auto& val = value["gamma"];
            if (val.is_number()) 
                m_gamma = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["gamma"] = imgui_json::number(m_gamma);
    }

    void DrawNodeLogo(ImGuiContext * ctx, ImVec2 size, std::string logo) const override
    {
        // Node::DrawNodeLogo(ctx, size, std::string(u8"\uf7b9"));
        if (ctx) ImGui::SetCurrentContext(ctx); // External Node must set context
        if (!m_logo) m_logo = Node::LoadNodeLogo((void *)logo_data, logo_size);
        Node::DrawNodeLogo(m_logo, m_logo_index, logo_cols, logo_rows, size);
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }
    Pin* GetAutoLinkInputFlowPin() override { return &m_Enter; }
    Pin* GetAutoLinkOutputFlowPin() override { return &m_Exit; }
    vector<Pin*> GetAutoLinkInputDataPin() override { return {&m_MatIn}; }
    vector<Pin*> GetAutoLinkOutputDataPin() override { return {&m_MatOut}; }


    FlowPin   m_Enter   = { this, "Enter" };
    FlowPin   m_Exit    = { this, "Exit" };
    MatPin    m_MatIn   = { this, "In" };
    FloatPin  m_GammaIn = { this, "Gamma"};
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[3] = { &m_Enter, &m_MatIn, &m_GammaIn };
    Pin* m_OutputPins[2] = { &m_Exit, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    int m_device            {-1};
    ImGui::Gamma_vulkan * m_filter   {nullptr};
    float m_gamma           {1.0f};
    mutable ImTextureID  m_logo {nullptr};
    mutable int m_logo_index {0};

    const unsigned int logo_width = 100;
    const unsigned int logo_height = 100;
    const unsigned int logo_cols = 1;
    const unsigned int logo_rows = 1;
    const unsigned int logo_size = 5950;
    const unsigned int logo_data[5952/4] =
{
    0xe0ffd8ff, 0x464a1000, 0x01004649, 0x01000001, 0x00000100, 0x8400dbff, 0x02020300, 0x03020203, 0x04030303, 0x05040303, 0x04050508, 0x070a0504, 
    0x0c080607, 0x0b0c0c0a, 0x0d0b0b0a, 0x0d10120e, 0x0b0e110e, 0x1016100b, 0x15141311, 0x0f0c1515, 0x14161817, 0x15141218, 0x04030114, 0x05040504, 
    0x09050509, 0x0d0b0d14, 0x14141414, 0x14141414, 0x14141414, 0x14141414, 0x14141414, 0x14141414, 0x14141414, 0x14141414, 0x14141414, 0x14141414, 
    0x14141414, 0x14141414, 0xc0ff1414, 0x00081100, 0x03640064, 0x02002201, 0x11030111, 0x01c4ff01, 0x010000a2, 0x01010105, 0x00010101, 0x00000000, 
    0x01000000, 0x05040302, 0x09080706, 0x00100b0a, 0x03030102, 0x05030402, 0x00040405, 0x017d0100, 0x04000302, 0x21120511, 0x13064131, 0x22076151, 
    0x81321471, 0x2308a191, 0x15c1b142, 0x24f0d152, 0x82726233, 0x17160a09, 0x251a1918, 0x29282726, 0x3635342a, 0x3a393837, 0x46454443, 0x4a494847, 
    0x56555453, 0x5a595857, 0x66656463, 0x6a696867, 0x76757473, 0x7a797877, 0x86858483, 0x8a898887, 0x95949392, 0x99989796, 0xa4a3a29a, 0xa8a7a6a5, 
    0xb3b2aaa9, 0xb7b6b5b4, 0xc2bab9b8, 0xc6c5c4c3, 0xcac9c8c7, 0xd5d4d3d2, 0xd9d8d7d6, 0xe3e2e1da, 0xe7e6e5e4, 0xf1eae9e8, 0xf5f4f3f2, 0xf9f8f7f6, 
    0x030001fa, 0x01010101, 0x01010101, 0x00000001, 0x01000000, 0x05040302, 0x09080706, 0x00110b0a, 0x04020102, 0x07040304, 0x00040405, 0x00770201, 
    0x11030201, 0x31210504, 0x51411206, 0x13716107, 0x08813222, 0xa1914214, 0x2309c1b1, 0x15f05233, 0x0ad17262, 0xe1342416, 0x1817f125, 0x27261a19, 
    0x352a2928, 0x39383736, 0x4544433a, 0x49484746, 0x5554534a, 0x59585756, 0x6564635a, 0x69686766, 0x7574736a, 0x79787776, 0x8483827a, 0x88878685, 
    0x93928a89, 0x97969594, 0xa29a9998, 0xa6a5a4a3, 0xaaa9a8a7, 0xb5b4b3b2, 0xb9b8b7b6, 0xc4c3c2ba, 0xc8c7c6c5, 0xd3d2cac9, 0xd7d6d5d4, 0xe2dad9d8, 
    0xe6e5e4e3, 0xeae9e8e7, 0xf5f4f3f2, 0xf9f8f7f6, 0x00dafffa, 0x0001030c, 0x11031102, 0xdf003f00, 0xfe96b4b3, 0x5725ee5c, 0x4f784dae, 0x66743bed, 
    0xc48006f1, 0x7d4c4f00, 0xbb667a45, 0x00ff94e3, 0xe1678f85, 0xd255296b, 0x779a772b, 0xb13bc189, 0xebe938c0, 0xed4f79cd, 0x35d92759, 0x850e00ff, 
    0xd48f3cc1, 0x1518e357, 0xfa858a25, 0xfdb86fa7, 0xec1513f7, 0xdfdadb26, 0x7078c899, 0x4f1eed9f, 0x69ebbb02, 0xc7030081, 0x0f9ff36a, 0x24ba6c5d, 
    0x411d49ae, 0x9dc55de9, 0xf605b0c2, 0xb324b846, 0xfae71f74, 0xfc5aafd7, 0xb92d0cd6, 0xfc8c3634, 0x953ece6e, 0x92a45f58, 0x06d70d2f, 0x415fa69b, 
    0xa8fe7373, 0x4301673c, 0x3afd7eb7, 0x377cb6d7, 0x103f74d1, 0x683279d9, 0x729232d6, 0x9c5558e6, 0x52f0f4d6, 0x79ccafaa, 0x9ed6ccd8, 0xc8415d15, 
    0x00875bf1, 0x15fe31f0, 0x980ce283, 0x5e3b6456, 0x10fa543e, 0x784d5f33, 0xb672f6f7, 0xd4a4d2bc, 0x6d8f207c, 0xcf8c42a8, 0xa5f27ca5, 0xc97703c8, 
    0xb05b99f7, 0x18781000, 0xffe56b1d, 0xee351900, 0xa5737b85, 0x697376ea, 0x2bb98d9a, 0x66dc9d35, 0x2a231339, 0x0407b076, 0xc8408260, 0x0b4bd520, 
    0xda4e8352, 0x990f2777, 0x6f0a7361, 0x3d7bbdd9, 0x85f075cf, 0x3b3cd6c8, 0xfd307365, 0x600394ef, 0x45b4e2f5, 0xec5ddac4, 0x18725617, 0x91c481e6, 
    0xe4c0ecb1, 0x0be10b57, 0x4af8a6a1, 0x6c9bf1ce, 0x24e7563e, 0x4dea01e0, 0x9baacf69, 0xc14437e8, 0x1d1960e0, 0x5a1f410e, 0x5cd36bf0, 0x9e6b4bee, 
    0x16de69b4, 0xf6d74e7d, 0x3eacf0a8, 0x8166f034, 0x514a7ff1, 0x692ab20c, 0xe324ccba, 0x386a07e6, 0x2b1bae07, 0x2b3d62c7, 0xc4771de6, 0xd0105ef6, 
    0x408d752e, 0xc2e26d48, 0xb40c712c, 0x47b5738e, 0xd4d7d4a7, 0x2a7e099f, 0xf40edf68, 0xda1100ff, 0x4f48d278, 0x291bb581, 0x993bfe56, 0xb02d3512, 
    0x55b954e1, 0x06daf8dc, 0x575c4946, 0x3c8dd7e5, 0xfc8b2755, 0xbe7c7747, 0x5722566c, 0x36b25616, 0xdf0d22ef, 0x8c13fa22, 0xef35f764, 0x8b573865, 
    0x63f8124e, 0x233ef4d7, 0x4c798c1b, 0xef2a42a5, 0xffb3f678, 0xbda12300, 0xc9e3eff8, 0x4f93262f, 0xa2a326f1, 0x5b96bb5a, 0xe2e53e2d, 0x79003f89, 
    0x5093e73e, 0xf5c200ff, 0x00ff93f8, 0xffbfd643, 0x7ff28100, 0x5eb6728d, 0x68f5d617, 0x2e3af68c, 0xe20e7fa5, 0x475b4ba6, 0x52774395, 0x8fd3eb40, 
    0x00ffb17a, 0xff892708, 0xd758e800, 0x2f5df03f, 0x2adc57f8, 0x8ab25214, 0x788d0ffc, 0xdc9d4cbc, 0xe9b3f7e5, 0x85fa82bf, 0x83c4efc5, 0x9629757b, 
    0x696578ee, 0x91583d24, 0x66ed1f5d, 0xe1497c55, 0x4fcfdddd, 0xc12756c4, 0x1c6f2368, 0xf763b0c1, 0x00ff7732, 0xda6fba66, 0xf123d5fe, 0x3a70861f, 
    0xa1fddf83, 0x47b99d5f, 0x5d1646fb, 0xb47e463f, 0x5d39f5af, 0x0500ffeb, 0xcca1731e, 0xb6b871a9, 0xa36b1d17, 0x5d90e5d4, 0x1e683a9d, 0x9d012309, 
    0xd483605c, 0x35087d0c, 0xdc6e78cf, 0x56366b5f, 0x71758343, 0xa727b815, 0xad7fe1ce, 0x03eda77a, 0x7ca64ba1, 0x10eed55d, 0x903f4322, 0xffb35511, 
    0x3be82e00, 0x6bba00ff, 0xb48f72a2, 0x2715f549, 0xfc585e2e, 0xac1d9aca, 0xab0b5016, 0x52771799, 0xfdabfb09, 0x820fe82b, 0x07f80dda, 0x95ceba50, 
    0xb8fa3765, 0xf5224ffa, 0xa0fb2904, 0x077d9efa, 0x098ff17a, 0x8bf66969, 0xdf959b7b, 0x2d6e852c, 0x540a4f91, 0xfabdbd83, 0xe4399271, 0x67f8bf57, 
    0x0274dec4, 0x51a62575, 0xbdcc48e6, 0xed743c1f, 0x147aa5d7, 0xf16cba1a, 0x673f25b3, 0xbfa70ec9, 0x0be67ef8, 0x912d9558, 0xc69d902c, 0x21cd6235, 
    0xd793e4c0, 0xda5ff38a, 0xd272f683, 0x66783c7e, 0xeed9727d, 0xa36dfc2c, 0x3e8dd3e9, 0xea5c354b, 0x1b9a5125, 0x3cea4159, 0x40dd00cc, 0x378c3b62, 
    0x5b73f148, 0x8491634c, 0x4ec1b9bb, 0xba62dcbe, 0xef883f5b, 0xe45946a3, 0xd0db7066, 0xc1884024, 0xf47a0ee4, 0x595fe9f7, 0xb13aad4a, 0xf9b1a9e5, 
    0x8c185abd, 0x4f6b4f35, 0xa2557c46, 0x00ffb74e, 0xb2fc9904, 0x55bea733, 0x908383fa, 0xbfe55a7f, 0x181fdb62, 0xeef05a7c, 0xda69a3a3, 0x0ea3d625, 
    0xbf63db92, 0x9c585838, 0x103b289f, 0x9c418f0b, 0x7d054e0e, 0x7403df88, 0x376a145f, 0x9f4d6d6e, 0x7962af87, 0xe196ac6e, 0x8434c9f7, 0xc4fc9cb4, 
    0xc3c9162b, 0x02e3640c, 0xf6e12fbd, 0x822fe185, 0xe185469a, 0xb4d1064d, 0x3647228d, 0xe5fbd6d6, 0xd8c5829f, 0xc6667616, 0xf39cebe6, 0x853b5fe9, 
    0xd52863cb, 0x2d672386, 0x8e7dea1f, 0x4e278837, 0x35e16a74, 0xe9ba8b92, 0xae954fd3, 0xd6fe487e, 0xbdbf377e, 0x82dff5f1, 0xc29ab725, 0x0f97c3d3, 
    0xbcbcdd05, 0x87639734, 0xdd2f7a24, 0xf764dc53, 0x49f83eaf, 0xe2f3e2f0, 0xb3a28a47, 0xa06c5989, 0x8573b7c4, 0x0acf44c8, 0x009e583d, 0x47577bfc, 
    0xc2f859fb, 0xb4bf880f, 0x6a9dc4e7, 0x5edab3c0, 0x01c7b3eb, 0x2359d874, 0x3b4e296f, 0xd27f2764, 0x9ffda4be, 0xf043157e, 0x2471c1ef, 0x2eb53fc8, 
    0x89d43d31, 0xf3327529, 0x05800eb4, 0xb94e0752, 0x5eafc1eb, 0xc32ecf7e, 0xb7e952f2, 0x53e2999f, 0x12df6c95, 0x7f3ee2eb, 0x6d475ea2, 0x6905afe1, 
    0x6dd11c5e, 0xdb3cf8b4, 0x085ce138, 0x8e6adb6d, 0x7dea38e7, 0x7f5aeb49, 0xf31fd6d8, 0x00ff79f5, 0xe54a4d7f, 0x699e1ccb, 0xf95a6f3e, 0x39793e29, 
    0xcaf4d93d, 0x47b2529c, 0x20057fcf, 0xa16abc48, 0xf6296ff8, 0x35df00ff, 0x23d97ed0, 0x56e1b36e, 0xfe76b9fb, 0x855fb962, 0xc500ff93, 0x78c7b4c2, 
    0xd000ff65, 0xb3fdaf6b, 0xe1a9cee3, 0xf17e9c4c, 0x0bae98bf, 0x3f18ed3f, 0xe933f927, 0x46f525e5, 0x3caaf597, 0xa85ac3a3, 0xa345bcd0, 0x44a6acea, 
    0xed82beb0, 0x9163d5d3, 0x1ac88f5c, 0x9af68ffa, 0xc4474d82, 0x4c3ada7f, 0x447ab756, 0x7bf6c5d2, 0xb8432ab4, 0xee869585, 0x2abf619c, 0xb44487f9, 
    0x3758ae49, 0x2378dadd, 0x35e73f39, 0x283e37eb, 0x1e857f8e, 0x9e98f017, 0x247d7b0b, 0x5c5a6e48, 0x5c6bbea4, 0x7d0e485c, 0x1583ce36, 0xd23e5abd, 
    0x4fb724b4, 0x8600fff3, 0x97544e32, 0xabc94eb4, 0xf167a57e, 0x6e99b60d, 0x36ba266e, 0x9977a8d6, 0xc67cf925, 0x628f05c7, 0xe6d50e7b, 0xa4fd217e, 
    0x56c48f27, 0x5397e9b1, 0xdd65380b, 0x21c59837, 0x87059768, 0xb60f361f, 0xcd73241b, 0x0fc43779, 0x8a8ffa89, 0x1fc89b6e, 0x64b7b163, 0xdc398774, 
    0x9ce7e479, 0x1f35389e, 0x7b0bfc81, 0xb35a3bf1, 0x68a4f4d0, 0x4b9ca5ef, 0x2112a024, 0xe7bc9384, 0x1d29baf8, 0x7160cf39, 0x152cd8f7, 0x4a1d2c3c, 
    0x873e5fab, 0xb41966c1, 0x38f613f1, 0xf4f7bb6d, 0xc1db3df2, 0x2c3e133f, 0x1cf14d7c, 0x74f01d1e, 0x92f6c1b0, 0x22894ba6, 0x11cb7e59, 0x8edb9227, 
    0xeef50158, 0xee6b3270, 0x07ba86bf, 0x1e7e22fc, 0xe7d6e0d3, 0x20f1d1fe, 0xbabb3486, 0x053385db, 0xbe1dc81b, 0xafb89e6c, 0xf057f828, 0x0bf843d3, 
    0xa7674da5, 0x4d75895e, 0x1bda0842, 0x64091889, 0xa37000dc, 0x49b1818c, 0xb547ea38, 0x9dc3c77b, 0x3f423d3a, 0x99bf5fb4, 0x567ee570, 0x714e6fe3, 
    0x8e3579ea, 0x5419bd2e, 0x8e95a651, 0x72b16148, 0x6829e7a6, 0xfd283e72, 0xe13d7ca5, 0xc3e7562d, 0xf03f24da, 0x2fbaf893, 0x9b441bdd, 0x5ede82ca, 
    0xf09825bb, 0xa8f2330e, 0x7cc5b127, 0x7e46fbb7, 0xf0223ed1, 0xe735a78e, 0x5b5bb588, 0x59dc1d8f, 0x87771ab4, 0xb46586b4, 0x206423d1, 0x2cd9ce5c, 
    0xc6c48df2, 0xe7b0f887, 0x9fda0000, 0x2797ceb5, 0xf3daefc2, 0x9276b7c2, 0x243e5679, 0x10313f8a, 0x26f1fd85, 0xf21df83d, 0xbedae714, 0xe25ad508, 
    0xefc9ab5b, 0x26af491e, 0xcab28c9d, 0x279b9dc5, 0x4ff22471, 0xab83705a, 0xabfb3bcb, 0x2acc9da7, 0xca481842, 0xeb93579a, 0x27fe6be5, 0xdec9feaf, 
    0x8400ff01, 0xa9ebc7a3, 0xf4eda14c, 0xe12612b5, 0xdb39a6f7, 0xdf0cc0f5, 0xaeb9af80, 0xaa114c2d, 0x5e314027, 0xf830fb3d, 0xe01dfc11, 0x45e61b5b, 
    0xe609d717, 0x4bc6ad48, 0x17fc4880, 0xd6ebfc68, 0x89a4f5ee, 0xbc50778c, 0xbc9f3a0c, 0xd5c6c22b, 0x5b9ad5f6, 0x056f0f2d, 0x5d8a4215, 0x3373a55e, 
    0x7b242329, 0x6fcf9366, 0xabce9fef, 0x485ba94b, 0xa99495e5, 0x3f5377e8, 0x5ff520b4, 0x9f57e8fb, 0xb83fbbca, 0xcd67e6ef, 0x750b00ff, 0xd34c7c2c, 
    0xd0303b60, 0xdcafd3cd, 0xed9f7acd, 0xd52165a1, 0x03b922bc, 0x51bf1e32, 0x1bfc3c5f, 0x8b7fb9bd, 0x79b952da, 0x7370737f, 0xcdeec783, 0x64fb1d7d, 
    0xde6ad24a, 0x53c8660c, 0x33beb473, 0x587364b4, 0x783a2962, 0x4f7549fa, 0x3ddc3df5, 0x290b5b55, 0x00ffef3e, 0xc2b33c6d, 0x1c1ee973, 0x2f752ed4, 
    0x93b65c17, 0x490341a3, 0x9ef7716f, 0x422a65e6, 0x22134024, 0x00ff4a90, 0xdcfbc908, 0xf15d5e11, 0x3f37c607, 0x7b5d7c10, 0x768c0d7b, 0xee08a809, 
    0x3b0b3216, 0xe78b4504, 0x63eece75, 0xbcec1827, 0x105f5af1, 0xd45d176f, 0x63a509d6, 0x14cfa405, 0x6a34e448, 0xc14e068a, 0x3f8fe585, 0xe39a9c51, 
    0xd9df72bc, 0x21ee127f, 0xcffeb9b7, 0x63118db6, 0x21128161, 0x1eb41388, 0xc49f5830, 0x0927fd9a, 0x53c1d294, 0xb79aba58, 0xfecdbf65, 0x9679e787, 
    0x52135f6d, 0x45bb6858, 0xa91ff83f, 0x4d8df8ca, 0x637bb1ae, 0x8aa55b2c, 0x8885b34f, 0x0baf72e7, 0x00eefd83, 0xded7d0e9, 0x82efb35f, 0x7c033eed, 
    0xd5d8fe33, 0xe3f8dc94, 0xb2b190c4, 0x174e3bb0, 0x71ecd066, 0xe75e62b9, 0x3b702c27, 0x0f1fced7, 0xefe1217c, 0x87497c11, 0x9aa2d55c, 0x6ef8086f, 
    0xcd3539da, 0x8b7c9149, 0xcb079599, 0xdd819dbc, 0xb2b95181, 0x938cb3cd, 0xd3f0855e, 0xf1afa6e2, 0x9ffac547, 0x6835f588, 0x8d90a42d, 0x4c524c37, 
    0xe2b6751a, 0xe0810816, 0x80641400, 0xc9931c37, 0xc4331faf, 0x6d4909b7, 0x75e8cd7f, 0xd71898e4, 0xdf94c6c5, 0xea2fbb7d, 0xa0756bc7, 0x8de2f778, 
    0x8d1fbfde, 0x2dedb4ee, 0x74ddd3e3, 0x5cdab09f, 0xc92a8f4c, 0x9c1b34ba, 0xfec6e388, 0xc7913172, 0x037fee35, 0xa541fcaf, 0x4cf11b7c, 0xba772c71, 
    0xd33487f7, 0xcdc8a20d, 0x0cabf9e5, 0x726e3786, 0xf9180d63, 0x30828354, 0xd776d57b, 0xee86c76d, 0x6ec45a98, 0x97ed8c63, 0xbd8d2d68, 0x07f04905, 
    0x7f71d6c5, 0xb8b4cac2, 0x7361b6b8, 0xc4224da3, 0x00895c65, 0xfda40d20, 0xf315bbe1, 0xa4c6c4b0, 0x756bade2, 0x5cbefe3e, 0xaa7ad8f0, 0xbdb5b50f, 
    0x4ff05fb5, 0x6b2b3e83, 0xc5c7ddda, 0x627d0a2d, 0xa84bf5f9, 0x64eeadae, 0x914b94bd, 0x46d22a30, 0xa2584286, 0x7dfe019e, 0xf0bc9b6b, 0x1cdf6a8b, 
    0x76eef027, 0xa9acf6a7, 0xa87c5c29, 0x767c6624, 0x9f7445e1, 0x6eaf9eb4, 0x1624217e, 0x10363562, 0xc6a39d19, 0x38e39643, 0xe70170e8, 0xe12fbdd6, 
    0xd4e182bf, 0x3f6900ff, 0xc232eb16, 0x6385b525, 0xca9588bc, 0x278b7013, 0x58e81807, 0x46ef357e, 0x7bc3b3bf, 0xcf465b4b, 0xa361c683, 0x8351d9f5, 
    0xb92f4dba, 0x600d7d6a, 0x4158a48b, 0x5afda768, 0xce539ff3, 0x75ab5a3f, 0xdac5bda9, 0xbddb5adb, 0x6ca7e4dd, 0x8ba54670, 0x75537a1c, 0x38339c2b, 
    0xaff53b60, 0xbe9ffd5d, 0xa779451f, 0xd8f27e8d, 0x2177794f, 0x164f3bfb, 0x38106fe1, 0x131cb90c, 0xbe623bea, 0xe168ad4a, 0xaba5eda3, 0x4ecfe2bf, 
    0x79344da3, 0xf10bfca2, 0xe6a2a84e, 0x9d2ced68, 0x97967cc6, 0x0ff03d90, 0xcff0bf34, 0x00ff245e, 0xf77ff69e, 0x8500fff4, 0x189e8e7d, 0x44010ab6, 
    0x53bfdc63, 0x8400ff4e, 0x79fedb62, 0x3fd6fac5, 0x47f6c65f, 0xee51eb90, 0xcbfe397e, 0xf86e5f77, 0x38921c99, 0x29b2b650, 0x8f8363cf, 0x7e455fe5, 
    0x7db116d8, 0x3529c1a7, 0xb40f70cc, 0x9f804c61, 0xc771e430, 0x345fef41, 0xd4b411fc, 0x47fc187e, 0xb25d335d, 0x91aed992, 0x41066ecc, 0x0f726cc4, 
    0x3ffa8a71, 0x17b2d0f6, 0x7c911eda, 0xa7db6adf, 0xb4f834c5, 0xd0ae5c8d, 0x2096735b, 0x24dbf12a, 0xf51a1cf6, 0x00ff5431, 0x6d9452e1, 0xe903fca7, 
    0xcbd235f0, 0xafee5165, 0x24f9c07f, 0xf1259eac, 0xadd6d6bc, 0xcbd344e5, 0x1b8b6672, 0x0d777e9d, 0x771f658f, 0x995eeba0, 0xd085cfe0, 0x2dd6537c, 
    0xde6e9d74, 0x6ef7cfee, 0x3086be25, 0xe51acb92, 0xfda71dd6, 0xe8381bb2, 0xff788dbf, 0x68268400, 0x522dcb75, 0xb1b7a5e5, 0xcf520495, 0xcc97f910, 
    0xbe0ea389, 0x0e244fbb, 0x5eedd5de, 0xa116f10f, 0x614fcc15, 0x92f83b7b, 0xf39a6915, 0xa4f2b84b, 0x16cc4e52, 0x6cf58f20, 0x478e0b07, 0x28d7af39, 
    0x1c7c8dd3, 0xc7fb45a9, 0xa3aab2e5, 0x664bda5f, 0xebf837cf, 0x9d3511f1, 0xd141e372, 0x40c782e3, 0x6b7395b6, 0xcc278f63, 0x08aa2a8f, 0x5b40301f, 
    0xb3331ce6, 0x3a6ee330, 0x229e801f, 0xd8dcd3b6, 0x8676e1ca, 0x92f3ef29, 0x5e3dfe07, 0x4f69ab29, 0xcef6cca3, 0x4e1e4943, 0xb90cc8e5, 0x9e67581b, 
    0xbff05e45, 0xadd566da, 0xb6678622, 0x31564992, 0x3d46753a, 0x0d5fe90f, 0x94aac28c, 0x9fbf4925, 0x4ef9f499, 0x644c583e, 0xcb5f0931, 0xf8c4fbd4, 
    0xdacaf6b2, 0x8ee02c29, 0x643b07ea, 0x4e59fb0e, 0x39180806, 0x7ff75beb, 0x77f0a416, 0x7f4e7dc3, 0x69176dec, 0xea96b8b5, 0xfbe429e2, 0xd88cc04e, 
    0x720b4039, 0x8ae35cd8, 0x0b1c3ee2, 0xf6212a9d, 0x23d5d4db, 0x607b3942, 0xaef7a0a7, 0xec7fe2db, 0xd1feace9, 0x5db505be, 0xd1b45e13, 0x2263b726, 
    0x42405c59, 0x5596b7dc, 0xca4f9d55, 0x04c93809, 0x7cb5e760, 0x55311296, 0xcf68296d, 0x5c5cf3d4, 0x50738661, 0x7f3ddd7c, 0x0fcf4fad, 0x9678ddb7, 
    0x2ed459fb, 0xf0e69289, 0xbb277697, 0xfea04f37, 0xc1e7f695, 0x05be122b, 0xc166b5d2, 0xedd3509f, 0xe573bf8b, 0x9f8cb1c4, 0x95af09f8, 0xbff0497c, 
    0xfc067fc4, 0x7378aa49, 0xda447ac6, 0x2716aa2e, 0x24c3248d, 0x228973d1, 0x1fba7670, 0x6bdd3e55, 0xeb016fe9, 0xf089b61a, 0xd910c3e7, 0x657547d9, 
    0x721e9b1e, 0xb3242171, 0x1586edf1, 0xbc8219f4, 0xd07f5779, 0xdd00c7fc, 0x7ff6de9c, 0x7e1d3b8b, 0xccdab35f, 0x09b5b159, 0x7c755feb, 0xd3d0f030, 
    0x47a1053c, 0x3036a1f2, 0xf9a023b1, 0xf9b5fe01, 0xfaf8a9bf, 0x4f936d1d, 0xd2eace92, 0x90235d79, 0x0c807b34, 0x750499ea, 0x27e8d7e4, 0x7e8ecfc6, 
    0xfe9efd1d, 0xaba72e1e, 0xb644229c, 0xcc291696, 0x002a52d7, 0x4cbf83a8, 0x7ccd419f, 0x4a093ab6, 0xb5bc5294, 0xa972d2d0, 0xcbda3851, 0x7be2e8b9, 
    0xfbec7d6c, 0x7d1ecdec, 0x43feafb7, 0x63ddcaaf, 0xf10f0afe, 0x66d457af, 0x439bb0bb, 0x8794fdf0, 0x0f0bd830, 0xb2bdc63c, 0xbf4f72ec, 0xffa54a1f, 
    0xf1e90d00, 0x0d86fedb, 0x0500ff13, 0xdb35fe29, 0xf7474cf5, 0xc591af7f, 0x9cdd45f5, 0xd7e4d5d9, 0x1ffc16fe, 0xd9ce1df1, 0xbeba16b5, 0x3cb9b8b5, 
    0x408a05bb, 0xbe00ff01, 0x6ebfd56b, 0xdfb44dbd, 0x0bc5d802, 0xd9e98a38, 0x8fb6511d, 0x4f8052f5, 0x7b3cf53c, 0x6f7895d7, 0xfed8f3c2, 0x4b0b7acc, 
    0x9bc63d32, 0x944a19e2, 0x482e8f6c, 0x4ffc185f, 0xfba15eeb, 0x5e1c4d77, 0x7d61d10d, 0x716799b8, 0xa8cc0792, 0xfafe63c0, 0xade3e835, 0xd70edb2c, 
    0x397ae49f, 0xf1cada6c, 0xffda9311, 0x858f8d00, 0xa331de6d, 0xfb60fe4a, 0x11465c4b, 0x2b8cec3a, 0x6827fee4, 0x777ab5c7, 0x5b763dc1, 0x80057a8d, 
    0x242c37bc, 0x61922569, 0x75dc1e83, 0xbfe04e3d, 0xb42aaf80, 0x460b2e49, 0x00565aaa, 0x00f21de7, 0x6bfc78fe, 0x095df8b3, 0x4616bfbb, 0x9548e7f2, 
    0xbb0bda95, 0xee3e3265, 0xdd9f8c32, 0x3af0182a, 0x60a15f81, 0x47d538aa, 0x8e5755c3, 0x368abf85, 0x595fdfd0, 0x1ad495bd, 0x3ad99194, 0x716ecfdc, 
    0x3af279f3, 0xa5825306, 0x08526463, 0x5693a7ea, 0x62f81f7e, 0xff0d7fe2, 0xc1156b00, 0x8ab5539b, 0x879c03df, 0xf6fc2319, 0xbe521bae, 0x895ff1b9, 
    0x8924b9bc, 0x380fb633, 0x3eb18b00, 0x9c7bcc50, 0x2479920c, 0x0fbfd1d7, 0x6fd58474, 0x347bab74, 0x381ad83f, 0x5c6616f3, 0xb88dab0b, 0x54d57305, 
    0x9fdc7312, 0xdf4cf96a, 0x2edfa012, 0xdffdbb89, 0xc8b3cfe6, 0x56136b30, 0xcbd257f2, 0x2bf9f5d5, 0xeca9bbbf, 0x6369347a, 0x14402012, 0x5c8f24b0, 
    0xbfedb50e, 0x28df750e, 0x2a24465a, 0x9e814703, 0xf1f18a3a, 0x09099d0f, 0xaec071d5, 0x999ac293, 0x63149ab5, 0x6394d5b5, 0xdcf915f1, 0x994bda2a, 
    0x38e6ae9f, 0xd2a2d578, 0xdb7eb347, 0xfe2c087f, 0xfacbfe32, 0x2370f4f5, 0xd6c28ff8, 0x77e9accd, 0x55bcbf28, 0x7378668c, 0xb8c7abd4, 0xa6cafbe3, 
    0x61d062be, 0xbac1375d, 0x98e4ba2d, 0x1e5c60ac, 0x1fa3463a, 0xf0f57da5, 0xd6b554c3, 0xdeb26ff4, 0x3241dc2a, 0xb989a618, 0x8161840e, 0x2b12841e, 
    0x76e2cff2, 0x853fe3a5, 0x4ef1385e, 0x5ef21d8b, 0x536aa5c9, 0xe69afdda, 0xb962972f, 0x05b857d8, 0xaff59176, 0x5617a9b4, 0x8f1d278d, 0x557ae1c7, 
    0xf5b9272a, 0xf0bfec37, 0x47e2c7a2, 0x7d75af8c, 0xd1b7ed48, 0xb7a381e6, 0xbb2c3350, 0xb0e70c8b, 0xd77b9cd9, 0x9fdb7e95, 0x8bf82110, 0xfced99f1, 
    0xf04637cf, 0x006f2df4, 0x4fccc3dd, 0x730f18ef, 0xd7a2cf85, 0xe267f059, 0xb3f095f6, 0xc5bf0de1, 0x9a4c117f, 0x6775a96e, 0xa06b6b79, 0xc5bdc4c0, 
    0xf9c4b6da, 0xe2fd9373, 0xb91e004f, 0xf12590af, 0x8de2a326, 0xcb37ea50, 0x66fafeba, 0xa9473896, 0x877d2c39, 0xf67ccdf3, 0x465c8d1e, 0x46ba753a, 
    0xdfe5e7eb, 0xce69f5d4, 0xfb9c349d, 0xba67162b, 0x2e428eb5, 0xc5df3c54, 0xdf1e38b5, 0xdaed3385, 0xfff2fc77, 0x8b2bc700, 0x7dbf16f1, 0x054dab6b, 
    0x1991a793, 0x48ce93d9, 0xcfac79ea, 0x9fb549f8, 0x00ffc7ef, 0xc5fa8a7c, 0xf2d85687, 0x33978ca5, 0xab0ffcb3, 0x2e0d67bf, 0x8efd58d7, 0x797563bc, 
    0xdbe29a10, 0x9161855f, 0x440cca89, 0xb6ef3f30, 0xa3e07fa8, 0xc083b833, 0x513e0670, 0xee2b678e, 0x5ae98f4a, 0xfcefb21f, 0xbf8e7f99, 0xff8760ec, 
    0x67566800, 0xaf6b14fc, 0x8ceb7f80, 0x0557ccdf, 0xd01bf96f, 0xc143fef9, 0xabc800ff, 0x34bfea11, 0xdba3ab7c, 0x8052bc22, 0x07878677, 0xfe0fb33e, 
    0xb1b57e24, 0x14cddfa9, 0x1131eb13, 0x8538fa0d, 0xda6d8c24, 0x3ec619b8, 0xbdacf3a7, 0xfb27ee27, 0xeae87fd0, 0xc87fabb9, 0x00ff73b3, 0x85fc0b5f, 
    0x8e74fc7d, 0x76cbc787, 0x7c0d5f75, 0x128fa521, 0xb8258d6a, 0x5270c39c, 0xdae63b3f, 0xbed63f38, 0x58a2d28a, 0x421b05c2, 0x573b0680, 0xff37fc87, 
    0x5a8b9100, 0xafaf00ff, 0xd372aff0, 0x19e4577b, 0x5cbc49cd, 0xf7337bd3, 0x4da51aae, 0x15b52960, 0x4ab78e76, 0xcf7fce19, 0x85b5d64a, 0xc1406016, 
    0xadac39c8, 0x00ffa927, 0x6fb1953e, 0x15f1b7fe, 0xd187aee2, 0x1ffaba62, 0x9e21fc42, 0x581ba448, 0xf2f58ef1, 0x679fed4f, 0x5fed970d, 0x8c71f9aa, 
    0x37f6695f, 0x33e4a913, 0xf9138298, 0x5fd6d72a, 0x7ed67f09, 0x54bed25f, 0xe4bfb6fd, 0x00ffa6eb, 0xfe874db0, 0xf0ee6b82, 0x88ee00ff, 0xbf146bfc, 
    0x7ee359db, 0x21edd13c, 0xf2b866d0, 0x5316c9bc, 0x0a39ef1a, 0xedc0c9b8, 0x4134e29a, 0xf80e7ff6, 0x9b5f5b87, 0x48258250, 0x03426ea4, 0x13bd02d7, 
    0x8afc17e2, 0xf2f51fd7, 0xe679957f, 0x2400ff97, 0xee1fc5f3, 0xb69667a5, 0xeddc6be9, 0x548d92cd, 0xf08e6397, 0x3c6d85af, 0xdb9ba547, 0xb43327c9, 
    0xc047098c, 0xeb3dfd3d, 0xc615fe63, 0xaffbfd95, 0xc23ff8fb, 0xfcbfe197, 0x00ff4b8b, 0x57fa5b5d, 0xa4c45e53, 0xc6ce67d3, 0x3fdaa429, 0x0000d9ff, 
};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(GammaNode, "Gamma", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Filter#Video#Color")