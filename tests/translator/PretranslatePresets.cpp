// Pre-translates the warp and composite shaders of MilkDrop presets into a shader cache directory
// for projectm_set_shader_cache_dir().
//
// Usage: projectM-pretranslate-presets [--target es100|es300|glsl330] <out_dir> <preset dir or file>...
//
// Writes <out_dir>/shaders/<key>.glsl per translated stage, with the runtime's cache key and
// translation code. The default composite shader (used by presets without one and as the
// fallback) is always written. Assumes every texture a preset references exists at runtime;
// a missing random texture makes the runtime key differ, which only costs a translation.
//
// Prints one TSV row per stage: path (relative to its preset dir), stage, status (ok|err), key,
// error message. Exits 1 if a file can't be written, 2 on usage errors.

#include "MilkdropPreset/PresetFileParser.hpp"
#include "MilkdropPreset/PresetShaderTranslator.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using libprojectM::MilkdropPreset::PresetFileParser;
namespace Translator = libprojectM::MilkdropPreset::PresetShaderTranslator;
namespace fs = std::filesystem;

struct PresetFile
{
    fs::path path;
    std::string displayPath;
};

auto OneLine(std::string text) -> std::string
{
    std::replace_if(text.begin(), text.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
    return text;
}

auto CollectPresets(const std::vector<std::string>& inputs, std::vector<PresetFile>& presets) -> bool
{
    for (const auto& input : inputs)
    {
        fs::path const inputPath(input);
        std::error_code error;
        if (fs::is_directory(inputPath, error))
        {
            std::vector<PresetFile> found;
            for (fs::recursive_directory_iterator it(inputPath, error), end; it != end && !error; it.increment(error))
            {
                if (it->is_regular_file() && libprojectM::Utils::ToLower(it->path().extension().string()) == ".milk")
                {
                    found.push_back({it->path(), it->path().lexically_relative(inputPath).generic_string()});
                }
            }
            std::sort(found.begin(), found.end(), [](const PresetFile& a, const PresetFile& b) { return a.displayPath < b.displayPath; });
            presets.insert(presets.end(), found.begin(), found.end());
        }
        else if (fs::is_regular_file(inputPath, error))
        {
            presets.push_back({inputPath, inputPath.filename().generic_string()});
        }
        else
        {
            std::cerr << "not a preset file or directory: " << input << "\n";
            return false;
        }
    }
    return true;
}

class Pretranslator
{
public:
    Pretranslator(Translator::Target target, fs::path shaderDir)
        : m_target(target)
        , m_shaderDir(std::move(shaderDir))
    {
    }

    // Translates one stage like MilkdropShader::LoadCode + LoadTexturesAndCompile and prints its row.
    // Returns false only if the GLSL file can't be written.
    auto Stage(const std::string& displayPath, Translator::Stage stage, const std::string& code,
               std::map<int, std::string>& randomSlotNames) -> bool
    {
        std::string key;
        std::string error;
        bool written = true;
        try
        {
            auto references = Translator::GetReferencedSamplers(code);
            auto const program = Translator::PreprocessPresetShader(stage, m_target, code);
            auto const declarations = Translator::OfflineDeclarations(references, randomSlotNames);
            key = Translator::CacheKey(stage, m_target, program, declarations);
            if (m_written.count(key) == 0)
            {
                auto const glsl = Translator::TranslateToGlsl(stage, m_target, program, declarations);
                written = Write(key, glsl);
            }
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }

        std::cout << displayPath << "\t" << Translator::StageName(stage) << "\t" << (error.empty() ? "ok" : "err") << "\t"
                  << key << "\t" << OneLine(error) << "\n";
        return written;
    }

    auto Preset(const PresetFile& preset) -> bool
    {
        PresetFileParser parser;
        if (!parser.Read(preset.path.string()))
        {
            std::cout << preset.displayPath << "\twarp\terr\t\tcannot read preset\n";
            return true;
        }

        auto const sources = Translator::ReadStageSources(parser);
        std::map<int, std::string> randomSlotNames;
        bool ok = true;

        // Same order and conditions as PerPixelMesh::LoadWarpShader and FinalComposite::LoadCompositeShader.
        if (sources.warpVersion > 0 && !sources.warpCode.empty())
        {
            ok = Stage(preset.displayPath, Translator::Stage::Warp, sources.warpCode, randomSlotNames) && ok;
        }
        if (sources.compositeVersion > 0 && !sources.compositeCode.empty())
        {
            ok = Stage(preset.displayPath, Translator::Stage::Composite, sources.compositeCode, randomSlotNames) && ok;
        }
        return ok;
    }

    auto DefaultComposite() -> bool
    {
        std::map<int, std::string> randomSlotNames;
        return Stage("(default)", Translator::Stage::Composite, Translator::DefaultCompositeShader(), randomSlotNames);
    }

    auto Count() const -> size_t
    {
        return m_written.size();
    }

private:
    auto Write(const std::string& key, const std::string& glsl) -> bool
    {
        fs::path const path = m_shaderDir / Translator::CacheFileName(key);
        fs::path const temporaryPath = path.string() + ".tmp";
        {
            std::ofstream file(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
            file << glsl;
            if (!file)
            {
                std::cerr << "cannot write " << temporaryPath << "\n";
                return false;
            }
        }
        std::error_code error;
        fs::rename(temporaryPath, path, error);
        if (error)
        {
            std::cerr << "cannot rename " << temporaryPath << ": " << error.message() << "\n";
            return false;
        }
        m_written.insert(key);
        return true;
    }

    Translator::Target m_target;
    fs::path m_shaderDir;
    std::set<std::string> m_written;
};

} // namespace

int main(int argc, char** argv)
{
    Translator::Target target = Translator::RuntimeTarget();
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; i++)
    {
        std::string const argument = argv[i];
        if (argument == "--target" && i + 1 < argc)
        {
            if (!Translator::TargetFromName(argv[++i], target))
            {
                std::cerr << "unknown target " << argv[i] << "\n";
                return 2;
            }
        }
        else
        {
            arguments.push_back(argument);
        }
    }
    if (arguments.size() < 2)
    {
        std::cerr << "usage: " << argv[0] << " [--target es100|es300|glsl330] <out_dir> <preset dir or file>...\n";
        return 2;
    }

    std::vector<PresetFile> presets;
    if (!CollectPresets({arguments.begin() + 1, arguments.end()}, presets))
    {
        return 2;
    }

    fs::path const shaderDir = fs::path(arguments.front()) / "shaders";
    std::error_code error;
    fs::create_directories(shaderDir, error);
    if (error)
    {
        std::cerr << "cannot create " << shaderDir << ": " << error.message() << "\n";
        return 1;
    }

    Pretranslator pretranslator(target, shaderDir);
    std::cout << "path\tstage\tstatus\tkey\terror\n";
    bool ok = pretranslator.DefaultComposite();
    for (const auto& preset : presets)
    {
        ok = pretranslator.Preset(preset) && ok;
    }

    std::cerr << "Translator " << Translator::TranslatorVersion() << ", target " << Translator::TargetName(target) << ": "
              << presets.size() << " presets, " << pretranslator.Count() << " shaders in " << shaderDir.string() << "\n";
    return ok ? 0 : 1;
}
