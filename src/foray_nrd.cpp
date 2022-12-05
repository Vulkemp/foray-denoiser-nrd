#include "foray_nrd.hpp"
#include "foray_nrd_helpers.hpp"
#include <nameof/nameof.hpp>

namespace foray::nrdd {
    void NrdDenoiser::Init(core::Context* context, const stages::DenoiserConfig& config)
    {
        Destroy();
        mContext            = context;
        mLibraryDescription = nrd::GetLibraryDesc();

        nrd::MethodDesc methods[] = {nrd::MethodDesc{.method = mActiveMethod, .fullResolutionWidth = 1280, .fullResolutionHeight = 720}};

        nrd::DenoiserCreationDesc cDesc{
            .requestedMethods   = methods,
            .requestedMethodNum = std::size(methods),
        };

        AssertNrdResult(nrd::CreateDenoiser(cDesc, mDenoiser));

        mDenoiserDescription = nrd::GetDenoiserDesc(*mDenoiser);

        // TODO: Evaluate and setup denoiser description
        InitSamplers();
        InitPermanentImages();
        InitTransientImages();
        InitDescriptorPool();
        InitSubStages();

        mImageLookup[nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST] = config.PrimaryOutput;
        mImageLookup[nrd::ResourceType::IN_NORMAL_ROUGHNESS]       = config.GBufferOutputs[(size_t)stages::GBufferStage::EOutput::Normal];
        mImageLookup[nrd::ResourceType::IN_VIEWZ]                  = config.GBufferOutputs[(size_t)stages::GBufferStage::EOutput::LinearZ];
        mImageLookup[nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST]  = config.PrimaryInput;
        mImageLookup[nrd::ResourceType::IN_MV]                     = config.GBufferOutputs[(size_t)stages::GBufferStage::EOutput::Motion];

    }  // namespace foray::nrdd

