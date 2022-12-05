#pragma once
#include "foray_nrd_substage.hpp"
#include "include_ndr.hpp"
#include <core/foray_managedimage.hpp>
#include <core/foray_samplercollection.hpp>
#include <stages/foray_denoiserstage.hpp>
#include <util/foray_dualbuffer.hpp>
#include <util/foray_historyimage.hpp>

namespace foray::nrdd {


    class NrdDenoiser : public stages::DenoiserStage
    {
        friend NrdSubStage;

      public:
        inline static constexpr uint32_t BIND_OFFSET_SAMPLERS    = 100U;
        inline static constexpr uint32_t BIND_OFFSET_TEXIMG      = 200U;
        inline static constexpr uint32_t BIND_OFFSET_CONSTANTBUF = 300U;
        inline static constexpr uint32_t BIND_OFFSET_STORAGEIMG  = 400U;

        virtual void        Init(core::Context* context, const stages::DenoiserConfig& config) override;
        virtual void        RecordFrame(VkCommandBuffer cmdBuffer, base::FrameRenderInfo& renderInfo) override;
        virtual std::string GetUILabel() override;
        virtual void        DisplayImguiConfiguration() override;
        virtual void        IgnoreHistoryNextFrame() override;

        virtual void Resize(const VkExtent2D& size) override;

        virtual void Destroy() override;

        virtual VkFormat ResolveImage(nrd::ResourceType type, uint32_t index, VkImage& outImage, VkImageView& outView);

      protected:
        void InitSamplers();
        void InitPermanentImages();
        void InitTransientImages();
        void InitDescriptorPool();
        void InitSubStages();

        static VkFormat sTranslateFormat(nrd::Format format);

        nrd::LibraryDesc    mLibraryDescription  = {};
        nrd::Method         mActiveMethod        = nrd::Method::REBLUR_DIFFUSE;
        nrd::Denoiser*      mDenoiser            = nullptr;
        nrd::DenoiserDesc   mDenoiserDescription = {};
        nrd::CommonSettings mSettings            = {};

        bench::DeviceBenchmark* mBenchmark = nullptr;

        std::vector<std::unique_ptr<NrdSubStage>>        mSubStages;
        std::vector<std::unique_ptr<core::ManagedImage>> mPermanentImages;
        std::vector<std::unique_ptr<core::ManagedImage>> mTransientImages;

        struct Sampler
        {
            core::SamplerReference Ref;
            uint32_t               RegIndex = 0;
        };

        std::vector<Sampler> mSamplers;

        std::unordered_map<nrd::ResourceType, core::ManagedImage*> mImageLookup;

        VkDescriptorPool mDescriptorPool = nullptr;

        bool mFirstFrameRendered = false;
    };
}  // namespace foray::nrdd
