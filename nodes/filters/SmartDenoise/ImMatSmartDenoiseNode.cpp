#include <UI.h>
#include <imgui_json.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include "SmartDenoise_vulkan.h"

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct SmartDenoiseNode final : Node
{
    BP_NODE_WITH_NAME(SmartDenoiseNode, "Smart Denoise", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Filter#Video#Denoise")
    SmartDenoiseNode(BP* blueprint): Node(blueprint) { m_Name = "Smart Denoise"; m_HasCustomLayout = true; m_Skippable = true; }

    ~SmartDenoiseNode()
    {
        if (m_filter) { delete m_filter; m_filter = nullptr; }
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
        if (m_SigmaIn.IsLinked()) m_sigma = context.GetPinValue<float>(m_SigmaIn);
        if (m_KSigmaIn.IsLinked()) m_ksigma = context.GetPinValue<float>(m_KSigmaIn);
        if (m_ThresholdIn.IsLinked()) m_threshold = context.GetPinValue<float>(m_ThresholdIn);
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
                m_filter = new ImGui::SmartDenoise_vulkan(gpu);
            }
            if (!m_filter)
            {
                return {};
            }
            m_device = gpu;
            ImGui::VkMat im_RGB; im_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_filter->filter(mat_in, im_RGB, m_sigma, m_ksigma, m_threshold);
            m_MatOut.SetValue(im_RGB);
        }
        return m_Exit;
    }

    void WasUnlinked(const Pin& receiver, const Pin& provider) override
    {
        if (receiver.m_ID == m_SigmaIn.m_ID)
        {
            m_SigmaIn.SetValue(m_sigma);
        }
        if (receiver.m_ID == m_KSigmaIn.m_ID)
        {
            m_KSigmaIn.SetValue(m_ksigma);
        }
        if (receiver.m_ID == m_ThresholdIn.m_ID)
        {
            m_ThresholdIn.SetValue(m_threshold);
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
        float _sigma = m_sigma;
        float _ksigma = m_ksigma;
        float _threshold = m_threshold;
        static ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Stick;
        ImGui::PushStyleColor(ImGuiCol_Button, 0);
        ImGui::PushItemWidth(200);
        ImGui::BeginDisabled(!m_Enabled || m_SigmaIn.IsLinked());
        ImGui::SliderFloat("Sigma##SmartDenoise", &_sigma, 0.02f, 8.f, "%.2f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_sigma##SmartDenoise")) { _sigma = 1.2f; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_sigma##SmartDenoise", key, ImGui::ImCurveEdit::DIM_X, m_SigmaIn.IsLinked(), "sigma##SmartDenoise@" + std::to_string(m_ID), 0.02f, 8.f, 1.2f, m_SigmaIn.m_ID);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled || m_KSigmaIn.IsLinked());
        ImGui::SliderFloat("KSigma##SmartDenoise", &_ksigma, 0.f, 3.f, "%.2f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_ksigma##SmartDenoise")) { _ksigma = 2.f; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_ksigma##SmartDenoise", key, ImGui::ImCurveEdit::DIM_X, m_KSigmaIn.IsLinked(), "ksigma##SmartDenoise@" + std::to_string(m_ID), 0.f, 3.f, 2.f, m_KSigmaIn.m_ID);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled || m_ThresholdIn.IsLinked());
        ImGui::SliderFloat("Threshold##SmartDenoise", &_threshold, 0.01f, 2.f, "%.2f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_threshold##SmartDenoise")) { _threshold = 0.2f; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_threshold##SmartDenoise", key, ImGui::ImCurveEdit::DIM_X, m_ThresholdIn.IsLinked(), "threshold##SmartDenoise@" + std::to_string(m_ID), 0.01f, 2.f, 0.2f, m_ThresholdIn.m_ID);
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        if (_sigma != m_sigma) { m_sigma = _sigma; changed = true; }
        if (_ksigma != m_ksigma) { m_ksigma = _ksigma; changed = true; }
        if (_threshold != m_threshold) { m_threshold = _threshold; changed = true; }
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
        if (value.contains("sigma"))
        {
            auto& val = value["sigma"];
            if (val.is_number()) 
                m_sigma = val.get<imgui_json::number>();
        }
        if (value.contains("ksigma"))
        {
            auto& val = value["ksigma"];
            if (val.is_number()) 
                m_ksigma = val.get<imgui_json::number>();
        }
        if (value.contains("threshold"))
        {
            auto& val = value["threshold"];
            if (val.is_number()) 
                m_threshold = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["sigma"] = imgui_json::number(m_sigma);
        value["ksigma"] = imgui_json::number(m_ksigma);
        value["threshold"] = imgui_json::number(m_threshold);
    }

    void DrawNodeLogo(ImGuiContext * ctx, ImVec2 size, std::string logo) const override
    {
        // Node::DrawNodeLogo(ctx, size, std::string(u8"\ue3a3"));
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
    FloatPin  m_SigmaIn = { this, "Sigma" };
    FloatPin  m_KSigmaIn= { this, "K Sigma" };
    FloatPin  m_ThresholdIn= { this, "Threshold" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[5] = { &m_Enter, &m_MatIn, &m_SigmaIn, &m_KSigmaIn, &m_ThresholdIn };
    Pin* m_OutputPins[2] = { &m_Exit, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    int m_device            {-1};
    float m_sigma           {1.2};
    float m_ksigma          {2.0};
    float m_threshold       {0.2};
    ImGui::SmartDenoise_vulkan * m_filter   {nullptr};
    mutable ImTextureID  m_logo {0};
    mutable int m_logo_index {0};

    const unsigned int logo_width = 100;
    const unsigned int logo_height = 100;
    const unsigned int logo_cols = 1;
    const unsigned int logo_rows = 1;
    const unsigned int logo_size = 5323;
    const unsigned int logo_data[5324/4] =
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
    0xe6e5e4e3, 0xeae9e8e7, 0xf5f4f3f2, 0xf9f8f7f6, 0x00dafffa, 0x0001030c, 0x11031102, 0xf6003f00, 0xe15e783b, 0xbc024ff2, 0xb88af64b, 0x39e9ac2f, 
    0x2be6c7f7, 0x1a4ff5d1, 0x0f6f3c46, 0x4764ed87, 0x294d032b, 0xa06f0747, 0x6b3d1e20, 0x89697fcb, 0xe8accb7e, 0x5b2c7981, 0x377eadf9, 0xf1f67287, 
    0x3d946e3f, 0xf8cad9d6, 0xe7bd8975, 0xf39677a5, 0xbff30a0c, 0xfffedc0e, 0x6b722400, 0x50b9b7b6, 0xeb016c99, 0x7e27f49a, 0x297a336b, 0x6c269b72, 
    0xb67fd50c, 0x9a3348ab, 0x0d759976, 0x0c95c0c4, 0xafd784be, 0xd1431b78, 0x1c618bf5, 0xadb4759a, 0xc66ab4dc, 0x65e069bb, 0x3c567c55, 0x4f655ebc, 
    0xd9caaf0c, 0xd00f17e4, 0x89d6c48a, 0x64384ac2, 0x7d851f39, 0x816fe30b, 0xd269dd50, 0x5e79f85e, 0xb190a82b, 0x59a401b4, 0x3206ce71, 0xa7037643, 
    0xe76b9d4e, 0xd635162f, 0xa64fa889, 0x58d216ea, 0x2471c0de, 0x71630817, 0xb6769091, 0x8e20080e, 0x3c510439, 0xbb3b4c2d, 0x8f0d0f5a, 0xfba1e3c3, 
    0xa76eafb7, 0xe05678b5, 0xac257a6a, 0x83fd7dd2, 0xcb15a535, 0x5c9197e9, 0x9878d842, 0xe24a3f30, 0xf67727fc, 0xdcdb0d2f, 0x993d6233, 0x5a019c24, 
    0xf266ea52, 0x68b0d12d, 0x3965c8c8, 0x8ad6e415, 0x4a477252, 0x746675a5, 0x855fb4df, 0x18dec4d3, 0x5822bed2, 0xad22b20d, 0x3819a1a6, 0x195638e3, 
    0x7e3b86eb, 0xf91a7a84, 0x2c7ec4cb, 0x8767f0b1, 0x44adb56e, 0x0c80b6b9, 0x9db94b2c, 0xfaa8c289, 0xf5780e9c, 0x05fca4af, 0xf04827f1, 0xc5ad913e, 
    0x788bdbe2, 0x6c712d7c, 0x4adee8e2, 0x90cac791, 0xe5422e17, 0x1919e8be, 0x587e4525, 0xbcf1417c, 0x5df13dde, 0xb10ca0ee, 0x8d33abe9, 0xcc43d23e, 
    0x013b6e10, 0x8db11bc7, 0xc5f6b9c7, 0x1976197b, 0x6ed9f7e2, 0x537cc57c, 0xdaa879cb, 0x3ffa78ef, 0x5b1d3af2, 0x81f887f6, 0x36cfa879, 0xdd2be29d, 
    0x31dbcd12, 0xb64dd9d8, 0xc9e5c738, 0xb0af27f5, 0xe13f15c0, 0xff4d7c7f, 0x59e3a100, 0x93c000ff, 0x8397ad5c, 0x44ae457c, 0x87771ad7, 0x004a7df5, 
    0x56d60cc5, 0x87814a52, 0x19412555, 0x2aee71e4, 0x4f2bfcc7, 0x9fd07f19, 0x15fc2fe2, 0xc400ffcd, 0x342ad7d7, 0x8fcaad62, 0xedba789c, 0x5d9fc9dd, 
    0x2ed41dfc, 0x491f0f35, 0x96217379, 0xbb9c68e6, 0xbae2a49e, 0x8580da0f, 0xf73c74d7, 0x63c5fc2d, 0x3f821b7c, 0x68d34bf8, 0xbf9feec7, 0xb53f74b5, 
    0xb5a6b25d, 0xbb8ef3e1, 0x7eadfbfb, 0x5cac787f, 0x7e68fd6d, 0xececaf96, 0x94410bce, 0x154f8609, 0x49526ad3, 0xa46c766f, 0x01448e46, 0x8e605899, 
    0x7345fd33, 0x17b7147e, 0x649fe6fa, 0x11373738, 0x96e909c0, 0x7aad3f60, 0x32ddc51f, 0x57881f4b, 0xc52a20f7, 0x07288828, 0x47c9f23f, 0x666bcafe, 
    0x46b832d7, 0x9f887b4d, 0xda8eb543, 0xcfad3421, 0xc2977ba5, 0x1fef228d, 0xcd7e996a, 0x7500ff6e, 0xfa30ef5b, 0xf024af76, 0x64be9eae, 0xc40ddc33, 
    0xf4a0c810, 0x5eeeb53e, 0xdbd4d61e, 0x1fee5ca1, 0x86b7d7ec, 0xcf67d3b5, 0x9cdc5c66, 0x468f3d91, 0x08542896, 0x53af4fc0, 0xede779c5, 0x4e13f00f, 
    0xf9e1cdf8, 0x2c614b35, 0x2cd32a3e, 0xdb13fba6, 0xd8daaf32, 0x48703423, 0xf8dd7d58, 0x9b1be3c1, 0xbee9add6, 0xab9cf226, 0xddaf2731, 0x185fcbad, 
    0xcc7c94fd, 0x1040b7c4, 0x84a8af74, 0x2c8fd6e9, 0x54063ef6, 0x2aaae1eb, 0xc867f494, 0xebc224ba, 0x909138e1, 0xb26436a9, 0x6f07c1b0, 0x13d7fb20, 
    0xe24f17fb, 0x13fe8a9f, 0xd2e1d3bf, 0xd1e1aeae, 0x5217f2e5, 0xcf658871, 0xe176583e, 0x0632469d, 0xade70a70, 0xf0757d7d, 0x2ef14c93, 0x41f274a3, 
    0xdfbd4ab7, 0x731ec425, 0x4b727706, 0x9200c9f3, 0x8aed117a, 0xe101fced, 0x1efc0caf, 0xe18ffef0, 0xb4cd0a9d, 0x18d826dd, 0x92c4adad, 0x164bb14d, 
    0x31b37323, 0x7a528fe5, 0xb261cfd7, 0x385235c5, 0x47a3af8d, 0xd9b362d6, 0x199ed341, 0x3dddd95d, 0x5f941fba, 0xef8f17b6, 0x5c1f1ff5, 0x92a11578, 
    0xd2c3cbca, 0x458abbed, 0x1deb96c1, 0xe154d57d, 0x79cc7d4f, 0xf83faf18, 0xefbef027, 0x228c77e2, 0x74198db4, 0x794b5ceb, 0xb9080770, 0x4cfb47e1, 
    0x9ed00746, 0x59fbb1d5, 0x773c9df8, 0x447c50fb, 0x620cb3bf, 0x387b5997, 0xded2aef7, 0x8c335840, 0xbd7e31f7, 0xdfd757ba, 0x88c2e7b3, 0x1e781a7e, 
    0x5354d6de, 0xfbfb4da9, 0xea5212c9, 0x004e9fd2, 0x070e0c00, 0xd36bfd6e, 0x8f068692, 0x99873e25, 0xabccd419, 0x7fbed53a, 0x1cbe76e4, 0xa09b4ef0, 
    0x16b6b668, 0x10ac2db1, 0x318ad5ae, 0x93c43f80, 0x5a734f92, 0x9f56d85f, 0x896d35df, 0xf99a9326, 0x76672ee7, 0x4a110a7b, 0x7c3f1fc9, 0xf161811b, 
    0x79ef1b5e, 0xb6ca8f6d, 0xb7416bbf, 0x74e3f051, 0xe87f9cf9, 0x2efcce35, 0x982bfe9f, 0x9f14ef00, 0xa3ae09fa, 0x758bb7f6, 0xda3387ef, 0xd900ff47, 
    0xfab3c36b, 0x87ae7fd4, 0x08da49d7, 0x0a00fff2, 0xf8a26bea, 0xe4d44597, 0xaf6c7856, 0x5791b9a0, 0x589154a9, 0xfa0af881, 0x87c5e353, 0x2d2f7e99, 
    0x246d2996, 0xd0b9fc78, 0x60ec0486, 0x5f431f79, 0x4bb66837, 0x4c4fa53c, 0x9b54af74, 0x1e7e41c4, 0x561f9e68, 0x0b6c188e, 0x36fc8cb4, 0x39ac2cf3, 
    0x5b8bc4f4, 0x917a2efb, 0xa3cae8b6, 0xb6a8da74, 0xe0d756d8, 0xcd3320d3, 0xf212c33a, 0xaf01ce59, 0xed17f131, 0x20bed62c, 0x67d3b7b7, 0x66924290, 
    0x28236e57, 0x00fffb3a, 0x89aff15a, 0x1f35175f, 0x7634dd11, 0x81b0b599, 0xab67ac94, 0x72fde4f5, 0x06feaa38, 0xf17f7df8, 0xd1b35767, 0x2dfbb6b4, 
    0xb826ccdb, 0x82482ab9, 0x843ee31f, 0xe738c0f4, 0x13fd1ad8, 0x86a78205, 0x6a55a983, 0x323f3e7c, 0xa8eca775, 0x17fadc6d, 0xbe147fc2, 0xf14c7c28, 
    0x832f1a14, 0x25d9d29a, 0xeb8b6b65, 0x6bc17c98, 0x5f4fce11, 0x1e38b099, 0x6b3270bd, 0x68013fed, 0x06fae8b2, 0x70b1169f, 0xc1c2fab7, 0x2adcdc1a, 
    0x2894576c, 0x027bc1dc, 0x5eed38d9, 0xe1b3f063, 0x5bf08fbe, 0x394b6b4d, 0xaadcdd4b, 0x06769295, 0x3f40f946, 0xaff61ac8, 0xfa255b07, 0x2c1bae09, 
    0x312a10c4, 0x893a6955, 0x9d475653, 0x4189381a, 0x99434fca, 0x9dc727f1, 0x945ac313, 0x9f9a26fa, 0x8b217edb, 0xfb8836e4, 0x04c88923, 0x0041c82b, 
    0x9f00f4e0, 0xbfcb575c, 0x2ded77b4, 0x7916f8ac, 0xadd452df, 0xddadf167, 0xa26147a3, 0x1ffd8769, 0x0552464b, 0x5d72eccc, 0xf050f9f9, 0x5600384f, 
    0xbf4bed9f, 0x4bed9fe1, 0x778bebe0, 0x49bcd355, 0x29329317, 0xaa8c1303, 0x5f81feb7, 0xbbf0fa9f, 0xe6fefa92, 0x356e59fe, 0x69878b1b, 0x488b9de6, 
    0x58dc49ee, 0xadcf499e, 0xc6516763, 0x79bae849, 0x0f47d98f, 0xbe5d1b17, 0xfeb347be, 0xbf0e3fc7, 0xd3f124e1, 0xbe09176b, 0x63404ad7, 0x66ba33de, 
    0x06be4fe1, 0xd74aece3, 0x985b72de, 0x00bc0a23, 0x66bfc72b, 0xe0ed872f, 0x6bfa878f, 0xd9db1ec9, 0x04e7f3a3, 0x707eef60, 0xffc2c07d, 0xd76bc000, 
    0x533435ae, 0x6b2a55b6, 0xf654c5c7, 0xf5d82db5, 0xa652e930, 0x23037297, 0x79d28c03, 0xe555ebad, 0x21b980bf, 0xf0ed3785, 0x7e7800ff, 0xd865c775, 
    0xce67e6ea, 0x20b50a3f, 0x2dac12df, 0x45de5fbe, 0x2684d72f, 0xb5f62bbd, 0xf0bc76b5, 0x8dcc31de, 0x01aff0d7, 0x757f1df8, 0x257dc63f, 0xe48bbc1c, 
    0x90dc125c, 0xe86b763f, 0x8cadda7f, 0xc816de97, 0x09c64df9, 0x97571cf4, 0x89a19c56, 0xe9b35b8a, 0x63555429, 0x1efae777, 0xad6be161, 0x1717c32f, 
    0x2e8aa75a, 0x9834b37f, 0xbf142f21, 0x6090e779, 0x96005485, 0x398023e3, 0x3ee635c7, 0xfe84f137, 0xe075f130, 0x4f8fb1b4, 0x7409beb5, 0x7d256e13, 
    0x2393b1a8, 0x9c72c329, 0x9e57d171, 0xb7c7572b, 0x106aaeab, 0x6d26c9da, 0xc66297e0, 0x1903534e, 0xaf757a3b, 0xf3b4f135, 0xe4135fe9, 0x9b0eaf48, 
    0x4955226e, 0x5335021f, 0xe3014e1e, 0x93f8b305, 0xb260a75f, 0x711a388a, 0xbbd555c5, 0x19f0997f, 0x135f6b8e, 0xed205456, 0xc543ce14, 0x413ceda5, 
    0x2cbca67d, 0x8531c7f3, 0xebbc1f12, 0x7e1d939f, 0xeff6f263, 0xb33fa05f, 0xbead80e7, 0x8e377c06, 0xe30253fb, 0x7edcdac5, 0xa41ddb6a, 0xb9001402, 
    0xc68bedc6, 0x712c4f4e, 0xf86fbed6, 0x0ddfe07b, 0x9ce28bf8, 0x86d5221e, 0xf80c00ff, 0xb10ed166, 0x5a10cea8, 0x3680dcdc, 0x0ee5e782, 0xc0dc3949, 
    0xaff7cbe7, 0xf149f846, 0xa9f85657, 0xc4431de2, 0x0b92905a, 0xb4d9893b, 0x30b130fd, 0xb5835bd8, 0x07c04162, 0x06100b08, 0x23afc9e6, 0xaefbc533, 
    0x8cce664b, 0x62ab02a7, 0xbf6f4a23, 0xa22d77e0, 0x34e2f7f8, 0x74f1c0fa, 0x239a5efa, 0xda6061b3, 0xbeedbc6d, 0xdb50f950, 0xbb71e1dc, 0x7b4546be, 
    0x4fbdc22f, 0x163ed35b, 0x98db906b, 0x5d74d627, 0x33b6a33d, 0xa9343107, 0x032e6c13, 0x19e56e67, 0x868e8300, 0x6ea9fd99, 0x5b9c2934, 0xe105e7c7, 
    0x2ae81980, 0xfc1a853f, 0xb48df138, 0xb5251e8d, 0x7558d876, 0xf84f87f9, 0xaf307815, 0x3e8a5419, 0xd20daeca, 0x962e51a1, 0x868fdeb7, 0xda6b323e, 
    0xc600ffed, 0xaecb0c2f, 0x48bafd5c, 0x64ae2def, 0xe4336fba, 0x46323890, 0x2c9fd885, 0x2ee58a71, 0x23fe223c, 0xf064a0fd, 0xd547c0f1, 0x1fc769a5, 
    0x7167072a, 0xc741f7e9, 0xd4fed315, 0xf8b8dd3a, 0x9fbd058b, 0x4058b296, 0x443c12d7, 0x018f5965, 0xa080d0b1, 0xf48ad8f3, 0x9e8600ff, 0xf6f78605, 
    0xb9aef196, 0x82e02124, 0x8a812638, 0x46dc7765, 0xa7a7ccae, 0x67c76118, 0xb91552af, 0x7cec3928, 0x7e8c2eae, 0x7417c9b3, 0xbde843bf, 0xb24c1739, 
    0x743f158a, 0x8d175754, 0x86207077, 0xc2ef3c23, 0x4c938ca2, 0x4a669fbf, 0x935e019c, 0x71c17bf0, 0x00ff5add, 0x26405d6a, 0xd9fd669a, 0x7db13391, 
    0x13784546, 0x68afa8a8, 0x569b35f7, 0x0b9f0667, 0x13d5dbb5, 0x6f096934, 0x53e466e0, 0xeb51e1bf, 0xfecff49f, 0xeaf435fa, 0x812a1068, 0xbf547eb0, 
    0xcf7f50d8, 0xf551f931, 0x79644fac, 0x739fdabe, 0x53f66bf2, 0xf8d2febd, 0xc0ee2487, 0x2774b405, 0xe48fe0dc, 0xdabfe86b, 0x784b089e, 0x429cce5f, 
    0x0419c705, 0xed713cef, 0x7f9aaff5, 0x552bdd67, 0x20f15ff8, 0x9bf5ba68, 0xe6c8f7c0, 0xb6ca4009, 0x0f92d8d8, 0x475f8171, 0xcb76d77e, 0x356d862f, 
    0xb3d13e19, 0x835b8e4e, 0x8d0475f6, 0x64cb96b1, 0x647b1bf4, 0xa9b556fa, 0x16c2b77f, 0x880da33e, 0xf66400ff, 0x30f9ccaf, 0xf120deb4, 0xcd76b6dc, 
    0x249ba7e5, 0xe5fdd0c1, 0x7d1c98f9, 0xeff44a07, 0xb22cfc0b, 0x41aaa1f8, 0x24a9eba6, 0x9bb7b128, 0xcbaa0275, 0xb12c2922, 0xb98183e4, 0x1afe7483, 
    0x5e046ff1, 0xc39678ba, 0x8aa49559, 0xf2da45ca, 0x9987f9a0, 0xf461201b, 0x57d02738, 0x5b53f8bf, 0xda569595, 0x8c57d7ed, 0x498b1b33, 0xc7193036, 
    0x20e11f5c, 0xae5fef74, 0x60aba6d3, 0x78bf28dd, 0x9455cefc, 0x5bd21e71, 0x00ffc733, 0x8b896f68, 0x78b3ebac, 0xda114563, 0x01b31668, 0x5898162c, 
    0x0b38549e, 0xe754dec7, 0xb227b1d8, 0x673fea9a, 0x585b11af, 0xee9e1e41, 0xbee2a614, 0xad7f72f9, 0x74882778, 0x753b7c09, 0x34c49c25, 0x2e7712b1, 
    0x189ecb70, 0x4fadb167, 0xfae43504, 0xf3368cd5, 0x4a92acbd, 0xf8d389b1, 0xfc1a3c09, 0xe6861df3, 0xec6d2aa5, 0x8c53667d, 0x6a4c7c78, 0x403ff7da, 
    0x9e26e17f, 0x110cd5da, 0xc36fc42d, 0x03c7d802, 0x3f75b5da, 0x0fe10989, 0xec95da87, 0x45aa6dda, 0x13d7ac1d, 0x291313cb, 0xc6634645, 0x81334e06, 
    0x1dbee49a, 0x2a10f624, 0xe00bdcde, 0x618cf908, 0xd3d50e7b, 0x9efd57fc, 0xfe8f3fb5, 0x45d38605, 0xa7d021d7, 0x14cb8c89, 0x834b31b1, 0xdd88b2e5, 
    0x9654e594, 0xecb8e1e4, 0x45c2c06b, 0x73cbd973, 0x4c3c33ef, 0x97cb4521, 0xfbb7f353, 0x1ac6ef4e, 0x5eabbb96, 0xcc57f71d, 0x0bcf39d2, 0x9e5138bb, 
    0x600f60c0, 0x82efee2b, 0x077c2576, 0x014e6aa3, 0xadd3bcb9, 0xb13b595a, 0x3f795558, 0xab8d7c85, 0x0ff12f7c, 0xea10cfc1, 0x86f11dfe, 0xa8932695, 
    0xdee28d84, 0x473c4743, 0xc7892476, 0x41a6a70e, 0x080e82e0, 0xf0abbe22, 0xa37dad16, 0xe4f03d7c, 0x6be2f656, 0x20e1d365, 0xfe01e5f7, 0x17e3ebb5, 
    0x83cf5dd9, 0xcfe1b6c2, 0xeccc5fd7, 0xd4f257f5, 0xd657e0e4, 0x4fb40fdf, 0x4b4bf8b0, 0x749b0018, 0x7c00ff63, 0xe76ef88a, 0xd76ad1c6, 0x12b4d930, 
    0xa8c82bc3, 0x60f55c7e, 0x977dad3f, 0x06e357f1, 0xc03ff081, 0xc4dada5f, 0xa0315cbe, 0x31dada86, 0xc7246e99, 0xdd6b1ccb, 0x02803e8e, 0x5e130049, 
    0x4a452136, 0x5165760b, 0x63b99fd4, 0x18180dbf, 0xafa21bcd, 0xf6570dcd, 0xaac3f8f2, 0x695d13ea, 0x8706fad6, 0x364cec62, 0x73c9d952, 0x379e6924, 
    0xe62ee649, 0x1e0052c7, 0x1bfe53d5, 0xfc6fe387, 0x777800ff, 0x2f0500ff, 0xabc700ff, 0x64a792ab, 0x9909f563, 0x25883f1a, 0x4fc34ff1, 0xba2a3e83, 
    0x71a32ed9, 0xcd0d7573, 0xfd2700c6, 0xf576e01e, 0x9ff48a31, 0x6d5677db, 0x54c0e123, 0x5e74c90c, 0xdcb6b6dd, 0x4f5e826e, 0x36aff6a8, 0x6e8327f0, 
    0xa3d9af74, 0x5c7296c3, 0x665af724, 0x235921b1, 0x53d6b1e9, 0xc5fcfdc7, 0xb9ed277a, 0x1be0493c, 0x89dba658, 0x71d39d62, 0x60627d1f, 0x7d00ff31, 
    0xd9c5ea1a, 0xdb5994e6, 0x7d599380, 0x73edc958, 0x9f2bbbe0, 0x49d2c327, 0xb5a47fe7, 0xfd452ac2, 0x27b1c2c8, 0x3d463bf1, 0x047ed8ab, 0xebdf8478, 
    0x92cca711, 0x1969e598, 0x00a720db, 0x6de77b72, 0x149f9e78, 0x2ad12a97, 0xd58e9096, 0x22091fe8, 0x9c4def69, 0x72691397, 0xb9a2dd98, 0xc8896580, 
    0xe94819f9, 0x05eac78d, 0xa882877e, 0x962e51e3, 0xbab22a3e, 0xa2f1bfd0, 0x5ebd1bc6, 0xa82b5bda, 0x572bda2f, 0x2020894b, 0xac7c3e6d, 0x5550af80, 
    0x83fb1419, 0xf815be56, 0x08b47946, 0x061459b5, 0x58b7519b, 0x220be738, 0x3faf8fe4, 0xabbe7995, 0xd3f85da9, 0xd26eefc6, 0xc8bf8bc6, 0xbeda2833, 
    0x276e1c5a, 0xeb73c7b9, 0x3bfc525f, 0x73a146d2, 0xc65adba6, 0x4a69a2cb, 0x81ebcc0a, 0x02dc3671, 0x9ea0aabe, 0x4ac7937b, 0x13e3acf8, 0x131d3569, 
    0x3864dc67, 0x792e6d4f, 0x576fdb2b, 0x0df648fe, 0x51a40509, 0x8304b882, 0x6ff60afa, 0x5a3eeb00, 0x0c7c205b, 0xaf881fa7, 0xc6b75825, 0x9faef636, 
    0xb791dac2, 0x05272596, 0x2bea4759, 0xe555a9e2, 0xe867e692, 0x52058559, 0xfedfb193, 0xed089fd9, 0xce7e30be, 0xf0f6fc1a, 0xc3937823, 0xba6ab2f6, 
    0xde5fc855, 0xa4bbc62b, 0x11491f88, 0x3bd3914a, 0x7845e509, 0x93ed8777, 0x918ef04c, 0x16f9bc6a, 0xf78f4571, 0x5fe90f50, 0xfe22f866, 0x3049571d, 
    0x9154164c, 0x21373a0a, 0x2b821c81, 0x76e287f3, 0x86efe2b7, 0x26f1357e, 0x5e326f93, 0xdf6700ff, 0x5c70144b, 0x0c6380c7, 0x8231b27c, 0x7da45d01, 
    0xa7abeb6b, 0x1de5742a, 0xf3a8cc8f, 0x3e835342, 0xf007f892, 0x3ec6cbee, 0xbc549f28, 0x249ba6c2, 0x3e1e4772, 0x4f90e4fb, 0x5d5edbb0, 0xca7860fb, 
    0x507c1c2f, 0x9320214b, 0x30e3d04c, 0x66f0a4c4, 0x7e18eb3f, 0x0100ff8b, 0xfcd4b53e, 0xa7bdf829, 0x41f83efc, 0x117fc40f, 0xae2fd36d, 0x3d8e67ad, 
    0x3c37391a, 0xaab2abd3, 0xb473ea28, 0x1b0ae31c, 0x94aff724, 0xfef7c45f, 0xfe9ed42c, 0xf2ee62f8, 0x5fcb9756, 0xf3354972, 0xbe52e1d4, 0x444b5226, 
    0x39d47aec, 0x76aaf920, 0xda2bb7d0, 0x1b45e2ab, 0xcd3785bd, 0xe9effeb1, 0xbc890f5c, 0xac677b51, 0x8fa90d4d, 0x9e974fcb, 0x5f59ef79, 0xbeea98f0, 
    0xf4557eb1, 0xaa6d842a, 0x958b2539, 0x97f599dd, 0x1e6babec, 0xeb2dfbb5, 0xe78b17f7, 0x52b606cd, 0xaeca6336, 0xfd077edb, 0x629ddff6, 0x44c100ff, 
    0xfcd175e7, 0x2362100f, 0xeb919c67, 0xffada0f2, 0xfe03d900, 0xffc4474d, 0x3f1ef600, 0x73b50dfa, 0x5f51f09f, 0x177807f9, 0x00ffe9fd, 0xeaac14f4, 
    0xd2c800ff, 0xcee8a78f, 0x8bfc374c, 0x00ff7a6b, 0x87e7f291, 0x2159e26c, 0x78380c94, 0x34fdcfb9, 0x3e5de11f, 0x13f672b1, 0x11017fc9, 0x1808eedc, 
    0x156d600c, 0xee7bf8cf, 0x3ffcfec7, 0xdf5630fa, 0xe300ff88, 0xeafa4fd2, 0x7e0dfabf, 0xa1231d83, 0x99f872f2, 0xc1b3f0b7, 0xa3784a9a, 0x84ed9555, 
    0x0ce193ce, 0x40d036bf, 0x7d9d1f38, 0xb044a429, 0x400104aa, 0x83570018, 0x00ff317c, 0xff578f91, 0xfd8faf00, 0xf4de6b95, 0x8c5f8bde, 0xc693f366, 
    0x3fb3374d, 0xc1a9c870, 0xb7b4a960, 0x0ca65b47, 0x65ad35e7, 0x40eaac1a, 0x75fec8c1, 0xc5d6a593, 0xfae5faa7, 0x438fe08a, 0xcfecc4d5, 0x3b67f873, 
    0xe318c0ae, 0xf663f98a, 0xdb86b1af, 0x28bc8ef6, 0x61f9f19f, 0xf2c03c6d, 0x9f29850b, 0xbe16e5fb, 0xd56bf8a3, 0x93f90a3f, 0x00ffb6f6, 0xf61f8d93, 
    0x00ffb70b, 0xe2be9ed0, 0xfc88ee3f, 0xe3fba7ae, 0xdac68b3c, 0x2bf6bf65, 0x43718f39, 0x3e900785, 0x8068c495, 0xf63a685b, 0xcb1ba0a8, 0x9b8d4878, 
    0x951ea390, 0xff363ee8, 0xbf0c9000, 0x699f57ef, 0x7824f27f, 0x1fb9fe97, 0x2e8f6be4, 0xa69e1ad6, 0x8f95a463, 0x86cff03e, 0xa649bcad, 0x67c9eb9b, 
    0x06230f37, 0xfa03e028, 0xfcb7d67b, 0xfe33cd2b, 0x00ff5c7a, 0x14fec1df, 0x00ff3a7c, 0xeb7f6d91, 0xe94a7fab, 0xe1b9d6eb, 0x00d9ff1f, 
};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(SmartDenoiseNode, "Smart Denoise", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Filter#Video#Denoise")
