#include <gtest/gtest.h>

#include <MilkdropPreset/PresetShaderTranslator.hpp>
#include <Utils.hpp>

#include <map>
#include <set>
#include <string>

namespace Translator = libprojectM::MilkdropPreset::PresetShaderTranslator;
using Translator::Stage;
using Translator::Target;

namespace {

const std::string compositeCode = "shader_body\n"
                                  "{\n"
                                  "    float3 n = tex2D(sampler_fw_noise_lq, uv).xyz;\n"
                                  "    float3 v = tex3D(sampler_noisevol_hq, float3(uv, 0.5)).xyz;\n"
                                  "    ret = tex2D(sampler_main, uv).xyz * n + GetBlur2(uv) + v;\n"
                                  "    ret += tex2D(sampler_rand00, uv).xyz * texsize_rand00.x;\n"
                                  "}\n";

auto Declarations(const std::string& code, std::map<int, std::string>& randomSlots) -> Translator::Declarations
{
    auto references = Translator::GetReferencedSamplers(code);
    return Translator::OfflineDeclarations(references, randomSlots);
}

} // namespace

TEST(PresetShaderTranslator, TargetNames)
{
    for (auto target : {Target::Glsl330, Target::GlslEs300, Target::GlslEs100})
    {
        Target parsed{};
        ASSERT_TRUE(Translator::TargetFromName(Translator::TargetName(target), parsed));
        EXPECT_EQ(parsed, target);
    }
    Target parsed{};
    EXPECT_FALSE(Translator::TargetFromName("es200", parsed));
}

TEST(PresetShaderTranslator, OfflineDeclarations)
{
    std::map<int, std::string> randomSlots;
    auto const declarations = Declarations(compositeCode, randomSlots);

    std::set<std::string> const samplers{
        "uniform sampler2D sampler_main;\n",
        "uniform sampler2D sampler_fw_noise_lq;\n",
        "uniform sampler3D sampler_noisevol_hq;\n",
        "uniform sampler2D sampler_rand00;\n",
        "uniform sampler2D sampler_blur1;\n",
        "uniform sampler2D sampler_blur2;\n"};
    std::set<std::string> const texSizes{
        "uniform float4 texsize_main;\n",
        "uniform float4 texsize_noise_lq;\n",
        "uniform float4 texsize_noisevol_hq;\n",
        "uniform float4 texsize_rand00;\n"};
    EXPECT_EQ(declarations.samplers, samplers);
    EXPECT_EQ(declarations.texSizes, texSizes);
}

TEST(PresetShaderTranslator, RandomSlotNameIsSharedBetweenStages)
{
    // The warp shader claims slot 0 with a prefix; the composite shader reuses that texture.
    std::map<int, std::string> randomSlots;
    Declarations("shader_body\n{\nret = tex2D(sampler_rand00_smalltiled, uv).xyz;\n}\n", randomSlots);
    auto const declarations = Declarations("shader_body\n{\nret = tex2D(sampler_rand00, uv).xyz;\n}\n", randomSlots);

    EXPECT_EQ(declarations.samplers.count("uniform sampler2D sampler_rand00_smalltiled;\nuniform sampler2D sampler_rand00;\n"), 1u);
    EXPECT_EQ(declarations.texSizes.count("uniform float4 texsize_rand00_smalltiled;\nuniform float4 texsize_rand00;\n"), 1u);
}

TEST(PresetShaderTranslator, Es100WarpHasNoMotionVectorOutput)
{
    auto const warp = "shader_body\n{\nret = tex2D(sampler_main, uv).xyz;\n}\n";
    EXPECT_EQ(Translator::PreprocessPresetShader(Stage::Warp, Target::GlslEs100, warp).find("_mv_tex_coords"), std::string::npos);
    EXPECT_NE(Translator::PreprocessPresetShader(Stage::Warp, Target::GlslEs300, warp).find("_mv_tex_coords"), std::string::npos);
}

TEST(PresetShaderTranslator, TranslatesToTargetVersion)
{
    std::map<int, std::string> randomSlots;
    auto const declarations = Declarations(compositeCode, randomSlots);
    auto const program = Translator::PreprocessPresetShader(Stage::Composite, Target::GlslEs100, compositeCode);
    auto const glsl = Translator::TranslateToGlsl(Stage::Composite, Target::GlslEs100, program, declarations);
    EXPECT_EQ(glsl.rfind("#version 100", 0), 0u);
}

TEST(PresetShaderTranslator, CacheKeyDefinition)
{
    std::map<int, std::string> randomSlots;
    auto const declarations = Declarations(compositeCode, randomSlots);
    auto const program = Translator::PreprocessPresetShader(Stage::Composite, Target::GlslEs100, compositeCode);

    // Spelled out here so a format change is a deliberate test change, as it invalidates every
    // pre-translated shader pack.
    std::string input = "projectM preset shader cache v1\n";
    input += std::string("translator ") + Translator::TranslatorVersion() + "\n";
    input += "target es100\nstage comp\n";
    input += "samplers " + std::to_string(declarations.samplers.size()) + "\n";
    for (const auto& declaration : declarations.samplers)
    {
        input += std::to_string(declaration.size()) + " " + declaration;
    }
    input += "texsizes " + std::to_string(declarations.texSizes.size()) + "\n";
    for (const auto& declaration : declarations.texSizes)
    {
        input += std::to_string(declaration.size()) + " " + declaration;
    }
    input += "source " + std::to_string(program.size()) + "\n" + program;

    EXPECT_EQ(Translator::CacheKey(Stage::Composite, Target::GlslEs100, program, declarations),
              libprojectM::Utils::Sha256Hex(input));
    EXPECT_EQ(std::string(Translator::TranslatorVersion()).size(), 64u);
}

TEST(PresetShaderTranslator, CacheKeyChangesWithEveryInput)
{
    std::map<int, std::string> randomSlots;
    auto declarations = Declarations(compositeCode, randomSlots);
    auto const program = Translator::PreprocessPresetShader(Stage::Composite, Target::GlslEs100, compositeCode);

    std::set<std::string> keys;
    keys.insert(Translator::CacheKey(Stage::Composite, Target::GlslEs100, program, declarations));
    keys.insert(Translator::CacheKey(Stage::Warp, Target::GlslEs100, program, declarations));
    keys.insert(Translator::CacheKey(Stage::Composite, Target::GlslEs300, program, declarations));
    keys.insert(Translator::CacheKey(Stage::Composite, Target::GlslEs100, program + " ", declarations));
    auto withoutTexSize = declarations;
    withoutTexSize.texSizes.erase("uniform float4 texsize_rand00;\n");
    keys.insert(Translator::CacheKey(Stage::Composite, Target::GlslEs100, program, withoutTexSize));
    auto volumeAs2D = declarations;
    volumeAs2D.samplers.erase("uniform sampler3D sampler_noisevol_hq;\n");
    volumeAs2D.samplers.insert("uniform sampler2D sampler_noisevol_hq;\n");
    keys.insert(Translator::CacheKey(Stage::Composite, Target::GlslEs100, program, volumeAs2D));

    EXPECT_EQ(keys.size(), 6u);
    EXPECT_EQ(Translator::CacheKey(Stage::Composite, Target::GlslEs100, program, declarations),
              Translator::CacheKey(Stage::Composite, Target::GlslEs100, program, declarations));
}

TEST(PresetShaderTranslator, ReadCachedShaderMissingFile)
{
    std::string glsl = "unchanged";
    EXPECT_FALSE(Translator::ReadCachedShader("/nonexistent-projectm-cache", "0000", glsl));
    EXPECT_EQ(glsl, "unchanged");
}
