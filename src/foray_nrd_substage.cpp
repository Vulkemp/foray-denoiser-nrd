#include "foray_nrd_substage.hpp"
#include "foray_nrd.hpp"

namespace foray::nrdd {
    void NrdSubStage::Init(NrdDenoiser* nrdDenoiser, const nrd::PipelineDesc& desc, VkDeviceSize constantsBufferSize)
    {
        Destroy();
        mContext      = nrdDenoiser->mContext;
        mNrdDenoiser  = nrdDenoiser;
        mPipelineDesc = desc;

        if(mPipelineDesc.hasConstantData)
        {
            VkBufferUsageFlags usage = VkBufferUsageFlagBits::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            core::ManagedBuffer::CreateInfo ci(usage, constantsBufferSize, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, "NRD Constants Buffer");
            mConstantsBuffer.Create(mContext, ci);
        }

        InitShader();
        CreateDescriptorSet();
        CreatePipelineLayout();


        VkComputePipelineCreateInfo pipelineCi{
            .sType  = VkStructureType::VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage  = VkPipelineShaderStageCreateInfo{.sType  = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                      .stage  = VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT,
                                                      .module = mShader,
                                                      .pName  = mPipelineDesc.shaderEntryPointName},
            .layout = mPipelineLayout,
        };

        vkCreateComputePipelines(mContext->Device(), nullptr, 1U, &pipelineCi, nullptr, &mPipeline);
    }
    void NrdSubStage::InitShader()
    {
        const nrd::ComputeShader& shader = mPipelineDesc.computeShaderSPIRV;
        mShader.LoadFromBinary(mContext, reinterpret_cast<const uint32_t*>(shader.bytecode), shader.size);
    }
    void NrdSubStage::CreateDescriptorSet()
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        std::vector<VkSampler> samplers;
        samplers.reserve(mNrdDenoiser->mSamplers.size());

        uint32_t offset = NrdDenoiser::BIND_OFFSET_SAMPLERS;
        for(const NrdDenoiser::Sampler& sampler : mNrdDenoiser->mSamplers)
        {
            samplers.push_back(sampler.Ref);
            bindings.push_back(VkDescriptorSetLayoutBinding{.binding            = offset,
                                                            .descriptorType     = VkDescriptorType::VK_DESCRIPTOR_TYPE_SAMPLER,
                                                            .descriptorCount    = 1U,
                                                            .stageFlags         = VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT,
                                                            .pImmutableSamplers = &samplers.back()});
            offset++;
        }


        if(mPipelineDesc.hasConstantData)
        {
            bindings.push_back(VkDescriptorSetLayoutBinding{.binding         = NrdDenoiser::BIND_OFFSET_CONSTANTBUF,
                                                            .descriptorType  = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                            .descriptorCount = 1U,
                                                            .stageFlags      = VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT});
        }

