#include <BluePrint.h>
#include <Node.h>
#include <Pin.h>
#include <UI.h>
#include <imgui_json.h>
#include <ImVulkanShader.h>
#include <SplitMerge_vulkan.h>

#define NODE_VERSION    0x01000000

namespace BluePrint
{
struct MatSplitterNode final : Node
{
    BP_NODE_WITH_NAME(MatSplitterNode, "Image Splitter", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, NodeType::External, NodeStyle::Default, "Media")
    MatSplitterNode(BP* blueprint): Node(blueprint) {
        m_Name = "Image Splitter";
        m_HasCustomLayout = true;
        m_Skippable = true; 
    }

    ~MatSplitterNode()
    {
        if (m_splitter) { delete m_splitter; m_splitter = nullptr; }
    }

    void Reset(Context& context) override
    {
        Node::Reset(context);
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint, bool threading = false) override
    {
        if (entryPoint.m_ID == m_IReset.m_ID)
        {
            Reset(context);
            return m_OReset;
        }
        auto mat_in = context.GetPinValue<ImGui::ImMat>(m_MatIn);
        if (!mat_in.empty())
        {
            if (!m_Enabled)
            {
                m_MatA.SetValue(mat_in);
                return m_Exit;
            }
            int gpu = mat_in.device == IM_DD_VULKAN ? mat_in.device_number : ImGui::get_default_gpu_index();
            if (!m_splitter)
            {
                m_splitter = new ImGui::SplitMerge_vulkan(gpu);
                if (!m_splitter)
                    return {};
            }
            if (m_splitter)
            {
                std::vector<ImGui::ImMat> split_mats;
                m_splitter->split(mat_in, split_mats);
                for (int i = 0; i < split_mats.size(); i++)
                {
                    split_mats[i].flags |= IM_MAT_FLAGS_IMAGE_FRAME;
                    switch (i)
                    {
                        case 0: m_MatA.SetValue(split_mats[i]); break;
                        case 1: m_MatB.SetValue(split_mats[i]); break;
                        case 2: m_MatC.SetValue(split_mats[i]); break;
                        case 3: m_MatD.SetValue(split_mats[i]); break;
                        default: break;
                    }
                }
            }
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

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }
    Pin* GetAutoLinkInputFlowPin() override { return &m_Enter; }
    Pin* GetAutoLinkOutputFlowPin() override { return &m_Exit; }
    vector<Pin*> GetAutoLinkInputDataPin() override { return {&m_MatIn}; }
    vector<Pin*> GetAutoLinkOutputDataPin() override { return {&m_MatA}; }

    FlowPin   m_Enter   = { this, "Enter" };
    FlowPin   m_IReset  = { this, "Reset In" };
    FlowPin   m_Exit    = { this, "Exit" };
    FlowPin   m_OReset  = { this, "Reset Out" };
    MatPin    m_MatIn   = { this, "In" };
    MatPin    m_MatA    = { this, "OA" };
    MatPin    m_MatB    = { this, "OB" };
    MatPin    m_MatC    = { this, "OC" };
    MatPin    m_MatD    = { this, "OD" };

    Pin* m_InputPins[3] = { &m_Enter, &m_IReset, &m_MatIn };
    Pin* m_OutputPins[6] = { &m_Exit, &m_OReset, &m_MatA, &m_MatB, &m_MatC, &m_MatD };

private:
    ImDataType m_mat_data_type {IM_DT_UNDEFINED};
    ImGui::SplitMerge_vulkan * m_splitter {nullptr};
};
} // namespace BluePrint

BP_NODE_DYNAMIC_WITH_NAME(MatSplitterNode, "Image Splitter", "CodeWin", NODE_VERSION, VERSION_BLUEPRINT_API, BluePrint::NodeType::External, BluePrint::NodeStyle::Default, "Media")
