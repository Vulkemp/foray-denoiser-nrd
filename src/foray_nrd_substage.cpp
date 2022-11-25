#include "foray_nrd_substage.hpp"
#include "foray_nrd.hpp"

namespace foray::nrdd {
    void NrdSubStage::Init(NrdDenoiser* nrdDenoiser, const nrd::PipelineDesc& desc)
    {
        Destroy();
        mContext      = nrdDenoiser->mContext;
        mNrdDenoiser  = nrdDenoiser;
        mPipelineDesc = desc;

        InitShader();
        CreateDescriptorSet();
        CreatePipelineLayout();
    }
    void NrdSubStage::InitShader()
    {
        const nrd::ComputeShader& shader = mPipelineDesc.computeShaderSPIRV;
        mShader.LoadFromBinary(mContext, reinterpret_cast<const uint32_t*>(shader.bytecode), shader.size);
    }
    void NrdSubStage::CreateDescriptorSet()
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        if(mPipelineDesc.hasConstantData)
        {
            bindings.push_back(VkDescriptorSetLayoutBinding{.binding         = NrdDenoiser::BIND_OFFSET_CONSTANTBUF,
                                                            .descriptorType  = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                            .descriptorCount = 1U,
                                                            .stageFlags      = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
        }

        uint32_t offsetStorage = NrdDenoiser::BIND_OFFSET_STORAGEIMG;
        uint32_t offsetReadTex = NrdDenoiser::BIND_OFFSET_TEXIMG;
        for(int32_t i = 0; i < mPipelineDesc.descriptorRangeNum; i++)
        {
            const nrd::DescriptorRangeDesc& desc = mPipelineDesc.descriptorRanges[i];
            VkDescriptorType                type;
            uint32_t*                       offset = nullptr;
            switch(desc.descriptorType)
            {
                case nrd::DescriptorType::TEXTURE:
                    type   = VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    offset = &offsetReadTex;
                    break;
                case nrd::DescriptorType::STORAGE_TEXTURE:
                    type   = VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    offset = &offsetStorage;
                    break;
                default:
                    Exception::Throw("Unhandled DescriptorType Enum Value");
            }

            const nrd::DescriptorRangeDesc& range = mPipelineDesc.descriptorRanges[i];
            for(int32_t j = 0; j < range.descriptorNum; j++)
            {
                bindings.push_back(VkDescriptorSetLayoutBinding{
                    .binding = *offset, .descriptorType = type, .descriptorCount = 1U, .stageFlags = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
                *offset = *offset + 1;
            }
        }

        VkDescriptorSetLayoutCreateInfo layoutCi{
            .sType = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = (uint32_t)bindings.size(), .pBindings = bindings.data()};

        AssertVkResult(vkCreateDescriptorSetLayout(mContext->Device(), &layoutCi, nullptr, &mDescriptorSetLayout));

        VkDescriptorSetLayout layouts[INFLIGHT_FRAME_COUNT];

        for(int32_t i = 0; i < INFLIGHT_FRAME_COUNT; i++)
        {
            layouts[i] = mDescriptorSetLayout;
        }

        VkDescriptorSetAllocateInfo allocInfo{.sType              = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                              .descriptorPool     = mNrdDenoiser->mDescriptorPool,
                                              .descriptorSetCount = INFLIGHT_FRAME_COUNT,
                                              .pSetLayouts        = layouts};

        AssertVkResult(vkAllocateDescriptorSets(mContext->Device(), &allocInfo, mDescriptorSets));

        if(mPipelineDesc.hasConstantData)
        {
            VkDescriptorBufferInfo bufInfo = mNrdDenoiser->mConstantsBuffer.GetVkDescriptorInfo();

            std::vector<VkWriteDescriptorSet> descriptorWrites(INFLIGHT_FRAME_COUNT);

            for(int32_t i = 0; i < INFLIGHT_FRAME_COUNT; i++)
            {
                descriptorWrites[i] = VkWriteDescriptorSet{
                    .sType           = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = mDescriptorSets[i],
                    .dstBinding      = NrdDenoiser::BIND_OFFSET_CONSTANTBUF,
                    .descriptorCount = 1u,
                    .descriptorType  = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pImageInfo      = nullptr,
                    .pBufferInfo     = &bufInfo,
                };
            }

            vkUpdateDescriptorSets(mContext->Device(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
    }
    void NrdSubStage::CreatePipelineLayout()
    {
        mPipelineLayout.AddDescriptorSetLayout(mDescriptorSetLayout);
        mPipelineLayout.Build(mContext);
    }

    void NrdSubStage::RecordFrame(VkCommandBuffer cmdBuffer, base::FrameRenderInfo& renderInfo, const nrd::DispatchDesc& desc)
    {
        {  // Update Descriptor Set
            std::vector<VkWriteDescriptorSet> descriptorWrites;



            vkUpdateDescriptorSets(mContext->Device(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
        {  // Pipeline Barrier

            VkMemoryBarrier2 memBarrier{
                .sType         = VkStructureType::VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            };

            VkDependencyInfo depInfo{.sType = VkStructureType::VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1U, .pMemoryBarriers = &memBarrier};

            vkCmdPipelineBarrier2(cmdBuffer, &depInfo);
        }
        {  // Dispatch
        }
    }
    void NrdSubStage::Destroy()
    {
        mShader.Destroy();
        mPipelineLayout.Destroy();
        if(!!mDescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(mContext->Device(), mDescriptorSetLayout, nullptr);
            mDescriptorSetLayout = nullptr;
        }
    }

}  // namespace foray::nrdd
