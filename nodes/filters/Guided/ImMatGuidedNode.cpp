#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <UI.h>
#include <ImVulkanShader.h>
#include "Guided_vulkan.h"

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct GuidedNode final : Node
{
    BP_NODE_WITH_NAME(GuidedNode, "Guided Filter", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Filter#Video#Matting")
    GuidedNode(BP* blueprint): Node(blueprint) { m_Name = "Guided Filter"; m_HasCustomLayout = true; m_Skippable = true; }
    ~GuidedNode()
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
        if (m_EPSIn.IsLinked()) m_eps = context.GetPinValue<float>(m_EPSIn);
        if (m_RangeIn.IsLinked()) m_range = context.GetPinValue<float>(m_RangeIn);
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
                m_filter = new ImGui::Guided_vulkan(gpu);
            }
            if (!m_filter)
            {
                return {};
            }
            m_device = gpu;
            ImGui::VkMat im_RGB; im_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_filter->filter(mat_in, im_RGB, m_range, m_eps);
            m_MatOut.SetValue(im_RGB);
        }
        return m_Exit;
    }

    bool DrawSettingLayout(ImGuiContext * ctx) override
    {
        // Draw Setting
        auto changed = Node::DrawSettingLayout(ctx);
        ImGui::Separator();
        changed |= Node::DrawDataTypeSetting("Mat Type:", m_mat_data_type);
        return changed;
    }

    void WasUnlinked(const Pin& receiver, const Pin& provider) override
    {
        if (receiver.m_ID == m_EPSIn.m_ID) m_EPSIn.SetValue(m_eps);
        if (receiver.m_ID == m_RangeIn.m_ID) m_RangeIn.SetValue(m_range);
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
        float _eps = m_eps;
        int _range = m_range;
        static ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Stick;
        ImGui::PushStyleColor(ImGuiCol_Button, 0);
        ImGui::PushItemWidth(200);
        ImGui::BeginDisabled(!m_Enabled || m_EPSIn.IsLinked());
        ImGui::SliderFloat("EPS##GuidedFilter", &_eps, 0.000001, 0.001f, "%.6f", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_eps##GuidedFilter")) { _eps = 0.0001; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_eps##GuidedFilter", key, ImGui::ImCurveEdit::DIM_X, m_EPSIn.IsLinked(), "eps##GuidedFilter@" + std::to_string(m_ID), 0.000001f, 0.001f, 0.0001f, m_EPSIn.m_ID);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled || m_RangeIn.IsLinked());
        ImGui::SliderInt("Range##GuidedFilter", &_range, 0, 30, "%.d", flags);
        ImGui::SameLine(setting_offset);  if (ImGui::Button(ICON_RESET "##reset_range##GuidedFilter")) { _range = 4; changed = true; }
        ImGui::ShowTooltipOnHover("Reset");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_Enabled);
        if (key) ImGui::ImCurveCheckEditKeyWithIDByDim("##add_curve_range##GuidedFilter", key, ImGui::ImCurveEdit::DIM_X, m_RangeIn.IsLinked(), "range##GuidedFilter@" + std::to_string(m_ID), 0.f, 30.f, 4.f, m_RangeIn.m_ID);
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::PopStyleColor();
        if (_eps != m_eps) { m_eps = _eps; changed = true; }
        if (_range != m_range) { m_range = _range; changed = true; }
        
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
        if (value.contains("eps"))
        {
            auto& val = value["eps"];
            if (val.is_number()) 
                m_eps = val.get<imgui_json::number>();
        }
        if (value.contains("range"))
        {
            auto& val = value["range"];
            if (val.is_number()) 
                m_range = val.get<imgui_json::number>();
        }
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
        value["eps"] = imgui_json::number(m_eps);
        value["range"] = imgui_json::number(m_range);
    }

    void DrawNodeLogo(ImGuiContext * ctx, ImVec2 size, std::string logo) const override
    {
        // Node::DrawNodeLogo(ctx, size, std::string(u8"\uf2ab"));
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
    FloatPin  m_EPSIn   = { this, "EPS" };
    FloatPin  m_RangeIn = { this, "Range" };
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[4] = { &m_Enter, &m_MatIn, &m_EPSIn, &m_RangeIn };
    Pin* m_OutputPins[2] = { &m_Exit, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    int m_device            {-1};
    float   m_eps           {1e-4};
    int     m_range         {4};
    ImGui::Guided_vulkan * m_filter   {nullptr};
    mutable ImTextureID  m_logo {0};
    mutable int m_logo_index {0};

    const unsigned int logo_width = 100;
    const unsigned int logo_height = 100;
    const unsigned int logo_cols = 1;
    const unsigned int logo_rows = 1;
    const unsigned int logo_size = 5864;
    const unsigned int logo_data[5864/4] =
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
    0xe6e5e4e3, 0xeae9e8e7, 0xf5f4f3f2, 0xf9f8f7f6, 0x00dafffa, 0x0001030c, 0x11031102, 0xf6003f00, 0x2f595b5b, 0x55e2ce24, 0x87d7e47a, 0x46b7d27e, 
    0x2740774d, 0xea633ca9, 0xaff5d12b, 0x2cfc271b, 0x590c3f3b, 0xdbb1aec8, 0x6eb4f3bc, 0x01c60d09, 0x6b5e4fc7, 0x696a3fcb, 0x78aec7be, 0x96042070, 
    0xf815f523, 0x57c919de, 0xa174fb89, 0x6bceb668, 0xb47f8cc3, 0xee0a3c39, 0x0018a6ed, 0x9d573b1e, 0x6deb7af8, 0x249912d3, 0x76a50775, 0xd80a77f6, 
    0xc735b22f, 0x7a05dd2c, 0x9935bf13, 0x4db914bd, 0x6c013e83, 0xc555e9e3, 0xf022c9f2, 0x746970dd, 0x9b7bbbcb, 0xd041f98c, 0xef7628e0, 0xf65aa7df, 
    0xfa68012f, 0x2266b92e, 0x43ca267d, 0x1592b99c, 0x4bdb3566, 0xfcca2a03, 0x5eac3cd6, 0xbf320c67, 0xdc8e6723, 0x7f741f38, 0x5e0fabfa, 0x9059612c, 
    0x53f978ed, 0x7dcd40e8, 0x806fe311, 0x747ad7b6, 0x5617de97, 0x5985beb6, 0xe4f94e9b, 0xf919c049, 0x60b7b27a, 0xc0a02300, 0xf8375feb, 0x16bd5bba, 
    0xd44de3f2, 0xefd3652d, 0xb56525e0, 0x2e9242b9, 0x006b0747, 0x90204770, 0x54340832, 0x3bc354c2, 0xc3e34edb, 0xf8d8f063, 0xecf5663f, 0xc2bb3df7, 
    0xd05603b7, 0x1f26ae2c, 0x6cc0f2bd, 0x27bc6a1f, 0x8abb4e97, 0x4bf6d9e2, 0x7b42870b, 0x8b2b7210, 0xd3d09df0, 0x76672bfc, 0xa07c88cd, 0x01e058ce, 
    0x4d694dea, 0x77d89ba9, 0x60a0c144, 0x410e5d19, 0x6bf25a1f, 0x233973c5, 0xb17652ad, 0x1ed27ed3, 0x5ec58f1b, 0x277ed115, 0x48e5cbe9, 0xac1a96ca, 
    0x3827e344, 0x86eb1946, 0x1eb1e3c8, 0x899ff395, 0xe0615b7c, 0xebdd0d7f, 0x6d989fba, 0x70acc260, 0x0f595aae, 0x93e951dd, 0x495ff1dc, 0x95f83cfc, 
    0x4c03f8a3, 0x84f1a1d7, 0xdbe047f0, 0xe80559bb, 0x4c2395bb, 0xb90038ed, 0x74dfdc55, 0x15494602, 0xbfe35bf9, 0xafe3cd19, 0x79eade14, 0x99042b96, 
    0x90b34ed7, 0xfbf6ece7, 0x9ce941be, 0xcdb92763, 0x1976197b, 0xcb9ed5e2, 0x5e3ef27f, 0xaee529be, 0xdebb5254, 0x8efc8f3e, 0xa1fd548f, 0x6ade223e, 
    0x78a7cf13, 0x6cd0519b, 0x61c972e4, 0xe2185ca7, 0xea537f8c, 0xffab5a4f, 0xe2ef0b00, 0xfa0ffd9f, 0x06fe00ff, 0x23d7f837, 0x102fe16d, 0x8b9bf1ea, 
    0x3aeb0e1f, 0x6590249c, 0x6492b0b1, 0x80a5ee56, 0xb8a787e1, 0x5fe17fa9, 0x84feb778, 0xe07f13cf, 0xaff05fa2, 0x452954b0, 0x361f4559, 0xbbdb75f1, 
    0xf8c03e93, 0x7b717d33, 0xddde1cf1, 0x9ebb664a, 0x0f491a19, 0xfdde1af1, 0x88a7caab, 0x8cbb377c, 0x8afb4cbf, 0x0433f8ca, 0xecf0c667, 0x9dc4fd18, 
    0xf67babfd, 0xd744b5b9, 0x72c631fc, 0xafb8bfc7, 0xc59279cf, 0xb1facbc6, 0xb3bf62fa, 0x00ff7ab3, 0x0a4d38c1, 0xcc824b65, 0xae755cd8, 0x41965393, 
    0xa0f97478, 0x19305e78, 0x7a108cc3, 0x06a18f81, 0xc00dcfb9, 0x95d5bab7, 0x33ede390, 0xfb4f06c7, 0xd7fa176e, 0xd174fca7, 0x2abed365, 0x152897ea, 
    0x08c99f62, 0x00ffb3a1, 0xff77142c, 0xcdd67400, 0x8c7065ae, 0x6b17f79a, 0xa02dd18d, 0xdc5c80b3, 0x20753748, 0xeeb5ba9f, 0x6b74093f, 0xe0faa6d0, 
    0x5fd2361d, 0x269e71f5, 0xa790eb75, 0x79ea83ee, 0xc9eb1df4, 0x27a62b3c, 0xc8cd059f, 0x9e9523df, 0xa5f014dd, 0xbfb7774e, 0x3d47324e, 0x0fb4dc2b, 
    0x09d07910, 0x46959694, 0x8d5e66f7, 0x4aaf3f9e, 0xbad6b0f6, 0xd9f3a16f, 0x24273797, 0xe7d1640f, 0x6015dd86, 0x7752b544, 0xe72c0714, 0x6b3d491d, 
    0xff683fce, 0x6f3a8000, 0xcd0e9fc6, 0xd6345bac, 0xacd230be, 0x5a121626, 0x216e1fed, 0x97b6a219, 0x98f9ee23, 0x4880ba01, 0xdf7457ef, 0xd8251812, 
    0xe76ee6ed, 0x58fe7a05, 0x1b6fcbad, 0xb3849836, 0x94db60b1, 0x1dec602c, 0xefe9e740, 0xd309555f, 0xec591ead, 0xd7a90c7c, 0x295554c3, 0x2ef2d1e8, 
    0xfcdf3a89, 0xca183217, 0x28dfd3ee, 0x1c1c04e3, 0x35473d82, 0x92c4fec2, 0xc7e2aff8, 0x1e1d7585, 0xbebb47d7, 0x14734687, 0xb00d935a, 0x41f9d44b, 
    0x7a98e1d8, 0x3863e00c, 0xf8a4b0af, 0x8a78a43b, 0xb175ebf2, 0x23efd0b6, 0xd66c2e79, 0xae24dd76, 0x627e7e49, 0xe164b715, 0x2bc63386, 0x870ff0b8, 
    0x73f02d7c, 0x2bfcd042, 0x9536daa0, 0x35fba9a3, 0x2499b7ad, 0x108b1bd8, 0xa99bd959, 0xaf794eea, 0x8a65c39d, 0x1b31a44a, 0xaebf465f, 0xf362d6a7, 
    0x9ed341d9, 0xddd95d15, 0xcaa7e974, 0xed07e5c7, 0xff02f17f, 0x85f85700, 0x8647e075, 0xc33f1d5b, 0x20ef9873, 0xaf696d90, 0xec3fc931, 0xdf5338a8, 
    0x03aef724, 0xfbc29fe0, 0x32fe89cf, 0x9611d186, 0xde13d7c2, 0x13e105dc, 0x568f2a3f, 0xa63e8027, 0x1f6bbfb5, 0x0ff14316, 0x8df89cf6, 0x926e58aa, 
    0x925cf3da, 0xb0eb1dde, 0x8e108bb8, 0xb37cc633, 0x7db5d7d7, 0xfc3bfb83, 0x8fe18724, 0x95b76081, 0x0f6ed501, 0x49252fda, 0xd84bab4b, 0x5401000e, 
    0xafd7d381, 0xa1e4f45a, 0x4bcba381, 0x9967dea6, 0xabccd509, 0x7fbed53a, 0x7e77e8e5, 0x996ef01c, 0xdb1adde1, 0x2c59b64f, 0xb07085a1, 0x5fd5b6db, 
    0xf5a9e35c, 0xfb4beb3d, 0x7efecf26, 0xef00ff6e, 0xb958a9e1, 0xcd27cd63, 0xcc27395f, 0xa8ec99dc, 0xf391ac24, 0x1298c1ef, 0xfc80191f, 0x7ffb9437, 
    0xb0fdd72a, 0xc2574dbe, 0xcc5c7e6c, 0x9f2bea47, 0xfc3f61f8, 0x1e4c2b5c, 0x00ff4bb0, 0xfb4bd7a0, 0x51bfc563, 0x314e9ef0, 0xbc62fe39, 0xc7ad3f5b, 
    0x6c7de8fa, 0x9f5e509a, 0x1c3ee6a9, 0xe28bc6d4, 0x6554230d, 0xb037962e, 0xea8e3a5d, 0xfc302b92, 0xffa6af81, 0x695b6800, 0xbfd5b56f, 0xbab662d3, 
    0x3f9665d3, 0x904ae522, 0x616522ee, 0xfc66a7bb, 0x0ecde6ab, 0x60ba26cd, 0xe02974df, 0xf3fce78c, 0xfcdaaa5e, 0xf02f7c72, 0xf00486e7, 0xe69906c1, 
    0x2b97c935, 0xced39827, 0x6cd3e7e0, 0xe96ec5c9, 0xeeb14a7b, 0xd34b9599, 0x5b76aa6a, 0xf863cc3f, 0xb64cdb82, 0x7d5d1337, 0xe30ed99a, 0xf66ea724, 
    0xe88905c7, 0xbcda614f, 0xb49fc4b7, 0x6be243d3, 0xe62e7d3b, 0xbb0c6b61, 0x480969e7, 0xe3822b0c, 0x00e460bb, 0x7a241b06, 0x4cfc8dd7, 0x78aabbf8, 
    0xa7cde9aa, 0x6a9f0efa, 0xf590c8f1, 0x27c37232, 0x0c9ce43c, 0x87f4731f, 0xef0f3fc1, 0xf6ea2bbe, 0x1a8e0e3a, 0xe49250d7, 0x52384d5c, 0x9f04bc45, 
    0x89d18330, 0x1c074821, 0xc701f2e4, 0x152cb8e8, 0x5a1d2c3c, 0xe1335fab, 0xaf93f9b1, 0xd987653f, 0x7de8eff5, 0x892fe101, 0x7e1600ff, 0xdf8ef826, 
    0x7b0bdec3, 0x28b8655b, 0x445c5dd3, 0x0c24fb25, 0x8edb925b, 0x737d01e7, 0x35074e9e, 0xedc3dff6, 0xe135fc11, 0x660d3efd, 0xc443fbe3, 0x7769040b, 
    0xb46a1774, 0x03f646c1, 0xae275bb6, 0x16fecc2b, 0xfed035fc, 0x58126906, 0x655eb769, 0xb431ecfd, 0xcdc3ca53, 0x00062093, 0x82f29171, 0xd51ea94f, 
    0xb2091eed, 0xebfcd84b, 0x9133dfc6, 0x9fd3edc1, 0x55f1aca9, 0x54a2caa8, 0xc6e791d5, 0xcac1258e, 0x3ce5d052, 0x6840fb47, 0x2ed51c7e, 0x311a1db4, 
    0xc3142fe2, 0x9268a3fb, 0x29c170f9, 0xc42c0919, 0x283b0e10, 0x7cc5b127, 0xfe47fbc1, 0x81efdad1, 0x5a97652f, 0x7c6e6dd5, 0xf166757b, 0xd21fbe69, 
    0x194d8b98, 0x33174819, 0x067949b6, 0x320e51e2, 0x3980637e, 0xa2a9fdd4, 0xed87e19b, 0x9bcbe071, 0x37ad7c69, 0x4bb1c8c4, 0xbfbf101a, 0xebf115e5, 
    0x0afbbc9d, 0x16d7fefc, 0x176adce4, 0xf72ccf37, 0x672e37f7, 0xec2c669e, 0x49e28efb, 0xd6fa3d39, 0x651c7536, 0x3b4f177d, 0x1e86d28d, 0xbe5d092e, 
    0x6ff64cbe, 0xd7e1dbd8, 0x3ede24fc, 0x24eab41a, 0x7417d2b5, 0x4e77c665, 0xf07d0ad9, 0xc5f73103, 0xb93ded7d, 0xe8553586, 0xecef7805, 0xfcf9f0b7, 
    0x4deff00d, 0xd45574b8, 0x66f3232e, 0x7096e056, 0xed821f09, 0x5daf811f, 0x2692d5b9, 0xc21371d9, 0x15debfc3, 0x7baae2e3, 0x6d4f965a, 0x950e4f0f, 
    0x2177692a, 0xc9383032, 0x9a278d1f, 0x9d3fdedf, 0x81509357, 0x832095dc, 0xed37d5fe, 0xfdaffad0, 0xcb8e2bf4, 0x99f93ab3, 0x0dc377f3, 0xb4131f53, 
    0xe1e573db, 0xd7f19ce0, 0x2bbd66ee, 0x76b4c5f6, 0xa322bcd4, 0xfa773303, 0xe0eff98a, 0x9fdcfdd5, 0xdc98341a, 0xd7eca3bc, 0x070eb624, 0xbfe86bc8, 
    0x7d46f6da, 0xe464c153, 0x67055e2b, 0x8e8c76c6, 0x4aadcc6b, 0x2dc1c450, 0xaa94f4d9, 0xf3bbb12a, 0xf0350ffd, 0xe193d6c5, 0x57adcb7b, 0x4827f7c5, 
    0x69e02dd1, 0x7327b820, 0x48a5cadc, 0xb400e248, 0xfc2b418a, 0xc1eb7123, 0x83f828af, 0x88af1be3, 0xb9be315e, 0x4f8bb0b7, 0xef58528f, 0xc8843216, 
    0x3e6111e1, 0xb921a963, 0xfc71e18c, 0x2bc6732a, 0xd43de273, 0xd2d4217e, 0xad1979da, 0xa378e7ec, 0x58a22147, 0x964f2ed4, 0xc02ea730, 0x4ecea81f, 
    0x21becf2b, 0xf18bfdcf, 0x2f18e282, 0x232db2bf, 0x2537168d, 0x30e31856, 0xfc543b86, 0xe24f2eb8, 0x3bfd7a4f, 0xc0519405, 0xaf2e8e52, 0xcdbfddbc, 
    0xf9e787fe, 0x135f6b8e, 0xed20545a, 0xc61fce14, 0xed356d4c, 0x029e4b4b, 0xf6cd10d7, 0x19664b55, 0xf6085c74, 0x851f0eb6, 0x10c3081d, 0x2fd0af00, 
    0xd6c0ebd9, 0x3e0200ff, 0x564f0d1a, 0xbff16a1f, 0x6f2fe288, 0xfd389565, 0xc64950d8, 0x324689e5, 0xe2c47272, 0x0ff06cbe, 0xe233fc82, 0xf8108b6f, 
    0x26495587, 0x6c858ff0, 0x547b6de3, 0xd7ec5395, 0x00832877, 0x23c14ec8, 0x81b9811c, 0x92f1e52d, 0x11fed16b, 0x7ed6577c, 0x5397f82b, 0xafa846f1, 
    0xa43c6d6f, 0x4f669ae9, 0x865aa793, 0x0f44902a, 0x88058503, 0x7992cdfb, 0xc5331baf, 0x664baefb, 0xa75300ff, 0xd8aac029, 0xbe9bd298, 0xd41d79fb, 
    0x1fc407ba, 0xfc1a7789, 0xd2b9365e, 0x075bd6b4, 0x3efb275d, 0x6955e2ce, 0x5ca39b44, 0x782484bc, 0x2363dcdf, 0xe0a3bdd6, 0xa5bfe6f5, 0x14f123fc, 
    0x7a57f910, 0x18a781fe, 0xc6e27c6d, 0xe5c43233, 0xec6cbe19, 0x52e583dc, 0xef010e0e, 0x1aa1d641, 0x780bcb05, 0x499ce8cc, 0x3ac66eb4, 0x3fc16f0a, 
    0xf8865a14, 0xc5356d81, 0x64daf9bb, 0x57f323ae, 0x8c61c021, 0x5ecdaf67, 0x55c22b1c, 0xec13bf22, 0x4be5606b, 0xa547510f, 0x3eaae5ad, 0xaec1f80e, 
    0x171fb7eb, 0xc5fa2cbc, 0xd094f6df, 0xc95ddbde, 0x0aca227d, 0xb1b40acc, 0xc6626446, 0xd71f7830, 0x87fa72ad, 0xfd5c5b83, 0xcef065a1, 0xbcf61f71, 
    0xf90076be, 0xf8ce4850, 0x8a51381d, 0x3d6abfe9, 0x237edd76, 0x969f05c3, 0xbd5aa6af, 0xdc168dd4, 0x36e096ab, 0x79001c3a, 0xf851aff5, 0x7548e067, 
    0xc68bda1f, 0x79b0ecba, 0x37a4d0b6, 0xc8523950, 0x7335e65a, 0x031de3b4, 0x7af5fe74, 0xe8ecad70, 0xf9d4ed73, 0x2c085d7c, 0x7407a753, 0x7de4be9d, 
    0xa58b641d, 0xa76c41d9, 0x9ff35afd, 0xbd2ace53, 0x5df3a2ce, 0x0fbcbd2d, 0xda217575, 0x5892a290, 0xa899d2e3, 0x06b3605c, 0x4aaff103, 0xb8e02df8, 
    0xb5866cee, 0xea9a0075, 0xd290cfe9, 0x31269fc7, 0xc1912bc0, 0xbedaa33e, 0x0515757e, 0xcc7f1fed, 0x7756a3d2, 0x7ff03138, 0x13883ac4, 0x8cd696ca, 
    0xa4c9f2df, 0x0e7c0fe4, 0x90c27fb4, 0xb5e77fd7, 0x3ffcfdaf, 0x26515fe1, 0x0a500083, 0xd6ad3ea9, 0x0f0afb97, 0xeb4f47ee, 0xf2c8ae38, 0xcf7d6add, 
    0x42d94fc9, 0x53fb00ff, 0x239d5fe2, 0xea6809a8, 0x1fc9897d, 0x7fd1d7c8, 0x2dac9db5, 0x9ea5e071, 0xf6613bea, 0x04ccb7b1, 0xe52387f9, 0xa73ea0e3, 
    0x7c335f81, 0x5f75d303, 0x4e12bf84, 0x171be28d, 0xedaa37b7, 0x0cb9aa0c, 0x07493256, 0x217dc5b8, 0x25d964fb, 0xed3487d7, 0x6cb64f4a, 0xeb9ee3d2, 
    0x72350afd, 0xe7b614bb, 0x310a402c, 0x618f65d0, 0x526badc1, 0x3b855f5f, 0xc486551f, 0x597bb27f, 0xfe279f79, 0x105fc5f7, 0x6fb4b366, 0x5b9e1622, 
    0x6e243cb3, 0x22990f91, 0x47953db6, 0x3d67d0c9, 0x2f3cd4eb, 0xe2d79af0, 0xa56ba9d6, 0x56b7f66b, 0x9b65ca87, 0x1c318650, 0xbaa444b3, 0x1fe7b4c1, 
    0x3ace5739, 0x275ee327, 0x4f876be0, 0xddda6910, 0x4ddb9ac1, 0x6b8a259e, 0x1eef9488, 0x74102d6c, 0x5846daf5, 0x6bb0dd0e, 0xe21f7cdf, 0x3f9ee51b, 
    0x374b5fb1, 0x99562189, 0x8fbbf4ee, 0xec24452a, 0xff0862c1, 0xf8c35600, 0x35e7c871, 0x4638f5fa, 0xa2540ebe, 0x991febfd, 0x238eb24e, 0x7966cbda, 
    0xf10fed07, 0x7acd3539, 0xa9e80d5f, 0x6d879f05, 0xd6d64d26, 0x5826441b, 0x814cd5ce, 0xb90504f3, 0xc32c171c, 0xbfeab880, 0xda119f67, 0x00ffd1e9, 
    0x5d20c966, 0x7fcf669e, 0xe40712de, 0xfe5ae0d5, 0xa5872785, 0x7908397b, 0xd7dbdca1, 0x2e0b5d21, 0x82f300d7, 0x8e1c7a3d, 0xcfe0a7d5, 0x366bdcb5, 
    0x963cc330, 0xc232c992, 0x1beb632e, 0xa7df871e, 0xe6f9f54e, 0x4ae70a3b, 0xfad8db54, 0xf018a7cc, 0xb5d6a8f8, 0xd04fbdfc, 0xe555f136, 0x5a52b495, 
    0x0373c75b, 0x9307b29d, 0xc1c0296b, 0x6b3d0703, 0xe200ff66, 0xe13b78ab, 0x267ba9d6, 0x9288a495, 0xa97be2d2, 0xc6cd99a2, 0x39f88cc0, 0xb6600b40, 
    0x90ab3d17, 0x3a9772f8, 0x5e5f2054, 0x47a8a8ae, 0x09c1d6cc, 0x35f540f9, 0xfd5cfcd3, 0xda7f759e, 0xd076c02b, 0xb6f5da68, 0x650c2c87, 0x3be0495b, 
    0xca968f2e, 0xe5a7cea8, 0x41327607, 0x1507e7e0, 0xae38f8f3, 0xf7b9657d, 0x9c269eb9, 0x6a72b928, 0x7f41757e, 0xdf0be379, 0x6477ea10, 0x97a1d6bd, 
    0xf6a62776, 0xc0610fe0, 0xc3fb0afa, 0x3fa175e0, 0x595db40f, 0x863a37c0, 0x3fd26a9b, 0xb1424a52, 0x09f89f8c, 0x2ff591af, 0xf8213e86, 0x6b3de239, 
    0xd233bec3, 0x3db5d165, 0xf82ab4b0, 0xf922ee78, 0x830c79bf, 0x90f15c87, 0x6b70e878, 0xba878fea, 0x7ca2a9a6, 0x36acf036, 0xd9dc7176, 0xd443a647, 
    0x6e39c9e1, 0x2b0c633b, 0xb22fc6d7, 0x85059fbb, 0xae9fc36d, 0x3d3b8bbf, 0xda224f72, 0xd4c66666, 0x777dad27, 0x69081de0, 0x23d20ffe, 0x88b449e1, 
    0x281f749c, 0x11bed63f, 0x067bbcd4, 0xc9a7e9b0, 0x3c697369, 0x1ac8918e, 0x7506c03d, 0xc9eb084e, 0xc697f615, 0x1ffe8c1f, 0x41e011f8, 0x560e6baa, 
    0xb2b64535, 0xae9941b3, 0x44850b64, 0x9f8ce75e, 0xc66b9241, 0x43a928c4, 0xa3ced4ab, 0xc7c23ba9, 0x1d83257e, 0x0000ff73, 0xd783cda3, 0xaf1c00ff, 
    0xf657adcc, 0xacd3f8ef, 0x62774b5f, 0xedf01fde, 0x74988494, 0x876bb1f7, 0x906e3b8d, 0x00fcc9b0, 0xdbf0bfaa, 0x00ff1c9f, 0xdfe13de8, 0xf67f14fc, 
    0x53cbd575, 0x12eac7c8, 0x41bc2d31, 0x13be8a37, 0x4df113fc, 0x29f5d9de, 0xee6d6fae, 0x38de4f6e, 0xff0097b7, 0xbd22df00, 0xd5e9f64b, 0x75f84b9b, 
    0x3c99510c, 0x38f1b2ab, 0x00ffdb46, 0x3c19652c, 0xeff1d4f3, 0xbfe0635e, 0xbfe85c06, 0xe286e7b2, 0x6b8eb8b8, 0x995e2b9d, 0x143ce295, 0x8f312992, 
    0xaff5ebfb, 0x66bafd4a, 0xd8003f8e, 0x4ddc2643, 0x88bbec1c, 0x808d381f, 0x6bf4fdc7, 0x6bae19a7, 0x8c7ef945, 0xac49c0ed, 0xf664acba, 0x117ce8bf, 
    0x0e8f3267, 0xa29a374d, 0x96b84bee, 0x543e8738, 0x7f921c21, 0x8e5421de, 0xc55e73e0, 0x9ef83ffb, 0xb75b434d, 0x2e58e216, 0xcdf24a9c, 0x8841c62d, 
    0xc939750c, 0x057d14dc, 0x1c9bb678, 0x45e285d7, 0x0c75b867, 0xc8c1191b, 0xd7ed4050, 0x815eeb03, 0x2f0966f0, 0x374f172f, 0x8e1c7a97, 0x7b6f49b2, 
    0x9b64be6a, 0x261b3386, 0xe750e93e, 0xf729d481, 0x5530d1af, 0xd2256a1c, 0x5756c5c7, 0x8ccf858e, 0x9aea3136, 0x17b6a48d, 0xb1b4da76, 0xd51d4958, 
    0x4300b3c5, 0x2349a27d, 0x5286840c, 0x08421491, 0x5693a7ea, 0x56f8187e, 0x87c2e3ea, 0xdc007559, 0xdb71d6ea, 0xb8f27360, 0x7f5e1fc9, 0x636df32a, 
    0xd7f1bc53, 0xcbbbcd8d, 0xb1c56b14, 0x5fcb03b8, 0x3b943f2a, 0x3990a41e, 0x7d4d9227, 0x49ebf04d, 0x49f3524d, 0x3c42b0b5, 0xf42ea239, 0x6e2165b8, 
    0x05ed36ee, 0x1614d533, 0x3cc9b13b, 0xce8aaf74, 0xa8492931, 0xbffc9be8, 0x877cfbcc, 0x9a4bab08, 0xea69fa4a, 0xcf95fcfa, 0x2cd1d164, 0x020844e3, 
    0xeb910496, 0x8bbdd681, 0xd5c67ac0, 0x00e440b6, 0x753c0353, 0x2f02e515, 0xaa13223a, 0x1f5d81e3, 0x6f233586, 0x0cc7482c, 0x881fa3ac, 0x55a589af, 
    0x9f4949c6, 0x151466a2, 0xf4c84e5a, 0xe15fdb0f, 0x0fc6bf25, 0x17c4a3d9, 0x78abf056, 0xcf76c393, 0x085769ac, 0x35a6783f, 0xf5402cdd, 0x1886f1da, 
    0x577b0af5, 0xdd7af888, 0x6807cf74, 0x6192cb96, 0x7d3081b1, 0xd28f51a3, 0x1ef0cfbe, 0xa5ad06a1, 0x2ce49efd, 0x39a648d1, 0x29436e63, 0x428f2018, 
    0xfc6c7e0d, 0x6b3cd64a, 0xfec5c3f0, 0xafedd226, 0x377dbf63, 0x5adc9a50, 0xecf2455d, 0xc76d4a0c, 0x472a1420, 0xbabebed6, 0x514aa773, 0x8fc6fcd8, 
    0x2c522534, 0x7ef66ffa, 0xe36771f8, 0xea5d15bf, 0xe910f8b7, 0x8063c272, 0x37c9fb01, 0x0dfb046e, 0xbfca2bbd, 0xc11a2f6c, 0x428bbfe3, 0xa4499cdd, 
    0x8a167ef8, 0x9886bb11, 0x7ac0489c, 0x803e179e, 0xf04fd7fa, 0x9d06e39f, 0xede0ebf0, 0x121fc5af, 0x3b75d2ed, 0x21a83bab, 0xcf6261d0, 0x55b1d23d, 
    0xeb937301, 0xbe06c093, 0xd4241e51, 0x2ea9557c, 0xfb837ea3, 0x9667fe46, 0x39f7752b, 0xf3877ddc, 0x38f57ccd, 0x948aaf54, 0xbe36d1d2, 0xc241af67, 
    0x6da79a0f, 0xf7fc523f, 0x45ca9156, 0xf89f87d2, 0xf640a6b6, 0x7f543afc, 0xe73fd36d, 0xaf43fe97, 0x8967f13e, 0x722d3bf5, 0xe2397d7b, 0x265f34fb, 
    0x1d96045b, 0xff91354f, 0x88870900, 0xfd0fbfbf, 0xbec23ff2, 0xb58d5089, 0x72b11447, 0xad3eb3bb, 0xe1ac98fd, 0x6400ffd6, 0x5e5c141f, 0x6fcde747, 
    0x3bb1c6ae, 0x88189413, 0x46df7f60, 0xe000ffb3, 0x2e3217a4, 0x704be09d, 0x66e644c4, 0x2b70ee2b, 0xff4df65b, 0xf1389300, 0x9387fd87, 0x566800ff, 
    0x8714fc2f, 0x7f8047fd, 0xacf4afed, 0xd2c87fea, 0x99fca38f, 0x00ff85cb, 0x5f6f7591, 0xd0603ef2, 0x8656a22c, 0x5a910860, 0x751c04df, 0x00ff5cb8, 
    0x35f303e8, 0x947a6bd0, 0x3e97f6f6, 0x3aac8420, 0xba2d72a4, 0xd8851a4b, 0xc7402e84, 0x58eff3d4, 0xa8fa17da, 0xb5de00ff, 0x8ad200ff, 0xf90fe2bf, 
    0xeb7f2f13, 0xd7927fe9, 0x3ad231e8, 0x892f271f, 0xdfc23f9d, 0xfed66802, 0x6ba4d62e, 0xddf07251, 0x7cdc8c14, 0xe3c101fb, 0xb4a4aff3, 0x0a8411b4, 
    0x0c008536, 0xf80daf76, 0x2300ff6d, 0xeb3f8857, 0x7b2dfaf3, 0xbfda9da6, 0x4eaacd13, 0xbd89da58, 0x45a6fb99, 0xd2c0114e, 0xaf8e6e69, 0x3f27194a, 
    0xdb6aa5e7, 0x4060a244, 0xac39c8c1, 0xffa92bad, 0xb1953e00, 0xf1b7fe6f, 0x911dc915, 0xfa7ae2e8, 0xd2f0e51e, 0x18868d67, 0xf2f58ef1, 0x695fedc7, 
    0x3bed9f0d, 0x8c71f9a8, 0x73f6e95e, 0x67c8adce, 0x923f53d8, 0x525fe5af, 0xf9eb377c, 0xb51fcc57, 0xeb9cfccf, 0xcb26d87f, 0xaf6600ff, 0xa2fb87b8, 
    0xfeab2b3f, 0xf12acff8, 0x1e698fee, 0x88c13581, 0x634a2249, 0x2ae4305f, 0xb60327e3, 0xfa70cd71, 0x862ffa17, 0xafaf4bbc, 0xc25ba9cd, 0xe4478a23, 
    0x0ed09122, 0x10bfe8d5, 0xb955e4bf, 0x83af00ff, 0xd7a000ff, 0xf27fe99c, 0xff5b3c4d, 0xf9d35c00, 0xef5a0e57, 0x73f5ec4e, 0x3892263d, 0x2af8043f, 
    0x20fac5c7, 0x6e09b5d4, 0x5849ea5a, 0xa05d8e31, 0xadf7f4f7, 0x54f800ff, 0xf4fc1f5a, 0xbf00ffbd, 0x6afd00ff, 0xf23f845f, 0x00ffc526, 0x57fa9f5d, 
    0x67bb5e6b, 0xd9ff7f82, 
};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(GuidedNode, "Guided Filter", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Filter#Video#Matting")
