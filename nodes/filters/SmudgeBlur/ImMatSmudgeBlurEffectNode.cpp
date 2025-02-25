#include <UI.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include "SmudgeBlur_vulkan.h"
#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct SmudgeBlurEffectNode final : Node
{
    BP_NODE_WITH_NAME(SmudgeBlurEffectNode, "Smudge Blur", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Filter#Video#Blur")
    SmudgeBlurEffectNode(BP* blueprint): Node(blueprint) { m_Name = "Smudge Blur"; m_HasCustomLayout = true; m_Skippable = true; }

    ~SmudgeBlurEffectNode()
    {
        if (m_effect) { delete m_effect; m_effect = nullptr; }
        ImGui::ImDestroyTexture(&m_logo);
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
        if (m_RadiusIn.IsLinked()) m_radius = context.GetPinValue<float>(m_RadiusIn);
        if (!mat_in.empty())
        {
            int gpu = mat_in.device == IM_DD_VULKAN ? mat_in.device_number : ImGui::get_default_gpu_index();
            if (!m_Enabled)
            {
                m_MatOut.SetValue(mat_in);
                return m_Exit;
            }
            if (!m_effect || gpu != m_device)
            {
                if (m_effect) { delete m_effect; m_effect = nullptr; }
                m_effect = new ImGui::SmudgeBlur_vulkan(gpu);
            }
            if (!m_effect)
            {
                return {};
            }
            m_device = gpu;
            ImGui::VkMat im_RGB; im_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_effect->effect(mat_in, im_RGB, m_radius, m_iterations);
            m_MatOut.SetValue(im_RGB);
        }
        return m_Exit;
    }

    void WasUnlinked(const Pin& receiver, const Pin& provider) override
    {
        if (receiver.m_ID == m_RadiusIn.m_ID)
        {
            m_RadiusIn.SetValue(m_radius);
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
        float _radius = m_radius;
        float _iterations = m_iterations;
        static ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Stick;
        ImGui::PushStyleColor(ImGuiCol_Button, 0);
        ImGui::PushItemWidth(200);
        ImGui::BeginDisabled(!m_Enabled || m_RadiusIn.IsLinked());
        ImGui::SliderFloat("Radius##SmudgeBlur", &_radius, 0.0, 0.05f, "%.3f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_radius##SmudgeBlur")) { _radius = 0.05f; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_radius##SmudgeBlur", key, ImGui::ImCurveEdit::DIM_X, m_RadiusIn.IsLinked(), "radius##SmudgeBlur@" + std::to_string(m_ID), 0.0f, 0.05f, 0.05f, m_RadiusIn.m_ID);
        ImGui::EndDisabled();
        ImGui::SliderFloat("Iterations##SmudgeBlur", &_iterations, 16.f, 40.f, "%.0f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_iterations##SmudgeBlur")) { _iterations = 16.f; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        if (_radius != m_radius) { m_radius = _radius; changed = true; }
        if (_iterations != m_iterations) { m_iterations = _iterations; changed = true; }
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
        if (value.contains("radius"))
        {
            auto& val = value["radius"];
            if (val.is_number()) 
                m_radius = val.get<imgui_json::number>();
        }
        if (value.contains("iterations"))
        {
            auto& val = value["iterations"];
            if (val.is_number()) 
                m_iterations = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["radius"] = imgui_json::number(m_radius);
        value["iterations"] = imgui_json::number(m_iterations);
    }

    void DrawNodeLogo(ImGuiContext * ctx, ImVec2 size, std::string logo) const override
    {
        // Node::DrawNodeLogo(ctx, size, std::string(u8"\ue00b"));
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
    FloatPin  m_RadiusIn  = { this, "Radius" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[3] = { &m_Enter, &m_MatIn, &m_RadiusIn };
    Pin* m_OutputPins[2] = { &m_Exit, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    int m_device            {-1};
    float m_radius          {0.05f};
    float m_iterations      {16.f};
    ImGui::SmudgeBlur_vulkan * m_effect   {nullptr};
    mutable ImTextureID  m_logo {0};
    mutable int m_logo_index {0};

    const unsigned int logo_width = 100;
    const unsigned int logo_height = 100;
    const unsigned int logo_cols = 1;
    const unsigned int logo_rows = 1;
    const unsigned int logo_size = 4365;
    const unsigned int logo_data[4368/4] =
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
    0xe6e5e4e3, 0xeae9e8e7, 0xf5f4f3f2, 0xf9f8f7f6, 0x00dafffa, 0x0001030c, 0x11031102, 0xf6003f00, 0x33b624d4, 0xc2f12b5e, 0x7fd89a6f, 0xbdcadfb4, 
    0xc500ff06, 0x2da9c5f0, 0x238d48b2, 0x19076c29, 0xf82fafc0, 0x3bb673e5, 0x9be51c36, 0x34e357f9, 0xfb51551b, 0x6ca3a184, 0x2606f4e3, 0xcefaae75, 
    0x357e905f, 0xb81e1ee7, 0xbb22a9cb, 0xb1805b9b, 0x8d1f4882, 0xcd2f357a, 0x72296a63, 0x056e9e9b, 0x543b7b33, 0xcfc27d5a, 0xfac81920, 0x5778a3d7, 
    0x00bfb742, 0xa3fe9518, 0x240cd335, 0x1cce2daa, 0x3a143a4e, 0xc9b9f35c, 0xb106a35e, 0x22db16f5, 0xdd6becb8, 0xcaf0433c, 0xed19ed39, 0x0f9074a3, 
    0x6fb014b8, 0xc63bbcd2, 0x17685396, 0xd1c804cf, 0xeada88e1, 0xb5cf8f54, 0xa0b3b054, 0x188ad0ef, 0xdaa838ea, 0xe1999e3b, 0x58b49f0b, 0x75a54f46, 
    0x9efd7316, 0xade7a96c, 0x6f83df79, 0x5c29ad3c, 0xe6de2e9c, 0x135b93b6, 0x3cb8e591, 0xd7201564, 0xf33e5a97, 0xddd9df68, 0x9bf8d658, 0xa5bd2ea3, 
    0xe870ea2d, 0xff6c8f83, 0xff7c8d00, 0x112fe200, 0x0b8be1c5, 0x31c8a789, 0x9c815802, 0xfeeab857, 0xd047872e, 0x78e21eae, 0x9c854062, 0x0000eaca, 
    0x7e4d4e1e, 0xf1627c73, 0xf134beec, 0x9d21a096, 0x62824a5f, 0x196e2cb0, 0x64f1c764, 0x7ba54770, 0xe2197e19, 0x371fd9b7, 0x43fdc589, 0x1b7a1d9a, 
    0x1fedc75a, 0x19b599e2, 0x2b964b5b, 0x8bfc2c7b, 0xfb987b24, 0xf05faaf5, 0xff3a5ed1, 0x7fcb9f00, 0x00fff3fb, 0x1a3bafd7, 0x4c7ca156, 0x4c5d59b1, 
    0x7074e3a4, 0x02a70fb3, 0xed11fe97, 0xb781fe57, 0xdf0ef8bf, 0xb05c5fe1, 0x2ab792d4, 0xabe3693e, 0xec637eb7, 0xc5ba87cf, 0x89e2b3de, 0xb49c2e2e, 
    0xe90a1b8d, 0xb61d39fe, 0xf3fe19f6, 0xe75c237f, 0xfe740bc3, 0x3b4725da, 0xea6a7f0f, 0x3e403b7e, 0xd19161c9, 0xe757f99b, 0x1562c5ed, 0xf9d5d28f, 
    0x0f0f3835, 0x3d4064be, 0x2fbbae2b, 0x2a67046e, 0x3ce59ac3, 0x95faaa26, 0xccfb78ac, 0xd75deb07, 0xa0d9b48a, 0x76f9659d, 0xcf731492, 0xa7495b43, 
    0x6b32422d, 0x878f46dd, 0x7bca8fed, 0x377cb8d7, 0x9b1562b3, 0xb9df53e5, 0xbdf021af, 0x385b3ea8, 0xeaa123e3, 0x39fcd86b, 0x1265c7a9, 0x03c39d0c, 
    0xc2da6ba6, 0x1978b4ab, 0x8f2cf792, 0x89b3b75a, 0xf20bc8ad, 0xe5d59efa, 0x1abe197f, 0x4fabf8db, 0xbee4d395, 0x71c99f48, 0xe1b463c0, 0xae38db5b, 
    0x83c5c7a6, 0x90712e23, 0x6bb55370, 0xd0b7704b, 0x3af22d87, 0xc131ae1c, 0x8ce5a4af, 0x3a7c66e3, 0xe6134a95, 0xcee2ca47, 0x12c21b7d, 0x225da51d, 
    0x56f59421, 0x867e9003, 0x62f6e7bc, 0xe3ad36f1, 0xce72bdfb, 0x74956658, 0xed8b86bb, 0x19c64e3b, 0x09dd0d24, 0x9e3e7200, 0xd6bfbea2, 0xa967077c, 
    0x6dd1eaac, 0x2861458a, 0x276e18f3, 0x59ede7af, 0x1ef0123e, 0xc3e3f091, 0x3d7a36b7, 0x23edd8a7, 0x5e998a05, 0x6289b943, 0x26f7c472, 0x08581abc, 
    0xf9549dcb, 0x385b4f1f, 0x3795e492, 0xfb1d7cea, 0xe5f6785b, 0x842f497c, 0x008d7620, 0xc8b05d12, 0xf551f4e0, 0xd7dc43fe, 0xf0367c8c, 0x3c8ecf45, 
    0x04616d4f, 0x1aacc264, 0x4f850367, 0xcfbac64f, 0x8f5897da, 0xaddbf85a, 0x45163718, 0x312c0686, 0xe49fdb96, 0xfd8fbec2, 0xaf10fe98, 0xee38b486, 
    0xee1a91ee, 0xe94ade7f, 0xc38e2031, 0x5eb11d18, 0x54308cb2, 0xf6c7036d, 0xde1a3395, 0xe16967d2, 0x1af6836f, 0x026f897e, 0xa3aac65b, 0xadf7f5ba, 
    0x9655f86f, 0x2fc6f37f, 0x09bde6fb, 0xa0506355, 0x5c8a0160, 0x699df08a, 0x54d67337, 0x7cac9560, 0x01caf0df, 0xe0f9ae16, 0xbaca3fed, 0x2d9f8f4f, 
    0x00ff8985, 0x1af4bf6d, 0x3f053ce6, 0x471b54fc, 0x55fe4619, 0x8f7a7cd5, 0x7f62997e, 0x06fd6fdb, 0xf6af79be, 0x76937d84, 0xc2c73c8a, 0x7b358873, 
    0xca400f39, 0xfe41e04f, 0x265eef95, 0xa3d16781, 0x90810175, 0x57e4f511, 0xdb7af883, 0x087d4233, 0x1e57ea35, 0x43c78a21, 0xa1602682, 0xf258051f, 
    0xfcf55832, 0xb9d9a36b, 0x541949d4, 0xe398967c, 0x43a5c5af, 0x3b2edcb9, 0xc4f3bce6, 0x7a14b4df, 0xa40da956, 0x6c76ee12, 0xef180f10, 0xc7eb7dfe, 
    0x587c273e, 0x9eb8bfbc, 0x91624fd7, 0x7ff282c5, 0x73b56778, 0xef850f5a, 0x964e20fe, 0x7f29695a, 0xdbe58a34, 0x184e8a9d, 0x16fa55fe, 0x100a0507, 
    0x3e7795f6, 0xbdcc301b, 0x239dfda4, 0xe303ade9, 0x47fc8807, 0xe8595bac, 0xbbde9116, 0x867b873a, 0x7082912a, 0x0752c749, 0x6bddfe6e, 0xc9868fec, 
    0x370b3470, 0x0c6f08be, 0xfd116628, 0x0319d8d0, 0x97f335db, 0x7687afc1, 0x10b20abe, 0xc2e4c1c6, 0x333f57e6, 0x3f0eaa10, 0xc25bfa2a, 0xa5477177, 
    0x20ee5b90, 0x8ce27ae0, 0xc97c5a55, 0x0a9d4753, 0xe4166735, 0xc6c3f867, 0xf20d1a16, 0x99c72dc3, 0x9f12413f, 0xf97a9fbb, 0xedd7e3a3, 0x2c860f09, 
    0x2b0a395a, 0xd182953a, 0x8eed2c1b, 0xe19f00ff, 0x9b338ed7, 0xae784efb, 0x4cfc0f4f, 0x3ba22ed0, 0xd994bb61, 0x82d4a7ca, 0x07307fc8, 0x7c185fe3, 
    0xd4bcbf43, 0x5eac4dfc, 0x3c49136a, 0x16457bde, 0x3f20c2f3, 0xc7b00328, 0x9760cdf9, 0x72d89a35, 0xcf46a9a6, 0xe0d3ce56, 0x7eae840f, 0x97487c21, 
    0x3651be54, 0x56e69ec9, 0x245606c7, 0xa53f99fe, 0x5a69877e, 0x747a696a, 0xc0eda250, 0xfd78bee6, 0x74027c92, 0x5e5b0b2f, 0x2e2e1e4f, 0x18f99a07, 
    0x42fef63c, 0xe99e90be, 0x8ff16054, 0x7571f25a, 0xd8cbe779, 0xf6d170f4, 0x6621f550, 0x356ed220, 0xdcc54b56, 0xfb4d7370, 0x290fd762, 0xcb7cced9, 
    0x27545de0, 0x8f8be28a, 0xb5bf1f99, 0x18c75f7a, 0xc4226d5a, 0xdfcecfa8, 0x9daf09fa, 0xb7ea1d3e, 0xb246fc72, 0x5a999989, 0xc7194f29, 0xaff435dd, 
    0x03f91fc6, 0xe7bb71d8, 0x5abe7d60, 0x94522bf3, 0xa72e452b, 0xaaaa52d3, 0xe1559eab, 0x59d322e9, 0x8675bca6, 0x60978720, 0x7f9ed331, 0x717ce21a, 
    0xf52eb9e3, 0x6ccf82e7, 0x1329b290, 0x321e2496, 0x556c1f7a, 0x0dea1cbf, 0x0c69453a, 0x57c848be, 0x4f8f7a2a, 0xc7afbcd6, 0x3ee95cb3, 0xc2408432, 
    0x801a712b, 0x5639f908, 0xfd9aebe7, 0x52940323, 0xaa58d2c2, 0x99079fba, 0x566bb566, 0x24bb2974, 0x9533fe72, 0xdbabb56c, 0xc2b81549, 0x84ae4ea8, 
    0x9aef1164, 0x6ef6dffa, 0x00ff7df8, 0xd686ef08, 0x055100ff, 0x3410ae6f, 0x7b910f9c, 0x8e733d28, 0x5e733d0f, 0x1be1651b, 0xdef8143f, 0x267ff2ca, 
    0xdd232c68, 0x8e41c178, 0x566d0047, 0x9c3306ea, 0x64afd81e, 0x26f153f8, 0x976b157f, 0xcaa3f6b6, 0x4190e0d1, 0x81c4d306, 0x32f2194e, 0xed712408, 
    0x99c3f58e, 0x4ad9fd62, 0x574e363b, 0x88b8f683, 0x12e891d3, 0x770de34f, 0x884bbc57, 0x32dd74ac, 0xd16a2b34, 0x21218f79, 0x70c752c9, 0x81038c03, 
    0xe0c55eeb, 0x00ff6aad, 0x49d1f04e, 0x6d734df6, 0x7657306c, 0x77bc6024, 0xb1ce8aef, 0x2c16b4b6, 0xc6b11acd, 0x853d4072, 0x21aed159, 0x55506c3f, 
    0x9c312654, 0x958e8c01, 0x29c491f3, 0x2c731f49, 0x484ea512, 0x88f69bf8, 0xafb74ef1, 0x4400fff8, 0x82b6d2fe, 0xe4b8f611, 0x23e20e2b, 0x09906165, 
    0x38b88e3c, 0x22fee715, 0xf8579bd0, 0xc4a46ba5, 0xd1890b37, 0x06b663dc, 0x5e03fa78, 0x6a4dfb83, 0xda78b1f6, 0x311238ce, 0x62674c24, 0x8f0bc8b8, 
    0xf427994e, 0x9e873fad, 0xf8dafe0e, 0x214975d4, 0xb3e22d69, 0x8462448e, 0x382ce6a6, 0xfd85333d, 0x5e85d76b, 0xc8474e4a, 0x6f2fe9e2, 0x8f7d17cb, 
    0xa609bcaa, 0x5ba08526, 0x81da38c4, 0x5be91879, 0x5df29a61, 0x39ec33aa, 0xad454eaa, 0x60bc46b2, 0xbeef8a63, 0x2f65781c, 0x1e4d5c94, 0x971b4ff2, 
    0xa9f3f335, 0xa2b3efcb, 0x6b73244d, 0xd4d9ebe0, 0x7b504638, 0x42f85f1a, 0x6bbabf2f, 0x9cf021df, 0x838d120b, 0x9fd4ee3f, 0xfd418af0, 0xa776fccf, 
    0xfdf3ccdb, 0x7f243fb2, 0xabe1fb67, 0xe0c8d0f8, 0xfa38b01d, 0x7c505f71, 0x874e8564, 0xe56d4b6a, 0x4f3bf7ba, 0xc067f215, 0xf05037bd, 0xb68d8d3f, 
    0x6e516ea9, 0x652a6fa3, 0x248558e5, 0xd457f8f3, 0xa41d00ff, 0x6e35f811, 0x23f2140b, 0x7a544296, 0x5be91f23, 0x175b29e2, 0x85491f18, 0xea0c5fc4, 
    0x574ff948, 0xc64f7dbb, 0xc709616b, 0xce91b8cf, 0xb6b92b46, 0x8abf45f0, 0xf58223ee, 0xcc026213, 0xcec031ec, 0x35f7e001, 0xd51c3ee4, 0x6c88cf4d, 
    0x40e07d35, 0xecd92ed8, 0xc48f0c43, 0xc20ffa8a, 0x0179a03a, 0x03ce4349, 0x7f8e9061, 0xb97ea50f, 0x11ae0a46, 0x9a9f5bd2, 0xd1feabd4, 0x9ecd5eed, 
    0xc6a3f143, 0xcb336911, 0x138169a0, 0x78b8ed7a, 0x8f57f9c0, 0x7f1df193, 0xf0b7f5fa, 0xd2b75807, 0x05b3daf6, 0xc48f9821, 0xb7f88dd7, 0x75d3bb49, 
    0x237dd13b, 0x3cd3764f, 0x00ff29ad, 0x7a3f9996, 0xdd85f0a7, 0xb29df6e7, 0x4ad1c85b, 0x3bde04f2, 0xf935d313, 0xe50e37fe, 0xd4675309, 0x63bf7865, 
    0x1f5b8d88, 0xbe6e1aa1, 0x5e1eb666, 0x3ec01d64, 0x8378acc2, 0x7f7851c5, 0x525777c3, 0xee404c28, 0x0006ccce, 0xf88f6b52, 0xb1c91d59, 0x4abab55f, 
    0x527003d9, 0xe88af233, 0x3bfc27fe, 0x83dff1b9, 0xee2cec2e, 0x6049d612, 0xe1740195, 0x0c5446b7, 0x78f335fe, 0xcd522558, 0x18e6a19f, 0x490eaa98, 
    0xaa137c6a, 0xe778d2eb, 0xa8ce3ac5, 0x159475d9, 0x3c933d8b, 0x5fe38f7f, 0x56cf7e62, 0x34856f9e, 0xfd85cbeb, 0x2bb196fc, 0x05921cb1, 0x00ffb57c, 
    0xf857f30a, 0x69dfad71, 0xd983f5da, 0x32442d9f, 0xcf518af5, 0x9dfedd28, 0x84bfea6b, 0x78a60d77, 0xa5d84f2f, 0x35748480, 0x4ef462ed, 0xecf3632b, 
    0xef5c732b, 0xbc4e4f7d, 0xa08a3494, 0xa2af38f5, 0x25d81bfe, 0x84048da6, 0xcfc8ed0c, 0x265febb0, 0x7fc7af43, 0x1c5b5bab, 0x868c33ab, 0x465fb31d, 
    0xb0399e6b, 0x9a844ff0, 0x6de1f6e6, 0xcbe16ded, 0xebc9d812, 0xa42bf18a, 0xca4d509c, 0x52e9e97e, 0xe5c8d6eb, 0x75c4dd7c, 0xc27fa639, 0xfffd6d43, 
    0xe7d7d400, 0x6bfbbfde, 0x5a6d82f8, 0x1e9ee8e0, 0xc5aa6912, 0xf6799262, 0x58fc8133, 0x670d70da, 0x69c300ff, 0x85fed778, 0x27f03fc8, 0xaeb000ff, 
    0xd8564a95, 0x3893eac3, 0xb1da870f, 0x7c8167f1, 0x2109e235, 0xb0237151, 0x0b01bd7d, 0x5ee99f22, 0xce4cfbbf, 0xa869f862, 0xda376220, 0x7efa0cdc, 
    0xbeccebe1, 0xe63af801, 0x9d2de1cb, 0x4d23c38d, 0xb98d3c6f, 0xc02d1839, 0xabc700ff, 0x196a3fd2, 0x6f0f3f04, 0xd6268f11, 0xd512fd7f, 0xc7b38cdd, 
    0x81d3b1d3, 0x913a5893, 0x9de14397, 0xd054ee26, 0x625fd9ae, 0x5f628d86, 0xf94f9e41, 0xf856aff4, 0xbd37e231, 0x4b1294d4, 0x2de593ec, 0xb51e4780, 
    0xb46adae3, 0xc88cc5c0, 0x381dc0cb, 0xc2d75de9, 0x53d396a6, 0xd55a4a31, 0x262bb7bc, 0x3ec6f13b, 0x30d0af99, 0x256a1c55, 0x56c6c7d2, 0x5f1b5a57, 
    0x9be5341a, 0xd5ed8155, 0x0252f75b, 0x08125c47, 0x55f854cd, 0x08b5c9e1, 0xc668bd24, 0xe323a80c, 0x5c93a7fb, 0xcb758b77, 0xc97811df, 0x2425a1ac, 
    0x2932600c, 0x3fc933ea, 0xe127fa4a, 0xe32d8696, 0x2386b2b2, 0x59aac0e4, 0xf1d773cf, 0xce8fafc9, 0x92322a31, 0x967d568f, 0xd3f68545, 0x7ed19252, 
    0x3278b867, 0x285b5adc, 0xf403a123, 0x175eec15, 0x632deff2, 0x8033908c, 0x577bb357, 0xde96e997, 0x23fe3844, 0xb2abfcc9, 0xb7f9c5f0, 0x304e3096, 
    0x7cc5fccb, 0x33ce3a35, 0x63a49fd5, 0xb1d129a8, 0xf011ed07, 0x7c1d1f6a, 0xadadd535, 0x47a80b61, 0xfad59209, 0x2903e618, 0xf6c113f8, 0x04fee535, 
    0xe185ced3, 0x9e4b2b8b, 0x03dd8e27, 0xbea27d0f, 0xd5238dbf, 0x43258634, 0x1e041993, 0xc457e4f8, 0xedf51a9f, 0x285ec15b, 0x81b622d4, 0xe41b8927, 
    0x18a71d88, 0x71feedc8, 0xb955575f, 0xe64783d3, 0x94134eb0, 0x73f85d4f, 0xbd6bebe1, 0x02f94b4e, 0x09dc0891, 0xd3fec715, 0x565d225e, 0x701f4d4b, 
    0x1def2f30, 0x34db137b, 0x26fe0abf, 0x130f9f44, 0x6d21d45c, 0x9267942c, 0x1564d819, 0x3fc37fcf, 0x882f788d, 0x8e37773c, 0xf68beb75, 0x90a45856, 
    0x258f9588, 0x94f01a7b, 0x36c4562a, 0x0e9d1eba, 0x22d3fb48, 0x3b63ed78, 0x828e814a, 0x414f8b9b, 0xfc885f5c, 0xa88e3d4d, 0x2a03c4f0, 0xf59e9381, 
    0x0900ff99, 0x07f7e795, 0xa0f25ee7, 0xe539aaad, 0x6776778b, 0x3cb27fd0, 0x0e1f6bf2, 0x63ba0675, 0xc878452b, 0x10c6f5a4, 0xb55a00ff, 0xf1cc68fb, 
    0x15d96278, 0x8abcab8a, 0xb63ceed8, 0xa7b13faa, 0x578d93fc, 0xff9bbffe, 0xb74a4100, 0x00ff6afb, 0xff67b9c8, 0xff645d00, 0x4a6dd100, 0x33fdfbbf, 
    0xb97fc2cf, 0xd14e3ed4, 0x8e55232d, 0x4138bf40, 0x5708fccf, 0xc2a27259, 0x0b084de4, 0xdbe77626, 0xbfe8cc15, 0xc1fd53ea, 0x0aa100ff, 0xbe6ff5e9, 
    0xc81fde3f, 0xd210e8d7, 0x27e5a329, 0x0fbf31f1, 0x5ea729fc, 0xb851dbf8, 0xac33339e, 0x20e750d9, 0xd5d76464, 0x82b0181e, 0xa8fc18ce, 0xf600f0c2, 
    0x1bfe9baf, 0xaad9c87f, 0xff51d77f, 0xfa8aa000, 0x00ff4263, 0x57d01f55, 0x49ddf9e3, 0x7dd35c3c, 0x7a78da4f, 0x0d0e169c, 0x3a766c2d, 0xb7e6c878, 
    0x54b6f134, 0x1e56d423, 0x766bf79d, 0x7d62fdc3, 0xe8f07845, 0x859ff87b, 0x07e1d19e, 0x2788d367, 0x3fe52b8e, 0x25ce3eda, 0x851a8df8, 0x61be48e2, 
    0xf3c783eb, 0x1bbcaaaf, 0x071e00ff, 0xbe62fef0, 0x3fa4fd5b, 0x7f5ba4e4, 0x00ff23d7, 0x78fb0aa1, 0x961fbb7f, 0x7900ff56, 0xb0c5cf3c, 0xb7e9f021, 
    0xf773eec7, 0x689f573b, 0x6cfaa770, 0x90e846bb, 0x571cf484, 0xfeb3f8a2, 0x798ddf40, 0xa3fe87ee, 0x7000ff53, 0xe52a00ff, 0x4ffd6cc0, 0xaca51d47, 
    0x9d504c79, 0xb9bae76d, 0x95d6fc91, 0x3870d881, 0x1cf62f35, 0x6fd2f31f, 0x4393eefb, 0x698f00ff, 0xffb3ebbf, 0xafd13a00, 0xff8ff064, 0x000000d9, 
};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(SmudgeBlurEffectNode, "Smudge Blur", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Filter#Video#Blur")