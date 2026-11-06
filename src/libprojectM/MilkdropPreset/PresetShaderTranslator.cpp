#include "PresetShaderTranslator.hpp"

#include "PresetFileParser.hpp"

#include <MilkdropStaticShaders.hpp>
#include <PresetShaderTranslatorVersion.hpp>

#include <Renderer/Shader.hpp>
#include <Renderer/TextureSamplerDescriptor.hpp>

#include <Engine.h>
#include <GLSLGenerator.h>
#include <HLSLParser.h>
#include <Logging.hpp>
#include <Utils.hpp>

#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace libprojectM {
namespace MilkdropPreset {
namespace PresetShaderTranslator {

namespace {

const std::regex samplerDeclaration("sampler(2D|3D|)(\\s+|\\().*");
const std::regex texSizeDeclaration("float4\\s+texsize_.*");

auto StageTypeString(Stage stage) -> std::string
{
    return stage == Stage::Warp ? "warp" : "composite";
}

auto IsRandomTextureName(const std::string& lowerCaseName) -> bool
{
    return lowerCaseName.length() >= 6 && lowerCaseName.substr(0, 4) == "rand" &&
           std::isdigit(static_cast<unsigned char>(lowerCaseName.at(4))) &&
           std::isdigit(static_cast<unsigned char>(lowerCaseName.at(5)));
}

auto GeneratorVersion(Target target) -> M4::GLSLGenerator::Version
{
    switch (target)
    {
        case Target::GlslEs100:
            return M4::GLSLGenerator::Version_100_ES;
        case Target::GlslEs300:
            return M4::GLSLGenerator::Version_300_ES;
        case Target::Glsl330:
        default:
            return M4::GLSLGenerator::Version_330;
    }
}

auto GeneratorFlags(Target target) -> unsigned int
{
    unsigned int flags = M4::GLSLGenerator::Flag_AlternateNanPropagation;
    if (target == Target::GlslEs100)
    {
        // projectM never mipmaps, so ignoring LODs is exact. The noise volumes are 2D atlases (MilkdropNoise).
        flags |= M4::GLSLGenerator::Flag_NoTextureLod |
                 M4::GLSLGenerator::Flag_NoDerivatives |
                 M4::GLSLGenerator::Flag_Texture3DAtlas;
    }
    return flags;
}

auto TranslateError(Stage stage, const std::string& message) -> Renderer::ShaderException
{
    return Renderer::ShaderException("[MilkdropShader] Error translating HLSL " + StageTypeString(stage) + " shader: " + message);
}

} // namespace

auto RuntimeTarget() -> Target
{
#if defined(USE_GLES2)
    return Target::GlslEs100;
#elif defined(USE_GLES)
    return Target::GlslEs300;
#else
    return Target::Glsl330;
#endif
}

auto TargetName(Target target) -> const char*
{
    switch (target)
    {
        case Target::GlslEs100:
            return "es100";
        case Target::GlslEs300:
            return "es300";
        case Target::Glsl330:
        default:
            return "glsl330";
    }
}

auto TargetFromName(const std::string& name, Target& target) -> bool
{
    for (auto candidate : {Target::Glsl330, Target::GlslEs300, Target::GlslEs100})
    {
        if (name == TargetName(candidate))
        {
            target = candidate;
            return true;
        }
    }
    return false;
}

auto StageName(Stage stage) -> const char*
{
    return stage == Stage::Warp ? "warp" : "comp";
}

auto DefaultCompositeShader() -> const std::string&
{
    static const std::string defaultCompositeShader = "shader_body\n{\nret = tex2D(sampler_main, uv).xyz;\n}";
    return defaultCompositeShader;
}

auto ReadStageSources(PresetFileParser& parsedFile) -> StageSources
{
    StageSources sources;

    int const presetVersion = parsedFile.GetInt("MILKDROP_PRESET_VERSION", 100);
    if (presetVersion < 200)
    {
        // Milkdrop 1.x did not use shaders.
        sources.warpVersion = 0;
        sources.compositeVersion = 0;
    }
    else if (presetVersion == 200)
    {
        // Milkdrop 2.0 only supported a single shader language level variable.
        sources.warpVersion = parsedFile.GetInt("PSVERSION", sources.warpVersion);
        sources.compositeVersion = parsedFile.GetInt("PSVERSION", sources.compositeVersion);
    }
    else
    {
        sources.warpVersion = parsedFile.GetInt("PSVERSION_WARP", sources.warpVersion);
        sources.compositeVersion = parsedFile.GetInt("PSVERSION_COMP", sources.compositeVersion);
    }

    sources.warpCode = parsedFile.GetCode("warp_");
    sources.compositeCode = parsedFile.GetCode("comp_");

    return sources;
}

auto GetReferencedSamplers(const std::string& code) -> SamplerReferences
{
    SamplerReferences references;
    auto& names = references.names;

    // "main" should always be present.
    names.insert("main");

    // Strip comments so that commented-out sampler/texsize declarations are not matched.
    std::string const stripped = Utils::StripComments(code);

    // Search for sampler usage
    auto found = stripped.find("sampler_", 0);
    while (found != std::string::npos)
    {
        found += 8;
        size_t const end = stripped.find_first_of(" ;,\n\r)", found);

        if (end != std::string::npos)
        {
            std::string const sampler = stripped.substr(static_cast<int>(found), static_cast<int>(end - found));
            // Skip "sampler_state", as it's a reserved word and not a sampler.
            if (sampler != "state")
            {
                names.insert(sampler);
            }
        }

        found = stripped.find("sampler_", found);
    }

    // Also search for texsize usage, some presets don't reference the sampler.
    found = stripped.find("texsize_", 0);
    while (found != std::string::npos)
    {
        found += 8;
        size_t const end = stripped.find_first_of(" ;,.\n\r)", found);

        if (end != std::string::npos)
        {
            std::string const sampler = stripped.substr(static_cast<int>(found), static_cast<int>(end - found));
            names.insert(sampler);
        }

        found = stripped.find("texsize_", found);
    }

    {
        // Remove duplicate mentions or "randXX" names, keeping the long forms only (first one will determine the actual texture loaded).
        auto samplerName = names.begin();
        while (samplerName != names.end())
        {
            std::string lowerCaseName = Utils::ToLower(*samplerName);
            if (lowerCaseName.length() == 6 && IsRandomTextureName(lowerCaseName))
            {
                auto additionalName = samplerName;
                additionalName++;
                if (additionalName != names.end())
                {
                    std::string addLowerCaseName = Utils::ToLower(*additionalName);
                    if (addLowerCaseName.length() > 7 &&
                        addLowerCaseName.substr(0, 6) == lowerCaseName &&
                        addLowerCaseName[6] == '_')
                    {
                        samplerName = names.erase(samplerName);
                    }
                }
            }
            samplerName++;
        }
    }

    if (stripped.find("GetBlur3") != std::string::npos)
    {
        RaiseBlurLevel(references, 3);
    }
    else if (stripped.find("GetBlur2") != std::string::npos)
    {
        RaiseBlurLevel(references, 2);
    }
    else if (stripped.find("GetBlur1") != std::string::npos)
    {
        RaiseBlurLevel(references, 1);
    }

    return references;
}

void RaiseBlurLevel(SamplerReferences& references, int blurLevel)
{
    if (references.blurLevel >= blurLevel)
    {
        return;
    }

    references.blurLevel = blurLevel;

    references.names.insert("blur1");
    if (blurLevel >= 2)
    {
        references.names.insert("blur2");
    }
    if (blurLevel >= 3)
    {
        references.names.insert("blur3");
    }
}

auto ClassifySampler(const std::string& samplerName) -> SamplerClass
{
    SamplerClass samplerClass;

    std::string const lowerCaseName = Utils::ToLower(UnqualifiedTextureName(samplerName));

    // The "main" and "blurX" textures are preset-specific and are not managed by TextureManager.
    if (lowerCaseName == "main")
    {
        samplerClass.kind = SamplerKind::Main;
        return samplerClass;
    }

    // A few presets directly use the (undocumented) sampler name.
    if (lowerCaseName == "blur1" || lowerCaseName == "blur2" || lowerCaseName == "blur3")
    {
        samplerClass.kind = SamplerKind::Blur;
        samplerClass.blurLevel = lowerCaseName.at(4) - '0';
        return samplerClass;
    }

    // Random textures need special treatment.
    if (IsRandomTextureName(lowerCaseName))
    {
        int const randomSlot = std::stoi(lowerCaseName.substr(4, 2));
        if (randomSlot <= 15)
        {
            samplerClass.kind = SamplerKind::Random;
            samplerClass.randomSlot = randomSlot;
            return samplerClass;
        }

        // Fall through if slot number is out of range and treat as normal texture.
    }

    samplerClass.kind = SamplerKind::Texture;
    return samplerClass;
}

auto UnqualifiedTextureName(const std::string& samplerName) -> std::string
{
    if (samplerName.length() <= 3 || samplerName.at(2) != '_')
    {
        return samplerName;
    }
    return samplerName.substr(3);
}

auto OfflineDeclarations(SamplerReferences& references, std::map<int, std::string>& randomSlotNames) -> Declarations
{
    using Renderer::TextureSamplerDescriptor;

    Declarations declarations;

    // Iterates the live set like MilkdropShader does; RaiseBlurLevel may insert while iterating.
    for (auto it = references.names.begin(); it != references.names.end(); ++it)
    {
        std::string const name = *it;
        auto const samplerClass = ClassifySampler(name);
        switch (samplerClass.kind)
        {
            case SamplerKind::Main:
                declarations.samplers.insert(TextureSamplerDescriptor::SamplerDeclaration(name, false));
                declarations.texSizes.insert(TextureSamplerDescriptor::TexSizeDeclaration("main"));
                break;

            case SamplerKind::Blur:
                RaiseBlurLevel(references, samplerClass.blurLevel);
                break;

            case SamplerKind::Random: {
                // The first shader requesting a slot determines the sampler name of the slot.
                auto const slot = randomSlotNames.emplace(samplerClass.randomSlot, name).first;
                declarations.samplers.insert(TextureSamplerDescriptor::SamplerDeclaration(slot->second, false));
                declarations.texSizes.insert(TextureSamplerDescriptor::TexSizeDeclaration(slot->second));
                break;
            }

            case SamplerKind::Texture: {
                std::string const unqualifiedName = UnqualifiedTextureName(name);
                bool const volume = unqualifiedName == "noisevol_lq" || unqualifiedName == "noisevol_hq";
                declarations.samplers.insert(TextureSamplerDescriptor::SamplerDeclaration(name, volume));
                declarations.texSizes.insert(TextureSamplerDescriptor::TexSizeDeclaration(unqualifiedName));
                break;
            }
        }
    }

    // Blur textures have no texsize uniform.
    for (int level = 1; level <= references.blurLevel; level++)
    {
        declarations.samplers.insert(TextureSamplerDescriptor::SamplerDeclaration("blur" + std::to_string(level), false));
    }

    return declarations;
}

auto PreprocessPresetShader(Stage stage, Target target, const std::string& code) -> std::string
{
    std::string const shaderTypeString = StageTypeString(stage);
    std::string program = code;

    if (program.length() <= 0)
    {
        throw Renderer::ShaderException("[MilkdropShader] Preset " + shaderTypeString + " shader is declared, but empty.");
    }

    size_t found;

    // Find "sampler_state" overrides and remove them first, as they're not supported by GLSL.
    // The logic isn't totally fool-proof, but should work in general.
    // Use a comment-stripped copy for searching so commented-out sampler_state blocks are skipped.
    // StripComments preserves string length, so positions map 1:1 to the original.
    std::string stripped = Utils::StripComments(program);
    found = stripped.find("sampler_state");
    while (found != std::string::npos)
    {
        // Now go backwards and find the assignment
        found = stripped.rfind('=', found);
        auto startPos = found;

        // Find closing brace and semicolon
        found = stripped.find('}', found);
        found = stripped.find(';', found);

        if (found != std::string::npos)
        {
            stripped.replace(startPos, found - startPos, "");
        }
        else
        {
            // No closing brace and semicolon.
            break;
        }

        found = stripped.find("sampler_state");
    }

    // ESSL 1.00 has a single fragment output, so the warp shader writes no motion vector u/v coords.
    bool const warpWritesMotionVectors = target != Target::GlslEs100;

    // replace shader_body with entry point function
    // Use the stripped copy so a commented-out shader_body is not matched.
    found = stripped.find("shader_body");
    if (found != std::string::npos)
    {
        if (stage == Stage::Warp)
        {
            if (warpWritesMotionVectors)
            {
                program.replace(int(found), 11, R"(
void PS(float4 _vDiffuse : COLOR,
        float4 _uv : TEXCOORD0,
        float2 _rad_ang : TEXCOORD1,
        out float4 _return_value : COLOR0,
        out float4 _mv_tex_coords : COLOR1)
)");
            }
            else
            {
                program.replace(int(found), 11, R"(
void PS(float4 _vDiffuse : COLOR,
        float4 _uv : TEXCOORD0,
        float2 _rad_ang : TEXCOORD1,
        out float4 _return_value : COLOR0)
)");
            }
        }
        else
        {
            program.replace(int(found), 11, R"(
void PS(float4 _vDiffuse : COLOR,
        float2 _uv : TEXCOORD0,
        float2 _rad_ang : TEXCOORD1,
        out float4 _return_value : COLOR)
)");
        }
    }
    else
    {
        LOG_DEBUG("[MilkdropShader] Failed " + shaderTypeString + " shader code:\n" + program);
        throw Renderer::ShaderException("[MilkdropShader] Preset " + shaderTypeString + " shader is missing \"shader_body\" entry point.");
    }

    // replace the "{" immediately following shader_body with some variable declarations
    found = program.find('{', found);
    if (found != std::string::npos)
    {
        std::string progMain = "{\nfloat3 ret = 0;\n";
        if (stage == Stage::Warp && warpWritesMotionVectors)
        {
            progMain.append("_mv_tex_coords.xy = _uv.xy;\n");
        }
        program.replace(int(found), 1, progMain);
    }
    else
    {
        LOG_DEBUG("[MilkdropShader] Failed " + shaderTypeString + " shader code:\n" + program);
        throw Renderer::ShaderException("[MilkdropShader] Preset " + shaderTypeString + " shader has no opening braces.");
    }

    // replace "}" with return statement (this can probably be optimized for the GLSL conversion...)
    found = program.rfind('}');
    if (found != std::string::npos)
    {
        program.replace(int(found), 1, "_return_value = float4(ret.xyz, 1.0);\n"
                                       "}\n");
    }
    else
    {
        LOG_DEBUG("[MilkdropShader] Failed " + shaderTypeString + " shader code:\n" + program);
        throw Renderer::ShaderException("[MilkdropShader] Preset " + shaderTypeString + " shader has no closing brace.");
    }

    // Find matching closing brace and cut off excess text after shader's main function
    int bracesOpen = 1;
    size_t pos = found + 1;
    for (; pos < program.length() && bracesOpen > 0; ++pos)
    {
        switch (program.at(pos))
        {
            case '/':
                // Skip line comments until EoL to prevent false counting
                if (pos < program.length() - 1 && program.at(pos + 1) == '/')
                {
                    for (; pos < program.length(); ++pos)
                    {
                        if (program.at(pos) == '\n')
                        {
                            break;
                        }
                    }
                }
                // Skip block comments to prevent false counting
                else if (pos < program.length() - 1 && program.at(pos + 1) == '*')
                {
                    pos += 2;
                    for (; pos < program.length() - 1; ++pos)
                    {
                        if (program.at(pos) == '*' && program.at(pos + 1) == '/')
                        {
                            ++pos; // skip past '/'
                            break;
                        }
                    }
                }
                continue;

            case '{':
                bracesOpen++;
                continue;

            case '}':
                bracesOpen--;
        }
    }

    if (pos < program.length() - 1)
    {
        program.resize(pos);
    }

    std::string fullSource; //!< Full shader source before translation, includes all uniforms etc.

    // First copy the generic "header" into the shader. Includes uniforms and some defines
    // to unwrap the packed 4-element uniforms into single values.
    fullSource.append(MilkdropStaticShaders::Get()->GetPresetShaderHeader());

    if (stage == Stage::Warp)
    {
        fullSource.append("#define rad _rad_ang.x\n"
                          "#define ang _rad_ang.y\n"
                          "#define uv _uv.xy\n"
                          "#define uv_orig _uv.zw\n");
    }
    else
    {
        fullSource.append("#define rad _rad_ang.x\n"
                          "#define ang _rad_ang.y\n"
                          "#define uv _uv.xy\n"
                          "#define uv_orig _uv.xy\n"
                          "#define hue_shader _vDiffuse.xyz\n");
    }

    fullSource.append(program);

    return fullSource;
}

auto TranslateToGlsl(Stage stage, Target target, const std::string& preprocessedCode,
                     const Declarations& declarations) -> std::string
{
    std::string const shaderTypeString = StageTypeString(stage);

    M4::GLSLGenerator generator;
    M4::Allocator allocator;

    M4::HLSLTree tree(&allocator);
    M4::HLSLParser parser(&allocator, &tree);

    // hlslparser only logs errors, keep the first one for the exception message.
    M4::Log_ClearFirstError();
    auto firstError = []() { return std::string(M4::Log_GetFirstError()); };

    // Preprocess define macros
    std::string sourcePreprocessed;
    if (!parser.ApplyPreprocessor("", preprocessedCode.c_str(), preprocessedCode.size(), sourcePreprocessed))
    {
        LOG_DEBUG("[MilkdropShader] Failed " + shaderTypeString + " shader code:\n" + preprocessedCode);
        throw TranslateError(stage, "Preprocessing failed: " + firstError());
    }

    // Remove previous shader declarations
    // ToDo: Quite some presets declare a sampler_state{} struct to change the wrap mode.
    //       The below code causes invalid syntax as it leaves part of the expression.
    //       Leaving it in causes HLSLParser to add "sampler_XYZ = sampler2D( <unknown expression> );"
    //       in the main() function, which is also bad...
    Utils::EraseRegexMatches(sourcePreprocessed, "sampler", samplerDeclaration);

    // Remove previous texsize declarations
    Utils::EraseRegexMatches(sourcePreprocessed, "float4", texSizeDeclaration);

    // Now insert the sampler and texsize declarations on top.
    for (const auto& texSizeDecl : declarations.texSizes)
    {
        sourcePreprocessed.insert(0, texSizeDecl);
    }
    for (const auto& samplerDecl : declarations.samplers)
    {
        sourcePreprocessed.insert(0, samplerDecl);
    }

    // Transpile from HLSL (aka preset shader aka DirectX shader) to GLSL (aka OpenGL shader lang)
    // First, parse HLSL into a tree
    if (!parser.Parse("", sourcePreprocessed.c_str(), sourcePreprocessed.size()))
    {
        LOG_DEBUG("[MilkdropShader] Failed " + shaderTypeString + " shader code:\n" + preprocessedCode);
        LOG_DEBUG("[MilkdropShader] Failed preprocessed " + shaderTypeString + " shader code:\n" + sourcePreprocessed);
        throw TranslateError(stage, "HLSL parsing failed: " + firstError());
    }

    // Then generate GLSL from the resulting parser tree
    if (!generator.Generate(&tree, M4::GLSLGenerator::Target_FragmentShader,
                            GeneratorVersion(target),
                            "PS", M4::GLSLGenerator::Options(GeneratorFlags(target))))
    {
        LOG_DEBUG("[MilkdropShader] Failed " + shaderTypeString + " shader code:\n" + preprocessedCode);
        LOG_DEBUG("[MilkdropShader] Failed preprocessed " + shaderTypeString + " shader code:\n" + sourcePreprocessed);
        throw TranslateError(stage, "GLSL generating failed: " + firstError() + "\nSource:\n" + sourcePreprocessed);
    }

    LOG_TRACE("[MilkdropShader] Transpiled GLSL " + shaderTypeString + " shader code:\n" + std::string(generator.GetResult()));

    return generator.GetResult();
}

auto TranslatorVersion() -> const char*
{
    return PROJECTM_PRESET_SHADER_TRANSLATOR_VERSION;
}

auto CacheKey(Stage stage, Target target, const std::string& preprocessedCode,
              const Declarations& declarations) -> std::string
{
    // Each field is length- or count-prefixed, so different inputs can't produce the same text.
    std::string keyInput = "projectM preset shader cache v1\n";
    keyInput.append("translator ").append(TranslatorVersion()).append("\n");
    keyInput.append("target ").append(TargetName(target)).append("\n");
    keyInput.append("stage ").append(StageName(stage)).append("\n");
    keyInput.append("samplers ").append(std::to_string(declarations.samplers.size())).append("\n");
    for (const auto& declaration : declarations.samplers)
    {
        keyInput.append(std::to_string(declaration.size())).append(" ").append(declaration);
    }
    keyInput.append("texsizes ").append(std::to_string(declarations.texSizes.size())).append("\n");
    for (const auto& declaration : declarations.texSizes)
    {
        keyInput.append(std::to_string(declaration.size())).append(" ").append(declaration);
    }
    keyInput.append("source ").append(std::to_string(preprocessedCode.size())).append("\n");
    keyInput.append(preprocessedCode);

    return Utils::Sha256Hex(keyInput);
}

auto CacheFileName(const std::string& key) -> std::string
{
    return key + ".glsl";
}

auto ReadCachedShader(const std::string& cacheDirectory, const std::string& key, std::string& glsl) -> bool
{
    std::ifstream file(cacheDirectory + "/" + CacheFileName(key), std::ios::in | std::ios::binary);
    if (!file)
    {
        return false;
    }

    std::stringstream contents;
    contents << file.rdbuf();
    if (file.bad())
    {
        return false;
    }

    glsl = contents.str();
    return !glsl.empty();
}

} // namespace PresetShaderTranslator
} // namespace MilkdropPreset
} // namespace libprojectM
