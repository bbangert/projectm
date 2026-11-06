/**
 * @file MilkdropShader.hpp
 * @brief Holds a warp or composite shader of Milkdrop presets.
 *
 * This class wraps the conversion from HLSL shader code to GLSL and also manages the
 * drawing.
 */
#pragma once

#include "BlurTexture.hpp"

#include <Renderer/Shader.hpp>
#include <Renderer/TextureManager.hpp>
#ifdef USE_GLES2
#include <Renderer/TextureStateCopies.hpp>
#endif

#include <array>
#include <set>

namespace libprojectM {
namespace MilkdropPreset {

class PerFrameContext;
class PresetState;

/**
 * @brief Holds a warp or composite shader of Milkdrop presets.
 * Also does the required shader translation from HLSL to GLSL using hlslparser.
 */
class MilkdropShader
{
public:
    enum class ShaderType
    {
        WarpShader,     //!< Warp shader
        CompositeShader //!< Composite shader
    };

    /**
     * constructor.
     * @param type The preset shader type.
     */
    explicit MilkdropShader(ShaderType type);

    /**
     * @brief Translates and compiles the shader code.
     * @param presetShaderCode The preset shader code.
     */
    void LoadCode(const std::string& presetShaderCode);

    /**
     * @brief Loads the required texture references into the shader.
     * Binds the underlying shader program.
     * @param presetState The preset state to pull the values and textures from.
     */
    void LoadTexturesAndCompile(PresetState& presetState);

    /**
     * @brief Loads all required shader variables into the uniforms.
     * Binds the underlying shader program.
     * @param presetState The preset state to pull the values from.
     * @param perFrameContext The per-frame context with dynamically calculated values.
     * @param mainSamplerOverride The sampler the caller binds to texture unit 0 (the first main
     *                            texture reference) after this call, if any.
     */
    void LoadVariables(const PresetState& presetState, const PerFrameContext& perFrameContext,
                       const Renderer::Sampler* mainSamplerOverride = nullptr);

    /**
     * @brief Returns the contained shader.
     * @return The shader program wrapper.
     */
    auto Shader() -> Renderer::Shader&;

private:
    /**
     * @brief Prepares the shader code to be translated into GLSL.
     * @param program The program code to work on.
     */
    void PreprocessPresetShader(std::string& program);

    /**
     * @brief Searches for sampler references in the program and stores them in m_samplerNames.
     * @param program The program code to work on.
     */
    void GetReferencedSamplers(const std::string& program);

    /**
     * @brief Translates the HLSL shader into GLSL.
     * @param presetState The preset state to pull the blur textures from.
     * @param program The shader to transpile.
     */
    void TranspileHLSLShader(const PresetState& presetState, std::string& program);

    /**
     * @brief Updates the requested blur level if higher than before.
     * Also adds the required samplers.
     * @param requestedLevel The requested blur level.
     */
    void UpdateMaxBlurLevel(BlurTexture::BlurLevel requestedLevel);

#ifdef USE_GLES2
    /**
     * @brief Binds all texture descriptors when sampler objects are unavailable.
     *
     * Textures referenced with several wrap/filter modes get a texture copy per extra mode.
     *
     * @param presetState The preset state to pull the blur textures and shader cache from.
     * @param mainSamplerOverride The sampler the caller binds to texture unit 0 afterwards, if any.
     */
    void BindTexturesWithoutSamplerObjects(const PresetState& presetState, const Renderer::Sampler* mainSamplerOverride);
#endif

    ShaderType m_type{ShaderType::WarpShader}; //!< Type of this shader.
    std::string m_fragmentShaderCode;          //!< The original preset fragment shader code.
    std::string m_preprocessedCode;            //!< The preprocessed preset shader code.

    std::set<std::string> m_samplerNames;                                        //!< All sampler names referenced in the shader code.
    std::vector<Renderer::TextureSamplerDescriptor> m_mainTextureDescriptors;              //!< Descriptors for all main texture references.
    std::vector<Renderer::TextureSamplerDescriptor> m_textureSamplerDescriptors;           //!< Descriptors of all referenced samplers in the shader code.
    BlurTexture::BlurLevel m_maxBlurLevelRequired{BlurTexture::BlurLevel::None}; //!< Max blur level of main texture required by this shader.
#ifdef USE_GLES2
    Renderer::TextureStateCopies m_textureStateCopies; //!< Per-mode texture copies when sampler objects are unavailable.
#endif

    std::array<float, 4> m_randValues{};               //!< Random values which don't change every frame.
    std::array<glm::vec3, 20> m_randTranslation{};     //!< Random translation vectors which don't change every frame.
    std::array<glm::vec3, 20> m_randRotationCenters{}; //!< Random rotation center vectors which don't change every frame.
    std::array<glm::vec3, 20> m_randRotationSpeeds{};  //!< Random rotation speeds which don't change every frame.

    Renderer::Shader m_shader;
};

} // namespace MilkdropPreset
} // namespace libprojectM
