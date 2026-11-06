#include "Utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace libprojectM {
namespace Utils {

auto ToLower(const std::string& str) -> std::string
{
    std::string lowerStr(str);
    ToLowerInPlace(lowerStr);
    return lowerStr;
}

auto ToUpper(const std::string& str) -> std::string
{
    std::string upperStr(str);
    ToUpperInPlace(upperStr);
    return upperStr;
}

void ToLowerInPlace(std::string& str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
}

void ToUpperInPlace(std::string& str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
}

auto StripComments(const std::string& source) -> std::string
{
    std::string result = source;
    size_t i = 0;

    while (i < result.size())
    {
        if (i + 1 < result.size() && result.at(i) == '/' && result.at(i + 1) == '/')
        {
            // Line comment: replace until end of line
            while (i < result.size() && result.at(i) != '\n' && result.at(i) != '\r')
            {
                result.at(i) = ' ';
                i++;
            }
        }
        else if (i + 1 < result.size() && result.at(i) == '/' && result.at(i + 1) == '*')
        {
            // Block comment: replace until closing */
            result.at(i) = ' ';
            result.at(i + 1) = ' ';
            i += 2;
            while (i < result.size())
            {
                if (i + 1 < result.size() && result.at(i) == '*' && result.at(i + 1) == '/')
                {
                    result.at(i) = ' ';
                    result.at(i + 1) = ' ';
                    i += 2;
                    break;
                }
                // Preserve newlines to keep line structure intact
                if (result.at(i) != '\n' && result.at(i) != '\r')
                {
                    result.at(i) = ' ';
                }
                i++;
            }
        }
        else
        {
            i++;
        }
    }

    return result;
}

void EraseRegexMatches(std::string& text, const std::string& prefix, const std::regex& pattern)
{
    std::smatch match;
    size_t pos = 0;
    while ((pos = text.find(prefix, pos)) != std::string::npos)
    {
        if (std::regex_search(text.cbegin() + pos, text.cend(), match, pattern, std::regex_constants::match_continuous))
        {
            text.erase(pos, match.length());
            pos = 0; // The erase can create a new match further up.
        }
        else
        {
            pos++;
        }
    }
}

auto Sha256Hex(const std::string& data) -> std::string
{
    static constexpr std::array<uint32_t, 64> roundConstants{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    std::array<uint32_t, 8> hash{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    auto rotateRight = [](uint32_t value, int bits) {
        return (value >> bits) | (value << (32 - bits));
    };

    // Message padding: 0x80, zeros up to 56 mod 64, then the bit length as 64-bit big endian.
    std::string message = data;
    const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8;
    message.push_back(static_cast<char>(0x80));
    while (message.size() % 64 != 56)
    {
        message.push_back('\0');
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        message.push_back(static_cast<char>((bitLength >> shift) & 0xff));
    }

    std::array<uint32_t, 64> words{};
    for (size_t block = 0; block < message.size(); block += 64)
    {
        for (size_t i = 0; i < 16; i++)
        {
            const auto* bytes = reinterpret_cast<const unsigned char*>(message.data() + block + i * 4);
            words[i] = (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
                       (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
        }
        for (size_t i = 16; i < 64; i++)
        {
            const uint32_t s0 = rotateRight(words[i - 15], 7) ^ rotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const uint32_t s1 = rotateRight(words[i - 2], 17) ^ rotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = hash[0];
        uint32_t b = hash[1];
        uint32_t c = hash[2];
        uint32_t d = hash[3];
        uint32_t e = hash[4];
        uint32_t f = hash[5];
        uint32_t g = hash[6];
        uint32_t h = hash[7];
        for (size_t i = 0; i < 64; i++)
        {
            const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choice + roundConstants[i] + words[i];
            const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }

    static const char* const hexDigits = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (const auto word : hash)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            hex.push_back(hexDigits[(word >> shift) & 0xf]);
        }
    }
    return hex;
}

} // namespace Utils
} // namespace libprojectM
