#include <UI.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include "Canny_vulkan.h"

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct CannyNode final : Node
{
    BP_NODE_WITH_NAME(CannyNode, "Canny Edge", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Filter#Video#Edge")
    CannyNode(BP* blueprint): Node(blueprint) { m_Name = "Canny Edge"; m_HasCustomLayout = true; m_Skippable = true; }
    ~CannyNode()
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
        if (m_RadiusIn.IsLinked()) m_blurRadius = context.GetPinValue<float>(m_RadiusIn);
        if (m_MinIn.IsLinked()) m_minThreshold = context.GetPinValue<float>(m_MinIn);
        if (m_MaxIn.IsLinked()) m_maxThreshold = context.GetPinValue<float>(m_MaxIn);
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
                m_filter = new ImGui::Canny_vulkan(gpu);
            }
            if (!m_filter)
            {
                return {};
            }
            m_device = gpu;
            ImGui::VkMat im_RGB; im_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_filter->filter(mat_in, im_RGB, m_blurRadius, m_minThreshold, m_maxThreshold);
            m_MatOut.SetValue(im_RGB);
        }
        return m_Exit;
    }

    void WasUnlinked(const Pin& receiver, const Pin& provider) override
    {
        if (receiver.m_ID == m_RadiusIn.m_ID) m_RadiusIn.SetValue(m_blurRadius);
        if (receiver.m_ID == m_MinIn.m_ID) m_MinIn.SetValue(m_minThreshold);
        if (receiver.m_ID == m_MinIn.m_ID) m_MaxIn.SetValue(m_maxThreshold);
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
        static ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Stick;
        int _blurRadius = m_blurRadius;
        float _minThreshold = m_minThreshold;
        float _maxThreshold = m_maxThreshold;
        ImGui::Dummy(ImVec2(160, 8));
        ImGui::PushStyleColor(ImGuiCol_Button, 0);
        ImGui::PushItemWidth(160);
        ImGui::BeginDisabled(!m_Enabled || m_RadiusIn.IsLinked());
        ImGui::SliderInt("Blur Radius##Canny", &_blurRadius, 0, 10, "%d", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_radius##Canny")) { _blurRadius = 3; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_radius##Canny", key, ImGui::ImCurveEdit::DIM_X, m_RadiusIn.IsLinked(), "radius##Canny@" + std::to_string(m_ID), 0.f, 10.f, 3.f, m_RadiusIn.m_ID);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled || m_MinIn.IsLinked());
        ImGui::SliderFloat("Min Threshold##Canny", &_minThreshold, 0, 1.f, "%.2f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_min##Canny")) { _minThreshold = 0.1; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_min##Canny", key, ImGui::ImCurveEdit::DIM_X, m_MinIn.IsLinked(), "min##Canny@" + std::to_string(m_ID), 0.f, 1.f, 0.1f, m_MinIn.m_ID);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled || m_MaxIn.IsLinked());
        ImGui::SliderFloat("Max Threshold##Canny", &_maxThreshold, _minThreshold, 1.f, "%.2f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_max##Canny")) { _maxThreshold = 0.45; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_max##Canny", key, ImGui::ImCurveEdit::DIM_X, m_MaxIn.IsLinked(), "max##Canny@" + std::to_string(m_ID), 0.f, 1.f, 0.45f, m_MaxIn.m_ID);
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        if (m_blurRadius != _blurRadius) { m_blurRadius = _blurRadius; changed = true; }
        if (m_minThreshold != _minThreshold) { m_minThreshold = _minThreshold; changed = true; }
        if (m_maxThreshold != _maxThreshold) { m_maxThreshold = _maxThreshold; changed= true; }
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
        if (value.contains("Radius"))
        {
            auto& val = value["Radius"];
            if (val.is_number()) 
                m_blurRadius = val.get<imgui_json::number>();
        }
        if (value.contains("minThreshold"))
        {
            auto& val = value["minThreshold"];
            if (val.is_number()) 
                m_minThreshold = val.get<imgui_json::number>();
        }
        if (value.contains("maxThreshold"))
        {
            auto& val = value["maxThreshold"];
            if (val.is_number()) 
                m_maxThreshold = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["Radius"] = imgui_json::number(m_blurRadius);
        value["minThreshold"] = imgui_json::number(m_minThreshold);
        value["maxThreshold"] = imgui_json::number(m_maxThreshold);
    }

    void DrawNodeLogo(ImGuiContext * ctx, ImVec2 size, std::string logo) const override
    {
        // Node::DrawNodeLogo(ctx, size, std::string(u8"\ue155"));
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
    FloatPin  m_RadiusIn = { this, "Radius" };
    FloatPin  m_MinIn = { this, "Min Threshold" };
    FloatPin  m_MaxIn = { this, "Max Threshold" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[5] = { &m_Enter, &m_MatIn, &m_RadiusIn, &m_MinIn, &m_MaxIn };
    Pin* m_OutputPins[2] = { &m_Exit, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    int m_device            {-1};
    float m_minThreshold    {0.1};
    float m_maxThreshold    {0.45};
    int m_blurRadius        {3};
    ImGui::Canny_vulkan * m_filter {nullptr};
    mutable ImTextureID  m_logo {nullptr};
    mutable int m_logo_index {0};

    const unsigned int logo_width = 100;
    const unsigned int logo_height = 100;
    const unsigned int logo_cols = 1;
    const unsigned int logo_rows = 1;
    const unsigned int logo_size = 9057;
    const unsigned int logo_data[9060/4] =
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
    0xe6e5e4e3, 0xeae9e8e7, 0xf5f4f3f2, 0xf9f8f7f6, 0x00dafffa, 0x0001030c, 0x11031102, 0xf8003f00, 0xc187e07f, 0x3eea0a0f, 0x2cfed401, 0xf5d4587c, 
    0x5ee1278d, 0x81aea99d, 0x48185e1c, 0x6df5d5a6, 0x45f3e251, 0xdd4f02bc, 0xfbc5b1c4, 0x296c96e7, 0x9d810855, 0xc47f4e89, 0x1f6bf27f, 0x73ec3f0f, 
    0x90fe2ff1, 0xfaeb55e8, 0x1b7dc10f, 0xc7ecdfe1, 0xc5bbf8a7, 0xf5d11f5e, 0x69de109f, 0x8f34be97, 0x785ed441, 0x1a4f9be6, 0xe9a59386, 0xd146e8c6, 
    0xfd03455c, 0x8fb854bb, 0xb3c6f2ec, 0xd2ce6e91, 0x152f8288, 0x4303ade0, 0xcfc25ff1, 0x89275a81, 0x6fec2ff5, 0x135fea0d, 0x532d9b75, 0x0fd2f351, 
    0x07c9dab2, 0x7c69d287, 0xda4401c9, 0x81b9cd8c, 0x81918c51, 0xc0af1c40, 0x6995f88f, 0x4fbc14df, 0xa07fcd17, 0xecef32f8, 0x3f31bfd1, 0xb42deedb, 
    0xc981b789, 0xcea96fdb, 0x223182eb, 0x0ce54e46, 0xeb55e58d, 0xe30d00ff, 0x00ffd7b1, 0xba8c4f67, 0x1617972c, 0x369996da, 0x36c8e287, 0xf10cafc0, 
    0xa74ddc5b, 0xf03e0b49, 0xd4da6d63, 0xc00a2952, 0xb91d5c88, 0x67bfb206, 0xc5fbd98f, 0xc5d7b45f, 0xf8021f0b, 0x52d36d45, 0x996b86b9, 0xa2842f2f, 
    0xd028d2d2, 0x8e7792b1, 0xb6a52837, 0x0b6e1b46, 0xb8f11ac9, 0xd19a0750, 0xd3684a5f, 0x57f820fc, 0xd53d7cac, 0xdde0297c, 0xfc9de343, 0xeaae4fd6, 
    0x9926bed6, 0x1b82e06d, 0x3ca3cb7b, 0x34daa62d, 0xde5619a6, 0x3d2f65cd, 0x4c3ec4c5, 0x955bf2ac, 0x8fdfb661, 0xfca8577c, 0xe05ff83d, 0x2ffc138f, 
    0xf073b4d7, 0xad8559fb, 0xb6093fb5, 0x36756793, 0xc81b7b9f, 0xacba06a9, 0x48fba5b2, 0xdae29663, 0x5632a05b, 0xb5b7b777, 0x1fa00854, 0xffd35730, 
    0x954eb200, 0x03bc8d37, 0x4fd31ef1, 0x0f34eed7, 0x853b7c16, 0x7a782a3e, 0xefe8b4f9, 0xd8b485a1, 0x283017a5, 0x6b224092, 0x8bd32489, 0x16574730, 
    0xcb5bbd69, 0x04bf5a41, 0x7e1d7e2e, 0xa756fad0, 0x4173fde0, 0xf819fed1, 0x52977986, 0x615b7cd5, 0x91166d67, 0xee126979, 0x75717529, 0x34617fe6, 
    0x49543e97, 0x12826271, 0x5a138bd7, 0xf49058b0, 0x0d34061f, 0xf87ef60f, 0xe846fb8f, 0xfda59eda, 0xdfeaa28b, 0x6febb509, 0xd3eb5e0e, 0xf9ac29a4, 
    0xd80f11c6, 0x7d8b6067, 0xa21c8d14, 0xfb45e319, 0x45902042, 0x001a6319, 0x46fbeb7c, 0x6f3a36e9, 0xeb6a1dc6, 0xd2b7b34c, 0x1b76ddf4, 0xe9da122f, 
    0x702c9176, 0x566ad1e9, 0x6609ea90, 0x60054081, 0x6008a15b, 0x184618aa, 0xe568672a, 0x5be3017c, 0x325e87ef, 0xa7477cd2, 0x71756fc5, 0xac913061, 
    0xda9e55ef, 0xcb0a22f2, 0xca8a706d, 0xd9688264, 0x803c92e2, 0x78eac8f1, 0xedaf5d63, 0x2500ff2d, 0x00ff4817, 0x7fc233b1, 0xd5698ffa, 0xf60154e5, 
    0xf019ed57, 0xffb6c387, 0xedad0700, 0x2deec874, 0xe11d3c6e, 0x3ec537fd, 0x49b5d415, 0x7eeb5d8a, 0xddaed50f, 0x2298bbfc, 0xbb4b61f3, 0x44fc423d, 
    0x54223c5c, 0x90114696, 0xffe76b88, 0x045f8000, 0x783bbe2f, 0x8b4cf7ca, 0x34d0b753, 0xb84c172d, 0xae41fcd7, 0xba4aa3dc, 0x016e9766, 0xa710b8b8, 
    0xca0033cf, 0x991b2416, 0x77554699, 0xecaffb3a, 0xc417f18f, 0x1e68a7ef, 0x7db4d507, 0xcec2f242, 0xc37b2df6, 0xa3a9e69a, 0x3ae9a845, 0xc33d951e, 
    0x5ef3fae9, 0xd209a8a4, 0xe79ab726, 0x683429cf, 0xcaa95ab3, 0x8faf3bb3, 0xdff86ffc, 0x2b2de363, 0x801ff04c, 0x0a8fcbf4, 0x26d024fc, 0x1ecdb469, 
    0xd4bb5677, 0x7dec8a6e, 0xe275514f, 0x8e17b95b, 0x1153483e, 0x5c820244, 0xbfc65b03, 0xa69976b4, 0x5f69b1ea, 0xe111bc0b, 0x3ac1037d, 0xc0ce2264, 
    0xbac293f8, 0xdfabb34e, 0xee5acc80, 0xdae6e6f6, 0x98956752, 0x8ac848b1, 0x0c1adb35, 0xffe7e626, 0xbca5e100, 0x4000ff5d, 0x00ff878f, 0x3d3c6ef8, 
    0x35c800ff, 0xd96fe193, 0x8e9fe2eb, 0x6f5d2b74, 0x3c0d7fc3, 0x160de261, 0xa89177eb, 0xd50d7ae9, 0x58dbbccd, 0x23916ca3, 0x6565d82a, 0x23153c38, 
    0xf86fada8, 0xff8def64, 0xfe46f400, 0x2fe17f20, 0xc600ff7d, 0xf0a003a8, 0x9f358fb7, 0xc7075a89, 0x9e5f7fdd, 0x5f4a7dde, 0xc4d66903, 0x674159da, 
    0xb706450a, 0x690cc1a1, 0xc691080c, 0xa05114a9, 0xa2305055, 0x1100ffb9, 0xacc900ff, 0x00ff3c7c, 0xbfc4cfb1, 0x51a143fa, 0x49fe6ff0, 0xfb6fc7d7, 
    0xf5bf2d13, 0xf1a8d121, 0xc79afc1f, 0x1cfbcfc3, 0x00ff4bfc, 0x00153aa4, 0x00ff081f, 0x7c173ed1, 0xf79fbb70, 0x66f89336, 0x798a4dcf, 0xe992543e, 
    0x7436bdf5, 0x65f05881, 0xa46ead68, 0x96623e08, 0x8d18b0f2, 0x00fff588, 0x68f627db, 0x59ecaff0, 0x6d810f6d, 0x8fe26b3c, 0xd741fc11, 0xf2fa8661, 
    0xb4481be2, 0x155d16fd, 0xf21693a4, 0x59922646, 0x71561727, 0xf0f2c9c9, 0x0372e314, 0xdfb25f59, 0x559fe9c5, 0xb6f005f8, 0xed884bda, 0x69c1f8e2, 
    0x36426de2, 0x66263f86, 0xedadd366, 0x86cbc90a, 0x97bb9740, 0x7ea8c485, 0x2a4f3e62, 0xab11f8bd, 0x36cffe5a, 0xd662181f, 0x0fcf7b74, 0xf880f85c, 
    0x163e5d66, 0xb7d617b8, 0x129f17a2, 0x95b1c22b, 0xe4be2231, 0xd600ff91, 0x0a86e028, 0x285eed01, 0xfbbdeef0, 0xc167783c, 0xb40500ff, 0xc3e3263d, 
    0x267e117f, 0xf8739669, 0x3617c4bb, 0xecf7c657, 0x6f91b3b7, 0x2dc6dba3, 0xa002ab84, 0xdd56ae89, 0xdfa67925, 0x62425806, 0xe07f813e, 0xdf8600ff, 
    0xad87a7b3, 0xa2793ced, 0x6ffa695c, 0x1dd1e688, 0xbca8137c, 0xab5be6c6, 0x7b9b7b7b, 0x7f52dd8b, 0x9626dcb2, 0x8ba865af, 0xcd2d2ca9, 0x5e32ecd5, 
    0x415234db, 0xf0952bb5, 0x7f87ef95, 0x2bde9ee0, 0xc28e7811, 0xa1fd50e3, 0xfd96b974, 0x7163b066, 0xf376e8a7, 0x56bc5269, 0x92786e52, 0x789ae549, 
    0x1365b963, 0x3fd2b646, 0xc049e5d9, 0x54abf965, 0xbfe818d7, 0x1d35af0f, 0x65f14763, 0xb60c93be, 0xe1617891, 0x6dac89ba, 0x8f5bf252, 0x38d2e922, 
    0x5cd2ac43, 0x9da1e1ce, 0x5a902bdc, 0x0f001432, 0x35acf87f, 0xcbfe191f, 0xf0397e97, 0x78acb11e, 0x1e7ce163, 0x8400ff20, 0xcbc1f75e, 0xa69f9e35, 
    0x700cc9dd, 0x41db15cf, 0x4ff3c81c, 0xb1109220, 0x0ad7b748, 0x5ef38eb6, 0xae5623c8, 0x8bb7e103, 0xf8327e2c, 0x06dfc167, 0x5b0adf35, 0x3a215eea, 
    0x0e3e699a, 0xfab54df1, 0x8669b3e9, 0xb5cd77f2, 0x7dd316e9, 0xce5cd0ae, 0x95850b27, 0x80249809, 0x9f54b449, 0xf1875ee3, 0x0d7ec797, 0xc98500ff, 
    0xe043a86b, 0x49d90fcf, 0x00ff43a4, 0x9a684c08, 0x9198a68d, 0x216cd837, 0x6fb443fb, 0x79f41214, 0x2ccb4c77, 0x91b264be, 0xe253de1e, 0xbfb48567, 
    0xfc8ca7b1, 0x9ffd6b2d, 0x5a4f5de0, 0x6b74d1bf, 0xaf19b94b, 0x664359f6, 0x96a16bc6, 0x79a92c41, 0xfb96bf0b, 0x4430c33c, 0x002d028d, 0x3586077b, 
    0xf66b1d6b, 0x714bf8fb, 0xdf5bdea4, 0x0fe249e8, 0x3269db08, 0x2ac92ac3, 0x6147f1e9, 0x2577a415, 0x90765149, 0xa9704924, 0xccc2adf2, 0xab000a17, 
    0x31bc147f, 0xf87c7caa, 0xe2a3f01d, 0x8d758997, 0x9ef1443f, 0xe07b7ba1, 0xc44b8deb, 0xfe6cabd7, 0xc4484b26, 0x7f807ab6, 0xdf6d7721, 0xd42d9ffd, 
    0xc52b32d2, 0x0191656c, 0xf657d395, 0x8400ffef, 0xedb7e08f, 0x56dde02b, 0x0d13fecf, 0x6cd35e1b, 0x3236c493, 0x1afbd7f8, 0xa4eec87b, 0x5a6fafd5, 
    0xeb761a64, 0x92b91173, 0x2c200b57, 0xe591ba18, 0xf2e31584, 0xfd497c59, 0xda3c7ca5, 0xbefdfdcd, 0xfff067ab, 0xfaf6c300, 0xb09c9f1d, 0x5b59b2db, 
    0x475ddddb, 0x1b44186b, 0x35ad2dd9, 0xd5c9f709, 0xb477996d, 0x00720c8d, 0x3c187f75, 0x7e0ddf62, 0xe17bf80b, 0x71a38e9d, 0xc58778a9, 0xe28d667a, 
    0x0deb195f, 0x5e485da9, 0xd167b625, 0x544795b4, 0x94bd0541, 0xc4b0f2d6, 0x5574cd83, 0x56d42d70, 0x869fc157, 0x2dd4155e, 0x5bf15dbc, 0xb0265ac6, 
    0x6278103e, 0x59dab66d, 0xd72789bf, 0x5f240935, 0xece2e8b2, 0x96378b88, 0x82e559c6, 0x2e0cc922, 0x78bc10c1, 0xc2c3f1a9, 0x3f8ca7da, 0xc0377d68, 
    0xdaaf45fa, 0x970d53fc, 0xf61cbc87, 0x35a6621e, 0xcb366d6b, 0xccdb9e4f, 0xbae51362, 0xcdfc4886, 0xc1dd0edb, 0xa7c6908a, 0x0f7b21ed, 0x9f4c7c86, 
    0x0bcb740b, 0xf048337d, 0x7761cf2c, 0x2049bb1d, 0x16c4d6bc, 0xefb5faf6, 0x15b32399, 0xe5317b9a, 0x45d0f2a8, 0x1b45441c, 0x2d809bb4, 0x9df6a7f8, 
    0xbb1ee297, 0xd7f8ab75, 0xfc83d7e1, 0xdbf2775d, 0xbeddf1e9, 0xa36f67a9, 0x60a146d8, 0xb3e0ecd3, 0xad2882be, 0x5ded00a2, 0x59eeb2a5, 0x9535b1d8, 
    0x930b00ff, 0x42f43fc2, 0x00ff1f7e, 0x7f8877e0, 0x555e6bf9, 0xaf7a0045, 0x6ff4bfc2, 0x6e1a5f84, 0x2fd37f6d, 0xfb74d1e5, 0xe7beec19, 0xb5fa6091, 
    0x6fdfb2a4, 0x7197373c, 0xe46f636f, 0xdfbe998f, 0x07b603e6, 0xd37fd104, 0x198f653f, 0xfff7a37d, 0x33ded900, 0xe6c5fed0, 0xfb65df7c, 0xf6ad8e4d, 
    0x733f2b9f, 0xa625fbcd, 0xf6dfb8fd, 0xcb677768, 0xff061f5c, 0x7c9de400, 0xb100ff76, 0x00ffdb32, 0x8f1a1d52, 0xacc97f0e, 0x00ff437c, 0x5fc3cfb1, 
    0xd0ae43fa, 0xf5ec4f06, 0xc02f9de2, 0x1a7e1fdf, 0xba5b9378, 0xa38b16fb, 0xd44c9bf8, 0xb67cae2f, 0x2b82c993, 0xf691e4a8, 0x55d82ca0, 0x38090a27, 
    0x4a5f13e0, 0x8ff042fc, 0x82d96fc4, 0xc3f84bcf, 0xc2785eaa, 0x956ee3cf, 0xac93fd7b, 0xd554af78, 0xde6afdf4, 0x65685a0d, 0x82d7f4f2, 0x740e6955, 
    0x17e72d89, 0x05845b42, 0xb2153bc4, 0xf0d5fcd0, 0x10c2c7fa, 0x06e213f8, 0xbfb8e2b1, 0xcad26eb4, 0xb041330d, 0xb505d4b4, 0xf2947ae4, 0xb079a9f9, 
    0x8ab62482, 0x94845b2b, 0x986bbe61, 0x09326c14, 0xfe03fa62, 0x3fc5cf15, 0x00ff62f8, 0xf05f54f8, 0x7f287ea4, 0x00ff49c2, 0x12fe030b, 0xc27fb5bf, 
    0x3ff67527, 0x6700ffb2, 0xed66977d, 0xfcddfcbb, 0xbfdd66e3, 0x03289ec5, 0xe341f88a, 0xc6f8169f, 0x846fe2e7, 0x38fc1cbe, 0x78fabdf0, 0x50471db2, 
    0xabb52fd6, 0x862d615b, 0xd9772390, 0x7b1bcb25, 0xe42d383b, 0x912c5c31, 0xd13e6ec5, 0xacf0bf0d, 0x7ca0d770, 0x17bed033, 0xcff0197c, 0xc41b2c8f, 
    0x1200ffd6, 0xc3a355bc, 0x6ba8bb16, 0xfbb4153e, 0x2d8ea625, 0x8ab4b967, 0x955b3f3d, 0xf74ed24d, 0xc9647a77, 0x44a3b53d, 0xbb5114cf, 0x2ccdbb35, 
    0x8b1ffa7b, 0xdfe10f7c, 0xc3ea780e, 0xd3071ec2, 0xd8047ff5, 0x6d4d6778, 0xd5bac443, 0xf7751a9c, 0xb5ad2d8f, 0x08b9e118, 0x0b4ba992, 0xa50b5219, 
    0x6e8da049, 0x656bd73e, 0xe43d5ce6, 0xa027c5b1, 0xf216785c, 0xf889f6ef, 0x8af8a197, 0x7a58f81f, 0x6eef872f, 0xb5d626ae, 0x9fdb094f, 0x1a6f7810, 
    0x56ccdd85, 0x6e736fe9, 0xbaec7dba, 0xdc95babc, 0x9b2fb156, 0xadcdb5e6, 0x6e238ac0, 0x9507a890, 0xf04a7c5b, 0xfd0bed4f, 0x161fe3a3, 0xd34f33f8, 
    0xcde021fc, 0x54c7da16, 0x2fc437f1, 0x277e6b16, 0x1faf6fd7, 0xd6b636cf, 0x647fc791, 0x243b4bb2, 0x472ae5ad, 0x252e6114, 0xd7c24567, 0x199f6895, 
    0xcfe1317c, 0x7c139f85, 0x10efe129, 0x7fc18378, 0x536b92f0, 0x069a445e, 0xd1fe288b, 0xe3098eb0, 0x99bab0b4, 0x5be583f5, 0xbecdb41f, 0x0ccb1523, 
    0xdaa769a9, 0x87077b14, 0x33f10174, 0x131ed7c0, 0xc7da804f, 0xae3d7c80, 0x78ae6fea, 0xb4d04a7f, 0x6d50631d, 0x324dde46, 0x54dfdefe, 0x9a75bcba, 
    0xfb34887b, 0xbf2dad8b, 0x5646cad1, 0xf21676f6, 0x9f471599, 0x091fc5af, 0x6972c97e, 0x7c80777a, 0xb3f8a333, 0x37a485e2, 0x2ebeeb71, 0x72918ef1, 
    0x3435d166, 0x8fe368ba, 0x629fb14e, 0xd9e6d642, 0x8de64927, 0x1d3e63e5, 0x50e02120, 0x0d1fc003, 0x6badb5ef, 0xb9f8b149, 0x6b5807f1, 0x0609cf7b, 
    0x93bef0b6, 0xf2c535ad, 0x9d5951e8, 0x66aed54e, 0x7a36342b, 0xb808697c, 0xe59a0530, 0xd931b47c, 0xaf379231, 0x99eef140, 0x1e852fe0, 0xaf46f001, 
    0xc23f4de2, 0x533656f6, 0xefe0ab36, 0x129ae80e, 0x2e7e176a, 0x92a458b7, 0xb858fbd2, 0xbcb6b7be, 0x73b9464b, 0xc7b6a514, 0x66df8a76, 0x523882b4, 
    0xc5a1a967, 0x95e0a7fd, 0xab45fcd3, 0xad49c27f, 0xf119de69, 0x33ddc3cf, 0xa916bac4, 0xae1543e6, 0xaf066d8b, 0xcebc3d6f, 0x3245d28c, 0x9dd85ddc, 
    0x396b35a6, 0x1f5000d5, 0x3f7ee5cc, 0x247e1668, 0x54fbbff8, 0xc500fff8, 0x9cf697da, 0xae89079a, 0xce1a1d34, 0x7b877bfe, 0xdb9bfb8b, 0x2897ad88, 
    0xcade8a38, 0x0d44f2ce, 0xbdbd2a22, 0x2d4462b2, 0xff790013, 0x2f758000, 0x1f878fec, 0x6b28fe13, 0x3fa1aeda, 0xde6df58b, 0x6eeed01b, 0xeb7bce57, 
    0x56e7e0ab, 0x906479b9, 0x8c2c9b79, 0x4876c8b0, 0xbba5faca, 0x6f8c2d9f, 0xff5e2b5e, 0xc54ff100, 0xbc835f7a, 0x874fe177, 0x35d32d5e, 0x4378150f, 
    0x368bdee1, 0x43a07185, 0xb09435ab, 0x4911d76a, 0x30cba42e, 0xc1122349, 0xaa4a5ca9, 0x91241beb, 0x79ab5bd9, 0x5fbdf3ab, 0xf0057c19, 0xa5738f96, 
    0x716d7c19, 0x5df5f1f0, 0xb65dd732, 0xa481f8b0, 0xca92ed5c, 0xbb2d31ee, 0xb1e95d5a, 0x55c50bdd, 0xc52bafdc, 0x1e28f36b, 0x38326f58, 0x8a2a3c80, 
    0x65f85ff5, 0x3ff78b5f, 0x1dbed2bc, 0x16c58378, 0x55affe0d, 0x6b9bb5f0, 0xbefe743a, 0x9b9fe555, 0x554ebb04, 0x656d3bb6, 0x60653865, 0xef64f80f, 
    0xf400ff8d, 0x7f20fe46, 0xff7d2fe1, 0x03a8c600, 0xe063f8b5, 0xd90f1bab, 0x8b67e24b, 0xeae096a3, 0x7f99ae5a, 0x32134fa5, 0xae152bf9, 0x6349e1b3, 
    0x18721b65, 0x89a1ecb5, 0x80a91024, 0xbce24930, 0x2600ff39, 0xfe0ff1b1, 0x7f0d3fc7, 0x5dbb0ee9, 0x7c8500ff, 0xcff8af5f, 0xebc527e0, 0xf05553fd, 
    0x25d800ff, 0x87246ed1, 0x6dacd140, 0x9fb3f734, 0xc9c263fb, 0x5b5bc32d, 0x14471a43, 0xa1ba1dc9, 0x4edb331c, 0x3f7b1b70, 0xf9cfe101, 0x7f888f35, 
    0x6bf839f6, 0x754800ff, 0xfff700da, 0x077f8000, 0xfe133d74, 0xdff4c912, 0xdf7e7818, 0x5df4d24a, 0xe267c63f, 0x9364a776, 0x97e387a7, 0x686b76ed, 
    0x2479d6a2, 0xaff25283, 0x4c38ead2, 0xd3be2089, 0x50493412, 0x7e757247, 0x0f9fe91e, 0x4db568f4, 0xf850e32a, 0xca7ea081, 0x77d1381e, 0x9e6a5cdd, 
    0xd5b9b224, 0xfa8ae72c, 0xe9b3ba22, 0xd14c7156, 0x727ad744, 0x8057e269, 0xdf0fde8f, 0xe818eb02, 0x5de1b70e, 0x73f84f37, 0x68dba963, 0xafd012fe, 
    0x13e05bfc, 0x614359ad, 0x6ba59916, 0x7a5a97b6, 0xd7b4faea, 0xbd2248a4, 0x202fee58, 0x08910cdd, 0xab36626c, 0x70b84510, 0x6bf6d77c, 0xb7f05f87, 
    0xd7f0e3c4, 0xfceff5e2, 0x6fed6f2a, 0x68a14b06, 0x7c8f977a, 0x96256931, 0x57d5009b, 0x6d71dd55, 0x96e308a8, 0xf2822529, 0x44de7734, 0xb85fa0b2, 
    0xf85b8036, 0x1b1de2d5, 0x7bb9f84a, 0x6f4de8a7, 0xe6792be2, 0xe085163a, 0xd3e80e1f, 0xe1b306c1, 0xb5ee716d, 0x9022497b, 0x12dc25e4, 0x28e2366a, 
    0x536cd71a, 0xcca2a325, 0x575d6161, 0xc423bdc4, 0xbbf1285e, 0xd6462778, 0xfc2d2d6e, 0xa9c5a90d, 0x3af6176a, 0x8acfd2e0, 0x6167613c, 0x34b9a6a7, 
    0x124b9717, 0x16b4b5db, 0xf6269757, 0x2b29e558, 0xdfe4ee48, 0x9e5b1dc1, 0xc5834bd7, 0x8ff10d5e, 0x3f4db482, 0xbcedd44c, 0xf8027fcb, 0x9e465b9f, 
    0x76bae0d5, 0xb35bd29a, 0x11b74e33, 0x494f7b78, 0x09ede2d6, 0x6709b736, 0x5a91a40b, 0x5e719279, 0xeff0b839, 0x1f8d6fc5, 0x0ebf3403, 0x138de2cf, 
    0xa987f842, 0x9b519f97, 0x92522355, 0x3de6f2ca, 0x55a6e502, 0xdcb51f06, 0x737197a6, 0xe7533871, 0xc9d1a036, 0xce2a2ce6, 0x1bfccf01, 0xf009fcd0, 
    0x101fc2d7, 0xfca46bbc, 0xd585f144, 0x965616fc, 0x00ff7a56, 0x3b1bfc82, 0x2e6978dc, 0xb874e3ae, 0x9b4be69e, 0x29b9b574, 0x91a4356b, 0x17212bc7, 
    0xc23d45c0, 0xffbccaf9, 0x6e8dc400, 0x329ec7fb, 0x67f167f8, 0xf8b8b64a, 0x7aeaab85, 0x4d7e789a, 0x0d56c43f, 0xa8bb2e71, 0x38ce2269, 0x9255db7c, 
    0x6c6e9156, 0xd47cd3e6, 0xc33d3b33, 0x794280dd, 0xafb29f64, 0x1fb5ef86, 0x0d1e7813, 0xd7b85a3f, 0xc467ddb4, 0x257e3717, 0xba6773f8, 0xfa69ab63, 
    0x92fa162c, 0x888196a4, 0x6b856bba, 0x448aa0a8, 0x8d88650a, 0x143300ff, 0xff2b3ee8, 0xc087c400, 0x4ffc119f, 0x33c57fa0, 0x966de2b3, 0x1eede1b3, 
    0x5113fddf, 0x2e9d87f0, 0x3cda1bbf, 0x5cb08f29, 0xb6c400ff, 0xcf38dec9, 0x92d086dd, 0x0fd02676, 0xb98ef16a, 0x71fca871, 0x5e0c0bf8, 0x8400ff1e, 
    0x237ec357, 0x8d9e9df1, 0x4f8de07f, 0x7d4d9b4a, 0x48647802, 0x7b4b0b35, 0xce2e5f06, 0xd5b154c7, 0xb6a7ae2c, 0xf24a9c57, 0x02599295, 0xd77efc65, 
    0xf1b1be56, 0x2fbc87e7, 0x785c5eae, 0x1e9ec08b, 0x16e3db86, 0xadf49abe, 0xa1c6ba70, 0xf769a173, 0xee7cc1b2, 0xdcb3a27c, 0x4986b205, 0x515b9911, 
    0x90e67c2a, 0x0f1a7423, 0x771a27fc, 0xfc1a3e84, 0xfc97f142, 0xf1241e24, 0x64af854f, 0x5483b2f8, 0x599be6f3, 0x4cef92b5, 0x7bb4b6b5, 0x29e97299, 
    0x1bf5d0b5, 0x5164dbc0, 0x5d246c69, 0x733124ca, 0x15b400ff, 0xadc38fb5, 0xbf3de22b, 0x7d8ff6f6, 0x338d7be2, 0xee0f3fc3, 0xc57bcbf4, 0xd3a24892, 
    0x09b5ed95, 0xd595af7c, 0xed86ef85, 0x7d3c361e, 0xe822fa9a, 0xc08832af, 0xb5d5553e, 0x557fed6b, 0x3bf5d4bc, 0x52478dcb, 0x6eee99bd, 0x69e52eaf, 
    0x62579e66, 0x12bb23cf, 0x25899959, 0x49922489, 0x96fd75af, 0x7a681abe, 0xf8a5bb96, 0xfe835ddb, 0x1e4d3b13, 0xe0937bf6, 0x4b12295d, 0xacb28f8b, 
    0xa9c6ad12, 0x6bf6eb24, 0x34c9201d, 0x3375cd51, 0x11450237, 0xfc481e90, 0xc4d7bd02, 0xe08727de, 0x092fc1ef, 0x36c363f8, 0x8c271a97, 0x3ea6633c, 
    0x71cde2af, 0xf98ea46f, 0xe69aa5ac, 0x46286d1b, 0x49ab7c9b, 0x8e37846d, 0x1fedbb8f, 0xf273b768, 0xa9005924, 0xde809fe2, 0xdbb5d114, 0xcbf877ab, 
    0x4600fff0, 0xd374b6dd, 0xd653f71a, 0x5124cd5e, 0xed254122, 0xb3f69a8e, 0x47b81acb, 0xf5a65878, 0x1918636c, 0xfca6f05f, 0x4500ff23, 0x00ffe1db, 
    0x87f800fe, 0x559500ff, 0x075054e5, 0x83c79ad2, 0x7a873fb4, 0xf4d8c677, 0xfc2cd424, 0x3ffca837, 0x47b6d4f0, 0xe549ba58, 0xe342ad5b, 0x99c5d443, 
    0x2489229d, 0x32de6297, 0x8e664585, 0x84db46d6, 0x03af3872, 0xffda3769, 0xe3dbec00, 0xb34c336d, 0x2b75d4b8, 0x5af81cdf, 0xd2ced6da, 0xe5699626, 
    0x52753d7b, 0xb3044534, 0x80021033, 0x5e012449, 0xd93fe0ab, 0x105fc57f, 0xcfe03dbc, 0x68a80f85, 0xc7785cf3, 0xbcd7bf53, 0x6628e201, 0x54324d96, 
    0x5157dd86, 0x65914a32, 0x783be256, 0x730fc70e, 0xb19e266f, 0x2e6e6dcb, 0xfdca4a9a, 0x35657c99, 0xfcae8787, 0xc655fa44, 0xf113feb2, 0x517c83cf, 
    0x1d9e69a8, 0x2b37e3d3, 0xfa7669a4, 0xf2bbb998, 0xc6360a63, 0x779200ad, 0x8d4c32c0, 0x7197ee2b, 0x4f0df600, 0x9ff6f6c3, 0xe16dfc14, 0xd3457bcd, 
    0xe9a23ffc, 0x3eb566be, 0x0aa726b1, 0xe9a67f4b, 0xe9a1c573, 0x4b8a1f3e, 0xfbb7a688, 0x11b45a0c, 0x42dff3c4, 0x101cd13e, 0x4624dcdc, 0x164f0d20, 
    0x78d0f678, 0x3efec787, 0xf167c1f8, 0xa57ee223, 0xe16f9906, 0x1ec26bad, 0xfcd3b723, 0xb9c52d39, 0x82bab3b8, 0x27789b6b, 0xd12ebd7b, 0xe42ba01d, 
    0x70cbd1c0, 0x8f5c33b1, 0x396ec9b5, 0x3dfc0b5f, 0x7c892ff1, 0x02efe11f, 0xdede360d, 0x58f856f3, 0xba9857ba, 0xd1c485bd, 0x38e2714b, 0x9a316db5, 
    0x48da29de, 0xed5beed6, 0xe24cd12a, 0xe6f18612, 0x9303c720, 0x4c7c59d5, 0xfea3a9f8, 0x05fe37cf, 0x3487bff1, 0xd1f6847f, 0xb36b8078, 0xf0569378, 
    0x8bae8dad, 0x81148624, 0x48478d34, 0x9d86c882, 0x56c48b94, 0x478c7b82, 0xb092376b, 0x0044dd2a, 0xdde1b92e, 0x8647e353, 0xbf781a3e, 0x3a1e77c4, 
    0x1fc567f1, 0xc43fd916, 0x45eb7a8d, 0x59516a31, 0x69b3cd35, 0xdc6c1ffe, 0x9265b428, 0x6f693734, 0xe5edecb7, 0xd92f8dc2, 0xf88f3736, 0xdff05187, 
    0x6fd90fc5, 0xd2191e52, 0xe07fcf3e, 0x9594ec0f, 0x699b6b75, 0xd9f4ed2f, 0x3b105f75, 0x8f0d52b4, 0x4df5413c, 0x23593926, 0x9850c84c, 0xd69a42d5, 
    0x117ea0fd, 0xc407faeb, 0xf8db0def, 0x7f7ac42b, 0x0bed358b, 0x2cfe37c3, 0x3efac797, 0xf81b69b5, 0xeaaa4380, 0x43aa4f57, 0x4d32b838, 0x23401473, 
    0x5e9de6ae, 0xe3bb5dce, 0x9f6a6489, 0xf82abe0f, 0xed1fe2ab, 0x3c8a1f47, 0xf85ba435, 0x6a2dfe05, 0xf25ef871, 0xd4a5adf5, 0x7975d1b4, 0x8eeaa225, 
    0x95375cb0, 0x19f3ae72, 0x357fd65a, 0x00a45b23, 0x50655806, 0xf80a9e0a, 0xe1f3f057, 0xfc6d859f, 0x887fe265, 0xe2a17eda, 0xfdb4ac48, 0xf0b24b7b, 
    0x854d8b77, 0xa492a4e5, 0xaf6ec3d2, 0xff789d1e, 0xb6476e00, 0xdbb22261, 0xd8c66cc7, 0x62dc9c99, 0x7ed5752f, 0xf83b7c29, 0x4abff0f9, 0x5bfd35d2, 
    0x2e101fc7, 0x86c5742c, 0x51fc497b, 0xc2836bad, 0xd43df2d6, 0xd4253ed7, 0xd52a5d62, 0x374f9f27, 0x65b5c331, 0x8289e478, 0xdb15016e, 0x00ff75c5, 
    0xf40c9f88, 0xe3471f3f, 0x2ef8020f, 0x7bddb4dd, 0xd6b7b7c5, 0xf48be75a, 0x8744538d, 0x044b84c3, 0x49dadbb1, 0x24ac6aa4, 0x1c99555b, 0xa8ac70f9, 
    0xb7a1321a, 0x78b0afa2, 0xd25f00ff, 0xdcae2f3c, 0xf162fcdb, 0x348800ff, 0xf829fed8, 0xd0ba4f67, 0xebe04bfc, 0xe3f646fd, 0xcf0ad347, 0x9a74970f, 
    0x4d5c9384, 0x3cc95da4, 0xb76e4dd7, 0x588b733e, 0x3bf0968c, 0xf300a288, 0xd7e333fd, 0x02fe24fc, 0x1ef1fbf8, 0xd24fc3e2, 0xb4d6bfa3, 0xf04d2fdd, 
    0x4bb95ff4, 0x0b9d5b7b, 0xad553ac4, 0x7f0b06a4, 0xda16fd2f, 0x62bd6174, 0x1b464b57, 0xf386ba83, 0xcbda00ff, 0x7adb5fc7, 0xf690a6b5, 0xa1de553e, 
    0x0b00ff7b, 0x65e9f20a, 0xbfd635ca, 0x771769a4, 0xdc1e4710, 0x1c4b51a4, 0xdbcc329b, 0x5012431d, 0xb51ff4b3, 0xa0ad9625, 0x57d706f8, 0xf5d1bfbe, 
    0x3e8a8f4b, 0x6bbcd42d, 0x61f8ab77, 0x35d2246e, 0x7e285628, 0x3a49e4c9, 0xe7ad8d06, 0x7822d6be, 0xccc86c1b, 0xc823e53c, 0xff1ae863, 0x87dfc100, 
    0x1bd23f5e, 0xb827d65d, 0x9966f1bc, 0x0a00ffe1, 0x37973643, 0x31bcad8b, 0xc35f6d6c, 0xbcd9477a, 0x3ecdbbbb, 0xdb9d576b, 0x3de5cded, 0xb4634ecc, 
    0x367c4466, 0xd32a3e00, 0xe58ab124, 0xb8b750db, 0xcc30b7ba, 0xacd396aa, 0xb031252c, 0x3832cb89, 0x6614b22a, 0xacca025c, 0x75b090a1, 0xabdabff4, 
    0x8ef61bfd, 0x47a58bf8, 0x3a68d8f2, 0xb487c7d5, 0x16f9bae8, 0x2bf60d16, 0xbcd57338, 0x53787bbb, 0xb3cd1273, 0x49621673, 0xf60f5df7, 0x147d8618, 
    0x289e44de, 0xef75d3be, 0x117e6810, 0x9ff5d5b1, 0xba64490d, 0x0c5e5f6b, 0x6fc930eb, 0xf5a53d73, 0xde96c8a6, 0x64059732, 0x8fd9e06b, 0x9a3f0d6c, 
    0x6e0f3f68, 0x8b9f6b3f, 0xddf13efe, 0xf8a7a9ce, 0x8f7ac307, 0xf2e69289, 0x503bbf5b, 0x4e7d8eb8, 0x950543f6, 0x66bcad9d, 0x0492eb7b, 0x502351e5, 
    0xd12c07c2, 0x8007508c, 0x7c455f51, 0xc33ff15e, 0x329e86bf, 0xbed14093, 0xe25ef80a, 0xd318d6ce, 0xad36f1c9, 0x5bad4dad, 0x75858db2, 0x435fb32f, 
    0x6ddb1d6f, 0x0904c912, 0x923703b6, 0x152992d8, 0x5cf8af78, 0x00ff119e, 0xfff013a2, 0x0300ff00, 0x00ff43bc, 0xfb005acb, 0x34f600ff, 0x0ff5d2b5, 
    0xd74047da, 0x4f13ad34, 0x4f5df4d1, 0xddb5d05a, 0x464ad332, 0x9fb3cefe, 0xb686f050, 0x06d8bada, 0x37e2f266, 0x111966b6, 0x29d60a1b, 0x55dde611, 
    0x81fd553e, 0x7fe3e67f, 0x8ff88cec, 0xeb6d00ff, 0xfe0b7ed5, 0xfd079ed3, 0x21f86298, 0xe241aa7f, 0x86456b0d, 0xda52fbcb, 0x12183b6b, 0x6f585be2, 
    0x0b6667bc, 0x521bd7c4, 0xdd4a6d56, 0x401909b6, 0x55727e59, 0x00fffc77, 0xeb7838fb, 0xc2f714fe, 0x321e8dcf, 0xbff60ffb, 0x3e8acef6, 0xcd8b7d1d, 
    0xed9f3cf2, 0xfbe52e68, 0xbbadfd66, 0xc7ccfeca, 0xf36fb495, 0x6136bdb3, 0xc12f3d80, 0x653c165f, 0x63f6a3f0, 0x3dc37ff1, 0x43140f62, 0xea70e8ab, 
    0x10feea33, 0xb0bb25d5, 0xba4ecb86, 0x61343cd4, 0x61993117, 0xb5e10471, 0x7c404a18, 0x70485fcc, 0x5dc933a9, 0xbfec00ff, 0x6a2efb7b, 0x2ed408be, 
    0x7f0b1b75, 0x6950680f, 0x417c5b92, 0xda4e7cd2, 0x36affec5, 0x9ef8f69d, 0xfedae6da, 0x65b6e2c1, 0x11696bfb, 0x29d242f9, 0xb3c4a402, 0x00ffcc2f, 
    0x5b18fc2a, 0x57c64bed, 0xcd75129f, 0xf1f1472f, 0x4d3835d3, 0xb58c87dd, 0xd4b6b726, 0x3ad10b35, 0x71754ddb, 0x55a55829, 0x589a55b8, 0x585a9564, 
    0x894152d2, 0xdd8b242e, 0x5778127e, 0xfb21f843, 0x57c7f83e, 0xbc821ff1, 0x355a89ef, 0x8f78dcea, 0x0435d54e, 0xd1b6bdb2, 0x18d29225, 0xbd4bdf6c, 
    0x4d499249, 0x4277d6d4, 0xedf9f449, 0x5901a9bc, 0x1123e592, 0x2c1e7400, 0x36a3a6f0, 0x6fe27d9f, 0x3e3a6808, 0x1ec487a4, 0x7db4d236, 0xacd54f1f, 
    0xadb458e4, 0xb4f24f67, 0xab4b0bfb, 0xe7cd9a56, 0x25ef4982, 0xcd19265d, 0x45cb69c1, 0x519a5909, 0x27c9fed5, 0x9996bac3, 0x105f86f0, 0xda3cfae8, 
    0x88074d93, 0x433bfd34, 0x8235db84, 0x43c200ff, 0xda0e4de1, 0x5d489ef5, 0xa5cdf204, 0x9637c95e, 0x4bb2484b, 0xcacf8b96, 0x8800fff8, 0xe373fdf5, 
    0xbe34edef, 0x7f7cd109, 0xc21ff8a7, 0x64d11e1e, 0xe3a74ef0, 0xb7f74bab, 0x21d1e7d2, 0x17ed93b8, 0x42909453, 0xcb33d796, 0x92156d88, 0x4beb3cd9, 
    0x18552164, 0xfbc507fa, 0x1fc253fd, 0xe37d7c0f, 0xdac03b7d, 0x3fbc8587, 0x46da79a8, 0xb6716d8f, 0x5bb2bd44, 0xf9f4d005, 0xd3ce13c0, 0x5769da58, 
    0xc5a89353, 0xed6b8e30, 0x9a00d566, 0x0788d538, 0x96f0bfc2, 0x8af09f6b, 0x31c200ff, 0x7fa8b3fd, 0xdff637c2, 0x7fec2fed, 0xfed83fb5, 0xf3cbb3d5, 
    0xf3ec9cfc, 0xf19b7c76, 0xc5196fbb, 0x8bec077b, 0x56c50bf1, 0x1c3e1a5f, 0xc4df16f8, 0x091ec4ba, 0xa75bfcd7, 0x1a1e6b5b, 0xd355fe8a, 0xb9a72875, 
    0xb8e31986, 0xcae50db6, 0xb7fb45b2, 0x7e195257, 0x78c59153, 0x348b00ff, 0xf1482fed, 0xa261b356, 0xf000ff6a, 0xb7b66890, 0xab6341b3, 0xbead99fd, 
    0x58b902dd, 0x2d9ff2e7, 0xb601f51e, 0x07bb2b37, 0x01545991, 0xb506bfee, 0x9f36113f, 0xb7e007f1, 0xdbe29288, 0x663ad7c3, 0x68ddacb1, 0x13b085da, 
    0x97be7ae9, 0xc7dddca7, 0x2645223c, 0x9fd672de, 0x17009b66, 0xb0114986, 0xfc6f3242, 0xe087f870, 0xf85d437d, 0xd684e1bb, 0x35ed0935, 0xd7b6896f, 
    0x3d5a7736, 0x1676179c, 0xba9e067a, 0xd9bfad85, 0x240aed1b, 0xa49157c4, 0x4b1c0150, 0x9405ba1d, 0xbe320106, 0x5fa7f835, 0xc257f1b2, 0x6dda8d3f, 
    0xeba49fd6, 0xa78bd65a, 0x595016ea, 0xd9bf6847, 0xff439af7, 0x16876600, 0x4ea402a8, 0x804e72da, 0x350f91c2, 0x07e240a1, 0xfff8da35, 0x11f05100, 
    0x78ab89f8, 0x5ec3f332, 0x974d9717, 0x8af03f53, 0x9a9ace2d, 0x7af8db1a, 0x695cd8e2, 0xda5e8df3, 0x9ebb7664, 0x5b152d19, 0x48b8b655, 0xe4499699, 
    0x2085f44f, 0x171fd71a, 0xa1137c6c, 0x67e383f8, 0xac2f7c84, 0xeab2e95d, 0x3c846f7a, 0x96e9750e, 0xba47d3ea, 0x209834d2, 0x61d4558f, 0xea12c458, 
    0x1a91671f, 0x47643ed5, 0xdeb0f592, 0xf4e76f4a, 0x1e9786df, 0x95aef025, 0x4bfc59f0, 0x9e1669a8, 0x1fe3dd17, 0x616bc317, 0x5c58ea35, 0xeb9b91ce, 
    0x355ad199, 0x3eddb448, 0xe478e308, 0xd217681f, 0xc81a6fcf, 0xaf17dd26, 0x4c1f78d8, 0xe21bbcd3, 0xc5d7d6b4, 0xbdc06f1a, 0x75fac36f, 0xfe9f7883, 
    0xac25fd12, 0x8bc08a62, 0x3ecd74c9, 0x1d8260d5, 0xd0fe5043, 0x645b75d3, 0x7dc3269e, 0x395aaeb1, 0xea59e604, 0x120fe3c9, 0xc4db7859, 0xbcd1325e, 
    0xde5ca82d, 0x3ec48b7d, 0xc307f12a, 0xbeb9d6fb, 0x89b5edd2, 0xbea56f6e, 0xdb26b36b, 0x8f36fb25, 0x746b6573, 0xf6e37c85, 0xa158b6c4, 0x6acd94b9, 
    0x1efae301, 0x027fe3ab, 0xb6476778, 0x1b693fd3, 0x153a848f, 0x69a92ff4, 0xd7be0b5e, 0xd3bbf6f5, 0x469ea6ac, 0x1b5bcf87, 0xa9b40a13, 0x3c01c189, 
    0x42399ac4, 0x6fb5ca2a, 0xc46f16fe, 0xaf3dfa1f, 0xbf78adfc, 0xf59500ff, 0xf158fccf, 0x4fe185fe, 0xe26bc110, 0x17be867f, 0x5bc4b7f1, 0xf1545fa8, 
    0xdcacb396, 0xdacab0ea, 0xd7c4ddbd, 0x5ae54352, 0x13c7db5e, 0x41f01443, 0x184a5e2c, 0x4cb821ee, 0x2b9ea010, 0x842717fe, 0xfc84e87f, 0xc000ff3f, 
    0x00ff10ef, 0x3d80d6f2, 0xe6d4447f, 0x51ecb7f1, 0xc45b3de3, 0x2de26f89, 0x67d66675, 0x2dd65b8f, 0xa1d4bba3, 0x2e7cd564, 0x227745c0, 0x7da9d1b4, 
    0x44765157, 0x93d7ae70, 0x26660633, 0x1d64bfb2, 0xd25fc726, 0x5333adb4, 0x74d4b7b3, 0x1e8cdfdb, 0xb3bab604, 0x866589bb, 0x85759d78, 0x1018dd78, 
    0x2a48caca, 0x0d120441, 0xff7df85b, 0xe3272600, 0xff8efb3f, 0xc1d3e900, 0x7fc57e34, 0xd97f2ff3, 0xfe07f066, 0xa70228e6, 0xd8a483ec, 0x9556faeb, 
    0x76766aa6, 0x7b9b8efa, 0x96c083f1, 0x717756d7, 0x13cfb02c, 0x1bafb0ae, 0x591902a3, 0x20480549, 0x9faa4182, 0xff598df0, 0x3fbdc300, 0xb5f337e1, 
    0x3f49f80f, 0xc27f60e1, 0xed9ff61b, 0xfb2b8f2b, 0xda7fec1b, 0x3cf2623f, 0xfbca2bcf, 0x3646fa67, 0xb3f9cc67, 0x8afdd69a, 0x5ee600ff, 0xcdb200ff, 
    0xccfd0fe0, 0x3800ff51, 0xba00ffb2, 0xb800ffcd, 0x7fa9003a, 0xabf840fb, 0xee055fc3, 0xb6e17b7c, 0x8fb7f0b7, 0xb7f822fe, 0x0ff1b654, 0x2c63348b, 
    0x5ab1941a, 0x0677695b, 0x0ff36538, 0x9b7b96d9, 0x5826aec9, 0x8a6d0742, 0x108b4436, 0xe25b0df0, 0xffb58a17, 0x785e1300, 0x2ef1538f, 0x86f8a8b1, 
    0xbab617f6, 0xe5efaed5, 0x9e78ee96, 0xbcf16e03, 0x5ad9c5ac, 0x492a6212, 0xc7530905, 0x8ff8da15, 0xe1634dfe, 0x7e8efde7, 0xd200ff25, 0xaabc0a1d, 
    0xa2280a80, 0x2bfb0a80, 0xfc8fefe0, 0x527ca145, 0xf46c91fd, 0xcde25afd, 0x49b4f935, 0x21a159f5, 0x6d6bbc67, 0x60d3577f, 0x98074c12, 0x3d3396ca, 
    0x4cdbcdae, 0xaa20633b, 0x6b8d5f20, 0x5f86afea, 0x62bf58f2, 0xff87fb5f, 0xa95fea00, 0xf01507d0, 0x75dac4d7, 0xfe8000ff, 0x571aea20, 0x0b7ff484, 
    0x7ec287f8, 0xb2bdb412, 0x2d9106f1, 0xd56bbee1, 0xd66874d7, 0x12d77cf3, 0x650a2708, 0x382cdbc3, 0xc118a4f3, 0x00ff7a00, 0x6771bc80, 0x78016fe3, 
    0x087f576b, 0x8e4a7f78, 0x4a10df6b, 0x77945dfa, 0x1ed6640f, 0xb5add01b, 0xc5242d3b, 0xb16ed23c, 0x4b96bdb8, 0x2dc9b89b, 0x8c792b71, 0x7858d5e1, 
    0xf9bfc107, 0xbf1d5f27, 0xffb64cec, 0x4687d400, 0x19f855af, 0x772400ff, 0x54f79fc2, 0xb210f5bf, 0xffb70fa0, 0xd8af6200, 0xed1fe19b, 0x4d07f031, 
    0x4bf1b5f8, 0x311ed7c3, 0xf58cdff1, 0xd555533d, 0x1bb51935, 0x4df3358b, 0x8e95d1f4, 0x1589572b, 0x36be8c49, 0xc146ba93, 0xdd57b50b, 0x1fd7e17f, 
    0x13fd17b3, 0xeaaffc3f, 0x4793fc9f, 0xfee312fc, 0x9fe14f4c, 0x00ffc4fd, 0x5f77e9f4, 0xff075055, 0x000000d9, 
};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(CannyNode, "Canny Edge", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Filter#Video#Edge")