#include <UI.h>
#include <imgui_extra_widget.h>
#include <ImVulkanShader.h>
#include "DeInterlace_vulkan.h"

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct DeinterlaceNode final : Node
{
    BP_NODE_WITH_NAME(DeinterlaceNode, "Deinterlace", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Filter#Video#Enhance")
    DeinterlaceNode(BP* blueprint): Node(blueprint) { m_Name = "DeInterlace"; m_Skippable = true; }
    ~DeinterlaceNode()
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
                m_filter = new ImGui::DeInterlace_vulkan(gpu);
            }
            if (!m_filter)
            {
                return {};
            }
            m_device = gpu;
            ImGui::VkMat im_RGB; im_RGB.type = m_mat_data_type == IM_DT_UNDEFINED ? mat_in.type : m_mat_data_type;
            m_NodeTimeMs = m_filter->filter(mat_in, im_RGB);
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
        return ret;
    }

    void Save(imgui_json::value& value, std::map<ID_TYPE, ID_TYPE> MapID) override
    {
        Node::Save(value, MapID);
        value["mat_type"] = imgui_json::number(m_mat_data_type);
    }

    void DrawNodeLogo(ImGuiContext * ctx, ImVec2 size, std::string logo) const override
    {
        // Node::DrawNodeLogo(ctx, size, std::string(u8"\uf2a8"));
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
    MatPin    m_MatOut  = { this, "Out" };

    Pin* m_InputPins[2] = { &m_Enter, &m_MatIn };
    Pin* m_OutputPins[2] = { &m_Exit, &m_MatOut };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    int m_device            {-1};
    ImGui::DeInterlace_vulkan * m_filter {nullptr};
    mutable ImTextureID  m_logo {nullptr};
    mutable int m_logo_index {0};

    const unsigned int logo_width = 100;
    const unsigned int logo_height = 100;
    const unsigned int logo_cols = 1;
    const unsigned int logo_rows = 1;
    const unsigned int logo_size = 5642;
    const unsigned int logo_data[5644/4] =
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
    0xe6e5e4e3, 0xeae9e8e7, 0xf5f4f3f2, 0xf9f8f7f6, 0x00dafffa, 0x0001030c, 0x11031102, 0xf6003f00, 0x2f695b6b, 0x956fc964, 0x37bce67a, 0x32ba95f6, 
    0x3c01ba6b, 0xc5fc7812, 0xe3b3467a, 0x8500ff64, 0x8be16987, 0x3b961139, 0x8d769e67, 0x60e020c1, 0x6bfc741c, 0x6a00ffca, 0xaec7be69, 0x04207078, 
    0x15f52396, 0xc91ddef8, 0x74fb8957, 0xceb668a1, 0x7facc36b, 0x812749b4, 0xe0b4dd5d, 0xab9d8e81, 0x7439fcce, 0x4e16cb45, 0xb9abfc48, 0x0342b8b4, 
    0x0c10b549, 0x5d314e92, 0x9935bff3, 0x4db914bd, 0x6c013e73, 0xe48babe2, 0xd7eb4592, 0xf2268d06, 0x683eee19, 0xc040a884, 0xf59af973, 0x680300ff, 
    0x6cbf161a, 0xdba4c9b0, 0x2c97734a, 0xe9bb26b9, 0x5f596560, 0xc5ca2399, 0x2bc370e6, 0x431e39f2, 0xb713e072, 0xd7c38afc, 0x6516f249, 0x543e5e3b, 
    0xe86b8efb, 0x087c1eaf, 0x4d00ff82, 0xbcf05297, 0x05f6de66, 0x8a9cfd09, 0xe4dc24d2, 0x1e58009c, 0x2becc071, 0xcb16cfe6, 0x5c5ea375, 0xb485bae9, 
    0xe4f63796, 0xa1d3b6a4, 0xb8910746, 0x9e76002b, 0x54698ec4, 0x3bc354c2, 0xc3e34edb, 0x7c6c1866, 0xf67ab31f, 0xe1df9e7b, 0x68ab915b, 0x0f135716, 
    0x6fc098de, 0xe489aeca, 0xb8e2aed3, 0xc4927db6, 0x10fad0e1, 0xa5f08c6b, 0x29fcd3d0, 0x04cc7669, 0x2467503e, 0x9aeb01e0, 0x375397d2, 0x958506d0, 
    0xae0c75e0, 0xf7812187, 0x8ad7e415, 0x5a4772e6, 0xd2d1eca4, 0x1bfed1fe, 0x155ec58f, 0xe927bed1, 0x959122ca, 0x8f58352c, 0xb0c23919, 0x7d1cc3f5, 
    0xe2e97c0d, 0xfe69165f, 0xd6ddf008, 0x3e99a9b9, 0xa90809cd, 0x469ee512, 0x19f6d5ce, 0x7c485fef, 0xa399f83f, 0x35d33d7c, 0x3c63bcd8, 0xcbdbe009, 
    0x75d15b57, 0xda99462a, 0xab720170, 0x4800bab9, 0x78587ecd, 0x53c600ff, 0x97c5dbf8, 0xb6eeecd7, 0xe922cf51, 0x2072acf6, 0x07ca7783, 0x9f0c20a9, 
    0xec32f65a, 0x7bafc533, 0x1ff9bf65, 0x3cc5882f, 0x578acab1, 0xffd1c77b, 0xead39100, 0xc45fb41f, 0x67eeef89, 0x794dbcd2, 0xe54a59a0, 0x7cb24fd3, 
    0xa0d21747, 0x878600ff, 0x00ffb3f8, 0xff560345, 0xb882bf00, 0xac357c58, 0xb6674aea, 0xe86bf5d0, 0x699bc7c9, 0x7d26f266, 0x935a0732, 0x10bf10fe, 
    0xb1d000ff, 0xe000ffaf, 0xaff04fba, 0x476954af, 0x1ff81545, 0xdb75f136, 0xc53e93bb, 0x717d33f8, 0xaf1df17b, 0xdd32a4ae, 0x248d0ccf, 0x6f8df887, 
    0x57dbd57e, 0x271b3ec4, 0xe2c7f9fb, 0xcce02b2b, 0xc31b1f11, 0x89fb11b0, 0xf756fb3b, 0x1a6b73ed, 0x3818fe6b, 0xfb7b2cc9, 0x99fbfc8a, 0xbf6c5c2c, 
    0x2ba61fab, 0xaf373bfb, 0xd08513fc, 0x2cb85566, 0xe80a144e, 0xed9f29b5, 0x16964f87, 0xce50e385, 0xd483601c, 0x5c83c80f, 0x5be086ef, 0xc8ca6acd, 
    0xe399f671, 0xb7fd2783, 0xd46bfd0b, 0x92e83a3e, 0x3516df69, 0xadc2886b, 0x1811e2bc, 0x847c9edd, 0xa6fbaf07, 0x2b736db6, 0xb8d76484, 0x0e4d5cbb, 
    0x754b1bd2, 0x6470b8b8, 0x1ef48527, 0x091fee95, 0x8dd66b34, 0x4e07a6af, 0x12739db3, 0x8ee094f4, 0xe435db31, 0x17d3167e, 0xb8b9b7cf, 0x9ec8dd1b, 
    0x58e1c108, 0xf09f4310, 0xeed51ec9, 0xd6d71f9e, 0xcce03c68, 0xec0ea9d2, 0xd77abe57, 0xd3b586b5, 0xcc9f0f7d, 0x20399cb9, 0x3c91267b, 0x88fbc750, 
    0x0e49d5d2, 0x8f25a7ca, 0xeda779bd, 0x693ffb23, 0x3d3c1bdf, 0x2c6bb32e, 0xd22e5eb6, 0x323626ac, 0x9efe015b, 0xb6a185ca, 0xf89dfd97, 0xd618ec19, 
    0x41e29986, 0x63259b04, 0xb7716e21, 0xbbadf2af, 0x351d1b5f, 0x2c469e19, 0x8c85721b, 0x1fa8a31d, 0x4c7dbda7, 0x79b44e27, 0x12f0b167, 0x510d5fa5, 
    0x47a3a754, 0xeb24bac8, 0xa45df07f, 0x4f67ca68, 0x05e3c87d, 0x8e200748, 0xe29a73c4, 0x7c29623f, 0xc26bf153, 0x6d8d9eda, 0x1e734fa6, 0xa30e838d, 
    0xe80c2b28, 0x735540cc, 0x1c4110c3, 0x1f57d857, 0x105f3407, 0xe07e5cde, 0x9977e8db, 0x3b41bb7b, 0x939c91ee, 0x03920729, 0x18862716, 0xdbae18cf, 
    0xf01d9ec0, 0x0bbdc1af, 0x87aff043, 0xd14bdfec, 0x5b6b9bd4, 0xb049326f, 0xb360b7bb, 0xd45337b3, 0x36ecf99a, 0x43aaa458, 0x68f4b511, 0x7d56ccfa, 
    0xc273ba28, 0xe9cecaab, 0xb1f2d3ed, 0x61fb41f9, 0xd5bd417c, 0x785d20fe, 0xcf96a211, 0xc1d5f04f, 0x1b27ee8e, 0xa983d74c, 0x85a3aa3e, 0xbc26c13d, 
    0x97e000ff, 0x8abfebc2, 0xd186311e, 0xb54f9715, 0x0197f7c4, 0x15ce5478, 0xd013ab47, 0xfbad357e, 0x0fb67858, 0xefb49f88, 0xc3526dc4, 0xeb5edb73, 
    0xbdc15b92, 0x62111676, 0x339e9111, 0xb5fe9fe5, 0x3bfb837d, 0xe187237c, 0xb7618187, 0x97ea5499, 0xb98bf60d, 0xd2ea5252, 0x1c0800ff, 0x03a90200, 
    0xaff5f981, 0x1a184a4e, 0x6cfab43c, 0x5c9d9079, 0x5badb3ca, 0x875efee7, 0x089fe17d, 0xd119bee9, 0x55ebf46d, 0x38c45036, 0xa9f21186, 0x27a99f3c, 
    0xd8a7d69c, 0xe2e7bf2d, 0x63711b6a, 0x7af34993, 0xde4bced7, 0x54f64c6e, 0xf9485612, 0xbacde0fb, 0x1f30e343, 0x6f9ff286, 0x00ff5ae5, 0xaac91fb6, 
    0xeebe4df8, 0x15f52366, 0xb8307ccf, 0x858b00ff, 0x09868e69, 0xe91af47f, 0x77886cbf, 0xcf133e6a, 0x00ff9c18, 0x9f2d5e31, 0x74fda3d6, 0x724db63e, 
    0xf3d44f2f, 0x636a0e0f, 0x8f06f144, 0x4b9732aa, 0xa77beb1b, 0x1549555f, 0xd7407e98, 0x45b47fd3, 0xd21aea9b, 0x6d8969df, 0x2ccda775, 0x58e5de66, 
    0x32117615, 0xe14fcfb0, 0xcd57f861, 0x4b9a1dba, 0x50f9c172, 0x4f1de384, 0x977acdf9, 0xcdd624fc, 0xdec3d7f0, 0xc0101714, 0xbb76489a, 0x8b6c9bf3, 
    0x833c4f23, 0x6600ff39, 0x3a00ff68, 0x95f6d2dd, 0x2a33dd63, 0x54d5a697, 0x40fdb6ec, 0x10f6b6eb, 0x985cd2f9, 0x7c1b5220, 0x8e8d799c, 0xe938f709, 
    0x6fe2575e, 0xf1e275da, 0x8d169f46, 0x5b5b2b73, 0x235ce7cf, 0x093cb1fc, 0xf7fb3af3, 0x2b92eb18, 0x7c27fec6, 0x5b3cd55f, 0x9f962c79, 0xbb7d3ae8, 
    0x03702cc7, 0x8373b0fe, 0xf4386ef5, 0x5ff0a1e6, 0x2abe2f80, 0x1e1e36eb, 0xb5edd1d1, 0xfbfc2e19, 0x1649d5a9, 0xe4fce4d6, 0x8c92e41e, 0xbfd2d707, 
    0xa960c145, 0xd5ea60e1, 0x47fef95a, 0x27f3e3c2, 0x0fcb7e5e, 0xfadcebb3, 0x12bfc327, 0x4d7c287e, 0x071a3cf1, 0x16189a83, 0xee698a76, 0xb0166e9c, 
    0x3d27b03b, 0xf47160d8, 0x1f7eb5af, 0x8cf09fe8, 0xea3076e8, 0x5c439dd3, 0xcd154710, 0xa0341cc9, 0xad3ecc0d, 0xc29779cd, 0xfa8600ff, 0x025dc01f, 
    0xb54b4b4b, 0x11e465fb, 0x105c5dba, 0x01e03eb2, 0xec7e7bc6, 0xf69afa1c, 0x455a038f, 0xbab8007f, 0x6d932c71, 0x18c6e923, 0x78d67a7d, 0x5165d4aa, 
    0xe0c86a2a, 0xa64b0c8d, 0x726829e5, 0xf5f827de, 0x557778a5, 0x89f6f0be, 0x63fcd600, 0x464b4500, 0x6157203b, 0x8efdae9d, 0xe52b7d3b, 0xf647daef, 
    0x0fbcd696, 0xa7de2579, 0xe34bdba8, 0x8227ebbb, 0x73dac3c7, 0x63d2b4ee, 0x32377465, 0x46f5fc37, 0xfc1eaa20, 0x49ed9fd6, 0x3f0c9f1c, 0x5c078f6b, 
    0xe9b14e5a, 0x8a5524fe, 0x56809478, 0x8fafba7f, 0xcfaf855c, 0x12ed7ffd, 0x57776ade, 0x85fa24cf, 0x3c8ddcdd, 0x9c5d92d2, 0xfc49dcb1, 0x8e3a1beb, 
    0xa78bbe32, 0xe174ca9d, 0x57828b87, 0x3d93af6f, 0xf835f69b, 0x0900ff75, 0xb5bf8f37, 0xdb364fa7, 0x5ce85048, 0x90ed7467, 0x1600dfa7, 0xbdaff83e, 
    0xa030b7a7, 0x7805e855, 0xf0b5ece7, 0xfc024ffa, 0x7867d33a, 0x11176ad4, 0xee06b3f9, 0xfd2001ce, 0xebfc6817, 0x6475aed7, 0x8db9ec88, 0xd1fd71a3, 
    0x263e5e91, 0x6ea9b5a7, 0x1d9e1eda, 0xeed2542a, 0x38756542, 0xf73c29ce, 0x9357f5fe, 0xb0dc8150, 0x93e9c165, 0x3f74fb4d, 0xb8ce4fde, 0xb83fbbec, 
    0xcd67e6ea, 0x3c750cbf, 0x2dd34f7c, 0x133c4cfe, 0x263c0f9e, 0x6cbfd26b, 0x4b6d476b, 0x666020c2, 0x5100ff7e, 0x19fc3e5f, 0xe397bcbf, 0x648c9246, 
    0xe09a7d98, 0xf281e496, 0xf633fa1a, 0xd5b857ba, 0x8893193c, 0xfe0ed8cd, 0x5e93918b, 0x86526a65, 0x3e750926, 0x56459596, 0xa17f7e37, 0xb91b9ee6, 
    0x73257cd3, 0xb9e2bd3e, 0xb6c3c35a, 0xa032b8b1, 0x80b224dd, 0x8389aaac, 0xfa4810b8, 0x477c94d7, 0x34be9cf1, 0x79a5e6f1, 0x92969c0e, 0x7247116a, 
    0x22ba7b6d, 0xfa985ba4, 0x413030e6, 0x736b0ee0, 0xd7909ac7, 0x3c974eae, 0x0d9f26ed, 0x2421c6e3, 0xaea2c218, 0x4b42af1c, 0x7f5e731c, 0xb21616f1, 
    0x9070cdf8, 0x6700ffde, 0x59446140, 0xe2380241, 0x0f7083f2, 0x5f071900, 0xe9d77b52, 0x8ea22cf8, 0x55719402, 0xfeede63d, 0x3e3ff46f, 0xf85a73cc, 
    0x07a1d2aa, 0xbe70a668, 0xfa691232, 0xcfa6abfe, 0xdeeebe6f, 0xa148b566, 0xb60172e7, 0x0cef1f3c, 0x2bc6e72f, 0x76f60bf4, 0x001f44f0, 0xabae1cbe, 
    0xf1bc28ac, 0xdd2188bf, 0x9176606f, 0xe36460c2, 0xe40e8c70, 0xf98a73f2, 0x005ec0db, 0x7e8ae7f0, 0x9bf85b2d, 0xc2336f57, 0x8db31b1e, 0xe742adf5, 
    0x747b936f, 0x7ede3913, 0xc7dbcc4d, 0xab92c0cc, 0x3eef5ac7, 0xd6587c11, 0x9bf82d7e, 0xaa46f154, 0xe6790b1e, 0x32d34d27, 0xd83e9d7c, 0x35865431, 
    0xc9010c3c, 0xaf39491d, 0x7bc63317, 0xbfd992b2, 0x30caeacc, 0x8d89ad31, 0xb7efbb29, 0x2c9df997, 0x81f80efe, 0xd45b43f1, 0x9c3ac757, 0x5970445a, 
    0xf4b8ec9f, 0x9674e6d8, 0x71749344, 0x540869b1, 0x74247823, 0xf880bee2, 0xe9ae7b47, 0xbc0a00ff, 0x15091447, 0xfde1b7ce, 0x926ac33c, 0x12696c33, 
    0x9c5bd827, 0x422fcaed, 0xc6140f0e, 0x83ce2dd6, 0x6500ff70, 0x4223ae89, 0x58aa5d56, 0x3f8dbd0d, 0x6388a7e0, 0x1058f84f, 0x9f94d1ce, 0xa540924b, 
    0x0ec3de87, 0xbc26bf87, 0x29e3752a, 0x4f3d3fc5, 0x7482adb1, 0x7a39f570, 0xad5ade5a, 0x2dfe808f, 0xe7edba6b, 0xdd0b2fc6, 0xa88f37eb, 0xb5e50d4f, 
    0xf912b7c4, 0xc0ea249f, 0x66171acb, 0x0ec60125, 0x7cd49e6b, 0xedefda16, 0x01863709, 0xe5d5182b, 0x518ecaf3, 0xedef8c04, 0x5de918c0, 0xad45ed1f, 
    0x84c4af5b, 0xd558b1b7, 0xbb57cbb4, 0x91dba191, 0xb11b3723, 0xcd03e0d0, 0x3fc39f7a, 0xfea8c703, 0xd7377ed4, 0xb6cc8766, 0x81bb2189, 0xdc8492ca, 
    0x1c1cb2c6, 0x9f6ea063, 0x1552afde, 0xed7328b9, 0x5d6cf9d4, 0xa7532c08, 0xbe9d7407, 0x641d7de4, 0xc3d9a58b, 0x5afda76c, 0xbd6abdfe, 0x5cd3a4ce, 
    0xb510bcad, 0xabfccbcd, 0xd3242912, 0xb324a76f, 0x7a06c686, 0x167ca5d7, 0x56774cf0, 0x7057db3f, 0x4e37d72c, 0x7b2c937c, 0x328e5184, 0xcdf53b32, 
    0xaaeafc7c, 0x7f8ff611, 0xd5a834f3, 0xfe0bce9d, 0x2f89f714, 0x36247dbf, 0x27e416f0, 0xb31dc899, 0x3f29fc47, 0xbfd37f10, 0x515ff7fd, 0x280a8ba6, 
    0xee88a152, 0xd8df69dd, 0x69717ff0, 0xd91567fd, 0x4baf5b1e, 0xfb25f9b9, 0x531d5f27, 0xec9c5fe2, 0xa9a3ad02, 0x1fe7843e, 0x5fd1d7c8, 0x2d11b4b5, 
    0xf3c481cf, 0x426f4a2c, 0xdbf7e1ae, 0x03fa0023, 0x7c345feb, 0x4f35d304, 0xdb127f84, 0x9b0df146, 0xe6d09b5b, 0x31641429, 0x1e24c958, 0x7fd257fc, 
    0x059b2eb6, 0xac8600ff, 0xae990bf5, 0xde2a5da0, 0x446bd47b, 0x5b8a72dc, 0xc741cfe6, 0xad15fed4, 0xf0ed5f7a, 0xb0ea63a7, 0x4ff68fd8, 0xe4332f6b, 
    0x136f2acf, 0xca9e41fc, 0xc2b34fd2, 0x6770cbd3, 0x329ee588, 0x1e1b9164, 0xf44a2fa0, 0x237c0aef, 0xa8c7f8b3, 0xbc3ae959, 0x61ae1657, 0x28f69a75, 
    0x347f00ff, 0x09ebcc6a, 0x5fc54d5f, 0xc42b883e, 0xaca80dfc, 0xd6b0221e, 0x6b7b262e, 0xe2528127, 0xf9fafe78, 0xd421bcd0, 0x9fdcda77, 0xbcdf6b40, 
    0xf16aaf2f, 0x6fd318c5, 0x41165f64, 0xb3bd46ba, 0xcbcf387c, 0x9beb7f82, 0xeb57f901, 0x60abe0d4, 0xde2f4ae5, 0x55cafc48, 0xd61e7194, 0x7fc7335b, 
    0x2b897f68, 0x4f8278ac, 0x0d8be80c, 0x66ac8797, 0x226b7b54, 0x269a963c, 0xf711140a, 0x9ecd3095, 0x2b7decec, 0x3c9efdad, 0x2fa76b47, 0x148e64f6, 
    0xdf5398af, 0xfc408cf7, 0x5d07bc9a, 0xf3f065d0, 0xf7a3a435, 0xf5f902d1, 0x0f635742, 0x35cf9faa, 0xb6dfe0a9, 0x30366b5c, 0x92963c41, 0xf6c33acb, 
    0xc7c6fa98, 0xf32bfda1, 0x9e1b76cc, 0x7db6a994, 0x788c5366, 0x5a6b4c7c, 0xdfa75efe, 0x51abf8f3, 0xec85b7b1, 0xb7baa36d, 0xed5d967f, 0x82815386, 
    0xadb57e07, 0xef72f177, 0xf50f7fc1, 0x07adbb5b, 0x59dc864b, 0xd2c535cf, 0x8ce67cc8, 0x724c3f23, 0xed812dd8, 0xc8c3df5c, 0xab6e954e, 0x35757a75, 
    0x47d23242, 0xfca39083, 0xbbf8a72b, 0x1fea3dfb, 0xb8c31fb4, 0x84b5d160, 0x2ce26ed2, 0x613adaf0, 0x65ca5b2e, 0x09ec7644, 0xd7d06720, 0xb9e2e0cf, 
    0xdfe796f5, 0x42989866, 0xbd26978b, 0x2f78ce0f, 0x79617cef, 0xe84e2de2, 0x32d4bbef, 0xdef4c4ee, 0xe0a00fdc, 0x87f7157e, 0xdf42ebc0, 0x5d5df40f, 
    0x8e3a37c0, 0x3fd26a9b, 0x8c155272, 0x35f9fe67, 0xf0a94ef2, 0x051fc4bb, 0x78ae4ffc, 0x637ac66b, 0x62a47ae9, 0x45ef8710, 0xb4246d3a, 0x23c7d14f, 
    0x525fe938, 0x35d53dfc, 0xb7e1134d, 0xb3b26085, 0x3dcae28e, 0x1cb31c32, 0xc72d2739, 0x7cbdc2e0, 0xb92bfb62, 0xdc4658f0, 0xf8ebba39, 0x24d7b4b3, 
    0x31d3f636, 0xd784da38, 0xd001bed7, 0x0de19f46, 0x9b142e1e, 0x41c78948, 0x6bfd83f2, 0xc74bfde0, 0xaebbfab6, 0xc4b52d9d, 0x46b21217, 0x0024cbe3, 
    0xafc7a9ce, 0xf8d2be26, 0xc39ff1c5, 0xfc0100ff, 0x616d7508, 0x4b348688, 0xc6584b6b, 0x85c29d65, 0xdbef5555, 0xf19a833e, 0x502a0ab1, 0xaa4cbd5b, 
    0x2cf7933a, 0x7c4be277, 0xdd7f9273, 0xef6fdfa5, 0xd77c00ff, 0xfba97ee6, 0xd65afc76, 0x2bf21eef, 0xf0c32f7d, 0x15bbfabf, 0x73b5c98c, 0x91dc2dc1, 
    0xb5e13f55, 0xd07f2ffe, 0x00ff4e4b, 0xe5e97ac0, 0xff18d9a9, 0xe7cc6700, 0x3f41fcad, 0x7c13be8a, 0xd845f113, 0x6e29f559, 0x6eae2d6f, 0xb738de4f, 
    0x00ff199b, 0xb77a45be, 0x3eabc9ed, 0x22e1f097, 0xeb8eb084, 0xe305727c, 0x9f61268f, 0x5eeff1d4, 0x06afe05b, 0xb2df68dc, 0x9ea286df, 0xd2b926e1, 
    0x5a99e9b5, 0x48b29133, 0xef3fc6e4, 0x2bbdd6af, 0x1391ebf6, 0x527c3ac0, 0x73340b6d, 0xf3712cb0, 0x7f0cd888, 0xb1ba46df, 0x4569f69e, 0x3ba3dfae, 
    0x2e6b1270, 0x9b3d19ab, 0x6c820ffd, 0xb3c357dd, 0xeeb6efca, 0x7144b84b, 0x0faefe83, 0x893fc92d, 0xcd5e310a, 0x77c407f0, 0x07b7ce9a, 0x78c343d9, 
    0x952659c2, 0x1b1083df, 0xdaeefa7a, 0x47bc823e, 0xbd6b8e4e, 0xb86705d0, 0xc7564191, 0xfc01b843, 0x7bd7f8f1, 0x9e2579f0, 0x7b4ba671, 0x63c8a075, 
    0x2def6d79, 0xb7a57457, 0x7fcac60c, 0xf5f3ae84, 0xc144bf02, 0x97a87154, 0x59151f4b, 0xfc69685d, 0xfa12d364, 0xdeeacae6, 0x56c90be6, 0x449e54c6, 
    0x32373720, 0xec651049, 0xfa452656, 0x8fe1978a, 0xf027ae85, 0x2e126bab, 0xacd5b9d6, 0x9fe3b5e3, 0x3e924196, 0xe655febc, 0x77a3aefa, 0x1b1edfe3, 
    0x34712473, 0x33f320ae, 0xf9b47cb5, 0x06721f77, 0xa5bed64f, 0x2ea477f8, 0x59a579a9, 0xe8d1c4d9, 0x984f1e3a, 0x17edb7eb, 0xa38d7641, 0xe724a8ba, 
    0x67c5d7d4, 0xd4a69418, 0xeedf4d74, 0xee3300ff, 0xb6271c32, 0xf4953497, 0x23f95ffc, 0xd27874d9, 0x815034ce, 0xf549b654, 0x5eec1538, 0x2d36d603, 
    0x00c861b4, 0xd56e07a6, 0x9b571ee5, 0x5c390a09, 0x7de14757, 0x0acbdb44, 0x51568613, 0x26bec68f, 0x25195795, 0x98897e26, 0x3b695550, 0x6d3fd023, 
    0xff76846f, 0x663f1800, 0xdeee7e9d, 0x78126f15, 0x6df5d56a, 0xf717e12a, 0x4b37c680, 0x818e3e10, 0xbc02f5b8, 0xc3d6c347, 0x896ef04c, 0x1be6a06c, 
    0x74041318, 0xfa316ac4, 0x03fed957, 0xf4d5b7bf, 0x85dcb3af, 0xc754199a, 0x65c86d2c, 0xa81f0423, 0xf1b1f935, 0x97f15723, 0x1800ffc2, 0xb74d9f78, 
    0x9a8c1abb, 0xb83da16e, 0x285cb9b3, 0x1e536245, 0x75fe0ac5, 0x9dd3f5f5, 0xc78e523a, 0xa17914e6, 0xd367912a, 0x00ffb33f, 0x1d2f8bc3, 0x54ebaaf8, 
    0xd22321bf, 0x00c584e5, 0xbb8ff703, 0xdab04f76, 0x6c7fcb2b, 0xe3c3192f, 0xdd428bcf, 0x3e6912a4, 0x85a2851f, 0x784ec373, 0xddbf0772, 0x3eea0afa, 
    0xd364fc13, 0x1c7c1e3e, 0xe2a5f8b6, 0xa75e9a42, 0x6c736973, 0x6ebb15ba, 0x8a552e79, 0x9ed7dba1, 0xbe06f6a4, 0xd4231e50, 0x8da9577c, 0x6a3cfe46, 
    0xb1bcf337, 0x39d98edb, 0x6f877ddc, 0xa79dafa9, 0x548c980a, 0xb58996a6, 0x0e7a3df3, 0x3bd57c10, 0xe597fa69, 0xc547b2b8, 0x039b6fd2, 0x7b205725, 
    0x9f291d7e, 0xe7bfd26b, 0xbe00ff89, 0x5ffcce2b, 0xcf4a6de2, 0xd3b7b85e, 0x166f31de, 0x58f22413, 0xff8d3575, 0x88770900, 0x5fa4e73f, 0xa1227da5, 
    0x0c47b514, 0xb3bb52b1, 0x98fdb13e, 0xbfd621ad, 0x5c141f64, 0x9e27465e, 0x228d5cdf, 0x1183ea76, 0xe8fb0f0c, 0x14fc7fd6, 0xd345e682, 0x1b6e097c, 
    0xe44a3b11, 0xe50fe07b, 0x6fb2bf5a, 0x8cc799fc, 0x9f3cec3f, 0x7fb142fb, 0xea3fa4e0, 0x00ff033c, 0x65a57f6d, 0x9446fe53, 0xcee41f7d, 0x8bfc2f6c, 
    0x00ff7aab, 0x870ff391, 0x488e23ed, 0x55895167, 0xb77eb0ad, 0x00fd9f0b, 0xb5b7667e, 0x5b6b429d, 0xc010af5b, 0x48a23a44, 0x8cb124b0, 0x20174204, 
    0x66fec40f, 0xf52ff4b1, 0xbd00ff51, 0xa500ff6b, 0x1fc47f15, 0xff5e26f2, 0xffd2d700, 0xd0af2500, 0x3e74a463, 0x3a135f4e, 0x03fe856f, 0x6ef1a2d1, 
    0xf66befb4, 0xacec8687, 0x68bb6366, 0x9d1f3838, 0x28a6297d, 0x50408140, 0xc60000a0, 0xc3677805, 0xbc1af96f, 0x5f00ff41, 0xdd6bd19f, 0xf8d5ee34, 
    0x72526d9e, 0xec4dd4c6, 0x2a32ddcf, 0x93068e70, 0x7a75744b, 0xfce72457, 0x686badf4, 0x010466d5, 0xc99a839c, 0xf39fbad2, 0xff165be9, 0x117feb00, 
    0x1ed9915c, 0xa1af278e, 0x260d5fee, 0x8f61d878, 0x3f96af18, 0x6d580b6b, 0x44dd693f, 0xf7e21a47, 0x759eb34f, 0x857d8623, 0xfe2af933, 0xc327f555, 
    0x7c95bf7e, 0xff5cfbc1, 0xb7cec900, 0xbf6c82fd, 0x88fb6af6, 0xf223ba7f, 0x8cefbfba, 0x681faff2, 0x33e891f6, 0x9284985c, 0xde34a624, 0x93711572, 
    0xe18aef81, 0x6ff633f4, 0x5e97f80c, 0xb7529b5f, 0x468a6381, 0xd09122e4, 0x885ff40a, 0xdc2af25f, 0xc1d700ff, 0x6bd000ff, 0xf9bf74ce, 0xff2d9e26, 
    0xfc69ae00, 0x772d87ab, 0xe6eaa9a7, 0x70244d7a, 0x5df0099e, 0x4ff48b8f, 0x5b422ded, 0x5692ba96, 0x0090630c, 0xd67bfa7b, 0x2afc00ff, 0x7afe134d, 
    0xe1df7f5e, 0x217c14fe, 0x369100ff, 0xffecfa2f, 0x5abbd200, 0x133cdbf5, 0x0000d9ff, 
};
};
} //namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(DeinterlaceNode, "Deinterlace", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Filter#Video#Enhance")