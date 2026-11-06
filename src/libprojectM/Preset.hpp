#pragma once

#include <Audio/FrameAudioData.hpp>

#include <Renderer/RenderContext.hpp>
#include <Renderer/Texture.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace libprojectM {

/**
 * @brief Outcome of loading a preset, mainly of its warp and composite shaders.
 */
struct PresetLoadResult
{
    enum class Status
    {
        Ok,             //!< Loaded without errors.
        TranslateError, //!< A preset shader failed to preprocess or translate to GLSL.
        CompileError,   //!< A preset shader failed to compile.
        LinkError,      //!< A preset shader program failed to link.
        OtherError      //!< Anything else, e.g. the file could not be read or parsed.
    };

    static constexpr uint32_t NoStage = 0;        //!< Not a shader stage.
    static constexpr uint32_t WarpStage = 1;      //!< The warp shader. Also a bit mask value.
    static constexpr uint32_t CompositeStage = 2; //!< The composite shader. Also a bit mask value.

    Status status{Status::Ok}; //!< The first failure, or Ok.
    uint32_t stage{NoStage};   //!< The stage of the first failure, NoStage if Ok or not shader-related.
    std::string message;       //!< The error message of the first failure.
    uint32_t shaderStages{};   //!< Bit mask of stages that run a translated preset shader.
    uint32_t cachedStages{};   //!< Bit mask of shaderStages whose GLSL came from the shader cache.
};

class Preset
{
public:
    virtual ~Preset() = default;

    /**
     * @brief Returns the result of loading the preset and its shaders.
     * Valid after Initialize().
     * @return The load result.
     */
    virtual auto LoadResult() const -> PresetLoadResult
    {
        return {};
    }

    /**
     * @brief Initializes additional preset resources.
     * @param renderContext A render context with the initial data.
     */
    virtual void Initialize(const Renderer::RenderContext& renderContext) = 0;

    /**
     * @brief Renders the preset into the current framebuffer.
     * @param audioData Audio data to be used by the preset.
     * @param renderContext The current render context data.
     */
    virtual void RenderFrame(const libprojectM::Audio::FrameAudioData& audioData,
                             const Renderer::RenderContext& renderContext) = 0;

    /**
     * @brief Returns a pointer to the current rendering output texture.
     * This pointer (the actual texture) may change from frame to frame, so this pointer should not be stored for use
     * across multiple frames. Instead, a new pointer should be requested whenever needed.
     * @return A pointer to the current output texture of the preset.
     */
    virtual auto OutputTexture() const -> std::shared_ptr<Renderer::Texture> = 0;

    /**
     * @brief Draws an initial image into the preset, e.g. the last frame of a previous preset.
     * It's not guaranteed a preset supports using a previously rendered image. If not
     * supported, this call is simply a no-op.
     * @param image The texture to be copied as the initial preset image.
     * @param renderContext The current render context data.
     */
    virtual void DrawInitialImage(const std::shared_ptr<Renderer::Texture>& image,
                                  const Renderer::RenderContext& renderContext) = 0;

    /**
     * @brief Bind the preset's internal framebuffer.
     * This framebuffer contains the image passed on to the next frame, onto which warp effects etc.
     * are then applied. Has no effect if the framebuffer isn't initialized yet, e.g. before drawing
     * the first frame.
     *
     * Can be used to draw anything on top of the preset's image, "burning in" additional shapes,
     * images or text. Depending on the preset, it's not guaranteed though that the image actually
     * is used in the next frame, or completely painted over. That said, the effect varies between
     * presets.
     */
    virtual void BindFramebuffer() = 0;

    inline void SetFilename(const std::string& filename)
    {
        m_filename = filename;
    }

    inline auto Filename() const -> const std::string&
    {
        return m_filename;
    }

private:
    std::string m_filename;
};

} // namespace libprojectM
