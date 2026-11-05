#include <gtest/gtest.h>

#include <GLSLGenerator.h>
#include <HLSLParser.h>
#include <HLSLTree.h>

#include <cstring>
#include <string>

namespace {

using M4::GLSLGenerator;

constexpr unsigned int NoExtensions = GLSLGenerator::Flag_NoTextureLod |
                                      GLSLGenerator::Flag_NoDerivatives |
                                      GLSLGenerator::Flag_Texture3DAtlas;

/**
 * Translates an HLSL fragment shader with entry point "PS". Returns false if parsing or generating fails.
 */
bool Translate(const std::string& hlsl, GLSLGenerator::Version version, unsigned int flags, std::string& glsl)
{
    M4::Allocator allocator;
    M4::HLSLTree tree(&allocator);
    M4::HLSLParser parser(&allocator, &tree);
    if (!parser.Parse("test.hlsl", hlsl.c_str(), hlsl.size()))
    {
        return false;
    }
    GLSLGenerator generator;
    if (!generator.Generate(&tree, GLSLGenerator::Target_FragmentShader, version, "PS", GLSLGenerator::Options(flags)))
    {
        return false;
    }
    glsl = generator.GetResult();
    return true;
}

std::string TranslateES100(const std::string& hlsl, unsigned int flags = NoExtensions)
{
    std::string glsl;
    EXPECT_TRUE(Translate(hlsl, GLSLGenerator::Version_100_ES, flags, glsl)) << hlsl;
    return glsl;
}

bool Contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::string Line(const std::string& text, int index)
{
    size_t start = 0;
    for (int i = 0; i < index && start != std::string::npos; i++)
    {
        start = text.find('\n', start);
        start = start == std::string::npos ? start : start + 1;
    }
    if (start == std::string::npos)
    {
        return {};
    }
    return text.substr(start, text.find('\n', start) - start);
}

} // namespace

TEST(GLSLGenerator, ES100PadsNonSquareMatrices)
{
    auto glsl = TranslateES100(
        "uniform float4x3 rot;\n"
        "float4 PS(float3 p : TEXCOORD0) : COLOR {\n"
        "  float2x3 m = float2x3(p, p.zyx);\n"
        "  float2 a = mul(m, p);\n"
        "  float3 b = mul(float4(p, 1), rot);\n"
        "  float2 r = m[1].xy;\n"
        "  return float4(a + r, b.x, b.y);\n"
        "}\n");

    for (const char* nonSquare : {"mat2x3", "mat2x4", "mat3x2", "mat3x4", "mat4x2", "mat4x3"})
    {
        // Constructor helper names keep the HLSL shape, so look for the types only.
        EXPECT_FALSE(Contains(glsl, std::string(nonSquare) + " ")) << nonSquare << "\n" << glsl;
        EXPECT_FALSE(Contains(glsl, std::string(nonSquare) + "(")) << nonSquare << "\n" << glsl;
    }
    EXPECT_TRUE(Contains(glsl, "uniform mat4 rot;")) << glsl;
    // float2x3(row0, row1) fills columns of the padded mat3, with a zero third row.
    EXPECT_TRUE(Contains(glsl, "mat3 mat3x2_from_vec3_vec3(vec3 a, vec3 b) { return mat3(a.x,b.x,0,a.y,b.y,0,a.z,b.z,0); }")) << glsl;
    // mul(float2x3, float3) gives the first two components of the padded product.
    EXPECT_TRUE(Contains(glsl, "((m)*(p)).xy")) << glsl;
    // mul(float4, float4x3) gives the first three.
    EXPECT_TRUE(Contains(glsl, ")*(rot)).xyz")) << glsl;
}

TEST(GLSLGenerator, ES300KeepsNonSquareMatrices)
{
    std::string glsl;
    ASSERT_TRUE(Translate("uniform float4x3 rot;\n"
                          "float4 PS(float3 p : TEXCOORD0) : COLOR { return float4(mul(float4(p, 1), rot), 1); }\n",
                          GLSLGenerator::Version_300_ES, 0, glsl));
    EXPECT_TRUE(Contains(glsl, "uniform mat3x4 rot;")) << glsl;
}

