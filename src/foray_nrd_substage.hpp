#pragma once
#include "include_ndr.hpp"
#include <core/foray_shadermodule.hpp>
#include <stages/foray_renderstage.hpp>
#include <util/foray_pipelinelayout.hpp>

namespace foray::nrdd {
    class NrdDenoiser;

    class NrdSubStage : public stages::RenderStage
    {
      public:
        void Init(NrdDenoiser* nrdDenoiser, const nrd::PipelineDesc& desc);

        void RecordFrame(VkCommandBuffer cmdBuffer, base::FrameRenderInfo& renderInfo, const nrd::DispatchDesc& desc);

        virtual void Destroy() override;

        inline virtual ~NrdSubStage() { Destroy(); }

      protected:
        NrdDenoiser*      mNrdDenoiser  = nullptr;
        nrd::PipelineDesc mPipelineDesc = {};

        core::ShaderModule    mShader;
        VkDescriptorSet       mDescriptorSets[INFLIGHT_FRAME_COUNT];
        VkDescriptorSetLayout mDescriptorSetLayout;
        util::PipelineLayout  mPipelineLayout;

        void InitShader();
        void CreateDescriptorSet();
        void CreatePipelineLayout();
    };

}  // namespace foray::nrdd