    void NrdDenoiser::InitSamplers()
    {
        mSamplers.resize(mDenoiserDescription.staticSamplerNum);
        for(int32_t i = 0; i < mSamplers.size(); i++)
        {
            const nrd::StaticSamplerDesc& desc = mDenoiserDescription.staticSamplers[i];

            VkFilter            minMag;
            VkSamplerMipmapMode mipMode;
            switch(desc.sampler)
            {
                case nrd::Sampler::NEAREST_CLAMP:
                case nrd::Sampler::NEAREST_MIRRORED_REPEAT:
                    minMag  = VkFilter::VK_FILTER_NEAREST;
                    mipMode = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
                    break;
                case nrd::Sampler::LINEAR_CLAMP:
                case nrd::Sampler::LINEAR_MIRRORED_REPEAT:
                    minMag  = VkFilter::VK_FILTER_LINEAR;
                    mipMode = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR;
                    break;
                default:
                    Exception::Throw("Unhandled Sampler Enum Value");
                    break;
            };

            VkSamplerAddressMode addressMode;
            switch(desc.sampler)
            {
                case nrd::Sampler::NEAREST_CLAMP:
                case nrd::Sampler::LINEAR_CLAMP:
                    addressMode = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;  // TODO: Perhaps this is clamp to border
                    break;
                case nrd::Sampler::NEAREST_MIRRORED_REPEAT:
                case nrd::Sampler::LINEAR_MIRRORED_REPEAT:
                    addressMode = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                    break;
                default:
                    Exception::Throw("Unhandled Sampler Enum Value");
                    break;
            };


            VkSamplerCreateInfo samplerCi{
                .sType                   = VkStructureType::VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter               = minMag,
                .minFilter               = minMag,
                .mipmapMode              = mipMode,
                .addressModeU            = addressMode,
                .addressModeV            = addressMode,
                .addressModeW            = addressMode,
                .mipLodBias              = 1.0,
                .anisotropyEnable        = VK_FALSE,
                .maxAnisotropy           = 0,
                .compareEnable           = VK_FALSE,
                .compareOp               = {},
                .minLod                  = 0,
                .maxLod                  = VK_LOD_CLAMP_NONE,
                .borderColor             = {},
                .unnormalizedCoordinates = VK_FALSE,
            };

            mSamplers[i].Ref.Init(mContext->SamplerCol, samplerCi);
            mSamplers[i].RegIndex = desc.registerIndex;
        }
    }
    void NrdDenoiser::InitPermanentImages()
    {
        mPermanentImages.resize(mDenoiserDescription.permanentPoolSize);
        for(int32_t i = 0; i < mPermanentImages.size(); i++)
        {
            const nrd::TextureDesc& desc = mDenoiserDescription.permanentPool[i];

            VkImageUsageFlags usage = VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                      | VkImageUsageFlagBits::VK_IMAGE_USAGE_SAMPLED_BIT | VkImageUsageFlagBits::VK_IMAGE_USAGE_STORAGE_BIT;

            VkExtent2D size{desc.width, desc.height};

            VkFormat format = sTranslateFormat(desc.format);

            logger()->info("Permanent #{}: nrd::Format {}, VkFormat {}", i, NAMEOF_ENUM(desc.format), NAMEOF_ENUM(format));

            core::ManagedImage::CreateInfo ci(usage, format, size, fmt::format("NRD Perm #{}", i));
            Assert(desc.mipNum == 1, "Support only for single mip level images atm (due to requiring multiple VkImageViews)");
            // ci.ImageCI.mipLevels                       = desc.mipNum;
            // ci.ImageViewCI.subresourceRange.levelCount = desc.mipNum;

            mPermanentImages[i] = std::make_unique<core::ManagedImage>();
            mPermanentImages[i]->Create(mContext, ci);
        }
    }
    void NrdDenoiser::InitTransientImages()
    {
        mTransientImages.resize(mDenoiserDescription.transientPoolSize);
        for(int32_t i = 0; i < mTransientImages.size(); i++)
        {
            const nrd::TextureDesc& desc = mDenoiserDescription.transientPool[i];

            VkImageUsageFlags usage = VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                      | VkImageUsageFlagBits::VK_IMAGE_USAGE_SAMPLED_BIT | VkImageUsageFlagBits::VK_IMAGE_USAGE_STORAGE_BIT;

            VkExtent2D size{desc.width, desc.height};

            VkFormat format = sTranslateFormat(desc.format);

            logger()->info("Transient #{}: nrd::Format {}, VkFormat {}", i, NAMEOF_ENUM(desc.format), NAMEOF_ENUM(format));

            core::ManagedImage::CreateInfo ci(usage, format, size, fmt::format("NRD Transient #{}", i));
            ci.ImageCI.mipLevels                       = desc.mipNum;
            ci.ImageViewCI.subresourceRange.levelCount = desc.mipNum;

            mTransientImages[i] = std::make_unique<core::ManagedImage>();
            mTransientImages[i]->Create(mContext, ci);
        }
    }
    VkFormat NrdDenoiser::sTranslateFormat(nrd::Format format)
    {
        switch(format)
        {
            case nrd::Format::R8_UNORM:
                return VkFormat::VK_FORMAT_R8_UNORM;
            case nrd::Format::R8_SNORM:
                return VkFormat::VK_FORMAT_R8_SNORM;
            case nrd::Format::R8_UINT:
                return VkFormat::VK_FORMAT_R8_UINT;
            case nrd::Format::R8_SINT:
                return VkFormat::VK_FORMAT_R8_SINT;
            case nrd::Format::RG8_UNORM:
                return VkFormat::VK_FORMAT_R8G8_UNORM;
            case nrd::Format::RG8_SNORM:
                return VkFormat::VK_FORMAT_R8G8_SNORM;
            case nrd::Format::RG8_UINT:
                return VkFormat::VK_FORMAT_R8G8_UINT;
            case nrd::Format::RG8_SINT:
                return VkFormat::VK_FORMAT_R8G8_SINT;
            case nrd::Format::RGBA8_UNORM:
                return VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
            case nrd::Format::RGBA8_SNORM:
                return VkFormat::VK_FORMAT_R8G8B8A8_SNORM;
            case nrd::Format::RGBA8_UINT:
                return VkFormat::VK_FORMAT_R8G8B8A8_UINT;
            case nrd::Format::RGBA8_SINT:
                return VkFormat::VK_FORMAT_R8G8B8A8_SINT;
            case nrd::Format::RGBA8_SRGB:
                return VkFormat::VK_FORMAT_R8G8B8A8_SRGB;
            case nrd::Format::R16_UNORM:
                return VkFormat::VK_FORMAT_R16_UNORM;
            case nrd::Format::R16_SNORM:
                return VkFormat::VK_FORMAT_R16_SNORM;
            case nrd::Format::R16_UINT:
                return VkFormat::VK_FORMAT_R16_UINT;
            case nrd::Format::R16_SINT:
                return VkFormat::VK_FORMAT_R16_SINT;
            case nrd::Format::R16_SFLOAT:
                return VkFormat::VK_FORMAT_R16_SFLOAT;
            case nrd::Format::RG16_UNORM:
                return VkFormat::VK_FORMAT_R16G16_UNORM;
            case nrd::Format::RG16_SNORM:
                return VkFormat::VK_FORMAT_R16G16_SNORM;
            case nrd::Format::RG16_UINT:
                return VkFormat::VK_FORMAT_R16G16_UINT;
            case nrd::Format::RG16_SINT:
                return VkFormat::VK_FORMAT_R16G16_SINT;
            case nrd::Format::RG16_SFLOAT:
                return VkFormat::VK_FORMAT_R16G16_SFLOAT;
            case nrd::Format::RGBA16_UNORM:
                return VkFormat::VK_FORMAT_R16G16B16A16_UNORM;
            case nrd::Format::RGBA16_SNORM:
                return VkFormat::VK_FORMAT_R16G16B16A16_SNORM;
            case nrd::Format::RGBA16_UINT:
                return VkFormat::VK_FORMAT_R16G16B16A16_UINT;
            case nrd::Format::RGBA16_SINT:
                return VkFormat::VK_FORMAT_R16G16B16A16_SINT;
            case nrd::Format::RGBA16_SFLOAT:
                return VkFormat::VK_FORMAT_R16G16B16A16_SFLOAT;
            case nrd::Format::R32_UINT:
                return VkFormat::VK_FORMAT_R32_UINT;
            case nrd::Format::R32_SINT:
                return VkFormat::VK_FORMAT_R32_SINT;
            case nrd::Format::R32_SFLOAT:
                return VkFormat::VK_FORMAT_R32_SFLOAT;
            case nrd::Format::RG32_UINT:
                return VkFormat::VK_FORMAT_R32G32_UINT;
            case nrd::Format::RG32_SINT:
                return VkFormat::VK_FORMAT_R32G32_SINT;
            case nrd::Format::RG32_SFLOAT:
                return VkFormat::VK_FORMAT_R32G32_SFLOAT;
            case nrd::Format::RGB32_UINT:
                return VkFormat::VK_FORMAT_R32G32B32_UINT;
            case nrd::Format::RGB32_SINT:
                return VkFormat::VK_FORMAT_R32G32B32_SINT;
            case nrd::Format::RGB32_SFLOAT:
                return VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
            case nrd::Format::RGBA32_UINT:
                return VkFormat::VK_FORMAT_R32G32B32A32_UINT;
            case nrd::Format::RGBA32_SINT:
                return VkFormat::VK_FORMAT_R32G32B32A32_SINT;
            case nrd::Format::RGBA32_SFLOAT:
                return VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
            // case nrd::Format::R10_G10_B10_A2_UNORM:
            // case nrd::Format::R10_G10_B10_A2_UINT:
            // case nrd::Format::R11_G11_B10_UFLOAT:
            // case nrd::Format::R9_G9_B9_E5_UFLOAT:
            // return VkFormat::VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
            default:
                Exception::Throw("Unhandled Format Enum Value");
        }
    }
    void NrdDenoiser::InitDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 4> poolSizes({
            VkDescriptorPoolSize{VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, mDenoiserDescription.descriptorSetDesc.constantBufferMaxNum},
            VkDescriptorPoolSize{VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mDenoiserDescription.descriptorSetDesc.staticSamplerMaxNum},
            VkDescriptorPoolSize{VkDescriptorType::VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, mDenoiserDescription.descriptorSetDesc.textureMaxNum},
            VkDescriptorPoolSize{VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, mDenoiserDescription.descriptorSetDesc.storageTextureMaxNum},
        });

