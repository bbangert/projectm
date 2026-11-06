#include <gtest/gtest.h>

#include <Utils.hpp>

#include <regex>
#include <string>

using libprojectM::Utils::EraseRegexMatches;
using libprojectM::Utils::Sha256Hex;

TEST(Utils, EraseRegexMatches)
{
    std::string text = "sampler sampler_main;\n"
                       "float3 c = tex2D(sampler_main, uv).xyz;\n"
                       "sampler2D sampler_noise;\n";
    EraseRegexMatches(text, "sampler", std::regex("sampler(2D|3D|)(\\s+|\\().*"));
    EXPECT_EQ(text, "\nfloat3 c = tex2D(sampler_main, uv).xyz;\n\n");
}

TEST(Utils, EraseRegexMatchesFindsMatchCreatedByErase)
{
    // Erasing "sampler2D x;" leaves the first "sampler" followed by a line break, which matches too.
    std::string text = "samplersampler2D x;\n y";
    EraseRegexMatches(text, "sampler", std::regex("sampler(2D|3D|)(\\s+|\\().*"));
    EXPECT_EQ(text, "");
}

TEST(Utils, Sha256Hex)
{
    // FIPS 180-2 test vectors.
    EXPECT_EQ(Sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(Sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(Sha256Hex(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Utils, Sha256HexBlockBoundaries)
{
    // Padding needs a second block from 56 bytes of input on.
    EXPECT_EQ(Sha256Hex(std::string(55, 'a')), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    EXPECT_EQ(Sha256Hex(std::string(56, 'a')), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    EXPECT_EQ(Sha256Hex(std::string(64, 'a')), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}
