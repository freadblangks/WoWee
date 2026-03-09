#include <cstddef>
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>

namespace wowee::rendering {

// Compile-only probe for AMD FSR3 frame generation interfaces.
// This does not dispatch runtime frame generation yet; it ensures the
// expected SDK headers and core types are available for future integration.
bool amdFsr3FramegenCompileProbe() {
    FfxFsr3UpscalerContext fsr3Context{};
    FfxFrameInterpolationContext fiContext{};
    FfxOpticalflowContext ofContext{};
    FfxInterface backend{};
    FfxDimensions2D renderSize{};

    static_assert(FFX_FSR3UPSCALER_VERSION_MAJOR >= 3, "Expected FSR3 upscaler v3+");
    static_assert(FFX_FRAMEINTERPOLATION_VERSION_MAJOR >= 1, "Expected frame interpolation v1+");
    static_assert(FFX_OPTICALFLOW_VERSION_MAJOR >= 1, "Expected optical flow v1+");

    (void)fsr3Context;
    (void)fiContext;
    (void)ofContext;
    (void)backend;
    (void)renderSize;
    return true;
}

}  // namespace wowee::rendering