        VkDescriptorPoolCreateInfo poolCi{.sType         = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                          .flags         = 0,
                                          .maxSets       = mDenoiserDescription.descriptorSetDesc.setMaxNum * INFLIGHT_FRAME_COUNT,
                                          .poolSizeCount = poolSizes.size(),
                                          .pPoolSizes    = poolSizes.data()};

        AssertVkResult(vkCreateDescriptorPool(mContext->Device(), &poolCi, nullptr, &mDescriptorPool));
    }

    void NrdDenoiser::InitSubStages()
    {
        mSubStages.resize(mDenoiserDescription.pipelineNum);
        for(int32_t i = 0; i < mSubStages.size(); i++)
        {
            const nrd::PipelineDesc& desc = mDenoiserDescription.pipelines[i];

            mSubStages[i] = std::make_unique<NrdSubStage>();
            mSubStages[i]->Init(this, desc, mDenoiserDescription.constantBufferDesc.maxDataSize);
        }
    }

    void NrdDenoiser::RecordFrame(VkCommandBuffer cmdBuffer, base::FrameRenderInfo& renderInfo)
    {
        if(mFirstFrameRendered)
        {
            for(std::unique_ptr<core::ManagedImage>& image : mPermanentImages)
            {
                renderInfo.GetImageLayoutCache().Set(*image, VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
        mFirstFrameRendered = true;

        const nrd::DispatchDesc* dispatchDescriptions = nullptr;

        uint32_t dispatchCount = 0;

        AssertNrdResult(nrd::GetComputeDispatches(*mDenoiser, mSettings, dispatchDescriptions, dispatchCount));

        for(int32_t i = 0; i < dispatchCount; i++)
        {
            const nrd::DispatchDesc& dispatchDesc = dispatchDescriptions[i];

            logger()->info("Dispatch \"{}\" [{}]", dispatchDesc.name, dispatchDesc.pipelineIndex);

            mSubStages[dispatchDesc.pipelineIndex]->RecordFrame(cmdBuffer, renderInfo, dispatchDesc);
        }

        // TODO: Transfer all permanent images to shader read only
    }
    VkFormat NrdDenoiser::ResolveImage(nrd::ResourceType type, uint32_t index, VkImage& outImage, VkImageView& outView)
    {
        switch(type)
        {
            case nrd::ResourceType::PERMANENT_POOL: {
                core::ManagedImage& image = *mPermanentImages[index];
                outImage                  = image.GetImage();
                outView                   = image.GetImageView();
                return image.GetFormat();
                break;
            }
            case nrd::ResourceType::TRANSIENT_POOL: {
                core::ManagedImage& image = *mTransientImages[index];
                outImage                  = image.GetImage();
                outView                   = image.GetImageView();
                return image.GetFormat();
                break;
            }
            default: {
                if(!mImageLookup.contains(type))
                {
                    FORAY_THROWFMT("Missing Resource {}!", NAMEOF_ENUM(type))
                }
                core::ManagedImage& image = *mImageLookup[type];
                outImage                  = image.GetImage();
                outView                   = image.GetImageView();
                return image.GetFormat();
                break;
            }
        }
    }
    std::string NrdDenoiser::GetUILabel()
    {
        return fmt::format("NRD v{}.{}.{} \"{}\"", mLibraryDescription.versionMajor, mLibraryDescription.versionMinor, mLibraryDescription.versionBuild,
                           nrd::GetMethodString(mActiveMethod));
    }
    void NrdDenoiser::DisplayImguiConfiguration() {}
    void NrdDenoiser::IgnoreHistoryNextFrame() {}
    void NrdDenoiser::Resize(const VkExtent2D& size) {}
    void NrdDenoiser::Destroy()
    {
        if(!!mDenoiser)
        {
            nrd::DestroyDenoiser(*mDenoiser);
            mDenoiser = nullptr;
        }
        mSubStages.clear();
        mSamplers.clear();
        mPermanentImages.clear();
        mTransientImages.clear();
        if(!!mDescriptorPool)
        {
            vkDestroyDescriptorPool(mContext->Device(), mDescriptorPool, nullptr);
            mDescriptorPool = nullptr;
        }
        mFirstFrameRendered = false;
    }
}  // namespace foray::nrdd
