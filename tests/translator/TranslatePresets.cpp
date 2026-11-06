// Offline preset shader translator harness.
//
// Translates the warp and composite shaders of MilkDrop presets from HLSL to GLSL with the
// runtime's PresetShaderTranslator, without a GL context. Every referenced texture is assumed
// to exist.
//
// Usage: projectM-translate-presets <es100|es300|glsl330> <outdir> < list_of_milk_paths
//
// Writes <outdir>/<idx>_{warp,comp}.frag for each translated stage and prints one TSV row per stage.

#include "MilkdropPreset/PresetFileParser.hpp"
#include "MilkdropPreset/PresetShaderTranslator.hpp"
#include "Utils.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

namespace {

using libprojectM::MilkdropPreset::PresetFileParser;
namespace Translator = libprojectM::MilkdropPreset::PresetShaderTranslator;
namespace Utils = libprojectM::Utils;

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

const std::set<std::string> builtinTextures = {"noise_lq_lite", "noise_lq", "noise_mq", "noise_hq", "noisevol_lq", "noisevol_hq"};

auto ErrorClass(const std::string& message) -> std::string
{
    if (message.find("Preprocessing failed") != std::string::npos)
    {
        return "preprocess";
    }
    if (message.find("HLSL parsing failed") != std::string::npos)
    {
        return "parse";
    }
    if (message.find("GLSL generating failed") != std::string::npos)
    {
        return "generate";
    }
    return "preset";
}

// Translates one stage like MilkdropShader::LoadCode + LoadTexturesAndCompile, assuming every
// referenced texture exists.
auto TranslateStage(Translator::Target target, const std::string& code, Translator::Stage stage,
                    std::map<int, std::string>& randomSlotNames) -> StageResult
{
    StageResult result;
    try
    {
        auto references = Translator::GetReferencedSamplers(code);
        auto const program = Translator::PreprocessPresetShader(stage, target, code);
        auto const declarations = Translator::OfflineDeclarations(references, randomSlotNames);

        for (const auto& name : references.names)
        {
            auto const samplerClass = Translator::ClassifySampler(name);
            if (samplerClass.kind == Translator::SamplerKind::Random)
            {
                result.usesRandomTexture = true;
            }
            else if (samplerClass.kind == Translator::SamplerKind::Texture)
            {
                std::string const unqualifiedName = Translator::UnqualifiedTextureName(name);
                result.usesTex3D = result.usesTex3D || unqualifiedName == "noisevol_lq" || unqualifiedName == "noisevol_hq";
                if (builtinTextures.count(unqualifiedName) == 0)
                {
                    result.userTextures.insert(Utils::ToLower(unqualifiedName));
                }
            }
        }
        result.blurLevel = references.blurLevel;

        result.glsl = Translator::TranslateToGlsl(stage, target, program, declarations);
    }
    catch (const std::exception& e)
    {
        result.status = "err";
        result.errorMessage = e.what();
        result.errorClass = ErrorClass(result.errorMessage);
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

} // namespace

int main(int argc, char** argv)
{
    Translator::Target target{};
    if (argc < 3 || !Translator::TargetFromName(argv[1], target))
    {
        std::cerr << "usage: " << argv[0] << " <es100|es300|glsl330> <outdir> < list_of_milk_paths\n";
        return 2;
    }
    std::string const outDir = argv[2];

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

        int const presetVersion = parser.GetInt("MILKDROP_PRESET_VERSION", 100);
        auto const sources = Translator::ReadStageSources(parser);
        std::map<int, std::string> randomSlotNames;

        for (int stage = 0; stage < 2; stage++)
        {
            auto const type = stage == 0 ? Translator::Stage::Warp : Translator::Stage::Composite;
            const char* stageName = Translator::StageName(type);
            int const stageVersion = stage == 0 ? sources.warpVersion : sources.compositeVersion;
            std::string code = stage == 0 ? sources.warpCode : sources.compositeCode;

            std::cout << index << "\t" << path << "\t" << presetVersion << "\t" << sources.warpVersion << "\t" << sources.compositeVersion << "\t" << stageName << "\t";
            if (stageVersion <= 0)
            {
                std::cout << "none\t\t0\t0\t0\t\t0\t0\t\t0\n";
                continue;
            }
            bool isDefault = false;
            if (code.empty())
            {
                if (type == Translator::Stage::Warp)
                {
                    std::cout << "default\t\t0\t0\t0\t\t0\t0\t\t0\n";
                    continue;
                }
                code = Translator::DefaultCompositeShader();
                isDefault = true;
            }

            auto const start = std::chrono::steady_clock::now();
            StageResult const result = TranslateStage(target, code, type, randomSlotNames);
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
