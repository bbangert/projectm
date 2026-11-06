#include "MilkdropShader.hpp"

#include "PresetState.hpp"
#include "Utils.hpp"

#include <MilkdropStaticShaders.hpp>

#include <Logging.hpp>

#ifdef USE_GLES2
#include <Renderer/Platform/GladLoader.hpp>
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <set>

namespace libprojectM {
namespace MilkdropPreset {

using libprojectM::MilkdropPreset::MilkdropStaticShaders;

void ShaderStageResult::RecordFailure(const Renderer::ShaderException& exception)
{
    if (failed)
    {
        message.append(" Fallback: ").append(exception.message());
        return;
    }

    failed = true;
    kind = exception.kind();
    message = exception.message();
}

static auto floatRand = []() { return static_cast<float>(rand() % 7381) / 7380.0f; };

MilkdropShader::MilkdropShader(ShaderType type)
    : m_type(type)
    , m_randValues({floatRand(), floatRand(), floatRand(), floatRand()})
{
    unsigned int index = 0;
    do
    {
        for (int i = 0; i < 4; i++)
        {
            float const m_randTranslationMult = 1;
            float const rotMult = 0.9f * powf(index / 8.0f, 3.2f);
            m_randTranslation[index].x = (floatRand() * 2 - 1) * m_randTranslationMult;
            m_randTranslation[index].y = (floatRand() * 2 - 1) * m_randTranslationMult;
            m_randTranslation[index].z = (floatRand() * 2 - 1) * m_randTranslationMult;
            m_randRotationCenters[index].x = floatRand() * 6.28f;
            m_randRotationCenters[index].y = floatRand() * 6.28f;
            m_randRotationCenters[index].z = floatRand() * 6.28f;
            m_randRotationSpeeds[index].x = (floatRand() * 2 - 1) * rotMult;
            m_randRotationSpeeds[index].y = (floatRand() * 2 - 1) * rotMult;
            m_randRotationSpeeds[index].z = (floatRand() * 2 - 1) * rotMult;
            index++;
        }
    } while (index < sizeof(m_randTranslation) / sizeof(m_randTranslation[0]));
}

void MilkdropShader::LoadCode(const std::string& presetShaderCode)
{
    m_fragmentShaderCode = presetShaderCode;

    m_samplers = PresetShaderTranslator::GetReferencedSamplers(m_fragmentShaderCode);
    m_preprocessedCode = PresetShaderTranslator::PreprocessPresetShader(Stage(), PresetShaderTranslator::RuntimeTarget(), m_fragmentShaderCode);
}

void MilkdropShader::LoadTexturesAndCompile(PresetState& presetState)
{
    // Now request the textures and descriptors from the texture manager.
    // UpdateMaxBlurLevel() may insert blur sampler names while iterating, which std::set allows.
    for (const auto& name : m_samplers.names)
    {
        auto const samplerClass = PresetShaderTranslator::ClassifySampler(name);
        switch (samplerClass.kind)
        {
            case PresetShaderTranslator::SamplerKind::Main: {
                Renderer::TextureSamplerDescriptor desc(presetState.mainTexture.lock(),
                                                        presetState.renderContext.textureManager->GetSampler(name),
                                                        name,
                                                        "main");
                m_mainTextureDescriptors.push_back(std::move(desc));
                continue;
            }

            case PresetShaderTranslator::SamplerKind::Blur:
                UpdateMaxBlurLevel(static_cast<BlurTexture::BlurLevel>(samplerClass.blurLevel));
                continue;

            case PresetShaderTranslator::SamplerKind::Random: {
                // First look up the random texture index in the preset state so the texture matches between warp and composite shaders
                int const randomSlot = samplerClass.randomSlot;
                if (presetState.randomTextureDescriptors.find(randomSlot) != presetState.randomTextureDescriptors.end())
                {
                    // Use existing texture descriptor.
                    m_textureSamplerDescriptors.push_back(presetState.randomTextureDescriptors.at(randomSlot));
                    continue;
                }

                // Slot empty, request a new random texture.
                auto desc = presetState.renderContext.textureManager->GetRandomTexture(name);

                // Also store a copy in preset state!
                presetState.randomTextureDescriptors.insert({randomSlot, desc});

                m_textureSamplerDescriptors.push_back(std::move(desc));
                continue;
            }

            case PresetShaderTranslator::SamplerKind::Texture:
                break;
        }

        auto desc = presetState.renderContext.textureManager->GetTexture(name);
        m_textureSamplerDescriptors.push_back(std::move(desc));
    }

    // Now that we have the textures, transpile the code.
    TranspileHLSLShader(presetState, m_preprocessedCode);

    // Update blur texture level if shader was compiled successfully.
    presetState.blurTexture.SetRequiredBlurLevel(MaxBlurLevel());
}

void MilkdropShader::LoadVariables(const PresetState& presetState, const PerFrameContext& perFrameContext,
                                   const Renderer::Sampler* mainSamplerOverride)
{
    // These are the inputs: http://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html#3f6

    auto floatTime = static_cast<float>(presetState.renderContext.time);
    auto timeSincePresetStartWrapped = floatTime - static_cast<int>(floatTime / 10000.0) * 10000;
    auto mipX = logf(static_cast<float>(presetState.renderContext.viewportSizeX)) / logf(2.0f);
    auto mipY = logf(static_cast<float>(presetState.renderContext.viewportSizeY)) / logf(2.0f);
    auto mipAvg = 0.5f * (mipX + mipY);

    BlurTexture::Values blurMin;
    BlurTexture::Values blurMax;
    BlurTexture::GetSafeBlurMinMaxValues(perFrameContext, blurMin, blurMax);

    m_shader.Bind();

    m_shader.SetUniformMat4x4("vertex_transformation", PresetState::orthogonalProjection);

    m_shader.SetUniformFloat4("rand_frame", {floatRand(),
                                             floatRand(),
                                             floatRand(),
                                             floatRand()});
    m_shader.SetUniformFloat4("rand_preset", {m_randValues[0],
                                              m_randValues[1],
                                              m_randValues[2],
                                              m_randValues[3]});

    m_shader.SetUniformFloat4("_c0", {presetState.renderContext.aspectX,
                                      presetState.renderContext.aspectY,
                                      1.0f / presetState.renderContext.aspectX,
                                      1.0f / presetState.renderContext.aspectY});
    m_shader.SetUniformFloat4("_c1", {0.0,
                                      0.0,
                                      0.0,
                                      0.0});
    m_shader.SetUniformFloat4("_c2", {timeSincePresetStartWrapped,
                                      presetState.renderContext.fps,
                                      presetState.renderContext.frame,
                                      presetState.renderContext.progress});
    m_shader.SetUniformFloat4("_c3", {presetState.audioData.bass,
                                      presetState.audioData.mid,
                                      presetState.audioData.treb,
                                      presetState.audioData.vol});
    m_shader.SetUniformFloat4("_c4", {presetState.audioData.bassAtt,
                                      presetState.audioData.midAtt,
                                      presetState.audioData.trebAtt,
                                      presetState.audioData.volAtt});
    m_shader.SetUniformFloat4("_c5", {blurMax[0] - blurMin[0],
                                      blurMin[0],
                                      blurMax[1] - blurMin[1],
                                      blurMin[1]});
    m_shader.SetUniformFloat4("_c6", {blurMax[2] - blurMin[2],
                                      blurMin[2],
                                      blurMin[0],
                                      blurMax[0]});
    m_shader.SetUniformFloat4("_c7", {presetState.renderContext.viewportSizeX,
                                      presetState.renderContext.viewportSizeY,
                                      1.0f / static_cast<float>(presetState.renderContext.viewportSizeX),
                                      1.0f / static_cast<float>(presetState.renderContext.viewportSizeY)});

    m_shader.SetUniformFloat4("_c8", {0.5f + 0.5f * cosf(floatTime * 0.329f + 1.2f),
                                      0.5f + 0.5f * cosf(floatTime * 1.293f + 3.9f),
                                      0.5f + 0.5f * cosf(floatTime * 5.070f + 2.5f),
                                      0.5f + 0.5f * cosf(floatTime * 20.051f + 5.4f)});

    m_shader.SetUniformFloat4("_c9", {0.5f + 0.5f * sinf(floatTime * 0.329f + 1.2f),
                                      0.5f + 0.5f * sinf(floatTime * 1.293f + 3.9f),
                                      0.5f + 0.5f * sinf(floatTime * 5.070f + 2.5f),
                                      0.5f + 0.5f * sinf(floatTime * 20.051f + 5.4f)});

    m_shader.SetUniformFloat4("_c10", {0.5f + 0.5f * cosf(floatTime * 0.0050f + 2.7f),
                                       0.5f + 0.5f * cosf(floatTime * 0.0085f + 5.3f),
                                       0.5f + 0.5f * cosf(floatTime * 0.0133f + 4.5f),
                                       0.5f + 0.5f * cosf(floatTime * 0.0217f + 3.8f)});

    m_shader.SetUniformFloat4("_c11", {0.5f + 0.5f * sinf(floatTime * 0.0050f + 2.7f),
                                       0.5f + 0.5f * sinf(floatTime * 0.0085f + 5.3f),
                                       0.5f + 0.5f * sinf(floatTime * 0.0133f + 4.5f),
                                       0.5f + 0.5f * sinf(floatTime * 0.0217f + 3.8f)});

    m_shader.SetUniformFloat4("_c12", {mipX,
                                       mipY,
                                       mipAvg,
                                       0});
    m_shader.SetUniformFloat4("_c13", {blurMin[1],
                                       blurMax[1],
                                       blurMin[2],
                                       blurMax[2]});


    std::array<glm::mat4, 24> tempMatrices{};

    // write matrices
    for (int i = 0; i < 20; i++)
    {
        glm::mat4 const rotationX = glm::rotate(glm::mat4(1.0f), m_randRotationCenters[i].x + m_randRotationSpeeds[i].x * floatTime, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::mat4 const rotationY = glm::rotate(glm::mat4(1.0f), m_randRotationCenters[i].y + m_randRotationSpeeds[i].y * floatTime, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 const rotationZ = glm::rotate(glm::mat4(1.0f), m_randRotationCenters[i].z + m_randRotationSpeeds[i].z * floatTime, glm::vec3(0.0f, 0.0f, 1.0f));

        glm::mat4 const randomTranslation = glm::translate(glm::mat4(1.0f), glm::vec3(m_randTranslation[i].x, m_randTranslation[i].y, m_randTranslation[i].z));

        tempMatrices[i] = randomTranslation * rotationX;
        tempMatrices[i] = rotationZ * tempMatrices[i];
        tempMatrices[i] = rotationY * tempMatrices[i];
    }

    // the last 4 are totally random, each frame
    for (int i = 20; i < 24; i++)
    {
        glm::mat4 const rotationX = glm::rotate(glm::mat4(1.0f), floatRand() * 6.28f, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::mat4 const rotationY = glm::rotate(glm::mat4(1.0f), floatRand() * 6.28f, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 const rotationZ = glm::rotate(glm::mat4(1.0f), floatRand() * 6.28f, glm::vec3(0.0f, 0.0f, 1.0f));

        glm::mat4 const randomTranslation = glm::translate(glm::mat4(1.0f), glm::vec3(floatRand(), floatRand(), floatRand()));

        tempMatrices[i] = randomTranslation * rotationX;
        tempMatrices[i] = rotationZ * tempMatrices[i];
        tempMatrices[i] = rotationY * tempMatrices[i];
    }

    m_shader.SetUniformMat3x4("rot_s1", tempMatrices[0]);
    m_shader.SetUniformMat3x4("rot_s2", tempMatrices[1]);
    m_shader.SetUniformMat3x4("rot_s3", tempMatrices[2]);
    m_shader.SetUniformMat3x4("rot_s4", tempMatrices[3]);
    m_shader.SetUniformMat3x4("rot_d1", tempMatrices[4]);
    m_shader.SetUniformMat3x4("rot_d2", tempMatrices[5]);
    m_shader.SetUniformMat3x4("rot_d3", tempMatrices[6]);
    m_shader.SetUniformMat3x4("rot_d4", tempMatrices[7]);
    m_shader.SetUniformMat3x4("rot_f1", tempMatrices[8]);
    m_shader.SetUniformMat3x4("rot_f2", tempMatrices[9]);
    m_shader.SetUniformMat3x4("rot_f3", tempMatrices[10]);
    m_shader.SetUniformMat3x4("rot_f4", tempMatrices[11]);
    m_shader.SetUniformMat3x4("rot_vf1", tempMatrices[12]);
    m_shader.SetUniformMat3x4("rot_vf2", tempMatrices[13]);
    m_shader.SetUniformMat3x4("rot_vf3", tempMatrices[14]);
    m_shader.SetUniformMat3x4("rot_vf4", tempMatrices[15]);
    m_shader.SetUniformMat3x4("rot_uf1", tempMatrices[16]);
    m_shader.SetUniformMat3x4("rot_uf2", tempMatrices[17]);
    m_shader.SetUniformMat3x4("rot_uf3", tempMatrices[18]);
    m_shader.SetUniformMat3x4("rot_uf4", tempMatrices[19]);
    m_shader.SetUniformMat3x4("rot_rand1", tempMatrices[20]);
    m_shader.SetUniformMat3x4("rot_rand2", tempMatrices[21]);
    m_shader.SetUniformMat3x4("rot_rand3", tempMatrices[22]);
    m_shader.SetUniformMat3x4("rot_rand4", tempMatrices[23]);

    // set program uniform "_q[a-h]" values (_qa.x, _qa.y, _qa.z, _qa.w, _qb.x, _qb.y ... ) alias q[1-32]
    for (int i = 0; i < QVarCount; i += 4)
    {
        std::string varName = "_q";
        varName.push_back(static_cast<char>('a' + i / 4));
        m_shader.SetUniformFloat4(varName.c_str(), {presetState.frameQVariables[i],
                                                    presetState.frameQVariables[i + 1],
                                                    presetState.frameQVariables[i + 2],
                                                    presetState.frameQVariables[i + 3]});
    }

    // Bind all texture and sampler descriptors. This includes the main and blur textures.
    for (auto& desc : m_mainTextureDescriptors)
    {
        // Update main texture, swaps every frame.
        desc.Texture(presetState.mainTexture);
    }
    for (auto& desc : m_textureSamplerDescriptors)
    {
        if (desc.Empty())
        {
            desc.TryUpdate(*presetState.renderContext.textureManager);
        }
    }

#ifdef USE_GLES2
    if (!Renderer::Platform::GladLoader::Instance().HasSamplerObjects())
    {
        BindTexturesWithoutSamplerObjects(presetState, mainSamplerOverride);
        return;
    }
#else
    static_cast<void>(mainSamplerOverride);
#endif

    GLint textureUnit{0};
    for (auto& desc : m_mainTextureDescriptors)
    {
        desc.Bind(textureUnit, m_shader);
        textureUnit++;
    }
    presetState.blurTexture.Bind(textureUnit, m_shader);
    for (auto& desc : m_textureSamplerDescriptors)
    {
        desc.Bind(textureUnit, m_shader);
        textureUnit++;
    }
}

#ifdef USE_GLES2
void MilkdropShader::BindTexturesWithoutSamplerObjects(const PresetState& presetState, const Renderer::Sampler* mainSamplerOverride)
{
    // Resolve all textures first: making a copy draws, which unbinds the program and textures.
    m_textureStateCopies.Begin();
    auto& shaderCache = *presetState.renderContext.shaderCache;
    const auto resolve = [this, &shaderCache](const Renderer::TextureSamplerDescriptor& desc, const Renderer::Sampler* sampler) {
        if (desc.Empty() || sampler == nullptr)
        {
            return desc;
        }
        Renderer::TextureSamplerDescriptor resolved = desc;
        resolved.Texture(m_textureStateCopies.Resolve(shaderCache, desc.Texture(), sampler->WrapMode(), sampler->FilterMode()));
        return resolved;
    };

    std::vector<Renderer::TextureSamplerDescriptor> mainDescriptors;
    mainDescriptors.reserve(m_mainTextureDescriptors.size());
    for (const auto& desc : m_mainTextureDescriptors)
    {
        // The caller rebinds unit 0 with its own sampler, which then applies to the texture there.
        const bool overridden = mainDescriptors.empty() && mainSamplerOverride != nullptr;
        mainDescriptors.push_back(resolve(desc, overridden ? mainSamplerOverride : desc.Sampler().get()));
    }

    std::vector<Renderer::TextureSamplerDescriptor> otherDescriptors;
    otherDescriptors.reserve(m_textureSamplerDescriptors.size());
    for (const auto& desc : m_textureSamplerDescriptors)
    {
        otherDescriptors.push_back(resolve(desc, desc.Sampler().get()));
    }

    m_shader.Bind();

    GLint textureUnit{0};
    for (const auto& desc : mainDescriptors)
    {
        desc.Bind(textureUnit, m_shader);
        textureUnit++;
    }
    presetState.blurTexture.Bind(textureUnit, m_shader);
    for (const auto& desc : otherDescriptors)
    {
        desc.Bind(textureUnit, m_shader);
        textureUnit++;
    }
}
#endif

auto MilkdropShader::Shader() -> Renderer::Shader&
{
    return m_shader;
}

auto MilkdropShader::LoadedFromCache() const -> bool
{
    return m_loadedFromCache;
}

auto MilkdropShader::Stage() const -> PresetShaderTranslator::Stage
{
    return m_type == ShaderType::WarpShader ? PresetShaderTranslator::Stage::Warp : PresetShaderTranslator::Stage::Composite;
}

auto MilkdropShader::MaxBlurLevel() const -> BlurTexture::BlurLevel
{
    return static_cast<BlurTexture::BlurLevel>(m_samplers.blurLevel);
}

void MilkdropShader::TranspileHLSLShader(const PresetState& presetState, const std::string& program)
{
    // Collect unique samplers and texsize uniforms
    PresetShaderTranslator::Declarations declarations;
    for (const auto& desc : m_mainTextureDescriptors)
    {
        declarations.samplers.insert(desc.SamplerDeclaration());
        declarations.texSizes.insert(desc.TexSizeDeclaration());
    }
    for (const auto& desc : presetState.blurTexture.GetDescriptorsForBlurLevel(MaxBlurLevel()))
    {
        declarations.samplers.insert(desc.SamplerDeclaration());
        // No texsize_blur1 etc.
    }
    for (const auto& desc : m_textureSamplerDescriptors)
    {
        declarations.samplers.insert(desc.SamplerDeclaration());
        declarations.texSizes.insert(desc.TexSizeDeclaration());
    }

    auto const target = PresetShaderTranslator::RuntimeTarget();
    std::string glsl;
    const auto* cacheDirectory = presetState.renderContext.presetShaderCacheDirectory;
    if (cacheDirectory != nullptr && !cacheDirectory->empty())
    {
        auto const key = PresetShaderTranslator::CacheKey(Stage(), target, program, declarations);
        m_loadedFromCache = PresetShaderTranslator::ReadCachedShader(*cacheDirectory, key, glsl);
        LOG_DEBUG(std::string("[MilkdropShader] Pre-translated ") + PresetShaderTranslator::StageName(Stage()) + " shader " + key + (m_loadedFromCache ? " found." : " not found."));
    }
    if (!m_loadedFromCache)
    {
        glsl = PresetShaderTranslator::TranslateToGlsl(Stage(), target, program, declarations);
    }

    // Now we have GLSL source for the preset shader program (hopefully it's valid!)
    // Compile the preset shader fragment shader with the standard vertex shader and cross our fingers.
    if (m_type == ShaderType::WarpShader)
    {
        m_shader.CompileProgram(MilkdropStaticShaders::Get()->GetPresetWarpVertexShader(), glsl);
    }
    else
    {
        m_shader.CompileProgram(MilkdropStaticShaders::Get()->GetPresetCompVertexShader(), glsl);
    }
}

void MilkdropShader::UpdateMaxBlurLevel(BlurTexture::BlurLevel requestedLevel)
{
    PresetShaderTranslator::RaiseBlurLevel(m_samplers, static_cast<int>(requestedLevel));
}

} // namespace MilkdropPreset
} // namespace libprojectM
