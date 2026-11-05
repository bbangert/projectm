#include "Renderer/TransitionShaderManager.hpp"

#include "BuiltInTransitionsResources.hpp"

namespace libprojectM {
namespace Renderer {

TransitionShaderManager::TransitionShaderManager()
    : m_transitionShaders({CompileTransitionShader(kTransitionShaderBuiltInCircleGlsl330),
                           CompileTransitionShader(kTransitionShaderBuiltInPlasmaGlsl330),
                           CompileTransitionShader(kTransitionShaderBuiltInSimpleBlendGlsl330),
                           CompileTransitionShader(kTransitionShaderBuiltInSweepGlsl330),
                           CompileTransitionShader(kTransitionShaderBuiltInWarpGlsl330),
                           CompileTransitionShader(kTransitionShaderBuiltInZoomBlurGlsl330)})
    , m_mersenneTwister(m_randomDevice())
{
}

auto TransitionShaderManager::RandomTransition() -> std::shared_ptr<Shader>
{
    if (m_transitionShaders.empty())
    {
        return {};
    }

    return m_transitionShaders.at(m_mersenneTwister() % m_transitionShaders.size());
}

auto TransitionShaderManager::CompileTransitionShader(const std::string& shaderBodyCode) -> std::shared_ptr<Shader>
{
#if defined(USE_GLES2)
    // GLSL ES 1.00 has no default float precision in fragment shaders and no 3D samplers.
    constexpr char versionHeader[] = "#version 100\n\nprecision mediump float;\n";
    const std::string& vertexShader = kTransitionVertexShaderGlsl100;
    const std::string& headerSource = kTransitionShaderHeaderGlsl100;
    const std::string& mainSource = kTransitionShaderMainGlsl100;
#elif defined(USE_GLES)
    // GLES also requires a precision specifier for variables and 3D samplers
    constexpr char versionHeader[] = "#version 300 es\n\nprecision mediump float;\nprecision mediump sampler3D;\n";
    const std::string& vertexShader = kTransitionVertexShaderGlsl330;
    const std::string& headerSource = kTransitionShaderHeaderGlsl330;
    const std::string& mainSource = kTransitionShaderMainGlsl330;
#else
    constexpr char versionHeader[] = "#version 330\n\n";
    const std::string& vertexShader = kTransitionVertexShaderGlsl330;
    const std::string& headerSource = kTransitionShaderHeaderGlsl330;
    const std::string& mainSource = kTransitionShaderMainGlsl330;
#endif

    std::string fragmentShaderSource(static_cast<const char*>(versionHeader));
    fragmentShaderSource.append(headerSource);
    fragmentShaderSource.append("\n");
    fragmentShaderSource.append(shaderBodyCode);
    fragmentShaderSource.append("\n");
    fragmentShaderSource.append(mainSource);

    try
    {
        auto transitionShader = std::make_shared<Shader>();
        transitionShader->CompileProgram(static_cast<const char*>(versionHeader) + vertexShader, fragmentShaderSource);
        return transitionShader;
    }
    catch (const ShaderException&)
    {
        // ToDo: Log proper shader compile error once logging API is in place
        return {};
    }
}

} // namespace Renderer
} // namespace libprojectM
