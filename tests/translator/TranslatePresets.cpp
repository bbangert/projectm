// Offline preset shader translator harness.
//
// Translates the warp and composite shaders of MilkDrop presets from HLSL to GLSL the way
// MilkdropShader does at runtime (LoadCode + LoadTexturesAndCompile + TranspileHLSLShader),
// but without a GL context: preset parsing, Utils and hlslparser are projectM's own sources,
// the sampler/texsize declaration logic of MilkdropShader and TextureSamplerDescriptor is
// mirrored below. Keep the mirror in sync with MilkdropShader.cpp.
//
// Usage: projectM-translate-presets <es100|es300|glsl330> <outdir> [header.inc] < list_of_milk_paths
//
// Writes <outdir>/<idx>_{warp,comp}.frag for each translated stage and prints one TSV row per stage.

#include "MilkdropPreset/PresetFileParser.hpp"
#include "Utils.hpp"

#include <Engine.h>
#include <GLSLGenerator.h>
#include <HLSLParser.h>

#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

using libprojectM::MilkdropPreset::PresetFileParser;
namespace Utils = libprojectM::Utils;

enum class StageType
{
    Warp,
    Comp
};

struct TranslateError
{
    std::string errorClass;
    std::string message;
};

struct StageResult
{
    std::string status{"ok"}; // ok | err
    std::string errorClass;
    std::string errorMessage;
    std::string glsl;
    bool usesTex3D{false};
    std::set<std::string> userTextures;
    bool usesRandomTexture{false};
    int blurLevel{0};
};

struct Settings
{
    M4::GLSLGenerator::Version version{M4::GLSLGenerator::Version_330};
    std::string header;
};

const std::regex samplerDeclaration("sampler(2D|3D|)(\\s+|\\().*");
const std::regex texSizeDeclaration("float4\\s+texsize_.*");
const std::set<std::string> builtinTextures = {"noise_lq_lite", "noise_lq", "noise_mq", "noise_hq", "noisevol_lq", "noisevol_hq"};

auto IsRandomName(const std::string& lower) -> bool
{
    return lower.length() >= 6 && lower.substr(0, 4) == "rand" &&
           std::isdigit(static_cast<unsigned char>(lower[4])) && std::isdigit(static_cast<unsigned char>(lower[5]));
}

auto HasRandomSuffix(const std::string& name) -> bool
{
    return name.substr(0, 4) == "rand" && name.length() > 7 && name.at(6) == '_';
}

// MilkdropShader::GetReferencedSamplers
void GetReferencedSamplers(const std::string& program, std::set<std::string>& names, int& blurLevel)
{
    names.clear();
    names.insert("main");
    std::string const stripped = Utils::StripComments(program);

    auto found = stripped.find("sampler_", 0);
    while (found != std::string::npos)
    {
        found += 8;
        size_t const end = stripped.find_first_of(" ;,\n\r)", found);
        if (end != std::string::npos)
        {
            std::string const sampler = stripped.substr(found, end - found);
            if (sampler != "state")
            {
                names.insert(sampler);
            }
        }
        found = stripped.find("sampler_", found);
    }

    found = stripped.find("texsize_", 0);
    while (found != std::string::npos)
    {
        found += 8;
        size_t const end = stripped.find_first_of(" ;,.\n\r)", found);
        if (end != std::string::npos)
        {
            names.insert(stripped.substr(found, end - found));
        }
        found = stripped.find("texsize_", found);
    }

    auto it = names.begin();
    while (it != names.end())
    {
        std::string const lower = Utils::ToLower(*it);
        if (lower.length() == 6 && IsRandomName(lower))
        {
            auto next = it;
            next++;
            if (next != names.end())
            {
                std::string const nextLower = Utils::ToLower(*next);
                if (nextLower.length() > 7 && nextLower.substr(0, 6) == lower && nextLower[6] == '_')
                {
                    it = names.erase(it);
                }
            }
        }
        it++;
    }

    blurLevel = 0;
    if (stripped.find("GetBlur3") != std::string::npos)
    {
        blurLevel = 3;
    }
    else if (stripped.find("GetBlur2") != std::string::npos)
    {
        blurLevel = 2;
    }
    else if (stripped.find("GetBlur1") != std::string::npos)
    {
        blurLevel = 1;
    }
}

void UpdateMaxBlurLevel(int requested, int& blurLevel, std::set<std::string>& names)
{
    if (blurLevel >= requested)
    {
        return;
    }
    blurLevel = requested;
    names.insert("blur1");
    if (requested >= 2)
    {
        names.insert("blur2");
    }
    if (requested >= 3)
    {
        names.insert("blur3");
    }
}