        uint32_t offsetStorage = NrdDenoiser::BIND_OFFSET_STORAGEIMG;
        uint32_t offsetReadTex = NrdDenoiser::BIND_OFFSET_TEXIMG;
        for(int32_t i = 0; i < mPipelineDesc.descriptorRangeNum; i++)
        {
            const nrd::DescriptorRangeDesc& desc = mPipelineDesc.descriptorRanges[i];
            VkDescriptorType                type;
            uint32_t*                       offset     = nullptr;
            VkSampler*                      samplerArr = nullptr;
            switch(desc.descriptorType)
            {
                case nrd::DescriptorType::TEXTURE:
                    type       = VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    offset     = &offsetReadTex;
                    samplerArr = &samplers[desc.baseRegisterIndex];
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
                bindings.push_back(VkDescriptorSetLayoutBinding{.binding            = *offset,
                                                                .descriptorType     = type,
                                                                .descriptorCount    = 1U,
                                                                .stageFlags         = VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT,
                                                                .pImmutableSamplers = samplerArr});
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
            // TODO use one constantsbuffer per dispatch
            VkDescriptorBufferInfo bufInfo = mConstantsBuffer.GetVkDescriptorInfo();

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
        uint32_t inFlightIdx = renderInfo.GetFrameNumber() % INFLIGHT_FRAME_COUNT;

        std::vector<VkImageMemoryBarrier2> imageBarriers;
        {  // Update Descriptor Set
            std::vector<VkWriteDescriptorSet> descriptorWrites;

            uint32_t  offsetStorage = NrdDenoiser::BIND_OFFSET_STORAGEIMG;
            uint32_t  offsetReadTex = NrdDenoiser::BIND_OFFSET_TEXIMG;
            uint32_t* offset        = nullptr;
            for(size_t i = 0; i < desc.resourceNum; i++)
            {
                const nrd::Resource& resource = desc.resources[i];

                VkDescriptorType      descriptorType;
                VkDescriptorImageInfo imageInfo;

                VkImage     image     = nullptr;
                VkImageView imageView = nullptr;
                mNrdDenoiser->ResolveImage(resource.type, resource.indexInPool, image, imageView);

                switch(resource.stateNeeded)
                {
                    case nrd::DescriptorType::TEXTURE: {
                        descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        imageInfo      = VkDescriptorImageInfo{
                                 .sampler     = nullptr,
                                 .imageView   = imageView,
                                 .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        };
                        core::ImageLayoutCache::Barrier2 barrier{.SrcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                                 .SrcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                                                                 .DstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                                 .DstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                                                                 .NewLayout     = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                        imageBarriers.push_back(renderInfo.GetImageLayoutCache().MakeBarrier(image, barrier));
                        offset = &offsetReadTex;
                        break;
                    }
                    case nrd::DescriptorType::STORAGE_TEXTURE: {
                        descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                        imageInfo      = VkDescriptorImageInfo{
                                 .sampler     = nullptr,
                                 .imageView   = imageView,
                                 .imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_GENERAL,
                        };
                        core::ImageLayoutCache::Barrier2 barrier{.SrcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                                 .SrcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                                                                 .DstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                                 .DstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                                                                 .NewLayout     = VkImageLayout::VK_IMAGE_LAYOUT_GENERAL};
                        imageBarriers.push_back(renderInfo.GetImageLayoutCache().MakeBarrier(image, barrier));
                        offset = &offsetStorage;
                        break;
                    }
                    default:
                        Exception::Throw("Unhandled DescriptorType Enum Value");
                }

                descriptorWrites.push_back(VkWriteDescriptorSet{
                    .sType           = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = mDescriptorSets[inFlightIdx],
                    .dstBinding      = *offset,
                    .dstArrayElement = 0U,
                    .descriptorCount = 1U,
                    .descriptorType  = descriptorType,
                    .pImageInfo      = &imageInfo,
                });
                *offset = *offset + 1;
            }


            vkUpdateDescriptorSets(mContext->Device(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
        if(!!desc.constantBufferData && desc.constantBufferDataSize > 0)
        {  // Upload constant data

            mConstantsBuffer.StageSection(renderInfo.GetFrameNumber(), desc.constantBufferData, 0U, desc.constantBufferDataSize);
            mConstantsBuffer.CmdCopyToDevice(renderInfo.GetFrameNumber(), cmdBuffer);
            mConstantsBuffer.CmdPrepareForRead(cmdBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
        }
        {  // Pipeline Barrier


            VkMemoryBarrier2 memBarrier{
                .sType         = VkStructureType::VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            };

            VkDependencyInfo depInfo{.sType                   = VkStructureType::VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                     .memoryBarrierCount      = 1U,
                                     .pMemoryBarriers         = &memBarrier,
                                     .imageMemoryBarrierCount = (uint32_t)imageBarriers.size(),
                                     .pImageMemoryBarriers    = imageBarriers.data()};

            vkCmdPipelineBarrier2(cmdBuffer, &depInfo);
        }
        {  // Bind descriptor & pipeline
            vkCmdBindPipeline(cmdBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline);
            vkCmdBindDescriptorSets(cmdBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 0U, 1U, mDescriptorSets + inFlightIdx, 0, nullptr);
        }
        {  // Dispatch
            vkCmdDispatch(cmdBuffer, desc.gridWidth, desc.gridHeight, 1U);
        }
    }
    void NrdSubStage::Destroy()
    {
        if(!!mPipeline)
        {
            vkDestroyPipeline(mContext->Device(), mPipeline, nullptr);
            mPipeline = nullptr;
        }
        mShader.Destroy();
        mPipelineLayout.Destroy();
        if(!!mDescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(mContext->Device(), mDescriptorSetLayout, nullptr);
            mDescriptorSetLayout = nullptr;
        }
        mConstantsBuffer.Destroy();
    }

}  // namespace foray::nrdd