TEST(GLSLGenerator, ES100LowersArrayInitializers)
{
    auto glsl = TranslateES100(
        "static const float4 table[] = { 1, 2, 3, 4, 5, 6, 7, 8 };\n"
        "float4 PS(float2 uv : TEXCOORD0) : COLOR {\n"
        "  float2 d[2] = { float2(1, 0), float2(0, 1) };\n"
        "  return table[1] + d[1].xyxy;\n"
        "}\n");

    EXPECT_FALSE(Contains(glsl, "[](")) << glsl;
    EXPECT_TRUE(Contains(glsl, "vec4 table[2];")) << glsl;
    EXPECT_TRUE(Contains(glsl, "table[0] = vec4(1, 2, 3, 4); table[1] = vec4(5, 6, 7, 8);")) << glsl;
    EXPECT_TRUE(Contains(glsl, "vec2 d[2]; d[0] = vec2(1, 0); d[1] = vec2(0, 1);")) << glsl;
}

TEST(GLSLGenerator, ES100EmulatesRoundAndModf)
{
    auto glsl = TranslateES100(
        "float4 PS(float2 uv : TEXCOORD0) : COLOR {\n"
        "  float i;\n"
        "  float f = modf(uv.x, i);\n"
        "  return float4(round(uv), f, i);\n"
        "}\n");

    EXPECT_TRUE(Contains(glsl, "vec2 round0(vec2 x) { return floor(x + 0.5); }")) << glsl;
    EXPECT_TRUE(Contains(glsl, "round0(")) << glsl;
    EXPECT_TRUE(Contains(glsl, "ip = x < 0.0 ? ceil(x) : floor(x);")) << glsl;
}

TEST(GLSLGenerator, ES100ExtensionsFollowVersion)
{
    const std::string hlsl = "sampler2D s;\n"
                             "float4 PS(float2 uv : TEXCOORD0) : COLOR { return tex2Dlod(s, float4(uv, 0, 2)) + ddx(uv.x); }\n";
    auto glsl = TranslateES100(hlsl, 0);

    EXPECT_EQ(Line(glsl, 0), "#version 100");
    EXPECT_EQ(Line(glsl, 1), "#extension GL_EXT_shader_texture_lod : require");
    EXPECT_EQ(Line(glsl, 2), "#extension GL_OES_standard_derivatives : require");
    EXPECT_TRUE(Contains(glsl, "texture2DLodEXT(samp, texCoord.xy, texCoord.w)")) << glsl;
}

TEST(GLSLGenerator, ES100NoTextureLodSamplesBaseLevel)
{
    auto glsl = TranslateES100("sampler2D s;\n"
                               "samplerCUBE c;\n"
                               "float4 PS(float2 uv : TEXCOORD0) : COLOR { return tex2Dlod(s, float4(uv, 0, 2)) + texCUBElod(c, float4(uv, 1, 2)); }\n");

    EXPECT_FALSE(Contains(glsl, "#extension")) << glsl;
    EXPECT_TRUE(Contains(glsl, "{ return texture2D(samp, texCoord.xy);  }")) << glsl;
    EXPECT_TRUE(Contains(glsl, "{ return textureCube(samp, texCoord.xyz);  }")) << glsl;
}

TEST(GLSLGenerator, ES100NoDerivativesFails)
{
    std::string glsl;
    EXPECT_FALSE(Translate("float4 PS(float2 uv : TEXCOORD0) : COLOR { return fwidth(uv.x); }\n",
                           GLSLGenerator::Version_100_ES, NoExtensions, glsl));
}

TEST(GLSLGenerator, Texture3DAtlas)
{
    auto glsl = TranslateES100("uniform sampler3D noise;\n"
                               "float4 PS(float2 uv : TEXCOORD0) : COLOR {\n"
                               "  return tex3D(noise, float3(uv, 0.5)) + tex3Dlod(noise, float4(uv, 0.25, 0));\n"
                               "}\n");

    EXPECT_FALSE(Contains(glsl, "sampler3D")) << glsl;
    EXPECT_FALSE(Contains(glsl, "texture3D")) << glsl;
    EXPECT_TRUE(Contains(glsl, "uniform sampler2D noise;")) << glsl;
    EXPECT_TRUE(Contains(glsl, "vec4 tex3Datlas0(sampler2D s, vec3 p) {")) << glsl;
    EXPECT_TRUE(Contains(glsl, "vec2 scale = vec2(1.0 / 272.0, 1.0 / 136.0);")) << glsl;
    EXPECT_TRUE(Contains(glsl, "tex3Datlas0(noise, vec3(")) << glsl;
    EXPECT_TRUE(Contains(glsl, "vec4 tex3Dlod0(sampler2D samp, vec4 texCoord) { return tex3Datlas0(samp, texCoord.xyz);  }")) << glsl;
    EXPECT_TRUE(Contains(glsl, "tex3Dlod0(noise, ")) << glsl;
}