// MilkdropShader::PreprocessPresetShader
void PreprocessPresetShader(const Settings& settings, std::string& program, StageType type)
{
    // ESSL 1.00 has a single fragment output, so the warp shader cannot write motion vector UVs.
    bool const warpOutputsTexCoords = settings.version != M4::GLSLGenerator::Version_100_ES;

    if (program.empty())
    {
        throw TranslateError{"empty", "shader declared but empty"};
    }

    std::string stripped = Utils::StripComments(program);
    size_t found = stripped.find("sampler_state");
    while (found != std::string::npos)
    {
        found = stripped.rfind('=', found);
        auto startPos = found;
        found = stripped.find('}', found);
        found = stripped.find(';', found);
        if (found == std::string::npos)
        {
            break;
        }
        stripped.replace(startPos, found - startPos, "");
        found = stripped.find("sampler_state");
    }

    found = stripped.find("shader_body");
    if (found == std::string::npos)
    {
        throw TranslateError{"no_shader_body", "missing shader_body"};
    }
    if (type == StageType::Warp)
    {
        program.replace(found, 11, warpOutputsTexCoords ? R"(
void PS(float4 _vDiffuse : COLOR,
        float4 _uv : TEXCOORD0,
        float2 _rad_ang : TEXCOORD1,
        out float4 _return_value : COLOR0,
        out float4 _mv_tex_coords : COLOR1)
)"
                                                        : R"(
void PS(float4 _vDiffuse : COLOR,
        float4 _uv : TEXCOORD0,
        float2 _rad_ang : TEXCOORD1,
        out float4 _return_value : COLOR0)
)");
    }
    else
    {
        program.replace(found, 11, R"(
void PS(float4 _vDiffuse : COLOR,
        float2 _uv : TEXCOORD0,
        float2 _rad_ang : TEXCOORD1,
        out float4 _return_value : COLOR)
)");
    }

    found = program.find('{', found);
    if (found == std::string::npos)
    {
        throw TranslateError{"no_brace", "no opening brace"};
    }
    std::string progMain = "{\nfloat3 ret = 0;\n";
    if (type == StageType::Warp && warpOutputsTexCoords)
    {
        progMain.append("_mv_tex_coords.xy = _uv.xy;\n");
    }
    program.replace(found, 1, progMain);

    found = program.rfind('}');
    if (found == std::string::npos)
    {
        throw TranslateError{"no_brace", "no closing brace"};
    }
    program.replace(found, 1, "_return_value = float4(ret.xyz, 1.0);\n}\n");

    int bracesOpen = 1;
    size_t pos = found + 1;
    for (; pos < program.length() && bracesOpen > 0; ++pos)
    {
        switch (program.at(pos))
        {
            case '/':
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
                else if (pos < program.length() - 1 && program.at(pos + 1) == '*')
                {
                    pos += 2;
                    for (; pos < program.length() - 1; ++pos)
                    {
                        if (program.at(pos) == '*' && program.at(pos + 1) == '/')
                        {
                            ++pos;
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

    std::string fullSource = settings.header;
    if (type == StageType::Warp)
    {
        fullSource.append("#define rad _rad_ang.x\n#define ang _rad_ang.y\n#define uv _uv.xy\n#define uv_orig _uv.zw\n");
    }
    else
    {
        fullSource.append("#define rad _rad_ang.x\n#define ang _rad_ang.y\n#define uv _uv.xy\n#define uv_orig _uv.xy\n#define hue_shader _vDiffuse.xyz\n");
    }
    fullSource.append(program);
    program = fullSource;
}

auto UnqualifiedName(const std::string& name) -> std::string
{
    if (name.length() <= 3 || name.at(2) != '_')
    {
        return name;
    }
    return name.substr(3);
}

// MilkdropShader::LoadTexturesAndCompile + TranspileHLSLShader. Textures are assumed to exist,
// as all of them are declared as sampler2D (or sampler3D for the noise volumes) either way.
auto TranslateStage(const Settings& settings, const std::string& code, StageType type) -> StageResult
{
    StageResult result;
    std::set<std::string> names;
    int blurLevel = 0;
    std::string program = code;
    try
    {
        GetReferencedSamplers(program, names, blurLevel);
        PreprocessPresetShader(settings, program, type);

        std::set<std::string> samplerDeclarations;
        std::set<std::string> texSizeDeclarations;
        // Iterates the live set like MilkdropShader does; UpdateMaxBlurLevel may insert while iterating.
        for (auto it = names.begin(); it != names.end(); ++it)
        {
            std::string const name = *it;
            std::string const lower = Utils::ToLower(UnqualifiedName(name));
            if (lower == "main")
            {
                samplerDeclarations.insert("uniform sampler2D sampler_" + name + ";\n");
                texSizeDeclarations.insert("uniform float4 texsize_main;\n");
                continue;
            }
            if (lower == "blur1" || lower == "blur2" || lower == "blur3")
            {
                UpdateMaxBlurLevel(lower[4] - '0', blurLevel, names);
                continue;
            }

            std::string declaredSizeName = UnqualifiedName(name);
            bool is3D = false;
            if (IsRandomName(lower) && std::stoi(lower.substr(4, 2)) <= 15)
            {
                result.usesRandomTexture = true;
                declaredSizeName = name;
            }
            else
            {
                is3D = declaredSizeName == "noisevol_lq" || declaredSizeName == "noisevol_hq";
                result.usesTex3D = result.usesTex3D || is3D;
                if (builtinTextures.count(declaredSizeName) == 0)
                {
                    result.userTextures.insert(Utils::ToLower(declaredSizeName));
                }
            }

            // TextureSamplerDescriptor::SamplerDeclaration / TexSizeDeclaration
            std::string sampler = std::string("uniform ") + (is3D ? "sampler3D" : "sampler2D") + " sampler_" + name + ";\n";
            if (HasRandomSuffix(name))
            {
                sampler += "uniform sampler2D sampler_" + name.substr(0, 6) + ";\n";
            }
            samplerDeclarations.insert(sampler);
            std::string texSize = "uniform float4 texsize_" + declaredSizeName + ";\n";
            if (HasRandomSuffix(declaredSizeName))
            {
                texSize += "uniform float4 texsize_" + declaredSizeName.substr(0, 6) + ";\n";
            }
            texSizeDeclarations.insert(texSize);
        }
        for (int level = 1; level <= blurLevel; level++)
        {
            samplerDeclarations.insert("uniform sampler2D sampler_blur" + std::to_string(level) + ";\n");
        }
        result.blurLevel = blurLevel;

        M4::GLSLGenerator generator;
        M4::Allocator allocator;
        M4::HLSLTree tree(&allocator);
        M4::HLSLParser parser(&allocator, &tree);

        M4::Log_ClearFirstError();
        std::string preprocessed;
        if (!parser.ApplyPreprocessor("", program.c_str(), program.size(), preprocessed))
        {
            throw TranslateError{"preprocess", M4::Log_GetFirstError()};
        }
        Utils::EraseRegexMatches(preprocessed, "sampler", samplerDeclaration);
        Utils::EraseRegexMatches(preprocessed, "float4", texSizeDeclaration);
        for (const auto& declaration : texSizeDeclarations)
        {
            preprocessed.insert(0, declaration);
        }
        for (const auto& declaration : samplerDeclarations)
        {
            preprocessed.insert(0, declaration);
        }

        if (!parser.Parse("", preprocessed.c_str(), preprocessed.size()))
        {
            throw TranslateError{"parse", M4::Log_GetFirstError()};
        }

        // MilkdropShader::TranspileHLSLShader
        unsigned int flags = M4::GLSLGenerator::Flag_AlternateNanPropagation;
        if (settings.version == M4::GLSLGenerator::Version_100_ES)
        {
            flags |= M4::GLSLGenerator::Flag_NoTextureLod | M4::GLSLGenerator::Flag_NoDerivatives | M4::GLSLGenerator::Flag_Texture3DAtlas;
        }
        if (!generator.Generate(&tree, M4::GLSLGenerator::Target_FragmentShader, settings.version, "PS",
                                M4::GLSLGenerator::Options(flags)))
        {
            throw TranslateError{"generate", M4::Log_GetFirstError()};
        }
        result.glsl = generator.GetResult();
    }
    catch (const TranslateError& e)
    {
        result.status = "err";
        result.errorClass = e.errorClass;
        result.errorMessage = e.message;
    }
    catch (const std::exception& e)
    {
        result.status = "err";
        result.errorClass = "exception";
        result.errorMessage = e.what();
    }
    return result;
}

auto OneLine(std::string text) -> std::string
{
    for (auto& c : text)
    {
        if (c == '\t' || c == '\n' || c == '\r')
        {
            c = ' ';
        }
    }
    if (text.size() > 200)
    {
        text.resize(200);
    }
    return text;
}

auto ParseVersion(const std::string& name, M4::GLSLGenerator::Version& version) -> bool
{
    if (name == "es100")
    {
        version = M4::GLSLGenerator::Version_100_ES;
    }
    else if (name == "es300")
    {
        version = M4::GLSLGenerator::Version_300_ES;
    }
    else if (name == "glsl330")
    {
        version = M4::GLSLGenerator::Version_330;
    }
    else
    {
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Settings settings;
    if (argc < 3 || !ParseVersion(argv[1], settings.version))
    {
        std::cerr << "usage: " << argv[0] << " <es100|es300|glsl330> <outdir> [header.inc] < list_of_milk_paths\n";
        return 2;
    }
    std::string const outDir = argv[2];
    std::string const headerPath = argc > 3 ? argv[3] : PROJECTM_PRESET_SHADER_HEADER;
    {
        std::ifstream headerFile(headerPath);
        if (!headerFile)
        {
            std::cerr << "cannot read preset shader header " << headerPath << "\n";
            return 2;
        }
        std::stringstream contents;
        contents << headerFile.rdbuf();
        settings.header = contents.str();
    }

    std::cout << "idx\tpath\tpreset_version\tpsv_warp\tpsv_comp\tstage\tstatus\terr_class\thlsl_bytes\tglsl_bytes\ttex3d\tuser_tex\trand_tex\tblur\terr_msg\ttranslate_us\n";

    std::string path;
    int index = 0;
    while (std::getline(std::cin, path))
    {
        if (path.empty())
        {
            continue;
        }
        PresetFileParser parser;
        if (!parser.Read(path))
        {
            std::cout << index << "\t" << path << "\t\t\t\twarp\terr\tread\t0\t0\t0\t\t0\t0\tread failed\t0\n";
            index++;
            continue;
        }

        // PresetState: shaders only from version 200, PSVERSION for 200, per-stage versions above.
        int const presetVersion = parser.GetInt("MILKDROP_PRESET_VERSION", 100);
        int warpVersion = 0;
        int compVersion = 0;
        if (presetVersion == 200)
        {
            warpVersion = compVersion = parser.GetInt("PSVERSION", 2);
        }
        else if (presetVersion > 200)
        {
            warpVersion = parser.GetInt("PSVERSION_WARP", 2);
            compVersion = parser.GetInt("PSVERSION_COMP", 2);
        }

        for (int stage = 0; stage < 2; stage++)
        {
            StageType const type = stage == 0 ? StageType::Warp : StageType::Comp;
            const char* stageName = stage == 0 ? "warp" : "comp";
            int const stageVersion = stage == 0 ? warpVersion : compVersion;
            std::string code = parser.GetCode(stage == 0 ? "warp_" : "comp_");

            std::cout << index << "\t" << path << "\t" << presetVersion << "\t" << warpVersion << "\t" << compVersion << "\t" << stageName << "\t";
            if (stageVersion <= 0)
            {
                std::cout << "none\t\t0\t0\t0\t\t0\t0\t\t0\n";
                continue;
            }
            bool isDefault = false;
            if (code.empty())
            {
                if (type == StageType::Warp)
                {
                    std::cout << "default\t\t0\t0\t0\t\t0\t0\t\t0\n";
                    continue;
                }
                code = "shader_body\n{\nret = tex2D(sampler_main, uv).xyz;\n}";
                isDefault = true;
            }

            auto const start = std::chrono::steady_clock::now();
            StageResult const result = TranslateStage(settings, code, type);
            auto const micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();

            std::string userTextures;
            for (const auto& name : result.userTextures)
            {
                userTextures += (userTextures.empty() ? "" : ",") + name;
            }
            std::cout << (result.status == "ok" && isDefault ? "ok_default" : result.status) << "\t" << result.errorClass << "\t"
                      << code.size() << "\t" << result.glsl.size() << "\t" << (result.usesTex3D ? 1 : 0) << "\t"
                      << userTextures << "\t" << (result.usesRandomTexture ? 1 : 0) << "\t" << result.blurLevel << "\t"
                      << OneLine(result.errorMessage) << "\t" << micros << "\n";
            if (result.status == "ok")
            {
                std::ofstream out(outDir + "/" + std::to_string(index) + "_" + stageName + ".frag");
                out << result.glsl;
            }
        }
        index++;
    }
    return 0;
}
