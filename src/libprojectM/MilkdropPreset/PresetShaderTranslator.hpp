#pragma once

#include <map>
#include <set>
#include <string>

namespace libprojectM {
namespace MilkdropPreset {

class PresetFileParser;

/**
 * @brief GL-free translation of preset warp/composite shaders from HLSL to GLSL.
 *
 * MilkdropShader uses these functions at runtime. The offline pre-translation tool uses the same
 * functions, so its shader cache keys and GLSL output match the runtime's exactly.
 */
namespace PresetShaderTranslator {

/**
 * @brief Preset shader stage.
 */
enum class Stage
{
    Warp,     //!< Warp shader.
    Composite //!< Composite shader.
};

/**
 * @brief GLSL dialect the preset shaders are translated to.
 */
enum class Target
{
    Glsl330,   //!< Desktop OpenGL, GLSL 3.30.
    GlslEs300, //!< OpenGL ES 3, GLSL ES 3.00.
    GlslEs100  //!< OpenGL ES 2 (ENABLE_GLES2), GLSL ES 1.00. The warp shader has no motion vector output.
};

/**
 * @brief Shader versions and code of both stages, as read from a preset file.
 */
struct StageSources
{
    int warpVersion{2};         //!< PSVERSION or PSVERSION_WARP. 0 means no warp shader.
    int compositeVersion{2};    //!< PSVERSION or PSVERSION_COMP. 0 means no composite shader.
    std::string warpCode;       //!< The warp shader code, empty if none.
    std::string compositeCode;  //!< The composite shader code, empty if none.
};

/**
 * @brief Samplers referenced by a preset shader.
 */
struct SamplerReferences
{
    std::set<std::string> names; //!< Sampler names without the "sampler_" prefix. Always contains "main".
    int blurLevel{0};            //!< Highest blur level used, 0 to 3.
};

/**
 * @brief HLSL declarations of the samplers and texsize uniforms inserted before translation.
 */
struct Declarations
{
    std::set<std::string> samplers; //!< Sampler declarations, one per line.
    std::set<std::string> texSizes; //!< Texsize uniform declarations, one per line.
};

/**
 * @brief The kind of a sampler name referenced in a preset shader.
 */
enum class SamplerKind
{
    Main,    //!< The previous frame image.
    Blur,    //!< A blur texture, see SamplerClass::blurLevel.
    Random,  //!< A random texture slot, see SamplerClass::randomSlot.
    Texture  //!< A named texture (noise or user texture).
};

/**
 * @brief Classification of a sampler name.
 */
struct SamplerClass
{
    SamplerKind kind{SamplerKind::Texture};
    int blurLevel{0};   //!< 1-3 for SamplerKind::Blur.
    int randomSlot{-1}; //!< 0-15 for SamplerKind::Random.
};

/**
 * @brief Returns the translation target of this build.
 */
auto RuntimeTarget() -> Target;

/**
 * @brief Returns the short target name: "glsl330", "es300" or "es100".
 */
auto TargetName(Target target) -> const char*;

/**
 * @brief Parses a short target name.
 * @return True if the name is valid.
 */
auto TargetFromName(const std::string& name, Target& target) -> bool;

/**
 * @brief Returns the short stage name: "warp" or "comp".
 */
auto StageName(Stage stage) -> const char*;

/**
 * @brief Returns the composite shader used if a preset has none or its own fails.
 */
auto DefaultCompositeShader() -> const std::string&;

/**
 * @brief Reads the shader versions and code of both stages from a parsed preset file.
 */
auto ReadStageSources(PresetFileParser& parsedFile) -> StageSources;

/**
 * @brief Finds the samplers and the blur level referenced in preset shader code.
 * @param code The preset shader code.
 */
auto GetReferencedSamplers(const std::string& code) -> SamplerReferences;

/**
 * @brief Raises the blur level and adds the blur sampler names it requires.
 * @param references The sampler references to update.
 * @param blurLevel The required blur level, 1 to 3.
 */
void RaiseBlurLevel(SamplerReferences& references, int blurLevel);

/**
 * @brief Classifies a sampler name like the runtime texture lookup does.
 * @param samplerName The sampler name without the "sampler_" prefix.
 */
auto ClassifySampler(const std::string& samplerName) -> SamplerClass;

/**
 * @brief Returns a texture name without its two-letter wrap/filter prefix, e.g. "fw_".
 */
auto UnqualifiedTextureName(const std::string& samplerName) -> std::string;

/**
 * @brief Returns the declarations the runtime generates if every referenced texture is available.
 *
 * Shaders of one preset share random texture slots: pass the same map for the warp shader first,
 * then the composite shader, as the runtime loads them in this order.
 *
 * @param references The referenced samplers. The blur level is raised as at runtime.
 * @param randomSlotNames Sampler name per random texture slot already in use by the preset.
 */
auto OfflineDeclarations(SamplerReferences& references, std::map<int, std::string>& randomSlotNames) -> Declarations;

/**
 * @brief Turns preset shader code into a complete HLSL program.
 *
 * Removes sampler_state blocks, replaces shader_body with the PS() entry point and prepends the
 * preset shader header and defines.
 *
 * @throws Renderer::ShaderException if the code has no shader_body or no braces.
 */
auto PreprocessPresetShader(Stage stage, Target target, const std::string& code) -> std::string;

/**
 * @brief Translates a preprocessed preset shader to GLSL.
 * @throws Renderer::ShaderException if the translation fails.
 */
auto TranslateToGlsl(Stage stage, Target target, const std::string& preprocessedCode,
                     const Declarations& declarations) -> std::string;

/**
 * @brief Returns a hash of the translator sources, changing with every translator change.
 */
auto TranslatorVersion() -> const char*;

/**
 * @brief Returns the shader cache key of a preset shader.
 *
 * The key is the SHA-256 (hex) of the translator version, target, stage, declarations and the
 * preprocessed code. Any change to these yields a different key.
 */
auto CacheKey(Stage stage, Target target, const std::string& preprocessedCode,
              const Declarations& declarations) -> std::string;

/**
 * @brief Returns the cache file name for a key: "<key>.glsl".
 */
auto CacheFileName(const std::string& key) -> std::string;

/**
 * @brief Reads a cached GLSL shader.
 * @param cacheDirectory The cache directory.
 * @param key The cache key.
 * @param glsl Receives the file contents.
 * @return True if the file exists and was read.
 */
auto ReadCachedShader(const std::string& cacheDirectory, const std::string& key, std::string& glsl) -> bool;

} // namespace PresetShaderTranslator
} // namespace MilkdropPreset
} // namespace libprojectM
